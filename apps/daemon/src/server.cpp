#include "server.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/ipc/peer.hpp"
#include "avenoir/ipc/transport.hpp"
#include "avenoir/storage/secrets.hpp"
#include "avenoir/storage/settings.hpp"
#include "avenoir/storage/store.hpp"

#include "conversations.hpp"
#include "runtime.hpp"
#include "setup.hpp"
#include "ui.hpp"

namespace avenoir::daemon {

namespace {

using nlohmann::json;
using namespace std::chrono_literals;

/// Messages de la vérification des réglages : notés au journal d'audit au
/// démarrage, montrés au premier client.
struct SettingsNotices {
    std::vector<std::string> warnings;
    std::vector<std::string> notes;
    std::vector<std::string> changes; ///< Lignes changées hors d'Avenoir.
    bool shown = false;

    void audit() const {
        const auto logger = spdlog::get("audit");
        if (!logger) {
            return;
        }
        for (const auto& message : warnings) {
            logger->warn("réglages : {}", message);
        }
        for (const auto& message : notes) {
            logger->info("réglages : {}", message);
        }
        for (const auto& change : changes) {
            logger->warn("réglages : {}", change);
        }
    }

    /// Montre les messages au client de la requête (une seule fois).
    void show() {
        if (shown) {
            return;
        }
        shown = true;
        for (const auto& warning : warnings) {
            display().warning(warning);
        }
        for (const auto& change : changes) {
            display().info("  " + change);
        }
        for (const auto& note : notes) {
            display().info(note);
        }
    }
};

/// Un client connecté.
struct Session {
    std::unique_ptr<ipc::Peer> peer;
    std::shared_ptr<RemoteUi> ui;
};

class Server {
  public:
    explicit Server(const DaemonOptions& options) : options_(options) {}

    /// Réglages : emplacement, sceau, lecture ; Avenoir construit s'ils
    /// existent (sinon, création guidée avec le premier client). Un échec est
    /// gardé et dit aux clients.
    void prepare() {
        if (options_.config) {
            config_path_ = *options_.config;
            notices_.warnings.emplace_back(
                "Configuration non scellée (fichier donné en argument : mode développement).");
            save_ = [path = config_path_](const core::AppConfig& updated) {
                return core::save_config(path, updated);
            };
        } else {
            const auto path = core::default_config_path();
            const auto data = storage::data_directory();
            if (!path || !data) {
                build_error_ = "emplacement de la configuration ou dossier de données introuvable";
                spdlog::error("{}", build_error_);
                return;
            }
            config_path_ = *path;
            secrets_ = storage::open_secret_store(*data / "secrets");
            sealed_.emplace(config_path_, *secrets_);
            if (!verify()) {
                notices_.audit();
                build_error_ = notices_.warnings.back();
                notices_.warnings.pop_back();
                return;
            }
            save_ = [this](const core::AppConfig& updated) { return sealed_->save(updated); };
        }
        notices_.audit();
        std::error_code missing;
        if (std::filesystem::exists(config_path_, missing) || options_.config) {
            build_runtime();
        }
    }

    int run(const std::stop_token& stop) {
        // Écoute d'abord : un second démon s'arrête sans rien lancer.
        auto listener = ipc::listen(options_.endpoint);
        if (!listener) {
            spdlog::error("{}", core::describe(listener.error()));
            return EXIT_FAILURE;
        }
        prepare();
        set_default_ui(std::make_shared<BroadcastUi>([this] { return client_uis(); }));
        spdlog::info("démon prêt : {}", options_.endpoint.address);

        std::stop_source local;
        const std::stop_callback forward(stop, [&local] { local.request_stop(); });
        std::jthread housekeeping(
            [this, &local](const std::stop_token& done) { housekeep(done, local); });
        while (!local.stop_requested()) {
            auto connection = (*listener)->accept(local.get_token());
            if (!connection) {
                if (connection.error().code != core::ErrorCode::Cancelled) {
                    spdlog::warn("{}", core::describe(connection.error()));
                }
                continue;
            }
            add_session(std::move(*connection));
        }
        housekeeping.request_stop();
        housekeeping.join();
        // Clients d'abord (leurs requêtes finissent), puis Avenoir.
        std::list<Session> sessions;
        {
            const std::scoped_lock lock(sessions_mutex_);
            sessions = std::move(sessions_);
        }
        for (auto& session : sessions) {
            session.peer->close();
        }
        sessions.clear();
        set_default_ui(nullptr);
        const std::scoped_lock lock(runtime_mutex_);
        runtime_.reset();
        spdlog::info("démon arrêté");
        return EXIT_SUCCESS;
    }

