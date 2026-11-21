#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace fwc {

using namespace geode::prelude;

enum class CounterMode : int {
    Auto = 0,
    Manual = 1
};

enum class BannerMode : int {
    Off = 0,
    FirstOnly = 1,
    Always = 2
};

enum class TableStyle : int {
    TopDown24 = 0, // 1, 2, 3, 4, 5-6, 7-8, 9-12, 13-16, 17-24 (tightest at TOP!)
    CbfRanges = 1, // 10+, 8-10, 6-8, 5-6, 4-5, 3-4, 2-3, 1-2, 0-1
    Discrete  = 2  // 9-12, 7-8, 5-6, 4, 3, 2, 1
};

struct TierDef {
    std::string key;
    std::string label;
    std::string introDesc;
    std::string introMs;
    double minF;
    double maxF;
    ccColor3B color;
    float pitchOffset;
};

struct ModConfig {
    bool enabled = true;
    CounterMode mode = CounterMode::Auto;
    int64_t refFps = 240;
    bool cbfMode = true;
    TableStyle tableStyle = TableStyle::TopDown24;

    bool showCircles = true;
    bool persistentCircles = true;
    float circleScale = 1.0f;
    std::string ringThickness = "bold";
    bool showCircleLabel = true;

    bool showTierTable = true;
    bool showMsHz = true;
    bool showBottomStats = true;
    bool showWatermark = true;
    std::string watermarkText = "FWC COUNTER";

    bool showAutoComments = true;
    float commentFadeSec = 2.4f;

    BannerMode bannerMode = BannerMode::FirstOnly;
    float bannerDuration = 2.8f;
    std::string bannerComment = "";
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
        cfg.mode = (mod->getSettingValue<std::string>("mode") == "manual") ? CounterMode::Manual : CounterMode::Auto;
        cfg.refFps = std::clamp<int64_t>(mod->getSettingValue<int64_t>("ref-fps"), 30, 100000);
        cfg.cbfMode = mod->getSettingValue<bool>("cbf-mode");

        auto ts = mod->getSettingValue<std::string>("table-style");
        if (ts == "cbf") cfg.tableStyle = TableStyle::CbfRanges;
        else if (ts == "discrete") cfg.tableStyle = TableStyle::Discrete;
        else cfg.tableStyle = TableStyle::TopDown24;

        cfg.showCircles = mod->getSettingValue<bool>("show-circles");
        cfg.persistentCircles = mod->getSettingValue<bool>("persistent-circles");
        cfg.circleScale = static_cast<float>(mod->getSettingValue<double>("circle-scale"));
        cfg.ringThickness = mod->getSettingValue<std::string>("ring-thickness");
        cfg.showCircleLabel = mod->getSettingValue<bool>("show-circle-label");

        cfg.showTierTable = mod->getSettingValue<bool>("show-tier-table");
        cfg.showMsHz = mod->getSettingValue<bool>("show-ms-hz");
        cfg.showBottomStats = mod->getSettingValue<bool>("show-bottom-stats");
        cfg.showWatermark = mod->getSettingValue<bool>("show-watermark");
        cfg.watermarkText = mod->getSettingValue<std::string>("watermark-text");

        cfg.showAutoComments = mod->getSettingValue<bool>("show-auto-comments");
        cfg.commentFadeSec = static_cast<float>(mod->getSettingValue<double>("comment-fade-sec"));

        auto bm = mod->getSettingValue<std::string>("banner-mode");
        if (bm == "off") cfg.bannerMode = BannerMode::Off;
        else if (bm == "always") cfg.bannerMode = BannerMode::Always;
        else cfg.bannerMode = BannerMode::FirstOnly;

        cfg.bannerDuration = static_cast<float>(mod->getSettingValue<double>("banner-duration"));
        cfg.bannerComment = mod->getSettingValue<std::string>("banner-comment");
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
        mod->setSettingValue<bool>("cbf-mode", cbfMode);

        std::string ts = "topdown24";
        if (tableStyle == TableStyle::CbfRanges) ts = "cbf";
        else if (tableStyle == TableStyle::Discrete) ts = "discrete";
        mod->setSettingValue<std::string>("table-style", ts);

        mod->setSettingValue<bool>("show-circles", showCircles);
        mod->setSettingValue<bool>("persistent-circles", persistentCircles);
        mod->setSettingValue<double>("circle-scale", std::clamp(static_cast<double>(circleScale), 0.4, 2.5));
        mod->setSettingValue<std::string>("ring-thickness", ringThickness);
        mod->setSettingValue<bool>("show-circle-label", showCircleLabel);

