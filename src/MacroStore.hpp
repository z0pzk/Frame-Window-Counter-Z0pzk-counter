#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if __has_include(<Geode/Geode.hpp>)
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#define FWC_HAS_GEODE 1
#else
#define FWC_HAS_GEODE 0
#endif

namespace fwc {

struct MacroInput {
    uint64_t tick240 = 0; // Normalized to 240 Hz physics ticks
    bool down = true;
    int button = 1;       // 1 = Jump, 2 = Left, 3 = Right
    bool isP1 = true;
};

struct LoadedMacro {
    std::string name;
    std::string sourceBot; // "Silicate", "xdBot", "Eclipse", "FWC", "File"
    std::string path;
    double sourceFps = 240.0;
    std::vector<MacroInput> inputs;

    bool empty() const { return inputs.empty(); }
    size_t clickCount() const {
        size_t c = 0;
        for (auto const& in : inputs) {
            if (in.down && in.button == 1) c++;
        }
        return c;
    }
};

struct DiscoveredMacro {
    std::string displayName;
    std::string botTag;    // "SLC", "XDB", "ECL", "FWC"
    std::string sourceBot; // "Silicate", "xdBot", "Eclipse", "FWC"
    std::string fullPath;
    int64_t modTime = 0;
    bool matchesLevel = false;
};

namespace macro_detail {

inline std::string toLowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline uint64_t scaleFrameTo240(uint64_t frame, double srcFps) {
    if (srcFps <= 1.0 || std::fabs(srcFps - 240.0) < 0.05) return frame;
    double scaled = static_cast<double>(frame) * (240.0 / srcFps);
    return static_cast<uint64_t>(std::llround(std::max(0.0, scaled)));
}

inline bool readBytes(std::vector<uint8_t> const& buf, size_t& pos, void* dst, size_t n) {
    if (pos + n > buf.size()) return false;
    std::memcpy(dst, buf.data() + pos, n);
    pos += n;
    return true;
}

template <typename T>
inline bool readLE(std::vector<uint8_t> const& buf, size_t& pos, T& out) {
    return readBytes(buf, pos, &out, sizeof(T));
}

template <typename T>
inline bool readBE(std::vector<uint8_t> const& buf, size_t& pos, T& out) {
    if (pos + sizeof(T) > buf.size()) return false;
    uint8_t tmp[sizeof(T)];
    for (size_t i = 0; i < sizeof(T); ++i) {
        tmp[sizeof(T) - 1 - i] = buf[pos + i];
    }
    std::memcpy(&out, tmp, sizeof(T));
    pos += sizeof(T);
    return true;
}

inline bool readVarint(std::vector<uint8_t> const& buf, size_t& pos, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (pos < buf.size() && shift < 64) {
        uint8_t b = buf[pos++];
        out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) return true;
        shift += 7;
    }
    return false;
}

inline bool readGdrString(std::vector<uint8_t> const& buf, size_t& pos, std::string& out) {
    uint64_t len = 0;
    if (!readVarint(buf, pos, len)) return false;
    if (len > 1024 * 1024 || pos + static_cast<size_t>(len) > buf.size()) return false;
    out.assign(reinterpret_cast<const char*>(buf.data() + pos), static_cast<size_t>(len));
    pos += static_cast<size_t>(len);
    return true;
}

// =========================================================================
// 1. GDR2 Binary Format Parser (Eclipse Bot, xdBot v2, MegaHack)
//    Spec: https://github.com/maxnut/GDReplayFormat (branch gdr2)
// =========================================================================
inline bool parseGDR2Impl(std::vector<uint8_t> const& buf, LoadedMacro& out, bool beFloatHeader) {
    if (buf.size() < 16) return false;
    if (buf[0] != 'G' || buf[1] != 'D' || buf[2] != 'R') return false;

    size_t pos = 3;
    uint64_t version = 0;
    std::string inputTag;
    if (!readVarint(buf, pos, version)) return false;
    if (version == 0 || version > 16) return false;
    if (!readGdrString(buf, pos, inputTag)) return false;

    std::string author, desc, botName, levelName;
    uint64_t gameVer = 0, seed = 0, coins = 0, botVer = 0, levelId = 0, extSize = 0;
    double fps = 240.0;
    uint8_t ldm = 0, platformer = 0;

    if (!readGdrString(buf, pos, author)) return false;
    if (!readGdrString(buf, pos, desc)) return false;

    if (beFloatHeader) {
        float durF = 0.0f;
        double fpsD = 240.0;
        if (!readBE(buf, pos, durF)) return false;
        if (!std::isfinite(durF) || durF < -1.0f || durF > 1e7f) return false;
        if (!readVarint(buf, pos, gameVer)) return false;
        if (!readBE(buf, pos, fpsD)) return false;
        if (!std::isfinite(fpsD) || fpsD < 10.0 || fpsD > 100000.0) return false;
        fps = fpsD;
    } else {
        uint64_t durationV = 0, fpsV = 240;
        if (!readVarint(buf, pos, durationV)) return false;
        if (!readVarint(buf, pos, gameVer)) return false;
        if (!readVarint(buf, pos, fpsV)) return false;
        if (fpsV < 10 || fpsV > 100000) return false;
        fps = static_cast<double>(fpsV);
    }

    if (!readVarint(buf, pos, seed)) return false;
    if (!readVarint(buf, pos, coins)) return false;
    if (!readLE(buf, pos, ldm)) return false;
    if (!readLE(buf, pos, platformer)) return false;
    if (ldm > 1 || platformer > 1) return false;
    if (!readGdrString(buf, pos, botName)) return false;
    if (!readVarint(buf, pos, botVer)) return false;
    if (!readVarint(buf, pos, levelId)) return false;
    if (!readGdrString(buf, pos, levelName)) return false;
    if (!readVarint(buf, pos, extSize)) return false;
    if (extSize > buf.size() - pos) return false;
    pos += static_cast<size_t>(extSize);

    // Death frames
    uint64_t deathCount = 0;
    if (!readVarint(buf, pos, deathCount)) return false;
    if (deathCount > 500000) return false;
    for (uint64_t d = 0; d < deathCount; ++d) {
        uint64_t dummy = 0;
        if (!readVarint(buf, pos, dummy)) return false;
    }

    // Inputs (all P1 inputs first, then all P2 inputs)
    uint64_t inputCount = 0, p1Count = 0;
    if (!readVarint(buf, pos, inputCount)) return false;
    if (!readVarint(buf, pos, p1Count)) return false;
    if (inputCount == 0 || inputCount > 2000000 || p1Count > inputCount) return false;

    out.sourceFps = fps;
    if (!botName.empty() && out.sourceBot.empty()) out.sourceBot = botName;
    out.inputs.clear();
    out.inputs.reserve(static_cast<size_t>(inputCount));

    bool isPlat = (platformer != 0);
    bool hasInputExt = !inputTag.empty();
    uint64_t curFrameP1 = 0;
    uint64_t curFrameP2 = 0;

    for (uint64_t i = 0; i < inputCount; ++i) {
        uint64_t packed = 0;
        if (!readVarint(buf, pos, packed)) return false;

        if (hasInputExt) {
            uint64_t iExtSize = 0;
            if (!readVarint(buf, pos, iExtSize)) return false;
            if (iExtSize > buf.size() - pos) return false;
            pos += static_cast<size_t>(iExtSize);
        }

        bool isP1 = (i < p1Count);
        bool down = (packed & 1u) != 0;
        int btn = 1;
        uint64_t delta = 0;
        if (isPlat) {
            btn = static_cast<int>((packed >> 1) & 3u);
            delta = (packed >> 3);
        } else {
            btn = 1;
            delta = (packed >> 1);
        }

        uint64_t frame = 0;
        if (isP1) {
            curFrameP1 += delta;
            frame = curFrameP1;
        } else {
            curFrameP2 += delta;
            frame = curFrameP2;
        }

        if (btn == 1) {
            out.inputs.push_back({ scaleFrameTo240(frame, fps), down, 1, isP1 });
        }
    }

    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.tick240 < b.tick240;
    });
    return !out.inputs.empty();
}

