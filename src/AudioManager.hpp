#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace fwc {

using namespace geode::prelude;

class AudioManager {
private:
    std::unordered_map<std::string, FMOD::Sound*> m_cache;

    std::string resolvePath(std::string const& preset) const {
        std::string filename = "bell_crystal.wav";
        if (preset == "soft") filename = "bell_soft.wav";
        else if (preset == "tick") filename = "bell_tick.wav";
        else if (preset == "deep") filename = "bell_deep.wav";

        auto full = Mod::get()->getResourcesDir() / filename;
        return full.string();
    }

public:
    static AudioManager& get() {
        static AudioManager instance;
        return instance;
    }

    void playBell(std::string const& preset, float volume, float pitch) {
        if (volume <= 0.001f) return;

        auto* engine = FMODAudioEngine::sharedEngine();
        if (!engine) return;

        std::string path = resolvePath(preset);
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

        // Safe fallback if FMOD system channel creation fails
        engine->playEffect(path);
    }
};

} // namespace fwc
