#include <chrono>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/storage/transcript.hpp"

using namespace avenoir;

namespace {

/// 8 novembre 2026, 12 h 50 (heure locale).
std::chrono::local_time<std::chrono::minutes> sample_date() {
    using namespace std::chrono;
    return local_days{year{2026} / November / 8} + hours{12} + minutes{50};
}

} // namespace

TEST_CASE("transcription : en-tête, échanges et outils repliés sous la réponse", "[transcript]") {
    const std::vector<core::Message> messages{
        core::UserMessage{.content = "Va-t-il pleuvoir demain ?"},
        core::AssistantMessage{.text = {},
                               .tool_calls = {core::ToolCall{
                                   .id = "1",
                                   .name = "meteo__forecast",
                                   .arguments = nlohmann::json{{"city", "Arles"}, {"days", 2}}}}},
        core::ToolResultMessage{
            .call_id = "1", .content = "Demain : pluie faible", .is_error = false},
        core::AssistantMessage{.text = "Oui, un peu de pluie.", .tool_calls = {}},
        core::UserMessage{.content = "Lance firefox"},
        core::AssistantMessage{
            .text = {},
            .tool_calls = {core::ToolCall{.id = "2",
                                          .name = "launch_app",
                                          .arguments = nlohmann::json{{"name", "firefox"}}}}},
        core::ToolResultMessage{.call_id = "2", .content = "introuvable", .is_error = true},
    };
    const std::string text = storage::transcript_markdown("Essai", 3, sample_date(), messages);

    CHECK(text.starts_with("# Essai\n\n"));
    CHECK(text.contains("*Avenoir · conversation n° 3 · 8 novembre 2026 à 12 h 50 · 2 échanges*"));
    CHECK(text.contains("## › Vous\n\nVa-t-il pleuvoir demain ?\n"));
    // Une seule rubrique « Avenoir » par échange, l'outil replié avant la réponse.
    CHECK(text.contains("## ◆ Avenoir\n\n<details>\n<summary>» <code>meteo__forecast</code> · "
                        "city : Arles · days : 2 · ✓</summary>\n\n```text\nDemain : pluie "
                        "faible\n```\n\n</details>\n\nOui, un peu de pluie."));
    CHECK(text.contains("<code>launch_app</code> · name : firefox · ✗"));
    CHECK(text.find("## ◆ Avenoir") != text.rfind("## ◆ Avenoir")); // deux réponses
    CHECK(text.ends_with("introuvable\n```\n\n</details>\n"));
}

TEST_CASE("transcription : date du premier du mois, appel sans résultat", "[transcript]") {
    using namespace std::chrono;
    const local_time<minutes> first = local_days{year{2026} / January / 1} + hours{9} + minutes{5};
    const std::vector<core::Message> messages{
        core::UserMessage{.content = "Heure ?"},
        core::AssistantMessage{
            .text = {},
            .tool_calls = {core::ToolCall{
                .id = "1", .name = "current_time", .arguments = nlohmann::json::object()}}},
    };
    const std::string text = storage::transcript_markdown("T", 1, first, messages);
    CHECK(text.contains("1er janvier 2026 à 9 h 05 · 1 échange*\n"));
    CHECK(text.contains("<code>current_time</code> · <i>sans résultat</i>"));
}

TEST_CASE("transcription : rien d'actif à l'ouverture dans un lecteur Markdown", "[transcript]") {
    const std::vector<core::Message> messages{
        core::UserMessage{.content = "<script>alert(1)</script>"},
        core::AssistantMessage{.text = "Voici ![suivi](https://pirate.example/p.png)",
                               .tool_calls = {core::ToolCall{
                                   .id = "1",
                                   .name = "web",
                                   .arguments = nlohmann::json{{"q", "</summary><img src=x>"}}}}},
        // Le résultat tente de sortir de son bloc de code.
        core::ToolResultMessage{
            .call_id = "1", .content = "```\n<img src=https://pirate.example>", .is_error = false},
    };
    const std::string text = storage::transcript_markdown("T", 1, sample_date(), messages);

    CHECK_FALSE(text.contains("<script>"));
    CHECK(text.contains("&lt;script>"));
    CHECK_FALSE(text.contains("Voici ![suivi]"));
    CHECK(text.contains("Voici \\![suivi]"));
    CHECK(text.contains("q : &lt;/summary&gt;&lt;img src=x&gt;"));
    // Clôture plus longue que les accents graves du résultat.
    CHECK(text.contains("````text\n```\n<img src=https://pirate.example>\n````\n"));
}

TEST_CASE("transcription : le code des messages reste intact", "[transcript]") {
    const std::vector<core::Message> messages{
        core::UserMessage{.content = "Compare `a < b` & `c`"},
        core::AssistantMessage{
            .text =
                "Exemple :\n```python\nif a < b and c > d:\n    print(\"![x]\")\n```\nFin <b>.\n\n",
            .tool_calls = {}},
        // Bloc jamais refermé : refermé pour ne pas avaler la suite.
        core::UserMessage{.content = "~~~\n<img src=x>"},
        core::AssistantMessage{.text = "ok", .tool_calls = {}},
    };
    const std::string text = storage::transcript_markdown("T", 1, sample_date(), messages);

    CHECK(text.contains("Compare `a < b` &amp; `c`\n"));
    CHECK(text.contains("```python\nif a < b and c > d:\n    print(\"![x]\")\n```\nFin &lt;b>.\n"));
    CHECK_FALSE(text.contains("Fin &lt;b>.\n\n\n")); // blancs finaux retirés
    CHECK(text.contains("~~~\n<img src=x>\n~~~\n\n## ◆ Avenoir\n\nok"));
}

TEST_CASE("nom du fichier de transcription", "[transcript]") {
    CHECK(storage::transcript_file_name(sample_date(), 3, "Météo à Lyon ?") ==
          "2026-11-08-1250-n3-meteo-a-lyon.md");
    CHECK(storage::transcript_file_name(sample_date(), 1, "") ==
          "2026-11-08-1250-n1-conversation.md");
    CHECK(storage::transcript_file_name(sample_date(), 1, "«  »") ==
          "2026-11-08-1250-n1-conversation.md");

    // Jamais de séparateur de chemin ni de remontée de dossier.
    CHECK(storage::transcript_file_name(sample_date(), 2, "../../etc/passwd\\C:\\x") ==
          "2026-11-08-1250-n2-etc-passwd-c-x.md");

    // Titre long : 40 caractères au plus.
    CHECK(storage::transcript_file_name(sample_date(), 4, std::string(100, 'a')) ==
          "2026-11-08-1250-n4-" + std::string(40, 'a') + ".md");
}