inline bool parseGDR2(std::vector<uint8_t> const& buf, LoadedMacro& out) {
    // Official libGDR2 (xdBot, Eclipse, MegaHack) writes duration as 4-byte BE float
    // and framerate as 8-byte BE double. Fallback to varint header for legacy spec files.
    if (parseGDR2Impl(buf, out, true)) return true;
    out.inputs.clear();
    return parseGDR2Impl(buf, out, false);
}

// =========================================================================
// 2. Silicate (.slc) Parser (v1, v2, and v3)
//    Format by Peony (silicate-bot/slc)
// =========================================================================
inline bool parseSilicateV3(std::vector<uint8_t> const& buf, LoadedMacro& out) {
    // Official Silicate v3 format:
    //   [0..7]   "SLC3RPLY" (8 bytes)
    //   [8..9]   uint16_t metaSize
    //   [10..]   meta block (starts with double tps)
    //   Atoms:   uint32_t atomId, uint64_t atomLen, payload[atomLen] ... terminated by 0xCC
    //   Atom 1 (ActionAtom): uint64_t actionCount, then RLE chunks:
    //     uint8_t mode (0x00), uint8_t tag: width = ((tag >> 4) & 0xF) + 1, count = 1 << (tag & 0xF)
    //     Each little-endian value: delta = val >> 4, btn = (val >> 2) & 3, isP1 = (val >> 1) & 1, down = val & 1
    if (buf.size() < 24) return false;
    if (std::memcmp(buf.data(), "SLC3RPLY", 8) != 0) return false;

    size_t pos = 8;
    uint16_t metaSize = 0;
    if (!readLE(buf, pos, metaSize)) return false;
    if (pos + static_cast<size_t>(metaSize) > buf.size()) return false;

    double tps = 240.0;
    if (metaSize >= 8) {
        size_t tpsPos = pos;
        double rawTps = 240.0;
        if (readLE(buf, tpsPos, rawTps) && std::isfinite(rawTps) && rawTps >= 20.0 && rawTps <= 100000.0) {
            tps = rawTps;
        }
    }
    pos += static_cast<size_t>(metaSize);

    out.sourceFps = tps;
    out.sourceBot = "Silicate";
    out.inputs.clear();

    while (pos < buf.size()) {
        if (buf[pos] == 0xCCu) break;
        if (pos + 12 > buf.size()) break;
        uint32_t atomId = 0;
        uint64_t atomLen = 0;
        if (!readLE(buf, pos, atomId) || !readLE(buf, pos, atomLen)) break;
        if (atomLen > buf.size() - pos) break;
        size_t atomEnd = pos + static_cast<size_t>(atomLen);

        if (atomId == 1 && atomLen >= 8) {
            size_t aPos = pos;
            uint64_t actionCount = 0;
            if (readLE(buf, aPos, actionCount) && actionCount > 0 && actionCount < 2000000) {
                out.inputs.reserve(static_cast<size_t>(actionCount));
                uint64_t curFrame = 0;
                uint64_t decoded = 0;
                auto emitSlcVal = [&](uint64_t val) {
                    decoded++;
                    uint64_t delta = val >> 4;
                    curFrame += delta;
                    uint8_t low4 = static_cast<uint8_t>(val & 0xFu);
                    int btn = static_cast<int>((low4 >> 2) & 3u);
                    bool isP1 = ((low4 >> 1) & 1u) != 0;
                    bool down = (low4 & 1u) != 0;
                    if (btn == 1) {
                        if (curFrame == 0 && !down) return;
                        out.inputs.push_back({ scaleFrameTo240(curFrame, tps), down, 1, isP1 });
                    }
                };
                while (aPos + 2 <= atomEnd && decoded < actionCount) {
                    uint8_t mode = buf[aPos++];
                    uint8_t tag = buf[aPos++];
                    size_t width = static_cast<size_t>((tag >> 4) & 0xFu) + 1;
                    size_t runCount = static_cast<size_t>(1u) << (tag & 0xFu);
                    if (width > 8) break;
                    if (mode == 0x00u) {
                        // Literal run: runCount distinct values of `width` bytes
                        for (size_t r = 0; r < runCount && decoded < actionCount; ++r) {
                            if (aPos + width > atomEnd) break;
                            uint64_t val = 0;
                            for (size_t b = 0; b < width; ++b) {
                                val |= static_cast<uint64_t>(buf[aPos++]) << (b * 8);
                            }
                            emitSlcVal(val);
                        }
                    } else if (mode == 0x01u) {
                        // RLE repeat run: 1 value of `width` bytes repeated runCount times
                        if (aPos + width > atomEnd) break;
                        uint64_t val = 0;
                        for (size_t b = 0; b < width; ++b) {
                            val |= static_cast<uint64_t>(buf[aPos++]) << (b * 8);
                        }
                        for (size_t r = 0; r < runCount && decoded < actionCount; ++r) {
                            emitSlcVal(val);
                        }
                    } else {
                        break;
                    }
                }
            }
        }
        pos = atomEnd;
    }

    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.tick240 < b.tick240;
    });
    return !out.inputs.empty();
}

