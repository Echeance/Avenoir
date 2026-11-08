#include "avenoir/storage/store.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace avenoir::storage {

namespace {

/// Version du schéma (PRAGMA user_version). À incrémenter avec chaque
/// migration ajoutée dans `migrate`.
constexpr int kSchemaVersion = 2;

struct DatabaseDeleter {
    void operator()(sqlite3* db) const noexcept { sqlite3_close_v2(db); }
};
struct StatementDeleter {
    void operator()(sqlite3_stmt* statement) const noexcept { sqlite3_finalize(statement); }
};
using Database = std::unique_ptr<sqlite3, DatabaseDeleter>;

std::unexpected<core::Error> db_error(sqlite3* db, std::string_view action) {
    return core::make_error(core::ErrorCode::Config,
                            "base de conversations : " + std::string(action) + " : " +
                                sqlite3_errmsg(db));
}

/// Requête préparée. Les chaînes liées doivent rester valides jusqu'à la fin
/// de son exécution (liaison sans copie).
class Statement {
  public:
    static core::Result<Statement> prepare(sqlite3* db, std::string_view sql) {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &raw, nullptr) !=
            SQLITE_OK) {
            return db_error(db, "préparation");
        }
        return Statement(db, raw);
    }

    Statement& bind(int index, std::int64_t value) {
        if (sqlite3_bind_int64(statement_.get(), index, value) != SQLITE_OK) {
            ok_ = false;
        }
        return *this;
    }

    Statement& bind(int index, std::string_view value) {
        // Destructeur nul (SQLITE_STATIC) : pas de copie, la chaîne doit vivre
        // jusqu'à la fin de l'exécution.
        if (sqlite3_bind_text(statement_.get(), index, value.data(), static_cast<int>(value.size()),
                              nullptr) != SQLITE_OK) {
            ok_ = false;
        }
        return *this;
    }

    /// Avance d'une ligne : vrai s'il y a une ligne, faux à la fin.
    core::Result<bool> step() {
        if (!ok_) {
            return db_error(db_, "liaison des paramètres");
        }
        const int code = sqlite3_step(statement_.get());
        if (code == SQLITE_ROW) {
            return true;
        }
        if (code == SQLITE_DONE) {
            return false;
        }
        return db_error(db_, "exécution");
    }

    /// Exécute une requête sans résultat.
    core::Result<void> run() {
        auto row = step();
        if (!row) {
            return std::unexpected(std::move(row.error()));
        }
        return {};
    }

    [[nodiscard]] std::int64_t integer(int column) const {
        return sqlite3_column_int64(statement_.get(), column);
    }

    [[nodiscard]] std::string text(int column) const {
        const unsigned char* data = sqlite3_column_text(statement_.get(), column);
        const int size = sqlite3_column_bytes(statement_.get(), column);
        if (data == nullptr || size <= 0) {
            return {};
        }
        // API C de SQLite : texte UTF-8 rendu en unsigned char.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return {reinterpret_cast<const char*>(data), static_cast<std::size_t>(size)};
    }

  private:
    Statement(sqlite3* db, sqlite3_stmt* raw) : db_(db), statement_(raw) {}

    sqlite3* db_;
    std::unique_ptr<sqlite3_stmt, StatementDeleter> statement_;
    bool ok_ = true;
};

core::Result<void> execute(sqlite3* db, const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string text = message != nullptr ? message : "erreur inconnue";
        sqlite3_free(message);
        return core::make_error(core::ErrorCode::Config, "base de conversations : " + text);
    }
    return {};
}

core::Result<void> migrate(sqlite3* db) {
    auto version_query = Statement::prepare(db, "PRAGMA user_version");
    if (!version_query) {
        return std::unexpected(std::move(version_query.error()));
    }
    auto has_row = version_query->step();
    if (!has_row) {
        return std::unexpected(std::move(has_row.error()));
    }
    const auto version = *has_row ? version_query->integer(0) : 0;

    if (version > kSchemaVersion) {
        return core::make_error(core::ErrorCode::Config,
                                "base de conversations créée par une version plus récente "
                                "d'Avenoir (schéma " +
                                    std::to_string(version) + ")");
    }
    if (version < 1) {
        if (auto created = execute(db, R"(
            BEGIN;
            CREATE TABLE conversations (
                id         INTEGER PRIMARY KEY,
                title      TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now')),
                updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now')),
                -- Compteur strictement croissant : ordre fiable même quand
                -- deux actions tombent dans la même milliseconde.
                activity   INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE messages (
                id              INTEGER PRIMARY KEY,
                conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
                role            TEXT NOT NULL,
                content         TEXT NOT NULL,
                created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now'))
            );
            CREATE INDEX messages_by_conversation ON messages(conversation_id, id);
            PRAGMA user_version = 1;
            COMMIT;
        )");
            !created) {
            return created;
        }
    }
    if (version < 2) {
        // Risque des outils de modules choisi par l'utilisateur (/risque).
        if (auto created = execute(db, R"(
            BEGIN;
            CREATE TABLE tool_risks (
                tool      TEXT PRIMARY KEY,
                risk      TEXT NOT NULL,
                -- 1 si « Je sais ce que je fais » a été saisi (baisse sous
                -- le risque du manifeste).
                agreed    INTEGER NOT NULL DEFAULT 0,
                chosen_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now'))
            );
            PRAGMA user_version = 2;
            COMMIT;
        )");
            !created) {
            return created;
        }
    }
    return {};
}

/// Colonnes communes aux requêtes qui renvoient des conversations.
constexpr std::string_view kConversationColumns =
    "SELECT c.id, c.title, datetime(c.created_at, 'localtime'), "
    "datetime(c.updated_at, 'localtime'), "
    "(SELECT COUNT(*) FROM messages m WHERE m.conversation_id = c.id) "
    "FROM conversations c ";

ConversationInfo read_conversation(const Statement& row) {
    return ConversationInfo{
        .id = row.integer(0),
        .title = row.text(1),
        .created_at = row.text(2),
        .updated_at = row.text(3),
        .message_count = static_cast<std::size_t>(row.integer(4)),
    };
}

} // namespace