        mod->setSettingValue<bool>("show-tier-table", showTierTable);
        mod->setSettingValue<bool>("show-ms-hz", showMsHz);
        mod->setSettingValue<bool>("show-bottom-stats", showBottomStats);
        mod->setSettingValue<bool>("show-watermark", showWatermark);
        mod->setSettingValue<std::string>("watermark-text", watermarkText);

        mod->setSettingValue<bool>("show-auto-comments", showAutoComments);
        mod->setSettingValue<double>("comment-fade-sec", std::clamp(static_cast<double>(commentFadeSec), 0.5, 8.0));

        std::string bm = "first";
        if (bannerMode == BannerMode::Off) bm = "off";
        else if (bannerMode == BannerMode::Always) bm = "always";
        mod->setSettingValue<std::string>("banner-mode", bm);

        mod->setSettingValue<double>("banner-duration", std::clamp(static_cast<double>(bannerDuration), 0.8, 8.0));
        mod->setSettingValue<std::string>("banner-comment", bannerComment);
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

inline std::vector<TierDef> buildTiers(TableStyle style, double refFps) {
    double fps = std::max(30.0, refFps);
    double scale = fps / 240.0;

    auto msRangeStr = [&](double loF, double hiF) -> std::string {
        double loMs = (loF / fps) * 1000.0;
        double hiMs = (hiF / fps) * 1000.0;
        if (hiF > 1e6) return fmt::format("(>{:.1f}ms)", loMs);
        return fmt::format("({:.1f}-{:.1f}ms)", loMs, hiMs);
    };
    auto hzDescStr = [&](double loF, double hiF) -> std::string {
        if (hiF > 1e6) return "";
        double hz = fps / std::max(0.1, hiF);
        if (loF <= 0.001) return fmt::format("(>{:.0f}Hz FP)", hz);
        return fmt::format("(<{:.0f}Hz FP)", hz);
    };
    auto fmtNum = [](double v) -> std::string {
        if (std::fabs(v - std::round(v)) < 0.05) return fmt::format("{:.0f}", std::round(v));
        return fmt::format("{:.1f}", v);
    };

    std::vector<TierDef> tiers;
    if (style == TableStyle::TopDown24) {
        // Exact order & palette: 1, 2, 3, 4, 5-6, 7-8, 9-12, 13-16, 17-24
        struct RawTD { const char* key; double lo; double hi; const char* lbl; ccColor3B col; float pitch; };
        const RawTD raw[9] = {
            { "1",     0.0,  1.5,  "1:",     ccc3(255, 110, 102),  0.32f },
            { "2",     1.5,  2.5,  "2:",     ccc3(255, 184,  88),  0.22f },
            { "3",     2.5,  3.5,  "3:",     ccc3(255, 240, 106),  0.14f },
            { "4",     3.5,  4.5,  "4:",     ccc3(244, 248, 255),  0.07f },
            { "5-6",   4.5,  6.5,  "5-6:",   ccc3(114, 244, 134),  0.01f },
            { "7-8",   6.5,  8.5,  "7-8:",   ccc3(110, 224, 255), -0.05f },
            { "9-12",  8.5, 12.5,  "9-12:",  ccc3(118, 168, 255), -0.10f },
            { "13-16",12.5, 16.5,  "13-16:", ccc3(196, 164, 255), -0.15f },
            { "17-24",16.5, 1e9,   "17-24:", ccc3(244, 162, 232), -0.20f }
        };
        for (auto const& b : raw) {
            double lo = b.lo * scale;
            double hi = (b.hi > 1e6) ? 1e9 : (b.hi * scale);
            std::string lbl = (std::fabs(scale - 1.0) < 0.05)
                ? std::string(b.lbl)
                : ((b.hi > 1e6) ? fmt::format("{}+:", fmtNum(lo)) : fmt::format("{}-{}:", fmtNum(lo), fmtNum(hi)));
            std::string desc = fmt::format("{} FRAMES {}", lbl.substr(0, lbl.size() - 1), hzDescStr(lo, hi));
            tiers.push_back({ b.key, lbl, desc, msRangeStr(lo, hi), lo, hi, b.col, b.pitch });
        }
    } else if (style == TableStyle::CbfRanges) {
        struct RawBucket { const char* key; double lo; double hi; ccColor3B col; float pitch; };
        const RawBucket raw[9] = {
            { "10+",  10.0, 1e9,  ccc3(198, 154, 255), -0.14f },
            { "8-10",  8.0, 10.0, ccc3(112, 165, 255), -0.09f },
            { "6-8",   6.0,  8.0, ccc3( 96, 226, 255), -0.04f },
            { "5-6",   5.0,  6.0, ccc3( 92, 242, 206),  0.00f },
            { "4-5",   4.0,  5.0, ccc3(118, 244, 132),  0.05f },
            { "3-4",   3.0,  4.0, ccc3(236, 246, 104),  0.10f },
            { "2-3",   2.0,  3.0, ccc3(255, 198,  72),  0.16f },
            { "1-2",   1.0,  2.0, ccc3(255, 118,  92),  0.23f },
            { "0-1",   0.0,  1.0, ccc3(255,  86, 138),  0.32f }
        };
        for (auto const& b : raw) {
            double lo = b.lo * scale;
            double hi = (b.hi > 1e6) ? 1e9 : (b.hi * scale);
            std::string lbl = (b.hi > 1e6)
                ? fmt::format("{}+:", fmtNum(lo))
                : fmt::format("{}-{}:", fmtNum(lo), fmtNum(hi));
            std::string desc = (b.hi > 1e6)
                ? fmt::format("{}+ FRAMES", fmtNum(lo))
                : fmt::format("{}-{} FRAMES {}", fmtNum(lo), fmtNum(hi), hzDescStr(lo, hi));
            tiers.push_back({ b.key, lbl, desc, msRangeStr(lo, hi), lo, hi, b.col, b.pitch });
        }
    } else {
        struct RawDisc { const char* key; double lo; double hi; const char* lbl; ccColor3B col; float pitch; };
        const RawDisc raw[7] = {
            { "9-12", 8.5, 1e9,  "9-12:", ccc3(122, 160, 255), -0.10f },
            { "7-8",  6.5, 8.5,  "7-8:",  ccc3( 96, 220, 255), -0.04f },
            { "5-6",  4.5, 6.5,  "5-6:",  ccc3(106, 240, 150),  0.02f },
            { "4",    3.5, 4.5,  "4:",    ccc3(222, 246, 112),  0.08f },
            { "3",    2.5, 3.5,  "3:",    ccc3(255, 224,  88),  0.14f },
            { "2",    1.5, 2.5,  "2:",    ccc3(255, 156,  84),  0.22f },
            { "1",    0.0, 1.5,  "1:",    ccc3(255,  92, 112),  0.32f }
        };
        for (auto const& b : raw) {
            double lo = b.lo * scale;
            double hi = (b.hi > 1e6) ? 1e9 : (b.hi * scale);
            std::string lbl = (std::fabs(scale - 1.0) < 0.05)
                ? std::string(b.lbl)
                : ((b.hi > 1e6) ? fmt::format("{}+:", fmtNum(lo)) : fmt::format("{}-{}:", fmtNum(lo), fmtNum(hi)));
            std::string desc = fmt::format("{} FRAMES {}", lbl.substr(0, lbl.size() - 1), hzDescStr(lo, hi));
            tiers.push_back({ b.key, lbl, desc, msRangeStr(lo, hi), lo, hi, b.col, b.pitch });
        }
    }
    return tiers;
}

// Works for BOTH ascending (1 -> 24) and descending (10+ -> 0-1) tier tables!
inline int findTierIndex(std::vector<TierDef> const& tiers, double frames) {
    if (tiers.empty()) return 0;
    for (size_t i = 0; i < tiers.size(); ++i) {
        if (frames >= tiers[i].minF && frames < tiers[i].maxF) {
            return static_cast<int>(i);
        }
    }
    // Fallback to closest boundary
    return (tiers.front().minF <= 0.001) ? static_cast<int>(tiers.size()) - 1 : 0;
}

inline std::string formatCircleNumber(double frames, bool cbfMode) {
    if (!cbfMode) {
        int rounded = std::max(1, static_cast<int>(std::round(frames)));
        return fmt::format("{}", rounded);
    }
    if (frames < 2.0) return fmt::format("{:.2f}", frames);
    if (frames < 10.0) return fmt::format("{:.1f}", frames);
    return fmt::format("{:.0f}", std::round(frames));
}

inline double framesToMs(double frames, double refFps) {
    return (frames / std::max(1.0, refFps)) * 1000.0;
}

inline double framesToHz(double frames, double refFps) {
    double sec = frames / std::max(1.0, refFps);
    if (sec <= 1e-6) return 99999.0;
    return 1.0 / sec;
}

// Calibrated Raw Precision difficulty weight per click (in sigma/s units)
inline double clickPrecisionWeight(double ms) {
    double clampedMs = std::max(0.5, ms);
    return std::pow(18.0 / clampedMs, 1.45);
}

} // namespace fwc