inline bool parseSilicateV2OrV1(std::vector<uint8_t> const& buf, LoadedMacro& out) {
    if (buf.size() < 24) return false;
    size_t pos = 0;

    // Optional "SLC2" / "SLC\x02" header
    if (buf[0] == 'S' && buf[1] == 'L' && buf[2] == 'C' && (buf[3] == '2' || buf[3] == 2)) {
        pos = 4;
    }

    double tps = 0.0;
    if (!readLE(buf, pos, tps)) return false;
    if (!std::isfinite(tps) || tps < 20.0 || tps > 100000.0) return false;

    out.sourceFps = tps;
    out.sourceBot = "Silicate";
    out.inputs.clear();

    // Silicate v2 layout: [double tps] [uint64 metaLen] [meta bytes] [uint64 inputCount] [uint64 packed...]
    {
        size_t p2 = pos;
        uint64_t metaLen = 0;
        if (readLE(buf, p2, metaLen) && metaLen <= 65536 && p2 + static_cast<size_t>(metaLen) + 8 <= buf.size()) {
            p2 += static_cast<size_t>(metaLen);
            uint64_t count = 0;
            if (readLE(buf, p2, count) && count > 0 && count < 2000000) {
                size_t rem = buf.size() - p2;
                if (rem >= static_cast<size_t>(count) * 8) {
                    out.inputs.reserve(static_cast<size_t>(count));
                    double curTps = tps;
                    bool sane = true;
                    uint64_t prevFrame = 0;
                    for (uint64_t i = 0; i < count; ++i) {
                        uint64_t raw = 0;
                        if (!readLE(buf, p2, raw)) { sane = false; break; }
                        uint64_t frame = raw >> 4;
                        int btn = static_cast<int>((raw >> 2) & 3u);
                        bool isP2 = ((raw >> 1) & 1u) != 0;
                        bool down = (raw & 1u) != 0;
                        if (frame > 50000000ULL) { sane = false; break; }
                        if (btn == 0 && !down && isP2) {
                            // TPS change special record followed by double newTps
                            double newTps = curTps;
                            if (readLE(buf, p2, newTps) && std::isfinite(newTps) && newTps >= 20.0 && newTps <= 100000.0) {
                                curTps = newTps;
                            }
                            continue;
                        }
                        if (frame + 5 < prevFrame) { sane = false; break; }
                        prevFrame = frame;
                        if (btn == 1) {
                            out.inputs.push_back({ scaleFrameTo240(frame, curTps), down, 1, !isP2 });
                        }
                    }
                    if (sane && !out.inputs.empty()) {
                        std::stable_sort(out.inputs.begin(), out.inputs.end(), [](MacroInput const& a, MacroInput const& b) {
                            return a.tick240 < b.tick240;
                        });
                        return true;
                    }
                    out.inputs.clear();
                }
            }
        }
    }

    // Silicate v1 fallback layout: [double tps] [uint64 count] [struct { uint64 frame; uint8 state; }...]
    {
        size_t p1 = pos;
        uint64_t count = 0;
        if (readLE(buf, p1, count) && count > 0 && count < 2000000 && p1 + static_cast<size_t>(count) * 9 <= buf.size()) {
            out.inputs.reserve(static_cast<size_t>(count));
            for (uint64_t i = 0; i < count; ++i) {
                uint64_t frame = 0;
                uint8_t state = 0;
                if (!readLE(buf, p1, frame) || !readLE(buf, p1, state)) break;
                if (frame > 50000000ULL) { out.inputs.clear(); break; }
                bool down = (state & 1u) != 0;
                bool isP2 = ((state >> 1) & 1u) != 0;
                int btn = static_cast<int>((state >> 2) & 3u);
                if (btn == 0) btn = 1;
                if (btn == 1) {
                    out.inputs.push_back({ scaleFrameTo240(frame, tps), down, 1, !isP2 });
                }
            }
            if (!out.inputs.empty()) {
                std::stable_sort(out.inputs.begin(), out.inputs.end(), [](MacroInput const& a, MacroInput const& b) {
                    return a.tick240 < b.tick240;
                });
                return true;
            }
        }
    }

    return false;
}

