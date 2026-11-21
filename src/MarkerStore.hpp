#pragma once

#include "Config.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fwc {

struct ManualMarker {
    float x = 0.0f;
    float y = 30.0f;
    double frames = 1.0;
    std::string comment;
    bool triggered = false;
};

class MarkerStore {
private:
    std::string m_currentKey;
    std::vector<ManualMarker> m_markers;

    static std::string sanitizeKey(std::string const& raw) {
        std::string out;
        out.reserve(raw.size());
        for (char c : raw) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-') {
                out.push_back(c);
            } else if (c == ' ') {
                out.push_back('_');
            }
        }
        return out.empty() ? "level" : out;
    }

    static std::string sanitizeComment(std::string s) {
        for (char& c : s) {
            if (c == '|' || c == '\n' || c == '\r') c = ' ';
        }
        return s;
    }

    std::filesystem::path filePathFor(std::string const& key) const {
        auto dir = Mod::get()->getSaveDir() / "markers_v4";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / (key + ".fc4");
    }

public:
    static MarkerStore& get() {
        static MarkerStore instance;
        return instance;
    }

    static std::string buildLevelKey(GJGameLevel* level) {
        if (!level) return "unknown_level";
        int id = level->m_levelID.value();
        std::string name = sanitizeKey(std::string(level->m_levelName));
        if (id > 0) return fmt::format("id_{}_{}", id, name);
        return fmt::format("local_{}", name);
    }

    std::string const& currentKey() const { return m_currentKey; }
    std::vector<ManualMarker>& markers() { return m_markers; }
    std::vector<ManualMarker> const& markers() const { return m_markers; }

    void loadForLevel(GJGameLevel* level) {
        m_currentKey = buildLevelKey(level);
        m_markers.clear();

        auto path = filePathFor(m_currentKey);
        std::ifstream in(path);
        if (!in.is_open()) return;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string segX, segY, segF, segC;
            if (!std::getline(ss, segX, '|')) continue;
            if (!std::getline(ss, segY, '|')) continue;
            if (!std::getline(ss, segF, '|')) continue;
            std::getline(ss, segC);

            try {
                ManualMarker m;
                m.x = std::stof(segX);
                m.y = std::stof(segY);
                m.frames = std::max(0.05, std::stod(segF));
                m.comment = segC;
                m.triggered = false;
                m_markers.push_back(m);
            } catch (...) {}
        }
        sortMarkers();
    }

    void saveCurrent() const {
        if (m_currentKey.empty()) return;
        auto path = filePathFor(m_currentKey);
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) return;

        out << "# FWC Markers: x|y|frames|comment\n";
        for (auto const& m : m_markers) {
            out << m.x << "|" << m.y << "|" << m.frames << "|" << sanitizeComment(m.comment) << "\n";
        }
    }

    void sortMarkers() {
        std::sort(m_markers.begin(), m_markers.end(), [](ManualMarker const& a, ManualMarker const& b) {
            return a.x < b.x;
        });
    }

    void resetTriggers() {
        for (auto& m : m_markers) m.triggered = false;
    }

    void resetTriggersAfter(float x) {
        for (auto& m : m_markers) {
            if (m.x >= x - 1.0f) m.triggered = false;
        }
    }

    void addMarker(float x, float y, double frames, std::string const& comment = "") {
        for (auto& existing : m_markers) {
            if (std::fabs(existing.x - x) < 2.0f) {
                existing.y = y;
                existing.frames = std::max(0.05, frames);
                if (!comment.empty()) existing.comment = sanitizeComment(comment);
                saveCurrent();
                return;
            }
        }
        ManualMarker m;
        m.x = x;
        m.y = y;
        m.frames = std::max(0.05, frames);
        m.comment = sanitizeComment(comment);
        m.triggered = false;
        m_markers.push_back(m);
        sortMarkers();
        saveCurrent();
    }

    void setCommentAt(size_t idx, std::string const& comment) {
        if (idx >= m_markers.size()) return;
        m_markers[idx].comment = sanitizeComment(comment);
        saveCurrent();
    }

    void removeAt(size_t idx) {
        if (idx >= m_markers.size()) return;
        m_markers.erase(m_markers.begin() + static_cast<ptrdiff_t>(idx));
        saveCurrent();
    }

    void clearAll() {
        m_markers.clear();
        saveCurrent();
    }
};

} // namespace fwc
