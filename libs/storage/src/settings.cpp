#include "avenoir/storage/settings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <sodium.h>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/storage/secrets.hpp"

namespace avenoir::storage {

namespace {

namespace fs = std::filesystem;

constexpr int kSealFormat = 1;
constexpr std::size_t kMaxChanges = 40;
constexpr std::size_t kMaxChangeBytes = 160;
constexpr std::size_t kMaxSettingsBytes = std::size_t{1024} * 1024;

std::unexpected<core::Error> seal_error(std::string message) {
    return core::make_error(core::ErrorCode::Config, "réglages : " + std::move(message));
}

/// libsodium prêt (appel idempotent et sûr entre fils).
bool sodium_ready() {
    return sodium_init() >= 0;
}

std::string to_hex(const unsigned char* bytes, std::size_t size) {
    std::string hex((size * 2) + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), bytes, size);
    hex.pop_back(); // zéro final
    return hex;
}

std::optional<std::vector<unsigned char>> from_hex(const std::string& hex) {
    std::vector<unsigned char> bytes(hex.size() / 2);
    std::size_t size = 0;
    if (hex.size() % 2 != 0 ||
        sodium_hex2bin(bytes.data(), bytes.size(), hex.data(), hex.size(), nullptr, &size,
                       nullptr) != 0 ||
        size != bytes.size()) {
        return std::nullopt;
    }
    return bytes;
}

/// HMAC-SHA256 de `content`, en hexadécimal.
std::string hmac(const std::vector<unsigned char>& key, const std::string& content) {
    std::array<unsigned char, crypto_auth_hmacsha256_BYTES> mac{};
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, key.data(), key.size());
    crypto_auth_hmacsha256_update(&state,
                                  reinterpret_cast<const unsigned char*>(content.data()), // NOLINT
                                  content.size());
    crypto_auth_hmacsha256_final(&state, mac.data());
    sodium_memzero(&state, sizeof(state));
    return to_hex(mac.data(), mac.size());
}

/// Comparaison en temps constant de deux sceaux hexadécimaux.
bool same_seal(const std::string& a, const std::string& b) {
    return a.size() == b.size() && sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

/// Contenu d'un fichier, ou rien s'il n'existe pas.
core::Result<std::optional<std::string>> read_file(const fs::path& path) {
    std::error_code error;
    if (!fs::exists(path, error)) {
        return std::optional<std::string>{};
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return seal_error("lecture impossible : " + path.string());
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.size() > kMaxSettingsBytes) {
        return seal_error("fichier trop gros : " + path.string());
    }
    return std::optional<std::string>{std::move(content)};
}

/// Écrit `content` à `path` : lisible par l'utilisateur seul, remplacement
/// atomique (fichier temporaire puis renommage).
core::Result<void> write_private(const fs::path& path, const std::string& content) {
    std::error_code error;
    if (const fs::path directory = path.parent_path(); !directory.empty()) {
        fs::create_directories(directory, error);
        if (error) {
            return seal_error("dossier impossible à créer : " + error.message());
        }
        fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace, error);
    }
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return seal_error("écriture impossible : " + temporary.string());
        }
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, error);
        file << content;
        file.flush();
        if (!file) {
            file.close();
            fs::remove(temporary, error);
            return seal_error("écriture impossible : " + temporary.string());
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        const std::string reason = error.message();
        fs::remove(temporary, error);
        return seal_error("écriture impossible : " + path.string() + " (" + reason + ")");
    }
    return {};
}

struct SealFile {
    std::string mac;     ///< HMAC-SHA256 du contenu, hexadécimal.
    std::string content; ///< Copie de la version scellée (pour la restaurer).
};

std::optional<SealFile> parse_seal(const std::string& text) {
    const auto json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!json.is_object() || json.value("format", 0) != kSealFormat || !json.contains("hmac") ||
        !json["hmac"].is_string() || !json.contains("content") || !json["content"].is_string()) {
        return std::nullopt;
    }
    return SealFile{.mac = json["hmac"].get<std::string>(),
                    .content = json["content"].get<std::string>()};
}

/// Suffixe propre à un fichier de configuration : chaque fichier a sa clé,
/// un essai dans un autre dossier ne touche pas à celle des vrais réglages.
std::string path_tag(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::weakly_canonical(fs::absolute(path, error), error);
    const std::string text = absolute.generic_string();
    std::array<unsigned char, 16> digest{};
    crypto_generichash(digest.data(), digest.size(),
                       reinterpret_cast<const unsigned char*>(text.data()), // NOLINT
                       text.size(), nullptr, 0);
    return to_hex(digest.data(), digest.size());
}