// =========================================================================
// 3. Plaintext / legacy xdBot (.txt, .xd) Parser
// =========================================================================
inline bool parsePlaintextOrXD(std::vector<uint8_t> const& buf, LoadedMacro& out) {
    if (buf.empty()) return false;
    std::string text(reinterpret_cast<const char*>(buf.data()), buf.size());
    std::istringstream iss(text);
    std::string line;
    double fps = 240.0;
    bool gotFps = false;
    out.inputs.clear();

    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '/') continue;
        std::istringstream ls(line);
        std::vector<double> nums;
        double v = 0.0;
        while (ls >> v) nums.push_back(v);
        if (nums.empty()) continue;

        if (!gotFps && nums.size() == 1 && nums[0] >= 30.0 && nums[0] <= 100000.0) {
            fps = nums[0];
            gotFps = true;
            continue;
        }
        if (nums.size() >= 2) {
            uint64_t frame = static_cast<uint64_t>(std::max(0.0, nums[0]));
            bool down = (nums[1] > 0.5);
            bool isP1 = true;
            int btn = 1;
            if (nums.size() >= 3) {
                isP1 = (nums[2] < 0.5); // 0 = P1, 1 = P2
            }
            if (nums.size() >= 4) {
                btn = static_cast<int>(nums[3]);
            }
            if (btn == 1) {
                out.inputs.push_back({ scaleFrameTo240(frame, fps), down, 1, isP1 });
            }
        }
    }

    if (out.inputs.empty()) return false;
    out.sourceFps = fps;
    if (out.sourceBot.empty()) out.sourceBot = "xdBot/TXT";
    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.tick240 < b.tick240;
    });
    return true;
}

