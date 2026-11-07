// Module de test de l'hôte : parle JSON-RPC sur stdio comme un vrai module
// (protocole avenoir, ou MCP avec --mcp) et sait mal se comporter à la
// demande (plantage, lenteur, réponses piégées). Aucun accès au réseau.
//
// Options : --mcp, --paginate, --exit-at-start, --no-initialize.

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

using nlohmann::json;

struct Options {
    bool mcp = false;
    bool paginate = false;
    bool no_initialize = false;
};

void send(const json& message) {
    std::cout << message.dump() << '\n' << std::flush;
}

void reply(const json& id, json result) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
}

json text_result(const std::string& text, bool is_error = false) {
    return {{"content", json::array({{{"type", "text"}, {"text", text}}})}, {"isError", is_error}};
}

json tool(const std::string& name, json properties = json::object()) {
    return {{"name", name},
            {"description", "outil de test " + name},
            {"inputSchema", {{"type", "object"}, {"properties", std::move(properties)}}}};
}

json all_tools() {
    return json::array({
        tool("echo", {{"text", {{"type", "string"}}}}),
        tool("slow", {{"ms", {{"type", "integer"}}}}),
        tool("crash"),
        tool("fail"),
        tool("set_value", {{"value", {{"type", "string"}}}}),
        tool("env", {{"name", {{"type", "string"}}}}),
        tool("noisy"),
        tool("host_call"),
        tool("undeclared"),
        // Schéma invalide : doit être refusé par l'hôte.
        {{"name", "bad_schema"}, {"description", "x"}, {"inputSchema", {{"type", "string"}}}},
    });
}

/// Valeur modifiée par set_value et restaurée par avenoir/undo.
std::string& current_value() {
    static std::string value = "initiale";
    return value;
}

json call_tool(const std::string& name, const json& args) {
    if (name == "echo") {
        return text_result("écho : " + args.value("text", std::string()));
    }
    if (name == "slow") {
        std::this_thread::sleep_for(std::chrono::milliseconds(args.value("ms", 0)));
        return text_result("fini");
    }
    if (name == "crash") {
        std::exit(3); // NOLINT(concurrency-mt-unsafe)
    }
    if (name == "fail") {
        return text_result("échec voulu", true);
    }
    if (name == "set_value") {
        const std::string previous = current_value();
        current_value() = args.value("value", std::string());
        json result = text_result("valeur : " + current_value());
        result["_meta"] = {
            {"avenoir/undo",
             {{"description", "remettre la valeur « " + previous + " »"}, {"token", previous}}}};
        return result;
    }
    if (name == "env") {
        const std::string variable = args.value("name", std::string());
#ifdef _WIN32
        char* raw = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&raw, &size, variable.c_str()) != 0 || raw == nullptr) {
            return text_result("(absente)");
        }
        const std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
        return text_result(owned.get());
#else
        const char* value = std::getenv(variable.c_str()); // NOLINT(concurrency-mt-unsafe)
        return text_result(value == nullptr ? "(absente)" : value);
#endif
    }
    if (name == "noisy") {
        std::cout << "ceci n'est pas du JSON\n" << std::flush;
        std::cerr << "\x1b[31mrouge\x1b[0m sur stderr\n" << std::flush;
        // NOLINTNEXTLINE(misc-misleading-bidirectional) : piège voulu.
        return text_result("\x1b[2Jécran effacé\x07 et \xe2\x80\xae inversé");
    }
    if (name == "host_call") {
        // Requête du module vers l'hôte : doit être refusée proprement.
        send({{"jsonrpc", "2.0"},
              {"id", "h1"},
              {"method", "sampling/createMessage"},
              {"params", {}}});
        std::string line;
        std::getline(std::cin, line);
        const json answer = json::parse(line);
        return text_result("refusé : " + answer["error"]["message"].get<std::string>());
    }
    return text_result("outil inconnu", true);
}

void handle(const json& message, const Options& options) {
    if (!message.contains("id")) {
        return; // Notification (initialized, cancelled) : rien à répondre.
    }
    const json& id = message["id"];
    const std::string method = message.value("method", std::string());
    const json params = message.value("params", json::object());

    if (method == "initialize") {
        if (options.no_initialize) {
            return; // L'hôte doit abandonner après son délai.
        }
        reply(id, {{"protocolVersion", "2025-06-18"},
                   {"capabilities", {{"tools", json::object()}}},
                   {"serverInfo", {{"name", "fixture"}, {"version", "1"}}}});
    } else if (method == "tools/list") {
        json tools = all_tools();
        if (options.paginate && !params.contains("cursor")) {
            reply(id, {{"tools", json::array({tools[0]})}, {"nextCursor", "2"}});
            return;
        }
        if (options.paginate) {
            tools.erase(tools.begin());
        }
        reply(id, {{"tools", tools}});
    } else if (method == "tools/call") {
        reply(id, call_tool(params.value("name", std::string()),
                            params.value("arguments", json::object())));
    } else if (method == "avenoir/undo" && !options.mcp) {
        const std::string token = params.value("token", std::string());
        current_value() = token;
        reply(id, text_result("valeur restaurée : " + token));
    } else {
        send({{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", -32601}, {"message", "méthode inconnue"}}}});
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (const std::string_view arg : std::span(argv, static_cast<std::size_t>(argc)).subspan(1)) {
        if (arg == "--mcp") {
            options.mcp = true;
        } else if (arg == "--paginate") {
            options.paginate = true;
        } else if (arg == "--no-initialize") {
            options.no_initialize = true;
        } else if (arg == "--exit-at-start") {
            return 2;
        }
    }
    try {
        std::cerr << "module de test prêt\n" << std::flush;
        std::string line;
        while (std::getline(std::cin, line)) {
            const json message = json::parse(line, nullptr, false);
            if (message.is_object()) {
                handle(message, options);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "erreur : " << e.what() << '\n';
        return 1;
    }
    return 0;
}