std::string clean_line(std::string_view line) {
    std::string clean;
    for (const char c : line.substr(0, kMaxChangeBytes)) {
        const auto byte = static_cast<unsigned char>(c);
        clean += (byte < 0x20U || byte == 0x7FU) ? ' ' : c;
    }
    return clean;
}

std::multiset<std::string> lines_of(const std::string& text) {
    std::multiset<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.insert(std::move(line));
        start = end + 1;
    }
    return lines;
}

} // namespace

std::vector<std::string> changed_lines(const std::string& sealed, const std::string& found) {
    const auto before = lines_of(sealed);
    const auto after = lines_of(found);
    std::vector<std::string> removed;
    std::vector<std::string> added;
    std::ranges::set_difference(before, after, std::back_inserter(removed));
    std::ranges::set_difference(after, before, std::back_inserter(added));
    std::vector<std::string> changes;
    changes.reserve(removed.size() + added.size());
    for (const auto& line : removed) {
        changes.push_back("- " + clean_line(line));
    }
    for (const auto& line : added) {
        changes.push_back("+ " + clean_line(line));
    }
    if (changes.size() > kMaxChanges) {
        const std::size_t hidden = changes.size() - kMaxChanges;
        changes.resize(kMaxChanges);
        changes.push_back("… (" + std::to_string(hidden) + " ligne(s) de plus)");
    }
    return changes;
}

SealedSettings::SealedSettings(std::filesystem::path config_path, SecretStore& secrets)
    : config_path_(std::move(config_path)), secrets_(&secrets) {
    const std::string tag = sodium_ready() ? path_tag(config_path_) : std::string("0");
    key_name_ = "settings-key-" + tag;
    seal_name_ = "settings-seal-" + tag;
}

fs::path SealedSettings::seal_path() const {
    fs::path path = config_path_;
    path += ".seal";
    return path;
}

core::Result<std::string> SealedSettings::key(bool create) {
    auto stored = secrets_->read(key_name_);
    if (!stored) {
        // Trousseau muet : une clé peut y exister. Une nouvelle n'est créée
        // (dans le repli) que si rien n'a encore été scellé, sinon elle ne
        // validerait plus le sceau existant une fois le trousseau revenu.
        std::error_code error;
        if (!create || fs::exists(seal_path(), error)) {
            return std::unexpected(std::move(stored.error()));
        }
    } else if (*stored) {
        return std::move(**stored);
    } else if (!create) {
        return std::string{};
    }
    std::array<unsigned char, crypto_auth_hmacsha256_KEYBYTES> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    std::string fresh = to_hex(bytes.data(), bytes.size());
    sodium_memzero(bytes.data(), bytes.size());
    if (auto written = secrets_->write(key_name_, fresh); !written) {
        return std::unexpected(std::move(written.error()));
    }
    return fresh;
}

core::Result<void> SealedSettings::seal(const std::string& key, const std::string& content) {
    const auto bytes = from_hex(key);
    if (!bytes || bytes->size() != crypto_auth_hmacsha256_KEYBYTES) {
        return seal_error("clé du sceau illisible");
    }
    const std::string mac = hmac(*bytes, content);
    const nlohmann::json seal_file = {{"format", kSealFormat}, {"hmac", mac}, {"content", content}};
    // Sceau, puis trousseau (contre le rejeu), puis fichier : interrompu en
    // route, le prochain lancement remet la version scellée.
    if (auto written = write_private(seal_path(), seal_file.dump(2) + "\n"); !written) {
        return written;
    }
    if (auto written = secrets_->write(seal_name_, mac); !written) {
        return written;
    }
    return write_private(config_path_, content);
}

core::Result<void> SealedSettings::save(const core::AppConfig& config) {
    if (!sodium_ready()) {
        return seal_error("libsodium indisponible");
    }
    auto current = key(/*create=*/true);
    if (!current) {
        return std::unexpected(std::move(current.error()));
    }
    return seal(*current, core::format_config(config));
}

