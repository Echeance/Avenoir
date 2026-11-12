#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"
#include "avenoir/ipc/peer.hpp"

// Ce que le démon demande à l'interface qui l'utilise (docs/PROJECT.md § 2.1) :
// afficher (messages, actions, réponse au fil de l'eau, toupie), poser une
// question, faire confirmer une action. Chaque requête d'un client est
// traitée avec l'interface de ce client (`UiScope`) ; hors requête, les
// messages vont à tous les clients connectés.
namespace avenoir::daemon {

/// Symbole des actions (outil), comme dans la CLI.
inline constexpr std::string_view kToolGlyph = "»";

class Ui {
  public:
    Ui() = default;
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    Ui(Ui&&) = delete;
    Ui& operator=(Ui&&) = delete;
    virtual ~Ui() = default;

    virtual void info(std::string_view text) = 0;
    virtual void notify(std::string_view text) = 0; ///< Discret, même pendant la saisie.
    virtual void success(std::string_view text) = 0;
    virtual void warning(std::string_view text) = 0;
    virtual void error(std::string_view text) = 0;
    virtual void plain(std::string_view text) = 0; ///< Texte déjà mis en forme.

    virtual void user(std::string_view text) = 0;
    virtual void assistant(std::string_view markdown) = 0;
    virtual void tool_calls(std::span<const core::ToolCall> calls) = 0;
    virtual void tool_result(const core::ToolResultMessage& result) = 0;
    /// Réaffiche des messages (les `limit` derniers si demandé).
    virtual void messages(std::span<const core::Message> messages,
                          std::optional<std::size_t> limit) = 0;

    virtual void clear() = 0;
    virtual void banner(std::string_view llm, std::size_t tools, std::string_view conversation) = 0;

    virtual void start_thinking() = 0;
    virtual void set_activity(std::string_view activity) = 0;
    virtual void stop_thinking() = 0;
    virtual void stream_text(std::string_view fragment) = 0;
    virtual void stream_end(bool kept) = 0;
    /// Vrai si une réponse a été montrée au fil de l'eau depuis `start_thinking`.
    [[nodiscard]] virtual bool streamed() = 0;

    /// Question ouverte ; rien si l'utilisateur abandonne (Ctrl+C, Ctrl+D).
    [[nodiscard]] virtual std::optional<std::string> ask(std::string_view question) = 0;
    /// Question fermée : vrai seulement pour « o », « oui »… (non par défaut).
    [[nodiscard]] virtual bool ask_yes_no(std::string_view question) = 0;
    /// Cadre de confirmation d'une action puis « Autoriser ? ».
    [[nodiscard]] virtual bool confirm(const core::ToolSpec& spec, const core::ToolCall& call) = 0;
};

/// Interface d'un client connecté : chaque appel devient un message JSON-RPC
/// (notification « ui » ou appel « ask », « ask_yes_no », « confirm »).
class RemoteUi final : public Ui {
  public:
    explicit RemoteUi(ipc::Peer& peer) : peer_(&peer) {}

    void info(std::string_view text) override { send("info", text); }
    void notify(std::string_view text) override { send("notify", text); }
    void success(std::string_view text) override { send("success", text); }
    void warning(std::string_view text) override { send("warning", text); }
    void error(std::string_view text) override { send("error", text); }
    void plain(std::string_view text) override { send("plain", text); }
    void user(std::string_view text) override { send("user", text); }
    void assistant(std::string_view markdown) override { send("assistant", markdown); }
    void tool_calls(std::span<const core::ToolCall> calls) override;
    void tool_result(const core::ToolResultMessage& result) override;
    void messages(std::span<const core::Message> messages,
                  std::optional<std::size_t> limit) override;
    void clear() override { send("clear", ""); }
    void banner(std::string_view llm, std::size_t tools, std::string_view conversation) override;
    void start_thinking() override;
    void set_activity(std::string_view activity) override { send("activity", activity); }
    void stop_thinking() override { send("stop_thinking", ""); }
    void stream_text(std::string_view fragment) override;
    void stream_end(bool kept) override;
    [[nodiscard]] bool streamed() override;
    [[nodiscard]] std::optional<std::string> ask(std::string_view question) override;
    [[nodiscard]] bool ask_yes_no(std::string_view question) override;
    [[nodiscard]] bool confirm(const core::ToolSpec& spec, const core::ToolCall& call) override;

  private:
    void send(std::string_view op, std::string_view text);
    void send(nlohmann::json params);

    ipc::Peer* peer_;
    std::mutex mutex_;
    bool streamed_ = false;
    /// Une question à la fois : des agents en parallèle peuvent en poser
    /// ensemble, l'utilisateur y répond l'une après l'autre.
    std::mutex question_mutex_;
};

/// Hors requête : messages à tous les clients connectés (« notify »), ou au
/// journal s'il n'y en a aucun ; aucune question possible (réponse « non »).
class BroadcastUi final : public Ui {
  public:
    /// Clients connectés, à interroger à chaque message.
    using Clients = std::function<std::vector<std::shared_ptr<Ui>>()>;
    explicit BroadcastUi(Clients clients) : clients_(std::move(clients)) {}

    void info(std::string_view text) override { broadcast(text); }
    void notify(std::string_view text) override { broadcast(text); }
    void success(std::string_view text) override { broadcast(text); }
    void warning(std::string_view text) override { broadcast(text); }
    void error(std::string_view text) override { broadcast(text); }
    void plain(std::string_view text) override { broadcast(text); }
    void user(std::string_view /*text*/) override {}
    void assistant(std::string_view /*markdown*/) override {}
    void tool_calls(std::span<const core::ToolCall> /*calls*/) override {}
    void tool_result(const core::ToolResultMessage& /*result*/) override {}
    void messages(std::span<const core::Message> /*messages*/,
                  std::optional<std::size_t> /*limit*/) override {}
    void clear() override {}
    void banner(std::string_view /*llm*/, std::size_t /*tools*/,
                std::string_view /*conversation*/) override {}
    void start_thinking() override {}
    void set_activity(std::string_view /*activity*/) override {}
    void stop_thinking() override {}
    void stream_text(std::string_view /*fragment*/) override {}
    void stream_end(bool /*kept*/) override {}
    [[nodiscard]] bool streamed() override { return false; }
    [[nodiscard]] std::optional<std::string> ask(std::string_view /*question*/) override {
        return std::nullopt;
    }
    [[nodiscard]] bool ask_yes_no(std::string_view /*question*/) override { return false; }
    [[nodiscard]] bool confirm(const core::ToolSpec& /*spec*/,
                               const core::ToolCall& /*call*/) override {
        return false; // personne pour confirmer : refusé
    }

  private:
    void broadcast(std::string_view text);
    Clients clients_;
};

/// Interface de la requête en cours dans ce fil, sinon celle par défaut
/// (`set_default_ui`, diffusion à tous les clients).
[[nodiscard]] Ui& display();

/// Interface hors requête (posée au démarrage du démon).
void set_default_ui(std::shared_ptr<Ui> ui);

/// Le temps de sa vie, `display()` rend `ui` dans ce fil.
class UiScope {
  public:
    explicit UiScope(Ui& ui);
    UiScope(const UiScope&) = delete;
    UiScope& operator=(const UiScope&) = delete;
    UiScope(UiScope&&) = delete;
    UiScope& operator=(UiScope&&) = delete;
    ~UiScope();

  private:
    Ui* previous_;
};

} // namespace avenoir::daemon