#if FWC_HAS_GEODE
// =========================================================================
// 4. JSON Replay Parser (.gdr, .gdr.json, .json — xdBot, Eclipse, MHR, TASbot)
// =========================================================================
inline bool parseJsonReplay(std::vector<uint8_t> const& buf, LoadedMacro& out) {
    if (buf.empty()) return false;
    // Skip leading whitespace and check '{'
    size_t s = 0;
    while (s < buf.size() && std::isspace(static_cast<unsigned char>(buf[s]))) s++;
    if (s >= buf.size() || buf[s] != '{') return false;

    std::string text(reinterpret_cast<const char*>(buf.data() + s), buf.size() - s);
    auto parsed = matjson::parse(text);
    if (!parsed.isOk()) return false;
    auto root = parsed.unwrap();
    if (!root.isObject()) return false;

    double fps = 240.0;
    if (root.contains("framerate") && root["framerate"].isNumber()) {
        fps = root["framerate"].asDouble().unwrapOr(240.0);
    } else if (root.contains("fps") && root["fps"].isNumber()) {
        fps = root["fps"].asDouble().unwrapOr(240.0);
    } else if (root.contains("tps") && root["tps"].isNumber()) {
        fps = root["tps"].asDouble().unwrapOr(240.0);
    }
    if (fps < 20.0 || fps > 100000.0) fps = 240.0;

    matjson::Value arr;
    if (root.contains("inputs") && root["inputs"].isArray()) arr = root["inputs"];
    else if (root.contains("events") && root["events"].isArray()) arr = root["events"];
    else if (root.contains("macro") && root["macro"].isArray()) arr = root["macro"];
    else return false;

    out.sourceFps = fps;
    if (root.contains("bot") && root["bot"].isObject() && root["bot"].contains("name")) {
        auto bn = root["bot"]["name"].asString().unwrapOr("");
        if (!bn.empty()) out.sourceBot = bn;
    }
    out.inputs.clear();

    auto vec = arr.asArray().unwrap();
    out.inputs.reserve(vec.size());
    for (auto const& item : vec) {
        if (!item.isObject()) continue;
        uint64_t frame = 0;
        if (item.contains("frame") && item["frame"].isNumber()) {
            frame = static_cast<uint64_t>(std::max(0.0, item["frame"].asDouble().unwrapOr(0.0)));
        } else {
            continue;
        }
        bool down = true;
        if (item.contains("down") && item["down"].isBool()) {
            down = item["down"].asBool().unwrapOr(true);
        } else if (item.contains("down") && item["down"].isNumber()) {
            down = item["down"].asDouble().unwrapOr(1.0) > 0.5;
        }
        int btn = 1;
        if (item.contains("btn") && item["btn"].isNumber()) {
            btn = static_cast<int>(item["btn"].asInt().unwrapOr(1));
        } else if (item.contains("button") && item["button"].isNumber()) {
            btn = static_cast<int>(item["button"].asInt().unwrapOr(1));
        }
        bool isP1 = true;
        if (item.contains("2p") && item["2p"].isBool()) {
            isP1 = !item["2p"].asBool().unwrapOr(false);
        } else if (item.contains("player2") && item["player2"].isBool()) {
            isP1 = !item["player2"].asBool().unwrapOr(false);
        } else if (item.contains("p2") && item["p2"].isBool()) {
            isP1 = !item["p2"].asBool().unwrapOr(false);
        }
        if (btn == 1) {
            out.inputs.push_back({ scaleFrameTo240(frame, fps), down, 1, isP1 });
        }
    }

    std::stable_sort(out.inputs.begin(), out.inputs.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.tick240 < b.tick240;
    });
    return !out.inputs.empty();
}
#endif

} // namespace macro_detail

