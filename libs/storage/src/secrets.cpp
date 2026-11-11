#include "avenoir/storage/secrets.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "avenoir/core/error.hpp"

#include "platform.hpp"

namespace avenoir::storage {

namespace {

constexpr std::size_t kMaxNameBytes = 100;
constexpr std::size_t kMaxValueBytes = 4096;

std::unexpected<core::Error> secret_error(std::string message) {
    return core::make_error(core::ErrorCode::Config, std::move(message));
}

bool is_printable_ascii(std::string_view value) {
    return value.size() <= kMaxValueBytes &&
           std::ranges::all_of(value, [](char c) { return c >= 0x20 && c < 0x7F; });
}

/// Trousseau du système d'abord, fichier ensuite : un secret écrit dans le
/// fichier quand le trousseau manquait reste lu ; une écriture va au
/// trousseau s'il répond.
class LayeredSecretStore final : public SecretStore {
  public:
    LayeredSecretStore(std::unique_ptr<SecretStore> keychain, std::filesystem::path directory)
        : keychain_(std::move(keychain)), file_(std::move(directory)) {}

    core::Result<std::optional<std::string>> read(std::string_view name) override {
        std::optional<core::Error> keychain_error;
        if (keychain_) {
            auto value = keychain_->read(name);
            if (value && *value) {
                return value;
            }
            if (!value) {
                keychain_failed_ = true;
                keychain_error = std::move(value.error());
            }
        }
        auto value = file_.read(name);
        if (value && *value) {
            in_file_ = true;
            return value;
        }
        // Trousseau muet et rien dans le fichier : on ne sait pas si le
        // secret existe (trousseau verrouillé ?), ce n'est pas « absent ».
        if (keychain_error) {
            return std::unexpected(std::move(*keychain_error));
        }
        return value;
    }

    core::Result<void> write(std::string_view name, std::string_view value) override {
        if (keychain_ && !keychain_failed_ && !in_file_) {
            if (auto written = keychain_->write(name, value)) {
                return written;
            }
            keychain_failed_ = true;
        }
        in_file_ = true;
        return file_.write(name, value);
    }

    [[nodiscard]] std::string description() const override {
        return is_system_keychain() ? keychain_->description() : file_.description();
    }

    [[nodiscard]] bool is_system_keychain() const override {
        return keychain_ && !keychain_failed_ && !in_file_;
    }

  private:
    std::unique_ptr<SecretStore> keychain_;
    FileSecretStore file_;
    bool keychain_failed_ = false; ///< Le trousseau a refusé de répondre.
    bool in_file_ = false;         ///< Des secrets sont gardés dans le fichier.
};

} // namespace

bool is_valid_secret_name(std::string_view name) {
    return !name.empty() && name.size() <= kMaxNameBytes && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

core::Result<std::optional<std::string>> MemorySecretStore::read(std::string_view name) {
    if (!is_valid_secret_name(name)) {
        return secret_error("nom de secret invalide");
    }
    const auto found = values_.find(name);
    if (found == values_.end()) {
        return std::optional<std::string>{};
    }
    return std::optional<std::string>{found->second};
}

core::Result<void> MemorySecretStore::write(std::string_view name, std::string_view value) {
    if (!is_valid_secret_name(name) || !is_printable_ascii(value)) {
        return secret_error("secret invalide");
    }
    values_.insert_or_assign(std::string(name), std::string(value));
    return {};
}

FileSecretStore::FileSecretStore(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

std::string FileSecretStore::description() const {
    return "fichier lisible par vous seul (" + directory_.string() + ")";
}

core::Result<std::optional<std::string>> FileSecretStore::read(std::string_view name) {
    if (!is_valid_secret_name(name)) {
        return secret_error("nom de secret invalide");
    }
    const std::filesystem::path path = directory_ / name;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return std::optional<std::string>{};
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return secret_error("lecture impossible : " + path.string());
    }
    std::string value((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!is_printable_ascii(value)) {
        return secret_error("secret illisible : " + path.string());
    }
    return std::optional<std::string>{std::move(value)};
}

core::Result<void> FileSecretStore::write(std::string_view name, std::string_view value) {
    namespace fs = std::filesystem;
    if (!is_valid_secret_name(name) || !is_printable_ascii(value)) {
        return secret_error("secret invalide");
    }
    std::error_code error;
    fs::create_directories(directory_, error);
    if (error) {
        return secret_error("dossier des secrets impossible à créer : " + error.message());
    }
    fs::permissions(directory_, fs::perms::owner_all, fs::perm_options::replace, error);
    const fs::path path = directory_ / name;
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return secret_error("écriture impossible : " + temporary.string());
        }
        // Droits restreints avant d'écrire le secret.
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, error);
        file << value;
        file.flush();
        if (!file) {
            file.close();
            fs::remove(temporary, error);
            return secret_error("écriture impossible : " + temporary.string());
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        const std::string reason = error.message();
        fs::remove(temporary, error);
        return secret_error("écriture impossible : " + path.string() + " (" + reason + ")");
    }
    return {};
}

std::unique_ptr<SecretStore> open_secret_store(const std::filesystem::path& fallback_directory) {
    return std::make_unique<LayeredSecretStore>(platform::system_keychain(), fallback_directory);
}

} // namespace avenoir::storage