struct Store::Impl {
    Database db;

    [[nodiscard]] core::Result<std::vector<ConversationInfo>>
    query_conversations(const std::string& sql, std::optional<std::int64_t> id) const {
        auto statement = Statement::prepare(db.get(), sql);
        if (!statement) {
            return std::unexpected(std::move(statement.error()));
        }
        if (id) {
            statement->bind(1, *id);
        }
        std::vector<ConversationInfo> out;
        while (true) {
            auto row = statement->step();
            if (!row) {
                return std::unexpected(std::move(row.error()));
            }
            if (!*row) {
                return out;
            }
            out.push_back(read_conversation(*statement));
        }
    }

    [[nodiscard]] core::Result<void> touch(std::int64_t id) const {
        auto statement = Statement::prepare(
            db.get(), "UPDATE conversations SET updated_at = strftime('%Y-%m-%d %H:%M:%f', 'now'), "
                      "activity = (SELECT COALESCE(MAX(activity), 0) + 1 FROM conversations) "
                      "WHERE id = ?1");
        if (!statement) {
            return std::unexpected(std::move(statement.error()));
        }
        return statement->bind(1, id).run();
    }
};

Store::Store(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;
Store::~Store() = default;

core::Result<Store> Store::open(const std::filesystem::path& path) {
    const std::string location = path.string();
    sqlite3* raw = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const int code = sqlite3_open_v2(location.c_str(), &raw, flags, nullptr);
    Database db(raw);
    if (code != SQLITE_OK) {
        return db_error(db.get(), "ouverture de « " + location + " »");
    }
    // Clés étrangères (suppression en cascade), écriture sûre en cas de
    // plantage (WAL), attente si la base est momentanément occupée.
    if (auto pragmas = execute(db.get(), "PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL; "
                                         "PRAGMA busy_timeout = 5000;");
        !pragmas) {
        return std::unexpected(std::move(pragmas.error()));
    }
    if (auto migrated = migrate(db.get()); !migrated) {
        return std::unexpected(std::move(migrated.error()));
    }
    if (location != ":memory:") {
        // Lisible par l'utilisateur seul (les fichiers -wal et -shm de SQLite
        // reprennent les droits de la base).
        std::error_code error;
        std::filesystem::permissions(
            path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, error);
    }
    return Store(std::make_unique<Impl>(Impl{.db = std::move(db)}));
}

core::Result<Store> Store::open_in_memory() {
    return open(":memory:");
}

core::Result<ConversationInfo> Store::create_conversation(std::string_view title) {
    auto statement = Statement::prepare(
        impl_->db.get(), "INSERT INTO conversations (title, activity) VALUES "
                         "(?1, (SELECT COALESCE(MAX(activity), 0) + 1 FROM conversations))");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    if (auto inserted = statement->bind(1, title).run(); !inserted) {
        return std::unexpected(std::move(inserted.error()));
    }
    const std::int64_t id = sqlite3_last_insert_rowid(impl_->db.get());
    auto created = conversation(id);
    if (!created) {
        return std::unexpected(std::move(created.error()));
    }
    if (!*created) {
        return core::make_error(core::ErrorCode::Config, "conversation créée introuvable");
    }
    return std::move(**created);
}

core::Result<std::vector<ConversationInfo>> Store::list_conversations() const {
    return impl_->query_conversations(
        std::string(kConversationColumns) + "ORDER BY c.activity DESC", std::nullopt);
}

core::Result<std::optional<ConversationInfo>> Store::conversation(std::int64_t id) const {
    auto found =
        impl_->query_conversations(std::string(kConversationColumns) + "WHERE c.id = ?1", id);
    if (!found) {
        return std::unexpected(std::move(found.error()));
    }
    if (found->empty()) {
        return std::optional<ConversationInfo>{};
    }
    return std::optional<ConversationInfo>{std::move(found->front())};
}

core::Result<std::optional<ConversationInfo>> Store::most_recent() const {
    auto found = impl_->query_conversations(
        std::string(kConversationColumns) + "ORDER BY c.activity DESC LIMIT 1", std::nullopt);
    if (!found) {
        return std::unexpected(std::move(found.error()));
    }
    if (found->empty()) {
        return std::optional<ConversationInfo>{};
    }
    return std::optional<ConversationInfo>{std::move(found->front())};
}

core::Result<void> Store::rename(std::int64_t id, std::string_view title) {
    auto statement =
        Statement::prepare(impl_->db.get(), "UPDATE conversations SET title = ?1 WHERE id = ?2");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    return statement->bind(1, title).bind(2, id).run();
}

core::Result<void> Store::remove(std::int64_t id) {
    auto statement = Statement::prepare(impl_->db.get(), "DELETE FROM conversations WHERE id = ?1");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    return statement->bind(1, id).run();
}

core::Result<void> Store::append_message(std::int64_t conversation, const core::Message& message) {
    const nlohmann::json json = detail::message_to_json(message);
    const std::string role = json["role"].get<std::string>();
    const std::string content = json.dump();
    auto statement = Statement::prepare(
        impl_->db.get(),
        "INSERT INTO messages (conversation_id, role, content) VALUES (?1, ?2, ?3)");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    if (auto inserted = statement->bind(1, conversation).bind(2, role).bind(3, content).run();
        !inserted) {
        return inserted;
    }
    return impl_->touch(conversation);
}

core::Result<void> Store::remove_last_message(std::int64_t conversation) {
    auto statement = Statement::prepare(
        impl_->db.get(), "DELETE FROM messages WHERE id = "
                         "(SELECT MAX(id) FROM messages WHERE conversation_id = ?1)");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    return statement->bind(1, conversation).run();
}

core::Result<void> Store::clear_messages(std::int64_t conversation) {
    auto statement =
        Statement::prepare(impl_->db.get(), "DELETE FROM messages WHERE conversation_id = ?1");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    if (auto cleared = statement->bind(1, conversation).run(); !cleared) {
        return cleared;
    }
    return impl_->touch(conversation);
}

core::Result<std::vector<core::Message>> Store::messages(std::int64_t conversation) const {
    auto statement = Statement::prepare(
        impl_->db.get(), "SELECT content FROM messages WHERE conversation_id = ?1 ORDER BY id");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    statement->bind(1, conversation);
    std::vector<core::Message> out;
    while (true) {
        auto row = statement->step();
        if (!row) {
            return std::unexpected(std::move(row.error()));
        }
        if (!*row) {
            return out;
        }
        const nlohmann::json json =
            nlohmann::json::parse(statement->text(0), nullptr, /*allow_exceptions=*/false);
        auto message = detail::message_from_json(json);
        if (!message) {
            return std::unexpected(std::move(message.error()));
        }
        out.push_back(std::move(*message));
    }
}

// ─── Risque des outils de modules ───────────────────────────────────────────

core::Result<std::vector<RiskChoice>> Store::risk_choices() const {
    auto statement = Statement::prepare(
        impl_->db.get(), "SELECT tool, risk, agreed, datetime(chosen_at, 'localtime') "
                         "FROM tool_risks ORDER BY tool");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    std::vector<RiskChoice> out;
    while (true) {
        auto row = statement->step();
        if (!row) {
            return std::unexpected(std::move(row.error()));
        }
        if (!*row) {
            return out;
        }
        const auto risk = core::risk_level_from_string(statement->text(1));
        if (!risk) {
            continue; // Valeur inconnue : ignorée plutôt que mal interprétée.
        }
        out.push_back(RiskChoice{.tool = statement->text(0),
                                 .risk = *risk,
                                 .agreed = statement->integer(2) != 0,
                                 .chosen_at = statement->text(3)});
    }
}

core::Result<void> Store::set_risk_choice(std::string_view tool, core::RiskLevel risk,
                                          bool agreed) {
    auto statement = Statement::prepare(
        impl_->db.get(),
        "INSERT INTO tool_risks (tool, risk, agreed) VALUES (?1, ?2, ?3) "
        "ON CONFLICT(tool) DO UPDATE SET risk = excluded.risk, agreed = excluded.agreed, "
        "chosen_at = strftime('%Y-%m-%d %H:%M:%f', 'now')");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    return statement->bind(1, tool)
        .bind(2, core::to_string(risk))
        .bind(3, std::int64_t{agreed ? 1 : 0})
        .run();
}

core::Result<void> Store::remove_risk_choice(std::string_view tool) {
    auto statement = Statement::prepare(impl_->db.get(), "DELETE FROM tool_risks WHERE tool = ?1");
    if (!statement) {
        return std::unexpected(std::move(statement.error()));
    }
    return statement->bind(1, tool).run();
}

} // namespace avenoir::storage
