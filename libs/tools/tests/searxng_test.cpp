// Instance SearXNG locale : faux lanceur de commandes, aucun conteneur
// réellement démarré, aucun accès réseau.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/tools/searxng.hpp"
#include "avenoir/tools/web.hpp"

using namespace avenoir;
using namespace std::chrono_literals;

namespace {

/// Faux podman : retient les commandes, répond selon un petit état.
struct FakeRuntime {
    bool has_podman = true;
    bool has_docker = false;
    bool has_image = false;
    bool pull_ok = true;
    bool leftover = false; ///< Conteneur laissé par un lancement précédent.
    bool running = false;
    std::vector<std::string> commands;

    static std::string join(const std::vector<std::string>& arguments) {
        std::string line;
        for (const auto& argument : arguments) {
            line += (line.empty() ? "" : " ") + argument;
        }
        return line;
    }

    core::Result<tools::CommandResult> run(const std::vector<std::string>& a) {
        commands.push_back(join(a));
        const auto result = [](int code, std::string output = {}) {
            return tools::CommandResult{.exit_code = code, .output = std::move(output)};
        };
        if (a.size() == 2 && a[1] == "--version") {
            const bool present = a[0] == "podman" ? has_podman : has_docker;
            return result(present ? 0 : 127);
        }
        if (a[1] == "image") {
            return result(has_image ? 0 : 1);
        }
        if (a[1] == "pull") {
            has_image = pull_ok;
            return result(pull_ok ? 0 : 1);
        }
        if (a[1] == "port") {
            return (running || leftover) ? result(0, "127.0.0.1:43123\n") : result(1);
        }
        if (a[1] == "rm") {
            leftover = false;
            return result(0);
        }
        if (a[1] == "run") {
            running = true;
            return result(0, "abc123\n");
        }
        if (a[1] == "stop") {
            running = false;
            return result(0);
        }
        return result(1);
    }
};

struct Fixture {
    std::shared_ptr<FakeRuntime> runtime = std::make_shared<FakeRuntime>();
    std::shared_ptr<std::vector<std::string>> notes = std::make_shared<std::vector<std::string>>();
    std::shared_ptr<bool> ready = std::make_shared<bool>(true);
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("avenoir-searxng-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Fixture() = default;
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

    [[nodiscard]] tools::SearxngOptions options() const {
        return tools::SearxngOptions{
            .config_dir = dir,
            .idle = std::chrono::minutes{10},
            .notify = [notes = notes](std::string_view text) { notes->emplace_back(text); },
            .run = [runtime = runtime](
                       const std::vector<std::string>& arguments, std::chrono::seconds /*timeout*/,
                       const std::stop_token& /*stop*/) { return runtime->run(arguments); },
            .ready = [ready = ready](const std::string& /*base*/) { return *ready; },
            .runtimes = {"podman", "docker"},
            .ready_timeout = 200ms,
            .poll_interval = 10ms,
            .idle_check = 0ms,
        };
    }
};

bool contains(const std::vector<std::string>& lines, std::string_view part) {
    return std::ranges::any_of(lines,
                               [part](const std::string& line) { return line.contains(part); });
}

} // namespace

TEST_CASE("SearXNG : premier lancement, image téléchargée d'elle-même", "[searxng]") {
    const Fixture f;
    tools::SearxngHost host(f.options());
    const auto base = host.ensure_started({});
    REQUIRE(base.has_value());
    CHECK(*base == "http://127.0.0.1:43123");
    CHECK(host.running());
    CHECK(contains(f.runtime->commands, "podman pull " + std::string(tools::SearxngHost::kImage)));
    REQUIRE(f.notes->size() == 1); // téléchargement annoncé
    CHECK(f.notes->front().contains("200 Mo"));
    // Boucle locale seulement, configuration montée, supprimé à l'arrêt.
    CHECK(contains(f.runtime->commands,
                   "podman run --detach --rm --name avenoir-searxng --publish 127.0.0.1::8080"));
    // Configuration écrite : JSON activé, clé secrète aléatoire.
    std::ifstream file(f.dir / "settings.yml");
    const std::string settings((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    CHECK(settings.contains("- json"));
    CHECK(settings.contains("limiter: false"));
    CHECK_FALSE(settings.contains("secret_key: \"\""));

    // Déjà lancé : ni téléchargement ni nouveau conteneur.
    const auto count = f.runtime->commands.size();
    REQUIRE(host.ensure_started({}).has_value());
    CHECK(f.runtime->commands.size() == count);
}

TEST_CASE("SearXNG : arrêt après inactivité et à la fermeture", "[searxng]") {
    const Fixture f;
    f.runtime->has_image = true;
    {
        tools::SearxngHost host(f.options());
        REQUIRE(host.ensure_started({}).has_value());
        CHECK(f.notes->empty()); // image déjà là : rien à annoncer
        const auto now = std::chrono::steady_clock::now();
        CHECK_FALSE(host.stop_if_idle(now + 9min));
        CHECK(host.running());
        CHECK(host.stop_if_idle(now + 11min));
        CHECK_FALSE(host.running());
        CHECK(contains(f.runtime->commands, "podman stop --time 2 avenoir-searxng"));

        // Relancé à la recherche suivante, puis arrêté à la fermeture.
        REQUIRE(host.ensure_started({}).has_value());
        CHECK(f.runtime->running);
    }
    CHECK_FALSE(f.runtime->running);
}

TEST_CASE("SearXNG : docker si pas de podman, reprise d'une instance laissée", "[searxng]") {
    const Fixture f;
    f.runtime->has_podman = false;
    f.runtime->has_docker = true;
    f.runtime->has_image = true;
    f.runtime->leftover = true;
    tools::SearxngHost host(f.options());
    REQUIRE(host.ensure_started({}).has_value());
    CHECK(contains(f.runtime->commands, "docker port avenoir-searxng 8080"));
    CHECK_FALSE(contains(f.runtime->commands, "docker run")); // reprise
}

TEST_CASE("SearXNG : échecs", "[searxng]") {
    SECTION("ni podman ni docker") {
        const Fixture f;
        f.runtime->has_podman = false;
        tools::SearxngHost host(f.options());
        const auto base = host.ensure_started({});
        REQUIRE_FALSE(base.has_value());
        CHECK(base.error().message.contains("podman"));
    }
    SECTION("téléchargement impossible") {
        const Fixture f;
        f.runtime->pull_ok = false;
        tools::SearxngHost host(f.options());
        CHECK_FALSE(host.ensure_started({}).has_value());
        CHECK_FALSE(f.runtime->running);
    }
    SECTION("ne répond pas : conteneur arrêté") {
        const Fixture f;
        f.runtime->has_image = true;
        *f.ready = false;
        tools::SearxngHost host(f.options());
        const auto base = host.ensure_started({});
        REQUIRE_FALSE(base.has_value());
        CHECK(base.error().message.contains("ne répond pas"));
        CHECK_FALSE(f.runtime->running);
        CHECK_FALSE(host.running());
    }
}

TEST_CASE("SearXNG : adresse publiée", "[searxng]") {
    using tools::detail::searxng_base_url;
    CHECK(searxng_base_url("127.0.0.1:43123\n") == "http://127.0.0.1:43123");
    CHECK(searxng_base_url("[::1]:8080\n127.0.0.1:8888") == "http://127.0.0.1:8888");
    CHECK(searxng_base_url("0.0.0.0:8080\n") == std::nullopt); // pas la boucle locale
    CHECK(searxng_base_url("127.0.0.1:80; rm -rf /") == std::nullopt);
    CHECK(searxng_base_url("") == std::nullopt);
}

TEST_CASE("recherche : instance locale, sinon DuckDuckGo", "[searxng]") {
    const Fixture f;
    f.runtime->has_podman = false; // SearXNG ne peut pas tourner
    tools::SearxngHost host(f.options());
    tools::SearchEngines engines(std::nullopt, &host, std::nullopt);
    CHECK(engines.active() == "searxng"); // par défaut
    CHECK(engines.label() == "SearXNG (local)");

    const auto asked = std::make_shared<std::vector<std::string>>();
    const tools::HttpGet get =
        [asked](const std::string& url, bool /*allow_private*/,
                const std::stop_token& /*stop*/) -> core::Result<tools::HttpPage> {
        asked->push_back(url);
        return tools::HttpPage{
            .status = 200, .content_type = "text/html", .body = "<html></html>", .final_url = {}};
    };
    std::shared_ptr<core::Tool> search;
    for (const auto& tool : tools::make_web_tools(engines, {}, get)) {
        if (tool->spec().name == "web_search") {
            search = tool;
        }
    }
    REQUIRE(search != nullptr);
    const auto result = search->call({{"query", "linux"}}, {});
    REQUIRE(result.has_value());
    CHECK(result->content.contains("SearXNG indisponible"));
    REQUIRE(asked->size() == 1);
    CHECK(asked->front().starts_with("https://html.duckduckgo.com/"));
}