class MacroStore {
private:
    LoadedMacro m_active;
    std::string m_lastError;

public:
    static MacroStore& get() {
        static MacroStore inst;
        return inst;
    }

    LoadedMacro const& active() const { return m_active; }
    bool hasMacro() const { return !m_active.empty(); }
    std::string const& lastError() const { return m_lastError; }

    void clear() {
        m_active = LoadedMacro{};
        m_lastError.clear();
    }

    bool loadFromBuffer(std::vector<uint8_t> const& buf, std::string const& fileName, std::string const& hintBot = "") {
        m_lastError.clear();
        if (buf.empty()) {
            m_lastError = "Empty macro file";
            return false;
        }

        LoadedMacro parsed;
        parsed.name = fileName;
        parsed.sourceBot = hintBot;

        bool ok = false;
        // 1. GDR2 binary ("GDR")
        if (buf.size() >= 3 && buf[0] == 'G' && buf[1] == 'D' && buf[2] == 'R') {
            ok = macro_detail::parseGDR2(buf, parsed);
        }
        // 2. Silicate v3 ("SLC3")
        if (!ok && buf.size() >= 4 && buf[0] == 'S' && buf[1] == 'L' && buf[2] == 'C') {
            ok = macro_detail::parseSilicateV3(buf, parsed);
            if (!ok) ok = macro_detail::parseSilicateV2OrV1(buf, parsed);
        }
#if FWC_HAS_GEODE
        // 3. JSON (.gdr, .gdr.json, .json)
        if (!ok) {
            ok = macro_detail::parseJsonReplay(buf, parsed);
        }
#endif
        // 4. Silicate v2/v1 without ASCII magic
        if (!ok) {
            std::string lower = macro_detail::toLowerAscii(fileName);
            if (lower.ends_with(".slc") || lower.ends_with(".slc2") || lower.ends_with(".slc3")) {
                ok = macro_detail::parseSilicateV2OrV1(buf, parsed);
            }
        }
        // 5. Plaintext / legacy xdBot (.xd, .txt)
        if (!ok) {
            ok = macro_detail::parsePlaintextOrXD(buf, parsed);
        }
        // 6. Final fallback: try Silicate v2/v1 regardless of extension
        if (!ok) {
            ok = macro_detail::parseSilicateV2OrV1(buf, parsed);
        }

        if (!ok) {
            m_lastError = "Unsupported or corrupted macro format";
            return false;
        }

        if (parsed.sourceBot.empty()) parsed.sourceBot = "File";
        m_active = std::move(parsed);
        return true;
    }

