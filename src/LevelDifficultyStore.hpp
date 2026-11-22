#pragma once

#include "Config.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace fwc {

class LevelDifficultyStore {
private:
    std::unordered_map<std::string, double> m_totals;
    bool m_loaded = false;

    std::filesystem::path filePath() const {
        auto dir = Mod::get()->getSaveDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / "level_difficulty_totals.cfg";
    }

    void ensureLoaded() {
        if (m_loaded) return;
        m_loaded = true;
        m_totals.clear();

        std::ifstream in(filePath());
        if (!in.is_open()) return;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string key, val;
            if (!std::getline(ss, key, '|')) continue;
            if (!std::getline(ss, val)) continue;
            try {
                double d = std::stod(val);
                if (d > 0.01) m_totals[key] = d;
            } catch (...) {}
        }
    }

    void save() const {
        std::ofstream out(filePath(), std::ios::trunc);
        if (!out.is_open()) return;
        out << "# FWC Saved Level Total Raw Precision: levelKey|totalDifficulty\n";
        for (auto const& [k, v] : m_totals) {
            if (v > 0.01) out << k << "|" << v << "\n";
        }
    }

public:
    static LevelDifficultyStore& get() {
        static LevelDifficultyStore instance;
        return instance;
    }

    double getTotal(std::string const& levelKey) {
        ensureLoaded();
        auto it = m_totals.find(levelKey);
        return (it != m_totals.end()) ? it->second : 0.0;
    }

    void setTotal(std::string const& levelKey, double totalDiff) {
        if (levelKey.empty() || totalDiff <= 0.05) return;
        ensureLoaded();
        m_totals[levelKey] = totalDiff;
        save();
    }
};

} // namespace fwc
