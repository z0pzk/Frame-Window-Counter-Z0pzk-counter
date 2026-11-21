#pragma once

#include "Config.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace fwc {

struct TierSoundOverride {
    bool enabled = false;
    std::string preset = "classic"; // classic / fb / chime / tick / custom
    std::string customFile = "";
    float volume = 0.85f;           // 0.0 .. 1.5 (completely independent of global volume!)
    float pitch = 1.0f;             // 0.4 .. 2.5 (completely independent of global pitch & auto pitch!)
};

class TierSoundStore {
private:
    std::unordered_map<std::string, TierSoundOverride> m_overrides;
    bool m_loaded = false;

    static std::string sanitize(std::string s) {
        for (char& c : s) {
            if (c == '|' || c == '\n' || c == '\r') c = ' ';
        }
        return s;
    }

    std::filesystem::path filePath() const {
        auto dir = Mod::get()->getSaveDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / "tier_sounds.cfg";
    }

public:
    static TierSoundStore& get() {
        static TierSoundStore instance;
        return instance;
    }

    void ensureLoaded() {
        if (m_loaded) return;
        m_loaded = true;
        m_overrides.clear();

        std::ifstream in(filePath());
        if (!in.is_open()) return;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string k, en, preset, vol, pitch, customFile;
            if (!std::getline(ss, k, '|')) continue;
            if (!std::getline(ss, en, '|')) continue;
            if (!std::getline(ss, preset, '|')) continue;
            if (!std::getline(ss, vol, '|')) continue;
            if (!std::getline(ss, pitch, '|')) continue;
            std::getline(ss, customFile);

            try {
                TierSoundOverride o;
                o.enabled = (en == "1");
                o.preset = preset.empty() ? "classic" : preset;
                o.volume = std::clamp(std::stof(vol), 0.0f, 1.5f);
                o.pitch = std::clamp(std::stof(pitch), 0.4f, 2.5f);
                o.customFile = customFile;
                m_overrides[k] = o;
            } catch (...) {}
        }
    }

    void save() const {
        std::ofstream out(filePath(), std::ios::trunc);
        if (!out.is_open()) return;
        out << "# FWC Tier Sound Overrides: tierKey|enabled|preset|volume|pitch|customFile\n";
        for (auto const& [k, o] : m_overrides) {
            if (!o.enabled && o.customFile.empty()) continue;
            out << sanitize(k) << "|"
                << (o.enabled ? "1" : "0") << "|"
                << sanitize(o.preset) << "|"
                << o.volume << "|"
                << o.pitch << "|"
                << sanitize(o.customFile) << "\n";
        }
    }

    TierSoundOverride getForTier(std::string const& tierKey) {
        ensureLoaded();
        auto it = m_overrides.find(tierKey);
        if (it != m_overrides.end()) return it->second;
        return TierSoundOverride{};
    }

    void setForTier(std::string const& tierKey, TierSoundOverride const& o) {
        ensureLoaded();
        m_overrides[tierKey] = o;
        save();
    }

    int countActiveOverrides(std::vector<TierDef> const& tiers) {
        ensureLoaded();
        int cnt = 0;
        for (auto const& t : tiers) {
            auto it = m_overrides.find(t.key);
            if (it != m_overrides.end() && it->second.enabled) cnt++;
        }
        return cnt;
    }
};

} // namespace fwc
