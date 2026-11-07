#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/core/tool.hpp"
#include "avenoir/modules/manifest.hpp"

#include "rpc_client.hpp"

namespace avenoir::modules {

/// Version du protocole MCP annoncée à la poignée de main.
inline constexpr std::string_view kMcpProtocolVersion = "2025-06-18";
/// Clé de `_meta` par laquelle un module `avenoir` propose une annulation.
inline constexpr std::string_view kUndoMetaKey = "avenoir/undo";

/// Module lancé : son processus et le client JSON-RPC qui lui parle.
class Module {
  public:
    Module(Manifest manifest, std::unique_ptr<RpcClient> client,
           std::chrono::milliseconds call_timeout);

    [[nodiscard]] const Manifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] bool alive() const { return client_->alive(); }
    [[nodiscard]] std::string failure() const { return client_->failure(); }

    /// `tools/call` : renvoie le `result` brut (à interpréter par `to_output`).
    [[nodiscard]] core::Result<nlohmann::json> call_tool(std::string_view name,
                                                         const nlohmann::json& arguments,
                                                         const std::stop_token& stop) const;

    /// `avenoir/undo` : défait une action d'après le jeton qu'il a renvoyé.
    [[nodiscard]] core::Result<nlohmann::json> undo(std::string_view token) const;

  private:
    Manifest manifest_;
    std::unique_ptr<RpcClient> client_;
    std::chrono::milliseconds call_timeout_;
};

/// Outil d'un module, exposé au LLM sous le nom `id__outil`.
class ModuleTool final : public core::Tool {
  public:
    ModuleTool(std::shared_ptr<const Module> module, std::string short_name, core::ToolSpec spec);

    [[nodiscard]] core::ToolSpec spec() const override;
    /// Change le risque (choix fait avec /risque), sans relancer le module.
    void set_risk(core::RiskLevel risk) noexcept { risk_ = risk; }
    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& stop) const override;

  private:
    std::shared_ptr<const Module> module_;
    std::string short_name_;
    core::ToolSpec spec_;
    /// Lu par le thread de la requête, modifié par le thread principal.
    std::atomic<core::RiskLevel> risk_;
};

/// Texte venant d'un module, débarrassé des caractères de contrôle (séquences
/// d'échappement du terminal comprises), retours à la ligne et tabulations
/// exceptés si `multiline`. UTF-8 invalide remplacé. Coupé à `max_bytes`.
[[nodiscard]] std::string clean_text(std::string_view text, std::size_t max_bytes, bool multiline);

/// Interprète un résultat `tools/call` (MCP) : texte des contenus, erreur si
/// `isError`, annulation proposée dans `_meta` si `allow_undo`.
[[nodiscard]] core::ToolResult to_output(const nlohmann::json& result,
                                         const std::shared_ptr<const Module>& module,
                                         bool allow_undo);

} // namespace avenoir::modules
