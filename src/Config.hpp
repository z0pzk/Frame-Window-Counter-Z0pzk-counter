#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace fwc {

using namespace geode::prelude;

constexpr float kPi = 3.14159265358979323846f;

enum class CounterMode : int {
    Auto = 0,
    Manual = 1
};

enum class ClickKind : int {
    Normal = 0,
    SFrame = 1,
    Swift = 2,
    Multiclick = 3,
    Release = 4
};

inline std::string clickKindShort(ClickKind kind) {
    switch (kind) {
        case ClickKind::SFrame:     return "S-FR";
        case ClickKind::Swift:      return "SWIFT";
        case ClickKind::Multiclick: return "MULTI";
        case ClickKind::Release:    return "REL";
        default:                    return "";
    }
}

inline std::string clickKindFull(ClickKind kind) {
    switch (kind) {
        case ClickKind::SFrame:     return "S-Frame";
        case ClickKind::Swift:      return "Swift";
        case ClickKind::Multiclick: return "Multiclick";
        case ClickKind::Release:    return "Release";
        default:                    return "Normal";
    }
}

struct TierStyle {
    std::string tag;
    std::string rangeMs;
    ccColor3B color3;
    ccColor4F color4;
    float pitchOffset; // additive pitch offset when dynamic pitch is on
    bool isFramePerfect;
};

struct ModConfig {
    bool enabled = true;
    CounterMode mode = CounterMode::Auto;
    int64_t refFps = 6000;
    bool autoTpsAdapt = true;

    bool showCircles = true;
    float circleScale = 1.0f;
    bool showCircleLabel = true;

    bool showHud = true;
    bool showPercentages = true;
    bool showSpecialStats = true;

    bool showBanner = true;
    float bannerDuration = 3.5f;
    std::string bannerComment = "Open Window Top 1";
    bool showRawPrecision = true;
    std::string rawPrecisionText = "Raw Precision: 240Hz in-game";

    bool soundEnabled = true;
    std::string soundPreset = "crystal";
    float soundVolume = 0.75f;
    float soundPitch = 1.0f;
    bool dynamicPitch = true;

    float swiftThresholdMs = 33.33f;
    float multiclickWindowMs = 135.0f;
    float autoSensitivity = 1.0f;
    bool captureToManual = false;

    static ModConfig load() {
        auto* mod = Mod::get();
        ModConfig cfg;
        cfg.enabled = mod->getSettingValue<bool>("enabled");
        auto modeStr = mod->getSettingValue<std::string>("mode");
        cfg.mode = (modeStr == "manual") ? CounterMode::Manual : CounterMode::Auto;
        cfg.refFps = std::clamp<int64_t>(mod->getSettingValue<int64_t>("ref-fps"), 60, 100000);
        cfg.autoTpsAdapt = mod->getSettingValue<bool>("auto-tps-adapt");

        cfg.showCircles = mod->getSettingValue<bool>("show-circles");
        cfg.circleScale = static_cast<float>(mod->getSettingValue<double>("circle-scale"));
        cfg.showCircleLabel = mod->getSettingValue<bool>("show-circle-label");

        cfg.showHud = mod->getSettingValue<bool>("show-hud");
        cfg.showPercentages = mod->getSettingValue<bool>("show-percentages");
        cfg.showSpecialStats = mod->getSettingValue<bool>("show-special-stats");

        cfg.showBanner = mod->getSettingValue<bool>("show-banner");
        cfg.bannerDuration = static_cast<float>(mod->getSettingValue<double>("banner-duration"));
        cfg.bannerComment = mod->getSettingValue<std::string>("banner-comment");
        cfg.showRawPrecision = mod->getSettingValue<bool>("show-raw-precision");
        cfg.rawPrecisionText = mod->getSettingValue<std::string>("raw-precision-text");

        cfg.soundEnabled = mod->getSettingValue<bool>("sound-enabled");
        cfg.soundPreset = mod->getSettingValue<std::string>("sound-preset");
        cfg.soundVolume = static_cast<float>(mod->getSettingValue<double>("sound-volume"));
        cfg.soundPitch = static_cast<float>(mod->getSettingValue<double>("sound-pitch"));
        cfg.dynamicPitch = mod->getSettingValue<bool>("dynamic-pitch");

        cfg.swiftThresholdMs = static_cast<float>(mod->getSettingValue<double>("swift-threshold-ms"));
        cfg.multiclickWindowMs = static_cast<float>(mod->getSettingValue<double>("multiclick-window-ms"));
        cfg.autoSensitivity = static_cast<float>(mod->getSettingValue<double>("auto-sensitivity"));
        cfg.captureToManual = mod->getSettingValue<bool>("capture-to-manual");
        return cfg;
    }