    bool savePlaintextToPath(std::filesystem::path const& path, std::vector<MacroInput> const& inputs, double fps = 240.0, std::string const& displayName = "") {
        m_lastError.clear();
        if (inputs.empty()) {
            m_lastError = "No inputs recorded";
            return false;
        }
        std::error_code ec;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            m_lastError = "Cannot write macro file";
            return false;
        }
        ofs << static_cast<int>(std::llround(fps)) << "\n";
        for (auto const& in : inputs) {
            ofs << in.tick240 << " " << (in.down ? 1 : 0) << " " << (in.isP1 ? 0 : 1) << " " << in.button << "\n";
        }
        ofs.flush();
        if (!ofs.good()) {
            m_lastError = "Write failed";
            return false;
        }
        LoadedMacro lm;
        lm.name = displayName.empty() ? path.filename().string() : displayName;
        lm.sourceBot = "FWC Rec";
        lm.path = path.string();
        lm.sourceFps = fps;
        lm.inputs = inputs;
        std::stable_sort(lm.inputs.begin(), lm.inputs.end(), [](MacroInput const& a, MacroInput const& b) {
            return a.tick240 < b.tick240;
        });
        m_active = std::move(lm);
        return true;
    }

#if FWC_HAS_GEODE
    std::string saveRecordedMacro(std::string const& levelName, std::vector<MacroInput> const& inputs, double fps = 240.0) {
        std::string safeName;
        for (char c : levelName) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ') {
                safeName.push_back(c);
            }
        }
        if (safeName.empty()) safeName = "Level";
        std::string fname = safeName + "_fwc.txt";
        auto outPath = getOwnMacrosDir() / fname;
        if (!savePlaintextToPath(outPath, inputs, fps, fname)) {
            return "";
        }
        return outPath.string();
    }
#endif

    bool loadFromFile(std::filesystem::path const& path, std::string const& hintBot = "") {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            m_lastError = "File not found";
            return false;
        }
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs) {
            m_lastError = "Cannot open file";
            return false;
        }
        auto sz = ifs.tellg();
        if (sz <= 0 || sz > 32 * 1024 * 1024) {
            m_lastError = "Invalid file size";
            return false;
        }
        std::vector<uint8_t> buf(static_cast<size_t>(sz));
        ifs.seekg(0, std::ios::beg);
        if (!ifs.read(reinterpret_cast<char*>(buf.data()), sz)) {
            m_lastError = "Read error";
            return false;
        }
        bool ok = loadFromBuffer(buf, path.filename().string(), hintBot);
        if (ok) m_active.path = path.string();
        return ok;
    }

