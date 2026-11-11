#include <algorithm>
#include <mutex>
#include <stop_token>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/llm/discovery.hpp"

using namespace avenoir;
using nlohmann::json;

TEST_CASE("parse_model_list : identifiants de /v1/models", "[discovery]") {
    const auto models = llm::parse_model_list(json::parse(
        R"({"object":"list","data":[{"id":"gemma4:26b-a4b"},{"id":"ministral-3:8b"},
            {"id":"gemma4:26b-a4b"},{"pas":"d'id"},{"id":42}]})"));
    CHECK(models == std::vector<std::string>{"gemma4:26b-a4b", "ministral-3:8b"});

    CHECK_FALSE(llm::parse_model_list(json::parse(R"({"models":[]})")).has_value());
    CHECK_FALSE(llm::parse_model_list(json::parse("[1,2]")).has_value());
}

TEST_CASE("parse_model_list : un serveur ne peut pas piéger le terminal", "[discovery]") {
    json response = {{"data", json::array()}};
    response["data"].push_back({{"id", "piege\x1b[2J"}});
    response["data"].push_back({{"id", "retour\nligne"}});
    response["data"].push_back({{"id", std::string(300, 'a')}});
    response["data"].push_back({{"id", ""}});
    response["data"].push_back({{"id", "propre"}});
    for (int i = 0; i < 200; ++i) {
        response["data"].push_back({{"id", "m" + std::to_string(i)}});
    }
    const auto models = llm::parse_model_list(response).value_or(std::vector<std::string>{});
    REQUIRE(models.size() == 100);
    CHECK(models.front() == "propre");
}

TEST_CASE("is_private_ipv4 et subnet_neighbours", "[discovery]") {
    CHECK(llm::is_private_ipv4("192.168.1.12"));
    CHECK(llm::is_private_ipv4("10.0.0.3"));
    CHECK(llm::is_private_ipv4("172.16.0.1"));
    CHECK(llm::is_private_ipv4("172.31.255.254"));
    CHECK_FALSE(llm::is_private_ipv4("172.32.0.1"));
    CHECK_FALSE(llm::is_private_ipv4("8.8.8.8"));
    CHECK_FALSE(llm::is_private_ipv4("100.64.0.1")); // CGNAT : réseau de l'opérateur
    CHECK_FALSE(llm::is_private_ipv4("127.0.0.1"));
    CHECK_FALSE(llm::is_private_ipv4("192.168.1"));
    CHECK_FALSE(llm::is_private_ipv4("192.168.1.256"));
    CHECK_FALSE(llm::is_private_ipv4("192.168.1.1.1"));
    CHECK_FALSE(llm::is_private_ipv4("192.168.01234.1"));
    CHECK_FALSE(llm::is_private_ipv4(" 192.168.1.1"));

    const auto hosts = llm::subnet_neighbours("192.168.1.12");
    CHECK(hosts.size() == 253);
    CHECK(hosts.front() == "192.168.1.1");
    CHECK(hosts.back() == "192.168.1.254");
    CHECK(std::ranges::find(hosts, "192.168.1.12") == hosts.end());
    CHECK(llm::subnet_neighbours("8.8.8.8").empty()); // jamais de sondage hors réseau privé
}

TEST_CASE("discover_servers : tous les ports, résultats dans l'ordre", "[discovery]") {
    std::vector<std::string> asked;
    std::mutex lock;
    const llm::ServerProbe probe =
        [&](const std::string& url) -> core::Result<std::vector<std::string>> {
        {
            const std::scoped_lock guard(lock);
            asked.push_back(url);
        }
        if (url == "http://10.0.0.2:11434/v1") {
            return std::vector<std::string>{"gemma4:26b-a4b"};
        }
        if (url == "http://10.0.0.1:1234/v1") {
            return std::vector<std::string>{"qwen"};
        }
        return core::make_error(core::ErrorCode::Llm, "injoignable");
    };
    const auto servers = llm::discover_servers({"10.0.0.1", "10.0.0.2"}, std::stop_token{}, probe);
    CHECK(asked.size() == 6);
    REQUIRE(servers.size() == 2);
    CHECK(servers[0].base_url == "http://10.0.0.1:1234/v1");
    CHECK(servers[1].base_url == "http://10.0.0.2:11434/v1");
    CHECK(servers[1].models == std::vector<std::string>{"gemma4:26b-a4b"});

    // request_stop() n'est pas const selon la norme (MSVC), même si libstdc++ le permet.
    std::stop_source cancelled; // NOLINT(misc-const-correctness)
    cancelled.request_stop();
    CHECK(llm::discover_servers({"10.0.0.2"}, cancelled.get_token(), probe).empty());
}