  private:
    bool verify() {
        using Status = storage::SealReport::Status;
        if (!sealed_) {
            return false;
        }
        auto report = sealed_->verify();
        if (!report) {
            notices_.warnings.push_back(core::describe(report.error()) +
                                        " — réglages non vérifiables, Avenoir ne démarre pas.");
            return false;
        }
        switch (report->status) {
        case Status::Valid:
        case Status::NoSettings:
            break;
        case Status::Adopted:
            notices_.notes.push_back(report->message + " Pour les modèles : /configurer.");
            break;
        case Status::Restored:
        case Status::Rejected:
        case Status::Unverified:
            notices_.warnings.push_back(report->message);
            break;
        }
        notices_.changes = std::move(report->changes);
        if (!secrets_->is_system_keychain()) {
            notices_.warnings.push_back("Trousseau du système indisponible : clé du sceau des "
                                        "réglages gardée dans un " +
                                        secrets_->description() + " (protection plus faible).");
        }
        return true;
    }

    /// Construit Avenoir à partir du fichier (verrou d'Avenoir pris, ou au
    /// démarrage). Une erreur est gardée pour être montrée aux clients.
    void build_runtime() {
        runtime_.reset(); // modules et modèle de l'ancien arrêtés d'abord
        auto config = core::load_config(config_path_);
        if (!config) {
            build_error_ =
                core::describe(config.error()) + " (fichier : " + utf8_path(config_path_) + ")";
            spdlog::error("{}", build_error_);
            return;
        }
        auto runtime = Runtime::create(
            ConfigFile{.path = config_path_, .config = std::move(*config), .save = save_});
        if (!runtime) {
            build_error_ = core::describe(runtime.error());
            spdlog::error("{}", build_error_);
            return;
        }
        build_error_.clear();
        runtime_ = std::move(*runtime);
        first_welcome_ = true;
    }

    std::vector<std::shared_ptr<Ui>> client_uis() {
        const std::scoped_lock lock(sessions_mutex_);
        std::vector<std::shared_ptr<Ui>> uis;
        for (const auto& session : sessions_) {
            if (!session.peer->is_closed()) {
                uis.push_back(session.ui);
            }
        }
        return uis;
    }

    void add_session(std::unique_ptr<ipc::Connection> connection) {
        auto peer = std::make_unique<ipc::Peer>(std::move(connection));
        auto ui = std::make_shared<RemoteUi>(*peer);
        RemoteUi* raw_ui = ui.get();
        peer->on_request("session/open",
                         [this, raw_ui](const json& params, const std::stop_token& stop) {
                             return open_session(*raw_ui, params, stop);
                         });
        peer->on_request("input", [this, raw_ui](const json& params, const std::stop_token& stop) {
            return handle_input(*raw_ui, params, stop);
        });
        peer->on_request("session/describe", [this](const json&, const std::stop_token&) {
            const std::scoped_lock lock(runtime_mutex_);
            return core::Result<json>(
                json{{"conversation", runtime_ ? runtime_->describe_conversation() : ""}});
        });
        peer->start();
        const std::scoped_lock lock(sessions_mutex_);
        had_client_ = true;
        sessions_.push_back(Session{.peer = std::move(peer), .ui = std::move(ui)});
    }

