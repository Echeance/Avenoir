// Trousseau sous Linux : Secret Service (GNOME Keyring, KWallet…) par le
// programme `secret-tool`, lancé par son chemin absolu avec un tableau
// d'arguments ; le secret passe par son entrée, jamais par ses arguments
// (visibles des autres processus). Aucun lien avec libsecret (LGPL).

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>
#include <spdlog/spdlog.h>

#include "avenoir/core/error.hpp"
#include "avenoir/storage/secrets.hpp"

#include "../platform.hpp"

namespace avenoir::storage::platform {

namespace {

/// Chemins absolus : jamais le PATH, qu'un programme piégé pourrait précéder.
constexpr std::array<std::string_view, 2> kSecretTool{"/usr/bin/secret-tool", "/bin/secret-tool"};
/// Le trousseau peut demander à l'utilisateur de le déverrouiller.
constexpr std::chrono::seconds kTimeout{60};

/// Erreur sans conséquence ici (fermeture, arrêt d'un processus qui se
/// termine) : notée dans le journal de débogage seulement.
void discard(std::error_code error) {
    if (error) {
        spdlog::debug("secret-tool : {}", error.message());
    }
}

struct Outcome {
    int status = -1;
    std::string out;
    std::string err;
};

std::unexpected<core::Error> keychain_error(const std::string& message) {
    return core::make_error(core::ErrorCode::Config, "trousseau du système : " + message);
}

class SecretToolStore final : public SecretStore {
  public:
    explicit SecretToolStore(std::string program) : program_(std::move(program)) {}

    core::Result<std::optional<std::string>> read(std::string_view name) override {
        if (!is_valid_secret_name(name)) {
            return keychain_error("nom de secret invalide");
        }
        auto outcome =
            run({program_, "lookup", "application", "avenoir", "secret", std::string(name)}, {});
        if (!outcome) {
            return std::unexpected(std::move(outcome.error()));
        }
        if (outcome->status == 0) {
            std::string value = std::move(outcome->out);
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }
            return std::optional<std::string>{std::move(value)};
        }
        // Absent : code 1 sans message ; Secret Service injoignable : message.
        if (outcome->status == 1 && outcome->err.empty()) {
            return std::optional<std::string>{};
        }
        return keychain_error(first_line(outcome->err));
    }

    core::Result<void> write(std::string_view name, std::string_view value) override {
        if (!is_valid_secret_name(name)) {
            return keychain_error("nom de secret invalide");
        }
        auto outcome = run({program_, "store", "--label=Avenoir : " + std::string(name),
                            "application", "avenoir", "secret", std::string(name)},
                           value);
        if (!outcome) {
            return std::unexpected(std::move(outcome.error()));
        }
        if (outcome->status != 0) {
            return keychain_error(first_line(outcome->err));
        }
        return {};
    }

    [[nodiscard]] std::string description() const override {
        return "trousseau du système (Secret Service)";
    }
    [[nodiscard]] bool is_system_keychain() const override { return true; }

  private:
    static std::string first_line(const std::string& text) {
        const std::string line = text.substr(0, text.find('\n'));
        return line.empty() ? "pas de réponse" : line;
    }

    /// Lance `secret-tool`, lui donne `input`, attend sa fin (borné).
    static core::Result<Outcome> run(const std::vector<std::string>& arguments,
                                     std::string_view input) {
        reproc::options options;
        options.deadline = reproc::milliseconds(
            std::chrono::duration_cast<std::chrono::milliseconds>(kTimeout).count());
        options.stop = {
            .first = {.action = reproc::stop::terminate, .timeout = reproc::milliseconds(1000)},
            .second = {.action = reproc::stop::kill, .timeout = reproc::milliseconds(1000)},
            .third = {},
        };
        reproc::process process;
        if (const auto error = process.start(arguments, options)) {
            return keychain_error("secret-tool impossible à lancer : " + error.message());
        }
        if (!input.empty()) {
            const std::vector<std::uint8_t> bytes(input.begin(), input.end());
            const auto [written, error] = process.write(bytes.data(), bytes.size());
            if (error || written != bytes.size()) {
                discard(process.kill());
                return keychain_error("secret non transmis à secret-tool");
            }
        }
        discard(process.close(reproc::stream::in));
        Outcome outcome;
        const auto drained = reproc::drain(process, reproc::sink::string(outcome.out),
                                           reproc::sink::string(outcome.err));
        const auto [status, error] = process.wait(reproc::milliseconds(5000));
        if (drained || error) {
            discard(process.kill());
            return keychain_error("secret-tool n'a pas répondu");
        }
        outcome.status = status;
        return outcome;
    }

    std::string program_;
};

} // namespace

std::unique_ptr<SecretStore> system_keychain() {
    for (const auto path : kSecretTool) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) {
            return std::make_unique<SecretToolStore>(std::string(path));
        }
    }
    return nullptr;
}

} // namespace avenoir::storage::platform
