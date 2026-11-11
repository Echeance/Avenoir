// avenoir-evals : passe les requêtes de référence (evals/requetes.toml) sur un
// vrai modèle, avec des doublures d'outils, et écrit un rapport Markdown.
//
//   avenoir-evals [--config config.toml] [--llm nom] [--model id] [--reasoning effort] [--cases
//   fichier]
//                 [--only id,id] [--repeat N] [--prompt fichier] [--report fichier]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

#include "avenoir/core/agent.hpp"
#include "avenoir/core/config.hpp"
#include "avenoir/core/env.hpp"
#include "avenoir/llm/factory.hpp"
#include "avenoir/llm/models.hpp"
#include "avenoir/modules/discovery.hpp"
#include "avenoir/modules/host.hpp"
#include "avenoir/tools/tools.hpp"

#include "cases.hpp"
#include "report.hpp"
#include "runner.hpp"

using namespace avenoir;

namespace {

struct Arguments {
    std::filesystem::path config = "config.toml";
    std::filesystem::path cases = AVENOIR_EVALS_CASES;
    std::filesystem::path modules = AVENOIR_EVALS_MODULES;
    std::optional<std::string> llm; ///< Nom d'un `[models.<nom>]` ; absent : le premier.
    std::optional<std::string> model;
    std::optional<std::string> reasoning;
    std::optional<std::filesystem::path> prompt;
    std::optional<std::filesystem::path> report;
    std::vector<std::string> only;
    std::size_t repeat = 1;
};

constexpr std::string_view kUsage =
    "Usage : avenoir-evals [options]\n"
    "  --config fichier   configuration (fournisseur LLM), config.toml par défaut\n"
    "  --llm nom          modèle de la configuration ([models.<nom>]), le premier par défaut\n"
    "  --model id         remplace l'identifiant de ce modèle (ex. ministral-3:8b)\n"
    "  --reasoning effort effort de réflexion (none, low, medium, high ; openai_compatible)\n"
    "  --cases fichier    requêtes de référence (evals/requetes.toml)\n"
    "  --only id,id       seulement ces cas\n"
    "  --repeat N         passer chaque cas N fois (1 à 20)\n"
    "  --prompt fichier   prompt système à essayer à la place de celui d'Avenoir\n"
    "  --report fichier   rapport (par défaut evals/resultats/<date>-<modèle>.md)\n"
    "  --modules dossier  modules fournis (pour la description de leurs outils)\n";

std::optional<Arguments> parse_arguments(std::span<char*> argv) {
    Arguments args;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string_view option = argv[i];
        if (option == "-h" || option == "--help") {
            std::print("{}", kUsage);
            std::exit(EXIT_SUCCESS); // NOLINT(concurrency-mt-unsafe) : aucun thread ici.
        }
        if (i + 1 >= argv.size()) {
            std::println(std::cerr, "option inconnue ou sans valeur : {}\n\n{}", option, kUsage);
            return std::nullopt;
        }
        const std::string value = argv[++i];
        if (option == "--config") {
            args.config = value;
        } else if (option == "--llm") {
            args.llm = value;
        } else if (option == "--model") {
            args.model = value;
        } else if (option == "--reasoning") {
            args.reasoning = value;
        } else if (option == "--cases") {
            args.cases = value;
        } else if (option == "--prompt") {
            args.prompt = value;
        } else if (option == "--report") {
            args.report = value;
        } else if (option == "--modules") {
            args.modules = value;
        } else if (option == "--only") {
            std::istringstream list(value);
            for (std::string id; std::getline(list, id, ',');) {
                if (!id.empty()) {
                    args.only.push_back(id);
                }
            }
        } else if (option == "--repeat") {
            std::size_t count = 0;
            for (const char digit : value) {
                count = digit >= '0' && digit <= '9' && count < 100
                            ? (count * 10) + static_cast<std::size_t>(digit - '0')
                            : 100;
            }
            if (count < 1 || count > 20) {
                std::println(std::cerr, "--repeat : 1 à 20");
                return std::nullopt;
            }
            args.repeat = count;
        } else {
            std::println(std::cerr, "option inconnue : {}\n\n{}", option, kUsage);
            return std::nullopt;
        }
    }
    return args;
}

/// « claude_code / sonnet ».
std::string model_label(const core::LlmConfig& config) {
    return std::visit(
        [](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, core::OpenAiCompatibleConfig>) {
                return "openai_compatible / " + c.model +
                       (c.reasoning_effort ? " (réflexion " + *c.reasoning_effort + ")" : "");
            } else if constexpr (std::is_same_v<T, core::AnthropicConfig>) {
                return "anthropic / " + c.model;
            } else {
                return "claude_code / " + c.model.value_or("défaut");
            }
        },
        config);
}

void override_model(core::LlmConfig& config, const std::string& model) {
    std::visit([&model](auto& c) { c.model = model; }, config);
}