    void save() const {
        auto* mod = Mod::get();
        mod->setSettingValue<bool>("enabled", enabled);
        mod->setSettingValue<std::string>("mode", mode == CounterMode::Manual ? "manual" : "auto");
        mod->setSettingValue<int64_t>("ref-fps", std::clamp<int64_t>(refFps, 60, 100000));
        mod->setSettingValue<bool>("auto-tps-adapt", autoTpsAdapt);

        mod->setSettingValue<bool>("show-circles", showCircles);
        mod->setSettingValue<double>("circle-scale", std::clamp(static_cast<double>(circleScale), 0.4, 2.5));
        mod->setSettingValue<bool>("show-circle-label", showCircleLabel);

        mod->setSettingValue<bool>("show-hud", showHud);
        mod->setSettingValue<bool>("show-percentages", showPercentages);
        mod->setSettingValue<bool>("show-special-stats", showSpecialStats);

        mod->setSettingValue<bool>("show-banner", showBanner);
        mod->setSettingValue<double>("banner-duration", std::clamp(static_cast<double>(bannerDuration), 1.0, 10.0));
        mod->setSettingValue<std::string>("banner-comment", bannerComment);
        mod->setSettingValue<bool>("show-raw-precision", showRawPrecision);
        mod->setSettingValue<std::string>("raw-precision-text", rawPrecisionText);

        mod->setSettingValue<bool>("sound-enabled", soundEnabled);
        mod->setSettingValue<std::string>("sound-preset", soundPreset);
        mod->setSettingValue<double>("sound-volume", std::clamp(static_cast<double>(soundVolume), 0.0, 1.0));
        mod->setSettingValue<double>("sound-pitch", std::clamp(static_cast<double>(soundPitch), 0.4, 2.5));
        mod->setSettingValue<bool>("dynamic-pitch", dynamicPitch);

        mod->setSettingValue<double>("swift-threshold-ms", std::clamp(static_cast<double>(swiftThresholdMs), 1.0, 120.0));
        mod->setSettingValue<double>("multiclick-window-ms", std::clamp(static_cast<double>(multiclickWindowMs), 20.0, 350.0));
        mod->setSettingValue<double>("auto-sensitivity", std::clamp(static_cast<double>(autoSensitivity), 0.25, 3.0));
        mod->setSettingValue<bool>("capture-to-manual", captureToManual);
    }
};

// Evaluate visual tier & showcase range from frame count and active reference FPS.
// Note: At 6000 FPS:
//   < 1F       -> Sub-Frame (Magenta)
//   < 25F      -> >= 240Hz FP (< 4.17 ms) (Crimson Red)
//   25F - 50F  -> 120Hz - 240Hz FP (4.17 ms - 8.33 ms) (Orange)
//   50F - 75F  -> ~80Hz - 120Hz FP (8.33 ms - 12.50 ms) (Yellow, i.e. < 76F)
//   76F - 100F -> 60Hz - 80Hz FP (12.50 ms - 16.67 ms) (Mint Green)
//   > 100F     -> Open Window (> 16.67 ms) (Ice Cyan)
inline TierStyle evaluateTier(double frames, double refFps) {
    double safeFps = std::max(1.0, refFps);
    double ms = (frames / safeFps) * 1000.0;

    if (frames < 1.0 || ms < 0.5) {
        return {
            "SUB-FRAME",
            "< 0.50 ms",
            ccc3(224, 86, 253),
            ccc4f(0.88f, 0.34f, 0.99f, 1.0f),
            0.35f,
            true
        };
    }
    if (ms < 4.17) {
        return {
            "240Hz+ FP",
            "< 4.17 ms",
            ccc3(244, 63, 94),
            ccc4f(0.96f, 0.25f, 0.37f, 1.0f),
            0.24f,
            true
        };
    }
    if (ms < 8.33) {
        return {
            "120Hz+ FP",
            "4.17 - 8.33 ms",
            ccc3(251, 146, 60),
            ccc4f(0.98f, 0.57f, 0.24f, 1.0f),
            0.14f,
            true
        };
    }
    if (ms <= 12.55) {
        return {
            "~80Hz FP",
            "8.33 - 12.50 ms",
            ccc3(250, 204, 21),
            ccc4f(0.98f, 0.80f, 0.08f, 1.0f),
            0.06f,
            true
        };
    }
    if (ms <= 16.75) {
        return {
            "60Hz FP",
            "12.50 - 16.67 ms",
            ccc3(74, 222, 128),
            ccc4f(0.29f, 0.87f, 0.50f, 1.0f),
            0.0f,
            true
        };
    }
    return {
        "OPEN",
        "> 16.67 ms",
        ccc3(56, 189, 248),
        ccc4f(0.22f, 0.74f, 0.97f, 1.0f),
        -0.08f,
        false
    };
}

inline std::string formatFrames(double frames) {
    if (frames < 1.0) {
        return fmt::format("{:.2f}F", frames);
    }
    if (frames < 10.0 && std::fabs(frames - std::round(frames)) > 0.04) {
        return fmt::format("{:.1f}F", frames);
    }
    return fmt::format("{:.0f}F", std::round(frames));
}

inline double framesToMs(double frames, double refFps) {
    return (frames / std::max(1.0, refFps)) * 1000.0;
}

inline double framesToHz(double frames, double refFps) {
    double sec = frames / std::max(1.0, refFps);
    if (sec <= 1e-6) return 99999.0;
    return 1.0 / sec;
}

} // namespace fwc
