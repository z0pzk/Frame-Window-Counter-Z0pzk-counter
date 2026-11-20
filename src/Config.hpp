#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace fwc {

using namespace geode::prelude;

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
    float pitchOffset;
    bool isFramePerfect;
};

struct ModConfig {
    bool enabled = true;
    CounterMode mode = CounterMode::Auto;
    int64_t refFps = 240;
    bool autoTpsAdapt = true;

    bool showCircles = true;
    float circleScale = 1.0f;
    bool showCircleLabel = true;

    bool showHud = true;
    bool showPercentages = true;
    bool showSpecialStats = false;
    bool detectSwifts = false;
    bool detectMulticlicks = false;

    bool showBanner = true;
    float bannerDuration = 3.5f;
    std::string bannerComment = "";
    bool showRawPrecision = true;
    std::string rawPrecisionText = "";

    bool soundEnabled = true;
    std::string soundPreset = "classic";
    std::string customSoundFile = "";
    float soundVolume = 0.75f;
    float soundPitch = 1.0f;
    bool dynamicPitch = true;

    bool captureToManual = false;

    static ModConfig load() {
        auto* mod = Mod::get();
        ModConfig cfg;
        cfg.enabled = mod->getSettingValue<bool>("enabled");
        auto modeStr = mod->getSettingValue<std::string>("mode");
        cfg.mode = (modeStr == "manual") ? CounterMode::Manual : CounterMode::Auto;
        cfg.refFps = std::clamp<int64_t>(mod->getSettingValue<int64_t>("ref-fps"), 30, 100000);
        cfg.autoTpsAdapt = mod->getSettingValue<bool>("auto-tps-adapt");

        cfg.showCircles = mod->getSettingValue<bool>("show-circles");
        cfg.circleScale = static_cast<float>(mod->getSettingValue<double>("circle-scale"));
        cfg.showCircleLabel = mod->getSettingValue<bool>("show-circle-label");

        cfg.showHud = mod->getSettingValue<bool>("show-hud");
        cfg.showPercentages = mod->getSettingValue<bool>("show-percentages");
        cfg.showSpecialStats = mod->getSettingValue<bool>("show-special-stats");
        cfg.detectSwifts = mod->getSettingValue<bool>("detect-swifts");
        cfg.detectMulticlicks = mod->getSettingValue<bool>("detect-multiclicks");

        cfg.showBanner = mod->getSettingValue<bool>("show-banner");
        cfg.bannerDuration = static_cast<float>(mod->getSettingValue<double>("banner-duration"));
        cfg.bannerComment = mod->getSettingValue<std::string>("banner-comment");
        cfg.showRawPrecision = mod->getSettingValue<bool>("show-raw-precision");
        cfg.rawPrecisionText = mod->getSettingValue<std::string>("raw-precision-text");

        cfg.soundEnabled = mod->getSettingValue<bool>("sound-enabled");
        cfg.soundPreset = mod->getSettingValue<std::string>("sound-preset");
        cfg.customSoundFile = mod->getSettingValue<std::string>("custom-sound-file");
        cfg.soundVolume = static_cast<float>(mod->getSettingValue<double>("sound-volume"));
        cfg.soundPitch = static_cast<float>(mod->getSettingValue<double>("sound-pitch"));
        cfg.dynamicPitch = mod->getSettingValue<bool>("dynamic-pitch");

        cfg.captureToManual = mod->getSettingValue<bool>("capture-to-manual");
        return cfg;
    }

    void save() const {
        auto* mod = Mod::get();
        mod->setSettingValue<bool>("enabled", enabled);
        mod->setSettingValue<std::string>("mode", mode == CounterMode::Manual ? "manual" : "auto");
        mod->setSettingValue<int64_t>("ref-fps", std::clamp<int64_t>(refFps, 30, 100000));
        mod->setSettingValue<bool>("auto-tps-adapt", autoTpsAdapt);

        mod->setSettingValue<bool>("show-circles", showCircles);
        mod->setSettingValue<double>("circle-scale", std::clamp(static_cast<double>(circleScale), 0.4, 2.5));
        mod->setSettingValue<bool>("show-circle-label", showCircleLabel);

        mod->setSettingValue<bool>("show-hud", showHud);
        mod->setSettingValue<bool>("show-percentages", showPercentages);
        mod->setSettingValue<bool>("show-special-stats", showSpecialStats);
        mod->setSettingValue<bool>("detect-swifts", detectSwifts);
        mod->setSettingValue<bool>("detect-multiclicks", detectMulticlicks);

        mod->setSettingValue<bool>("show-banner", showBanner);
        mod->setSettingValue<double>("banner-duration", std::clamp(static_cast<double>(bannerDuration), 1.0, 10.0));
        mod->setSettingValue<std::string>("banner-comment", bannerComment);
        mod->setSettingValue<bool>("show-raw-precision", showRawPrecision);
        mod->setSettingValue<std::string>("raw-precision-text", rawPrecisionText);

        mod->setSettingValue<bool>("sound-enabled", soundEnabled);
        mod->setSettingValue<std::string>("sound-preset", soundPreset);
        mod->setSettingValue<std::string>("custom-sound-file", customSoundFile);
        mod->setSettingValue<double>("sound-volume", std::clamp(static_cast<double>(soundVolume), 0.0, 1.0));
        mod->setSettingValue<double>("sound-pitch", std::clamp(static_cast<double>(soundPitch), 0.4, 2.5));
        mod->setSettingValue<bool>("dynamic-pitch", dynamicPitch);

        mod->setSettingValue<bool>("capture-to-manual", captureToManual);
    }
};

// Smooth pitch & color tiers mapped to real window duration in milliseconds
inline TierStyle evaluateTier(double frames, double refFps) {
    double safeFps = std::max(1.0, refFps);
    double ms = (frames / safeFps) * 1000.0;

    if (frames < 1.0 || ms < 0.8) {
        return { "SUB-FRAME", "< 0.80 ms", ccc3(225, 110, 255), 0.36f, true };
    }
    if (ms <= 4.35) {
        return { "240Hz+ FP", "<= 4.17 ms", ccc3(255, 78, 92), 0.24f, true };
    }
    if (ms <= 8.50) {
        return { "120Hz+ FP", "4.17 - 8.33 ms", ccc3(255, 160, 64), 0.14f, true };
    }
    if (ms <= 12.65) {
        return { "~80Hz FP", "8.33 - 12.50 ms", ccc3(255, 224, 66), 0.06f, true };
    }
    if (ms <= 16.90) {
        return { "60Hz FP", "12.50 - 16.67 ms", ccc3(96, 232, 136), 0.0f, true };
    }
    return { "OPEN", "> 16.67 ms", ccc3(120, 208, 255), -0.07f, false };
}

inline std::string formatFrames(double frames) {
    if (frames < 1.0) {
        return fmt::format("{:.2f}F", frames);
    }
    if (frames < 10.0 && std::fabs(frames - std::round(frames)) > 0.05) {
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
