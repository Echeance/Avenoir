#include <catch2/catch_test_macros.hpp>

#include "gpu.hpp"

using namespace avenoir;

TEST_CASE("gpu : fdinfo de DRM", "[gpu]") {
    const auto amd = daemon::parse_drm_fdinfo("pos:\t0\ndrm-driver:\tamdgpu\ndrm-client-id:\t15\n"
                                              "drm-total-vram:\t208488 KiB\n"
                                              "drm-resident-vram:\t216936 KiB\n"
                                              "drm-memory-vram:\t1 KiB\n");
    REQUIRE(amd.has_value());
    CHECK(amd.value_or(daemon::DrmUsage{}).client == "15");
    CHECK(amd.value_or(daemon::DrmUsage{}).video_memory == 216936ULL * 1024);
    // Ancien format (drm-memory-vram seul).
    const auto old = daemon::parse_drm_fdinfo("drm-client-id: 3\ndrm-memory-vram: 2 MiB\n");
    CHECK(old.value_or(daemon::DrmUsage{}).video_memory == 2ULL * 1024 * 1024);
    // Client sans mémoire vidéo (Intel intégré…) : compté pour zéro.
    CHECK(daemon::parse_drm_fdinfo("drm-client-id: 4\n")
              .value_or(daemon::DrmUsage{.client = "x", .video_memory = 9})
              .video_memory == 0);
    // Pas un descripteur de carte graphique.
    CHECK_FALSE(daemon::parse_drm_fdinfo("pos:\t0\nflags:\t02\n").has_value());
}

TEST_CASE("gpu : serveurs de modèles jamais comptés", "[gpu]") {
    CHECK(daemon::is_model_server("ollama"));
    CHECK(daemon::is_model_server("llama-server"));
    CHECK(daemon::is_model_server("avenoird"));
    CHECK_FALSE(daemon::is_model_server("steam"));
    CHECK_FALSE(daemon::is_model_server("cyberpunk2077.exe"));
}
