#include "report.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>

namespace avenoir::evals {

namespace {

/// Texte sur une ligne, sans casser un tableau Markdown.
std::string cell(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            out += ' ';
        } else if (c == '|') {
            out += "\\|";
        } else {
            out += c;
        }
    }
    return out;
}

std::string seconds(std::chrono::milliseconds duration) {
    std::string text = std::format("{:.1f}", static_cast<double>(duration.count()) / 1000.0);
    std::ranges::replace(text, '.', ',');
    return text + " s";
}

} // namespace

std::size_t CaseResult::passed() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        attempts, [](const Attempt& attempt) { return attempt.failures.empty(); }));
}

std::string make_report(const ReportHeader& header, const std::vector<CaseResult>& results) {
    std::size_t total = 0;
    std::size_t passed = 0;
    std::chrono::milliseconds time{0};
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::optional<double> cost;
    std::map<std::string, std::pair<std::size_t, std::size_t>> by_category;
    for (const auto& result : results) {
        for (const auto& attempt : result.attempts) {
            const auto& t = attempt.transcript;
            ++total;
            time += t.duration;
            input_tokens += t.input_tokens;
            output_tokens += t.output_tokens;
            if (t.cost_usd) {
                cost = cost.value_or(0) + *t.cost_usd;
            }
            auto& category = by_category[result.test->category];
            ++category.second;
            if (attempt.failures.empty()) {
                ++passed;
                ++category.first;
            }
        }
    }

    std::string out = std::format("# Essais : {}\n\n", header.model);
    out += std::format("- Date : {}\n- Système : {}\n- Prompt système : {}\n", header.date,
                       header.platform, header.prompt);
    out += std::format("- **Réussite : {}/{}**", passed, total);
    if (total > 0) {
        out += std::format(" ({} %)", passed * 100 / total);
    }
    out += std::format("\n- Durée totale : {}, moyenne par passage : {}\n", seconds(time),
                       seconds(total > 0 ? time / static_cast<long>(total) : time));
    out += std::format("- Tokens : {} en entrée, {} en sortie", input_tokens, output_tokens);
    if (cost) {
        std::string text = std::format("{:.3f}", *cost);
        std::ranges::replace(text, '.', ',');
        out += ", coût indicatif " + text + " $";
    }
    out += "\n\n| Catégorie | Réussite |\n|---|---|\n";
    for (const auto& [category, counts] : by_category) {
        out += std::format("| {} | {}/{} |\n", category, counts.first, counts.second);
    }

    out += "\n## Cas\n\n| Cas | Catégorie | Réussite | Durée moyenne |\n|---|---|---|---|\n";
    for (const auto& result : results) {
        std::chrono::milliseconds sum{0};
        for (const auto& attempt : result.attempts) {
            sum += attempt.transcript.duration;
        }
        const auto count = result.attempts.size();
        const auto mean = count > 0 ? sum / static_cast<long>(count) : sum;
        std::string_view mark = "~";
        if (result.passed() == count) {
            mark = "✓";
        } else if (result.passed() == 0) {
            mark = "✗";
        }
        out += std::format("| {} `{}` | {} | {}/{} | {} |\n", mark, result.test->id,
                           result.test->category, result.passed(), count, seconds(mean));
    }

    bool any_failure = false;
    for (const auto& result : results) {
        for (std::size_t i = 0; i < result.attempts.size(); ++i) {
            const auto& attempt = result.attempts[i];
            if (attempt.failures.empty()) {
                continue;
            }
            if (!any_failure) {
                out += "\n## Échecs\n";
                any_failure = true;
            }
            out += std::format("\n### `{}` (passage {})\n\n", result.test->id, i + 1);
            for (const auto& request : result.test->requests) {
                out += "> " + cell(request) + "\n";
            }
            out += '\n';
            for (const auto& failure : attempt.failures) {
                out += "- " + failure + "\n";
            }
            out += "- appels : ";
            if (attempt.transcript.calls.empty()) {
                out += "aucun";
            }
            for (std::size_t j = 0; j < attempt.transcript.calls.size(); ++j) {
                out += (j == 0 ? "`" : ", `") + describe_call(attempt.transcript.calls[j]) + "`";
            }
            out += "\n- réponse : « " + cell(attempt.transcript.reply) + " »\n";
        }
    }
    return out;
}

} // namespace avenoir::evals