core::Result<SealReport> SealedSettings::verify() {
    using Status = SealReport::Status;
    if (!sodium_ready()) {
        return seal_error("libsodium indisponible");
    }
    auto content = read_file(config_path_);
    auto seal_text = read_file(seal_path());
    if (!content) {
        return std::unexpected(std::move(content.error()));
    }
    if (!seal_text) {
        return std::unexpected(std::move(seal_text.error()));
    }
    const std::optional<SealFile> sealed =
        *seal_text ? parse_seal(**seal_text) : std::optional<SealFile>{};

    auto stored_key = key(/*create=*/false);
    if (!stored_key) {
        if (*seal_text) {
            return SealReport{.status = Status::Unverified,
                              .message = "Sceau des réglages non vérifié : " +
                                         core::describe(stored_key.error()),
                              .changes = {}};
        }
        // Trousseau muet et rien de scellé : première adoption, dans le
        // repli (le magasin le sait).
        stored_key = std::string{};
    }

    // Aucune clé : réglages jamais scellés (ou trousseau effacé).
    if (stored_key->empty()) {
        if (!*content) {
            return SealReport{.status = Status::NoSettings, .message = {}, .changes = {}};
        }
        auto fresh = key(/*create=*/true);
        if (!fresh) {
            return std::unexpected(std::move(fresh.error()));
        }
        if (auto sealed_now = seal(*fresh, **content); !sealed_now) {
            return std::unexpected(std::move(sealed_now.error()));
        }
        std::string message = "Réglages scellés : désormais, une modification faite hors "
                              "d'Avenoir sera refusée et la version scellée remise.";
        if (*seal_text) {
            message += " (Sceau précédent invérifiable : clé absente du trousseau.)";
        }
        return SealReport{.status = Status::Adopted, .message = std::move(message), .changes = {}};
    }

    // Ni fichier ni sceau (après un refus, ou fichiers effacés ensemble) :
    // rien à vérifier ni à remettre, la création guidée refera des réglages.
    if (!*content && !*seal_text) {
        return SealReport{.status = Status::NoSettings, .message = {}, .changes = {}};
    }
    const auto key_bytes = from_hex(*stored_key);
    if (!key_bytes || key_bytes->size() != crypto_auth_hmacsha256_KEYBYTES) {
        return seal_error("clé du sceau illisible dans " + secrets_->description());
    }
    auto last_seal = secrets_->read(seal_name_);
    if (!last_seal) {
        return std::unexpected(std::move(last_seal.error()));
    }
    const bool seal_valid = sealed && same_seal(hmac(*key_bytes, sealed->content), sealed->mac);
    const bool seal_current = seal_valid && (!*last_seal || same_seal(sealed->mac, **last_seal));

    if (!seal_current) {
        // Pas de version sûre à remettre : le fichier est mis de côté.
        std::string reason = "sceau d'une version précédente (rejeu)";
        if (!*seal_text) {
            reason = "sceau supprimé";
        } else if (!seal_valid) {
            reason = "sceau falsifié";
        }
        std::vector<std::string> changes;
        if (*content) {
            fs::path aside = config_path_;
            aside += ".rejected";
            std::error_code error;
            fs::rename(config_path_, aside, error);
            if (error) {
                return seal_error("fichier refusé impossible à mettre de côté : " +
                                  error.message());
            }
            reason += ", fichier mis de côté : " + aside.string();
        }
        std::error_code error;
        fs::remove(seal_path(), error);
        return SealReport{.status = Status::Rejected,
                          .message = "Réglages refusés (" + reason +
                                     ") ; aucune version scellée à remettre.",
                          .changes = std::move(changes)};
    }
    if (!*last_seal) {
        // Sceau valide d'avant la protection contre le rejeu : on la pose.
        if (auto written = secrets_->write(seal_name_, sealed->mac); !written) {
            return std::unexpected(std::move(written.error()));
        }
    }

    if (*content && same_seal(hmac(*key_bytes, **content), sealed->mac)) {
        return SealReport{.status = Status::Valid, .message = {}, .changes = {}};
    }
    // Modifié ou supprimé hors d'Avenoir : la version scellée est remise.
    auto changes = changed_lines(sealed->content, content->value_or(std::string{}));
    if (auto restored = write_private(config_path_, sealed->content); !restored) {
        return std::unexpected(std::move(restored.error()));
    }
    return SealReport{.status = Status::Restored,
                      .message =
                          *content ? "Modification des réglages faite hors d'Avenoir refusée : la "
                                     "version scellée a été remise."
                                   : "Fichier des réglages supprimé hors d'Avenoir : la version "
                                     "scellée a été remise.",
                      .changes = std::move(changes)};
}

} // namespace avenoir::storage
