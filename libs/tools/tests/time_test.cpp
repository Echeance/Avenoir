#include <chrono>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/tools/tools.hpp"

using namespace std::chrono;
using avenoir::tools::detail::time_sentence;

namespace {

local_seconds at(int y, unsigned m, unsigned d, int h, int min) {
    return local_days{year{y} / month{m} / day{d}} + hours{h} + minutes{min};
}

} // namespace

TEST_CASE("l'heure est une phrase française", "[time]") {
    CHECK(time_sentence(at(2026, 11, 6, 13, 41)) == "Il est 13 h 41, vendredi 6 novembre 2026.");
}

TEST_CASE("premier du mois et minutes sur deux chiffres", "[time]") {
    CHECK(time_sentence(at(2026, 2, 1, 8, 5)) == "Il est 8 h 05, dimanche 1er février 2026.");
}

TEST_CASE("minuit et accents des mois", "[time]") {
    CHECK(time_sentence(at(2027, 8, 15, 0, 0)) == "Il est 0 h 00, dimanche 15 août 2027.");
    CHECK(time_sentence(at(2026, 12, 31, 23, 59)) == "Il est 23 h 59, jeudi 31 décembre 2026.");
}