    core::Result<json> open_session(RemoteUi& ui, const json& params, const std::stop_token& stop) {
        const UiScope scope(ui);
        const bool interactive = params.value("interactive", false);
        const std::scoped_lock lock(runtime_mutex_);
        if (!runtime_ && build_error_.empty()) {
            // Premier lancement : réglages créés avec ce client.
            notices_.show();
            if (!interactive) {
                return core::make_error(core::ErrorCode::Config,
                                        "aucune configuration (" + utf8_path(config_path_) +
                                            ") : lancez Avenoir dans un terminal pour la créer, "
                                            "ou donnez un chemin, « avenoir chemin/config.toml »");
            }
            auto created = run_first_setup(config_path_, save_, stop);
            if (!created) {
                return core::make_error(core::ErrorCode::Cancelled, "création guidée abandonnée");
            }
            std::error_code missing;
            if (std::filesystem::exists(config_path_, missing)) {
                build_runtime();
            } else {
                // Aucun modèle, rien d'écrit : Avenoir tourne avec la voie rapide.
                auto runtime = Runtime::create(
                    ConfigFile{.path = config_path_, .config = std::move(*created), .save = save_});
                if (runtime) {
                    runtime_ = std::move(*runtime);
                } else {
                    build_error_ = core::describe(runtime.error());
                }
            }
        }
        if (!runtime_) {
            notices_.show();
            return core::make_error(core::ErrorCode::Config, build_error_);
        }
        runtime_->welcome(first_welcome_);
        notices_.show();
        first_welcome_ = false;
        json completions = json::array();
        for (const auto& name : runtime_->completions()) {
            completions.push_back(name);
        }
        return json{{"completions", std::move(completions)},
                    {"conversation", runtime_->describe_conversation()}};
    }

    core::Result<json> handle_input(RemoteUi& ui, const json& params, const std::stop_token& stop) {
        const UiScope scope(ui);
        if (!params.contains("line") || !params["line"].is_string()) {
            return core::make_error(core::ErrorCode::Ipc, "saisie attendue");
        }
        const std::scoped_lock lock(runtime_mutex_);
        if (!runtime_) {
            return core::make_error(core::ErrorCode::Config, build_error_);
        }
        const Outcome outcome = runtime_->input(params["line"].get<std::string>(), stop);
        runtime_->finish_input();
        switch (outcome) {
        case Outcome::Continue:
            break;
        case Outcome::Quit:
            return json{{"quit", true}, {"conversation", runtime_->describe_conversation()}};
        case Outcome::Restart:
            display().info("Redémarrage d'Avenoir…");
            build_runtime();
            if (!runtime_) {
                return core::make_error(core::ErrorCode::Config, build_error_);
            }
            runtime_->welcome(false);
            break;
        case Outcome::Shutdown: {
            const std::string conversation = runtime_->describe_conversation();
            display().info("Arrêt d'Avenoir.");
            shutdown_requested_ = true;
            return json{{"quit", true}, {"conversation", conversation}};
        }
        }
        return json{{"quit", false}};
    }

    /// Toutes les secondes : clients partis retirés, arrêt quand il le faut ;
    /// toutes les 3 s : modèles chargés ou déchargés hors d'Avenoir annoncés.
    void housekeep(const std::stop_token& done, std::stop_source& server) {
        int tick = 0;
        while (!done.stop_requested()) {
            std::this_thread::sleep_for(1s);
            std::list<Session> closed;
            bool empty = false;
            {
                const std::scoped_lock lock(sessions_mutex_);
                for (auto it = sessions_.begin(); it != sessions_.end();) {
                    if (it->peer->is_closed()) {
                        closed.splice(closed.end(), sessions_, it++);
                    } else {
                        ++it;
                    }
                }
                empty = sessions_.empty();
            }
            closed.clear(); // hors verrou : attend la fin de leurs requêtes
            if (shutdown_requested_ || (options_.exit_when_idle && had_client_ && empty)) {
                server.request_stop();
                return;
            }
            if (++tick % 3 != 0) {
                continue;
            }
            const std::unique_lock lock(runtime_mutex_, std::try_to_lock);
            if (lock.owns_lock() && runtime_) {
                for (const auto& message : runtime_->model_changes()) {
                    for (const auto& ui : client_uis()) {
                        ui->notify(message);
                    }
                }
            }
        }
    }

    const DaemonOptions& options_;
    std::filesystem::path config_path_;
    std::unique_ptr<storage::SecretStore> secrets_;
    std::optional<storage::SealedSettings> sealed_;
    SaveSettings save_;
    SettingsNotices notices_;

    std::mutex runtime_mutex_; ///< Une requête à la fois : Avenoir n'a qu'un agent.
    std::unique_ptr<Runtime> runtime_;
    std::string build_error_;
    bool first_welcome_ = true;

    std::mutex sessions_mutex_;
    std::list<Session> sessions_;
    std::atomic<bool> had_client_{false};
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace

int serve(const DaemonOptions& options, const std::stop_token& stop) {
    Server server(options);
    return server.run(stop);
}

} // namespace avenoir::daemon
