#pragma once

#include "Config.hpp"
#include "TierSoundStore.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fwc {

using namespace geode::prelude;

class AudioManager {
private:
    std::unordered_map<std::string, FMOD::Sound*> m_cache;

    static std::string toLowerStr(std::string s) {
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }

public:
    static AudioManager& get() {
        static AudioManager instance;
        return instance;
    }

    std::filesystem::path getCustomSoundsDir() const {
        auto dir = Mod::get()->getConfigDir() / "sounds";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::vector<std::string> listCustomSounds() const {
        std::vector<std::string> out;
        auto dir = getCustomSoundsDir();
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return out;

        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto ext = toLowerStr(entry.path().extension().string());
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
                out.push_back(entry.path().filename().string());
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::string resolvePath(std::string const& preset, std::string const& customFile) const {
        if (preset == "custom" && !customFile.empty()) {
            std::error_code ec;
            std::filesystem::path p(customFile);
            if (p.is_absolute() && std::filesystem::exists(p, ec)) {
                return p.string();
            }
            auto inDir = getCustomSoundsDir() / customFile;
            if (std::filesystem::exists(inDir, ec)) {
                return inDir.string();
            }
        }

        std::string filename = "bell_classic.wav";
        if (preset == "fb") filename = "bell_fb.wav";
        else if (preset == "chime") filename = "bell_chime.wav";
        else if (preset == "tick") filename = "bell_tick.wav";

        auto full = Mod::get()->getResourcesDir() / filename;
        return full.string();
    }

    void playBell(std::string const& preset, std::string const& customFile, float volume, float pitch) {
        if (volume <= 0.001f) return;

        auto* engine = FMODAudioEngine::sharedEngine();
        if (!engine) return;

        std::string path = resolvePath(preset, customFile);
        if (path.empty()) return;

        if (engine->m_system) {
            FMOD::Sound* sound = nullptr;
            auto it = m_cache.find(path);
            if (it != m_cache.end() && it->second != nullptr) {
                sound = it->second;
            } else {
                FMOD_RESULT res = engine->m_system->createSound(
                    path.c_str(),
                    FMOD_DEFAULT,
                    nullptr,
                    &sound
                );
                if (res == FMOD_OK && sound != nullptr) {
                    m_cache[path] = sound;
                } else {
                    sound = nullptr;
                }
            }

            if (sound != nullptr) {
                FMOD::Channel* channel = nullptr;
                FMOD_RESULT playRes = engine->m_system->playSound(sound, nullptr, true, &channel);
                if (playRes == FMOD_OK && channel != nullptr) {
                    channel->setVolume(std::clamp(volume, 0.0f, 1.5f));
                    channel->setPitch(std::clamp(pitch, 0.25f, 4.0f));
                    channel->setPaused(false);
                    return;
                }
            }
        }

        engine->playEffect(path);
    }

    void playForTier(ModConfig const& cfg, TierDef const& tier) {
        if (!cfg.soundEnabled) return;

        auto ov = TierSoundStore::get().getForTier(tier.key);
        if (ov.enabled) {
            playBell(ov.preset, ov.customFile, ov.volume, ov.pitch);
            return;
        }

        float pitch = cfg.soundPitch + (cfg.dynamicPitch ? tier.pitchOffset : 0.0f);
        playBell(cfg.soundPreset, cfg.customSoundFile, cfg.soundVolume, pitch);
    }
};

} // namespace fwc