/// Nom de fichier sûr : lettres, chiffres, `.`, `-`, `_` ; « é » devient « e »
/// et les autres caractères un tiret (jamais deux de suite, ni à la fin).
std::string file_safe(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (text.substr(i).starts_with("é")) {
            out += 'e';
            ++i;
            continue;
        }
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (keep) {
            out += c;
        } else if (!out.empty() && out.back() != '-') {
            out += '-';
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

/// Modèles entre lesquels basculer dans le banc : trois locaux, un distant.
std::vector<llm::ModelChoice> bench_models() {
    const auto local = [](std::string name, std::string model) {
        return llm::ModelChoice{
            .name = std::move(name),
            .config = core::OpenAiCompatibleConfig{.base_url = "http://localhost:11434/v1",
                                                   .model = std::move(model),
                                                   .api_key_env = std::nullopt,
                                                   .temperature = std::nullopt,
                                                   .reasoning_effort = std::nullopt},
            .local = true};
    };
    return {local("gemma", "gemma4:26b-a4b"), local("qwen", "qwen3.8:27b"),
            local("ministral", "ministral-3:8b"), local("phi", "phi4-mini"),
            llm::ModelChoice{.name = "claude",
                             .config = core::ClaudeCodeConfig{.model = "haiku"},
                             .local = false}};
}

/// Descriptions des vrais outils : natifs, puis ceux des modules fournis
/// (lancés le temps de lire leur liste d'outils, puis arrêtés).
core::Result<std::vector<core::ToolSpec>> real_specs(const core::AppConfig& config,
                                                     const std::filesystem::path& modules_dir) {
    core::ToolRegistry registry;
    if (auto native = tools::register_native_tools(registry); !native) {
        return std::unexpected(std::move(native.error()));
    }
    {
        modules::ModuleHost host(modules::HostOptions{});
        const std::vector<modules::SearchDirectory> directories{
            {.path = modules_dir, .origin = modules::Origin::Bundled}};
        const auto discovery = modules::discover(directories);
        for (const auto& problem : discovery.problems) {
            std::println(std::cerr, "module ignoré ({}) : {}", problem.directory.string(),
                         problem.message);
        }
        host.load(discovery, config.modules, {});
        host.register_tools(registry);
        for (const auto& warning : host.take_warnings()) {
            std::println(std::cerr, "{}", warning);
        }
    }
    // Outils de modèle (bascule, chargement…) décrits pour une liste de
    // modèles fixe : les cas du banc ne dépendent pas de la configuration.
    llm::ModelSwitcher switcher(bench_models(), nullptr);
    for (auto& tool : llm::make_model_tools(switcher)) {
        if (auto added = registry.add(std::move(tool)); !added) {
            return std::unexpected(std::move(added.error()));
        }
    }
    return registry.specs();
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

void report_fatal(const char* message) noexcept {
    (void)std::fputs("erreur fatale : ", stderr);
    (void)std::fputs(message, stderr);
    (void)std::fputs("\n", stderr);
}

/// Avertissements seulement (le journal d'audit est coupé), sauf AVENOIR_LOG.
void setup_logging() {
    spdlog::set_level(spdlog::level::warn);
    if (const auto level = core::read_env("AVENOIR_LOG")) {
        spdlog::set_level(spdlog::level::from_str(*level));
    }
}

int run(std::span<char*> argv) {
    setup_logging();
    const auto args = parse_arguments(argv);
    if (!args) {
        return EXIT_FAILURE;
    }
    auto config = core::load_config(args->config);
    if (!config) {
        std::println(std::cerr, "{}", core::describe(config.error()));
        return EXIT_FAILURE;
    }
    if (config->models.empty()) {
        std::println(std::cerr, "aucun modèle dans {} ([models.<nom>])", args->config.string());
        return EXIT_FAILURE;
    }
    const auto chosen = args->llm
                            ? std::ranges::find(config->models, *args->llm, &core::NamedLlm::name)
                            : config->models.begin();
    if (chosen == config->models.end()) {
        std::println(std::cerr, "modèle inconnu : {}", args->llm.value_or(""));
        return EXIT_FAILURE;
    }
    core::LlmConfig& model_config = chosen->config;
    if (args->model) {
        override_model(model_config, *args->model);
    }
    if (args->reasoning) {
        auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&model_config);
        if (openai == nullptr) {
            std::println(std::cerr, "--reasoning : seulement avec openai_compatible");
            return EXIT_FAILURE;
        }
        openai->reasoning_effort = *args->reasoning;
    }
    auto suite = evals::load_suite(args->cases);
    if (!suite) {
        std::println(std::cerr, "{}", core::describe(suite.error()));
        return EXIT_FAILURE;
    }

    // « {modele} » dans les résultats des doublures et les réponses
    // attendues : le nom du modèle essayé.
    const auto put_model = [&chosen](std::string& text) {
        constexpr std::string_view kKey = "{modele}";
        for (auto at = text.find(kKey); at != std::string::npos; at = text.find(kKey, at)) {
            text.replace(at, kKey.size(), chosen->name);
        }
    };
    std::ranges::for_each(suite->results | std::views::values, put_model);
    for (auto& test : suite->cases) {
        std::ranges::for_each(test.results | std::views::values, put_model);
        for (auto* texts : {&test.reply_contains, &test.reply_contains_any, &test.reply_excludes}) {
            std::ranges::for_each(*texts, put_model);
        }
    }

    std::vector<const evals::Case*> selected;
    for (const auto& test : suite->cases) {
        if (args->only.empty() || std::ranges::contains(args->only, test.id)) {
            selected.push_back(&test);
        }
    }
    for (const auto& id : args->only) {
        if (std::ranges::none_of(suite->cases, [&id](const auto& t) { return t.id == id; })) {
            std::println(std::cerr, "cas inconnu : {}", id);
            return EXIT_FAILURE;
        }
    }

    evals::RunOptions options;
    options.policy = config->permissions;
    options.max_steps = config->agent.max_steps;
    // Mêmes vérifications des réponses que la CLI, avec le modèle essayé
    // comme modèle actif parmi ceux du banc.
    llm::ModelSwitcher bench_switcher(bench_models(), nullptr);
    if (auto started = bench_switcher.start(chosen->name); !started) {
        std::println(std::cerr, "{}", core::describe(started.error()));
        return EXIT_FAILURE;
    }
    if (config->agent.verify_replies) {
        options.reply_checks.push_back(llm::make_model_claim_check(bench_switcher));
    }
    options.model_identity =
        llm::describe(llm::ModelChoice{.name = chosen->name,
                                       .config = model_config,
                                       .local = llm::is_local(model_config, chosen->local)});
    options.system_prompt =
        config->agent.system_prompt.value_or(std::string(core::kDefaultSystemPrompt));
    std::string prompt_label =
        config->agent.system_prompt ? "celui de la configuration" : "celui d'Avenoir (par défaut)";
    if (args->prompt) {
        auto text = read_file(*args->prompt);
        if (!text) {
            std::println(std::cerr, "prompt illisible : {}", args->prompt->string());
            return EXIT_FAILURE;
        }
        options.system_prompt = std::move(*text);
        prompt_label = args->prompt->string();
    }

    const auto specs = real_specs(*config, args->modules);
    if (!specs) {
        std::println(std::cerr, "{}", core::describe(specs.error()));
        return EXIT_FAILURE;
    }

    const std::string label = model_label(model_config);
    std::println("{} : {} cas × {}, {} outils", label, selected.size(), args->repeat,
                 specs->size());

    std::vector<evals::CaseResult> results;
    for (const auto* test : selected) {
        evals::CaseResult result{.test = test, .attempts = {}};
        for (std::size_t i = 0; i < args->repeat; ++i) {
            auto llm = llm::make_provider(model_config);
            if (!llm) {
                std::println(std::cerr, "{}", core::describe(llm.error()));
                return EXIT_FAILURE;
            }
            auto transcript = evals::run_case(*test, *suite, *specs, std::move(*llm), options);
            auto failures = evals::check(*test, transcript);
            std::println("{} {} ({:.1f} s){}", failures.empty() ? "✓" : "✗", test->id,
                         static_cast<double>(transcript.duration.count()) / 1000.0,
                         failures.empty() ? "" : " — " + failures.front());
            result.attempts.push_back(
                {.transcript = std::move(transcript), .failures = std::move(failures)});
        }
        results.push_back(std::move(result));
    }

    const auto now =
        std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()}
            .get_local_time();
    const auto minutes = std::chrono::floor<std::chrono::minutes>(now);
    const evals::ReportHeader header{
        .date = std::format("{:%Y-%m-%d %H:%M}", minutes),
        .model = label,
        .prompt = prompt_label,
#ifdef _WIN32
        .platform = "Windows",
#else
        .platform = "Linux",
#endif
    };
    const std::filesystem::path report_path = args->report.value_or(
        std::filesystem::path(AVENOIR_EVALS_RESULTS) /
        (std::format("{:%Y-%m-%d-%H%M}-", minutes) + file_safe(label) + ".md"));
    std::error_code ignored;
    std::filesystem::create_directories(report_path.parent_path(), ignored);
    std::ofstream file(report_path, std::ios::binary);
    file << evals::make_report(header, results);
    if (!file) {
        std::println(std::cerr, "rapport non écrit : {}", report_path.string());
        return EXIT_FAILURE;
    }
    std::size_t passed = 0;
    std::size_t total = 0;
    for (const auto& result : results) {
        passed += result.passed();
        total += result.attempts.size();
    }
    std::println("\nRéussite : {}/{} — rapport : {}", passed, total, report_path.string());
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    // Seul endroit où les exceptions sont tolérées : elles s'arrêtent ici.
    try {
        return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        report_fatal(e.what());
    } catch (...) {
        report_fatal("exception inconnue");
    }
    return EXIT_FAILURE;
}