#if FWC_HAS_GEODE
    std::filesystem::path getOwnMacrosDir() const {
        auto dir = geode::Mod::get()->getSaveDir() / "macros";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::vector<DiscoveredMacro> discoverMacros(std::string const& currentLevelName) const {
        std::vector<DiscoveredMacro> out;
        std::string lvlLow = macro_detail::toLowerAscii(currentLevelName);

        struct ScanDir {
            std::filesystem::path dir;
            std::string tag;
            std::string bot;
        };
        std::vector<ScanDir> dirs;

        auto ownSave = geode::Mod::get()->getSaveDir();
        auto ownCfg = geode::Mod::get()->getConfigDir();
        auto saveParent = ownSave.parent_path();
        auto cfgParent = ownCfg.parent_path();
        auto persistentDir = geode::dirs::getModPersistentDir();
        auto gameDir = geode::dirs::getGameDir();

        // 1. Silicate (by Peony — AppData/Local/GeometryDash/geode_persistent/peony.silicate/replays!)
        for (auto const& root : { persistentDir / "peony.silicate", saveParent / "peony.silicate", cfgParent / "peony.silicate" }) {
            dirs.push_back({ root / "replays", "SLC", "Silicate" });
            dirs.push_back({ root / "macros", "SLC", "Silicate" });
            dirs.push_back({ root, "SLC", "Silicate" });
        }

        // 2. xdBot (by Zilko)
        for (auto const& root : { persistentDir / "zilko.xdbot", saveParent / "zilko.xdbot", cfgParent / "zilko.xdbot" }) {
            dirs.push_back({ root / "macros", "XDB", "xdBot" });
            dirs.push_back({ root / "replays", "XDB", "xdBot" });
            dirs.push_back({ root, "XDB", "xdBot" });
        }

        // 3. Eclipse Menu Bot
        for (auto const& root : { persistentDir / "eclipse.eclipse-menu", saveParent / "eclipse.eclipse-menu", cfgParent / "eclipse.eclipse-menu" }) {
            dirs.push_back({ root / "replays", "ECL", "Eclipse" });
            dirs.push_back({ root / "macros", "ECL", "Eclipse" });
            dirs.push_back({ root / "bot", "ECL", "Eclipse" });
        }

        // 4. Own FWC folder + generic game replays/macros
        dirs.push_back({ getOwnMacrosDir(), "FWC", "FWC" });
        dirs.push_back({ ownCfg / "macros", "FWC", "FWC" });
        dirs.push_back({ gameDir / "replays", "FILE", "File" });
        dirs.push_back({ gameDir / "macros", "FILE", "File" });

        auto isMacroExt = [](std::string const& ext) -> bool {
            return ext == ".slc" || ext == ".slc2" || ext == ".slc3" ||
                   ext == ".gdr2" || ext == ".gdr" || ext == ".json" ||
                   ext == ".xd" || ext == ".txt";
        };

        std::vector<std::string> seenPaths;
        std::error_code ec;

        for (auto const& sd : dirs) {
            if (!std::filesystem::exists(sd.dir, ec) || !std::filesystem::is_directory(sd.dir, ec)) continue;
            for (auto const& entry : std::filesystem::directory_iterator(sd.dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                auto p = entry.path();
                std::string ext = macro_detail::toLowerAscii(p.extension().string());
                if (!isMacroExt(ext)) continue;

                std::string fullStr = p.string();
                if (std::find(seenPaths.begin(), seenPaths.end(), fullStr) != seenPaths.end()) continue;
                seenPaths.push_back(fullStr);

                std::string fname = p.filename().string();
                std::string fnameLow = macro_detail::toLowerAscii(fname);
                if (ext == ".json") {
                    if (fnameLow == "settings.json" || fnameLow == "keybinds.json" ||
                        fnameLow == "labels.json" || fnameLow == "config.json" ||
                        fnameLow == "mod.json" || fnameLow == "themes.json" ||
                        fnameLow == "saved.json") {
                        continue;
                    }
                }
                bool matches = !lvlLow.empty() && lvlLow.size() >= 3 && (fnameLow.find(lvlLow) != std::string::npos);

                int64_t mtime = 0;
                auto ft = entry.last_write_time(ec);
                if (!ec) {
                    mtime = static_cast<int64_t>(ft.time_since_epoch().count());
                }

                out.push_back({ fname, sd.tag, sd.bot, fullStr, mtime, matches });
                if (out.size() >= 120) break;
            }
            if (out.size() >= 120) break;
        }

        std::sort(out.begin(), out.end(), [](DiscoveredMacro const& a, DiscoveredMacro const& b) {
            if (a.matchesLevel != b.matchesLevel) return a.matchesLevel > b.matchesLevel;
            return a.modTime > b.modTime;
        });
        return out;
    }
#endif
};

} // namespace fwc
