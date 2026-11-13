#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace avenoir::cli {

class Prompt;

/// Un réglage, tel que le démon le décrit (`settings/list`).
struct SettingEntry {
    std::string key;         ///< « agent.local_only »
    std::string value;       ///< « oui »
    std::string accepted;    ///< « oui | non », « 1 à 100 »…
    std::string description; ///< À quoi il sert.
    bool security = false;   ///< L'affaiblir demande l'accord de l'utilisateur.
};

/// Réglages lus dans la réponse du démon (les entrées mal formées sont ignorées).
[[nodiscard]] std::vector<SettingEntry> settings_from_json(const nlohmann::json& list);

/// Famille d'un réglage, d'après sa clé (« agent. » → « Assistant »).
[[nodiscard]] std::string setting_family(std::string_view key);

/// Valeurs à choisir dans une liste (« oui | non ») ; vide si la valeur se
/// tape (nombre « 1 à 100 », adresse « http(s)://… »).
[[nodiscard]] std::vector<std::string> setting_choices(std::string_view accepted);

/// Réglages dont la clé, la famille ou la description contiennent `filter`
/// (sans tenir compte de la casse) : leurs positions dans `entries`.
[[nodiscard]] std::vector<std::size_t> filter_settings(const std::vector<SettingEntry>& entries,
                                                       std::string_view filter);

/// État affiché du menu.
struct MenuView {
    std::string title;                 ///< « Réglages » ou « agent.local_only ».
    std::vector<std::string> items;    ///< Lignes de la liste, déjà mises en forme.
    std::vector<std::string> families; ///< Famille de chaque ligne (en-têtes), ou vide.
    std::size_t selected = 0;
    std::vector<std::string> details; ///< Sous la liste : description de la sélection.
    std::string status;               ///< Résultat du dernier changement.
    std::string keys;                 ///< Rappel des touches.
};

/// Lignes du panneau (`height` lignes de `width` colonnes) : titre, liste
/// défilée pour garder la sélection visible, détails, touches.
[[nodiscard]] std::vector<std::string> render_menu(const MenuView& view, std::size_t height,
                                                   std::size_t width);

/// Ce dont le menu a besoin du démon : la liste des réglages, et un
/// changement (passé par /reglage : sceau et accord compris).
struct SettingsBackend {
    std::function<std::optional<std::vector<SettingEntry>>()> fetch;
    std::function<void(const std::string& key, const std::string& value)> apply;
};

/// Menu interactif des réglages (/settings, /parametres) en plein écran :
/// choisir un réglage, puis sa valeur dans une liste ou en la tapant.
void run_settings_menu(Prompt& prompt, const SettingsBackend& backend);

} // namespace avenoir::cli
