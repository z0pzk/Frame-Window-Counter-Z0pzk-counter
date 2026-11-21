#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "AudioManager.hpp"
#include "Config.hpp"
#include "HudOverlay.hpp"
#include "MarkerStore.hpp"
#include "SettingsPopup.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

using namespace geode::prelude;

namespace {

enum class ObjKind : uint8_t {
    Solid = 0,
    Hazard = 1,
    YellowOrb = 2,
    PinkOrb = 3,
    RedOrb = 4,
    BlueOrb = 5,
    GreenOrb = 6,
    BlackOrb = 7,
    DashOrb = 8
};

struct SpatialObj {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    ObjKind kind = ObjKind::Solid;
};

struct SimParams {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float halfW = 15.0f;
    float halfH = 15.0f;
    float vx = 311.58f;
    float vy0 = 0.0f;
    bool upsideDown = false;
    bool mini = false;
    bool isWave = false;
    bool isShip = false;
    bool isBall = false;
    bool isUfo = false;
    bool isSpider = false;
    bool onGround = true;
    bool isPress = true; // true = button down, false = button release (for Wave!)
};

inline bool isOrbKind(ObjKind k) {
    return static_cast<uint8_t>(k) >= static_cast<uint8_t>(ObjKind::YellowOrb);
}

} // namespace

class $modify(FWCPlayLayer, PlayLayer) {
    struct Fields {
        fwc::ModConfig cfg;
        fwc::HudOverlay* overlay = nullptr;
        std::vector<SpatialObj> spatialCache;
        bool objectsIndexed = false;

        double simTime = 0.0;
        double smoothTps = 240.0;
        float prevPlayerX = 0.0f;
        float measuredSpeedX = 311.58f;

        // Deduplication between GJBaseGameLayer::handleButton and PlayerObject::pushButton/releaseButton
        double lastDedupTime[2][2] = { { -1.0, -1.0 }, { -1.0, -1.0 } }; // [pIdx][down?1:0]
        float lastDedupX[2][2] = { { -9999.0f, -9999.0f }, { -9999.0f, -9999.0f } };

        // Live CPS sliding window (last 1.0 game second)
        std::deque<double> recentClicks;
    };

    void ensureObjectsIndexed() {
        auto& f = *m_fields.self();
        if (f.objectsIndexed && !f.spatialCache.empty()) return;
        if (!m_objects || m_objects->count() == 0) return;

        f.spatialCache.clear();
        f.spatialCache.reserve(m_objects->count() / 2);

        for (auto* obj : CCArrayExt<GameObject*>(m_objects)) {
            if (!obj) continue;
            auto t = obj->m_objectType;

            ObjKind kind = ObjKind::Solid;
            bool keep = false;

            if (t == GameObjectType::Hazard || t == GameObjectType::AnimatedHazard) {
                kind = ObjKind::Hazard;
                keep = true;
            } else if (t == GameObjectType::Solid || t == GameObjectType::Slope) {
                kind = ObjKind::Solid;
                keep = true;
            } else if (t == GameObjectType::YellowJumpRing) {
                kind = ObjKind::YellowOrb; keep = true;
            } else if (t == GameObjectType::PinkJumpRing) {
                kind = ObjKind::PinkOrb; keep = true;
            } else if (t == GameObjectType::RedJumpRing) {
                kind = ObjKind::RedOrb; keep = true;
            } else if (t == GameObjectType::GravityRing) {
                kind = ObjKind::BlueOrb; keep = true;
            } else if (t == GameObjectType::GreenRing) {
                kind = ObjKind::GreenOrb; keep = true;
            } else if (t == GameObjectType::DropRing) {
                kind = ObjKind::BlackOrb; keep = true;
            } else if (t == GameObjectType::DashRing || t == GameObjectType::GravityDashRing) {
                kind = ObjKind::DashOrb; keep = true;
            }

            if (!keep) continue;

            // Use GD's actual collision rectangle directly (GD already scales spikes to their real hitbox!)
            CCRect r = obj->getObjectRect();
            if (r.size.width <= 0.05f || r.size.height <= 0.05f) continue;

            SpatialObj s;
            s.minX = r.getMinX();
            s.maxX = r.getMaxX();
            s.minY = r.getMinY();
            s.maxY = r.getMaxY();
            s.kind = kind;
            f.spatialCache.push_back(s);
        }

        std::sort(f.spatialCache.begin(), f.spatialCache.end(), [](SpatialObj const& a, SpatialObj const& b) {
            return a.minX < b.minX;
        });
        f.objectsIndexed = true;
    }

    void gatherLocalSlice(float xMin, float xMax, std::vector<SpatialObj>& out) {
        auto const& cache = m_fields->spatialCache;
        out.clear();
        if (cache.empty()) return;

        auto it = std::lower_bound(
            cache.begin(), cache.end(), xMin - 90.0f,
            [](SpatialObj const& a, float val) { return a.minX < val; }
        );
        for (; it != cache.end() && it->minX <= xMax; ++it) {
            if (it->maxX >= xMin) {
                out.push_back(*it);
            }
        }
    }

    static bool hitsHazard(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool waveMode) {
        float px0 = cx - hw;
        float px1 = cx + hw;
        float py0 = cy - hh;
        float py1 = cy + hh;
        for (auto const& o : local) {
            if (o.kind == ObjKind::Hazard || (waveMode && o.kind == ObjKind::Solid)) {
                if (px1 > o.minX && px0 < o.maxX && py1 > o.minY && py0 < o.maxY) {
                    return true;
                }
            }
        }
        return false;
    }

    // Inner blue core (30% of player size) colliding with a solid block kills the player in GD
    static bool hitsSolidCore(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh) {
        float px0 = cx - hw * 0.32f;
        float px1 = cx + hw * 0.32f;
        float py0 = cy - hh * 0.32f;
        float py1 = cy + hh * 0.32f;
        for (auto const& o : local) {
            if (o.kind == ObjKind::Solid) {
                if (px1 > o.minX && px0 < o.maxX && py1 > o.minY && py0 < o.maxY) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool hasGroundSupport(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown) {
        if (!upsideDown && cy - hh <= 4.0f) return true; // Main floor y = 0
        float px0 = cx - hw * 0.95f;
        float px1 = cx + hw * 0.95f;
        float probeY0 = upsideDown ? (cy + hh - 3.0f) : (cy - hh - 5.0f);
        float probeY1 = upsideDown ? (cy + hh + 5.0f) : (cy - hh + 3.0f);
        for (auto const& o : local) {
            if (o.kind == ObjKind::Solid) {
                if (px1 > o.minX && px0 < o.maxX && probeY1 >= o.minY && probeY0 <= o.maxY) {
                    return true;
                }
            }
        }
        return false;
    }

    static ObjKind findOverlappingOrb(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool& found) {
        float px0 = cx - hw - 3.0f;
        float px1 = cx + hw + 3.0f;
        float py0 = cy - hh - 3.0f;
        float py1 = cy + hh + 3.0f;
        for (auto const& o : local) {
            if (isOrbKind(o.kind)) {
                if (px1 > o.minX && px0 < o.maxX && py1 > o.minY && py0 < o.maxY) {
                    found = true;
                    return o.kind;
                }
            }
        }
        found = false;
        return ObjKind::Solid;
    }

    // Returns survival distance (in GD X-units) achieved by a click shifted by deltaT seconds.
    // Returns -1.0f if the click is invalid (missed orb, walked off ledge, or died before clicking).
    static float simulateSurvivalDist(SimParams const& p, std::vector<SpatialObj> const& local, float deltaT, bool clickWasOrb, ObjKind orbKind) {
        constexpr float dt = 1.0f / 240.0f;
        float gravSign = p.upsideDown ? -1.0f : 1.0f;
        float xClick = p.x0 + p.vx * deltaT;
        float yClick = p.y0;

        float hazHW = p.isWave ? (p.halfW * 0.34f) : (p.halfW * 0.85f);
        float hazHH = p.isWave ? (p.halfH * 0.34f) : (p.halfH * 0.85f);

        // 1) Pre-click path (when shifting later or earlier)
        if (p.isWave) {
            float slope = p.mini ? 2.0f : 1.0f;
            // Before a press, wave was going down (-gravSign); before a release, wave was going up (+gravSign)
            float preVy = (p.isPress ? -1.0f : 1.0f) * gravSign * p.vx * slope;
            yClick = p.y0 + preVy * deltaT;
            if (deltaT > 0.0f) {
                int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
                for (int s = 1; s <= preSteps; ++s) {
                    float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                    float tx = p.x0 + p.vx * ts;
                    float ty = p.y0 + preVy * ts;
                    if (ty < hazHH || hitsHazard(local, tx, ty, hazHW, hazHH, true)) {
                        return -1.0f;
                    }
                }
            }
        } else if (p.onGround && !clickWasOrb && !p.isShip && !p.isUfo) {
            if (!hasGroundSupport(local, xClick, yClick, p.halfW, p.halfH, p.upsideDown)) {
                return -1.0f; // Fell off platform before clicking!
            }
            if (deltaT > 0.0f) {
                int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
                for (int s = 1; s <= preSteps; ++s) {
                    float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                    float tx = p.x0 + p.vx * ts;
                    if (hitsHazard(local, tx, yClick, hazHW, hazHH, false) ||
                        hitsSolidCore(local, tx, yClick, p.halfW, p.halfH)) {
                        return -1.0f;
                    }
                }
            }
        } else {
            float preG = (p.isShip ? -1050.0f : -3216.0f) * gravSign;
            yClick = p.y0 + p.vy0 * deltaT + 0.5f * preG * deltaT * deltaT;
        }

        if (clickWasOrb) {
            bool stillOnOrb = false;
            findOverlappingOrb(local, xClick, yClick, p.halfW, p.halfH, stillOnOrb);
            if (!stillOnOrb) return -1.0f; // Missed the orb hitbox!
        }

        // 2) Post-click kinematics (calibrated to real GD 2.2 jump height & distance!)
        float x = xClick;
        float y = yClick;
        float vy = 0.0f;
        float g = (p.mini ? -3920.0f : -3216.0f) * gravSign;

        if (clickWasOrb) {
            switch (orbKind) {
                case ObjKind::YellowOrb: vy = (p.mini ? 595.0f : 644.0f) * gravSign; break;
                case ObjKind::PinkOrb:   vy = (p.mini ? 470.0f : 515.0f) * gravSign; break;
                case ObjKind::RedOrb:    vy = (p.mini ? 720.0f : 790.0f) * gravSign; break;
                case ObjKind::BlueOrb:   gravSign = -gravSign; g = -g; vy = 220.0f * gravSign; break;
                case ObjKind::GreenOrb:  gravSign = -gravSign; g = -g; vy = 615.0f * gravSign; break;
                case ObjKind::BlackOrb:  vy = -880.0f * gravSign; break;
                case ObjKind::DashOrb:   vy = 0.0f; g = 0.0f; break;
                default:                 vy = 644.0f * gravSign; break;
            }
        } else if (p.isWave) {
            float slope = p.mini ? 2.0f : 1.0f;
            vy = (p.isPress ? 1.0f : -1.0f) * gravSign * p.vx * slope;
            g = 0.0f;
        } else if (p.isShip) {
            vy = p.vy0 + (p.isPress ? 150.0f : -150.0f) * gravSign;
            g = (p.isPress ? 1180.0f : -1050.0f) * gravSign;
        } else if (p.isUfo) {
            vy = (p.mini ? 430.0f : 480.0f) * gravSign;
            g = (p.mini ? -3100.0f : -2650.0f) * gravSign;
        } else if (p.isBall) {
            gravSign = -gravSign;
            vy = 160.0f * gravSign;
            g = -2650.0f * gravSign;
        } else if (p.isSpider) {
            // Instant teleport along vertical ray until hitting a solid surface
            float stepY = 4.0f * gravSign;
            for (int k = 1; k <= 140; ++k) {
                float ty = y + stepY * static_cast<float>(k);
                if (hitsHazard(local, x, ty, hazHW, hazHH, false)) return 0.0f;
                if (hasGroundSupport(local, x, ty, p.halfW, p.halfH, !p.upsideDown)) {
                    y = ty;
                    break;
                }
            }
            gravSign = -gravSign;
            vy = 0.0f;
            g = 0.0f;
        } else {
            // Exact GD 1x / mini Cube jump (height 2.15 blocks, air distance 4.16 blocks at 1x speed)
            vy = (p.mini ? 632.0f : 644.0f) * gravSign;
            g = (p.mini ? -3920.0f : -3216.0f) * gravSign;
        }

        // Simulate forward up to ~0.48s (enough to clear full triple spikes, orb chains, and wave corridors)
        constexpr int kSteps = 115;
        float maxDist = p.vx * (static_cast<float>(kSteps) * dt);

        for (int step = 1; step <= kSteps; ++step) {
            x += p.vx * dt;
            vy += g * dt;
            y += vy * dt;

            if (y < p.halfH && !p.upsideDown) {
                if (p.isWave) return x - xClick;
                y = p.halfH;
                vy = 0.0f;
                if (!p.isShip && step > 14) {
                    // Safely landed on ground after full jump!
                    return maxDist;
                }
            }

            if (hitsHazard(local, x, y, hazHW, hazHH, p.isWave)) {
                return x - xClick;
            }
            if (!p.isWave && hitsSolidCore(local, x, y, p.halfW, p.halfH)) {
                return x - xClick;
            }

            // Safely landed on a solid platform after apex
            if (!p.isWave && !p.isShip && !p.isUfo && step > 16 && (vy * gravSign < 0.0f)) {
                if (hasGroundSupport(local, x, y, p.halfW, p.halfH, p.upsideDown)) {
                    return maxDist;
                }
            }
        }
        return maxDist;
    }

    // Survival-based real frame window calculator (sub-frame 1/2400s resolution!)
    // Returns < 0 if the click is a meaningless mid-air spam click that should be ignored.
    double computeAutoWindowFrames(PlayerObject* player, bool isPress, double refFps) {
        auto& f = *m_fields.self();
        ensureObjectsIndexed();
        if (!player) return -1.0;

        SimParams p;
        CCPoint pos = player->getPosition();
        CCRect rect = player->getObjectRect();
        p.x0 = pos.x;
        p.y0 = pos.y;
        p.halfW = std::clamp(rect.size.width * 0.5f, 5.0f, 18.0f);
        p.halfH = std::clamp(rect.size.height * 0.5f, 5.0f, 18.0f);
        p.vx = std::clamp(f.measuredSpeedX, 180.0f, 950.0f);
        p.vy0 = static_cast<float>(player->m_yVelocity) * 60.0f;
        p.upsideDown = player->m_isUpsideDown;
        p.mini = (player->m_vehicleSize < 0.8f);
        p.isWave = player->m_isDart;
        p.isShip = player->m_isShip || player->m_isSwing;
        p.isBall = player->m_isBall;
        p.isUfo = player->m_isBird;
        p.isSpider = player->m_isSpider;
        p.onGround = player->m_isOnGround;
        p.isPress = isPress;

        // Release events only matter on Wave (direction reversal)
        if (!isPress && !p.isWave) {
            return -1.0;
        }

        std::vector<SpatialObj> local;
        gatherLocalSlice(p.x0 - 110.0f, p.x0 + p.vx * 0.52f + 110.0f, local);

        bool clickWasOrb = false;
        ObjKind orbKind = findOverlappingOrb(local, p.x0, p.y0, p.halfW, p.halfH, clickWasOrb);

        // Ignore useless mid-air clicks in Cube/Robot/Ball/Spider when not touching an orb!
        if (!p.isWave && !p.isShip && !p.isUfo && !p.onGround && !clickWasOrb) {
            return -1.0;
        }

        if (local.empty()) {
            return std::clamp(0.045 * refFps, 1.0, refFps * 0.5);
        }

        // Step 1: Compute baseline survival distance for the ACTUAL click (deltaT = 0)
        float baseSurv = simulateSurvivalDist(p, local, 0.0f, clickWasOrb, orbKind);
        if (baseSurv < 12.0f) {
            // Very tight or near-instant obstacle: compare against small probe radius
            baseSurv = std::max(baseSurv, 18.0f);
        }
        // Any candidate click that dies significantly sooner than the actual click is DEAD!
        float requiredSurv = baseSurv * 0.90f;

        // Step 2: Scan outward in 1/480s (~2.08ms) steps, then binary-search refine to ~0.13ms (2400+ subfps!)
        constexpr float coarseStep = 1.0f / 480.0f;
        constexpr int kMaxSteps = 26; // up to ~54ms each direction (~108ms total = ~26 frames @ 240Hz)

        auto isAlive = [&](float dT) -> bool {
            float d = simulateSurvivalDist(p, local, dT, clickWasOrb, orbKind);
            return d >= requiredSurv;
        };

        int safeLate = 0;
        for (int k = 1; k <= kMaxSteps; ++k) {
            if (isAlive(coarseStep * static_cast<float>(k))) safeLate = k;
            else break;
        }
        float lateEdge = coarseStep * static_cast<float>(safeLate);
        if (safeLate < kMaxSteps) {
            float lo = lateEdge;
            float hi = lateEdge + coarseStep;
            for (int it = 0; it < 5; ++it) {
                float mid = 0.5f * (lo + hi);
                if (isAlive(mid)) lo = mid;
                else hi = mid;
            }
            lateEdge = lo;
        }

        int safeEarly = 0;
        for (int k = 1; k <= kMaxSteps; ++k) {
            if (isAlive(-coarseStep * static_cast<float>(k))) safeEarly = k;
            else break;
        }
        float earlyEdge = coarseStep * static_cast<float>(safeEarly);
        if (safeEarly < kMaxSteps) {
            float lo = earlyEdge;
            float hi = earlyEdge + coarseStep;
            for (int it = 0; it < 5; ++it) {
                float mid = 0.5f * (lo + hi);
                if (isAlive(-mid)) lo = mid;
                else hi = mid;
            }
            earlyEdge = lo;
        }

        double windowSec = static_cast<double>(earlyEdge + lateEdge);
        if (windowSec < 0.00025) {
            windowSec = 0.0006; // tight sub-frame window
        }

        double frames = windowSec * std::max(30.0, refFps);
        return std::clamp(frames, 0.08, refFps * 0.5);
    }

    void triggerFeedback(PlayerObject* player, double frames, std::string const& customComment) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled || !player) return;

        // Track live CPS over the last 1.0 game second
        f.recentClicks.push_back(f.simTime);
        while (!f.recentClicks.empty() && (f.simTime - f.recentClicks.front()) > 1.0) {
            f.recentClicks.pop_front();
        }
        double liveCps = static_cast<double>(f.recentClicks.size());

        if (f.overlay) {
            f.overlay->onClickTriggered(m_objectLayer, player->getPosition(), frames, customComment, liveCps);
        }

        if (f.cfg.soundEnabled) {
            double fps = static_cast<double>(f.cfg.refFps);
            auto tiers = fwc::buildTiers(f.cfg.tableStyle, fps);
            int ti = fwc::findTierIndex(tiers, frames);
            float pitch = f.cfg.soundPitch + (f.cfg.dynamicPitch ? tiers[static_cast<size_t>(ti)].pitchOffset : 0.0f);
            fwc::AudioManager::get().playBell(f.cfg.soundPreset, f.cfg.customSoundFile, f.cfg.soundVolume, pitch);
        }
    }

    void onPlayerClick(PlayerObject* player, bool down, bool isPlayer1) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled) return;
        if (!player) player = isPlayer1 ? m_player1 : m_player2;
        if (!player || player->m_isDead) return;

        // Deduplicate between handleButton and PlayerObject::pushButton/releaseButton!
        int pIdx = isPlayer1 ? 0 : 1;
        int dIdx = down ? 1 : 0;
        float curX = player->getPositionX();
        if (std::fabs(f.simTime - f.lastDedupTime[pIdx][dIdx]) < 0.0005 &&
            std::fabs(curX - f.lastDedupX[pIdx][dIdx]) < 0.25f) {
            return;
        }
        f.lastDedupTime[pIdx][dIdx] = f.simTime;
        f.lastDedupX[pIdx][dIdx] = curX;

        if (f.cfg.mode == fwc::CounterMode::Auto || f.cfg.captureToManual) {
            double estFrames = computeAutoWindowFrames(player, down, static_cast<double>(f.cfg.refFps));
            if (estFrames < 0.0) return; // Ignored useless air click or non-wave release

            if (f.cfg.captureToManual) {
                fwc::MarkerStore::get().addMarker(curX, player->getPositionY(), estFrames, "");
            }

            if (f.cfg.mode == fwc::CounterMode::Auto) {
                triggerFeedback(player, estFrames, "");
            }
        }
    }

    void reloadFromSettings() {
        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        if (f.overlay) {
            f.overlay->reloadConfig(m_objectLayer);
        }
    }

    CCPoint getCurrentPlayerPosSafe() {
        return m_player1 ? m_player1->getPosition() : ccp(0.0f, 30.0f);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        f.simTime = 0.0;
        f.objectsIndexed = false;
        f.prevPlayerX = m_player1 ? m_player1->getPositionX() : 0.0f;

        (void)fwc::AudioManager::get().getCustomSoundsDir();
        fwc::MarkerStore::get().loadForLevel(level);

        std::string levelName = level ? std::string(level->m_levelName) : "LEVEL";
        f.overlay = fwc::HudOverlay::create(levelName, m_objectLayer);
        if (f.overlay) {
            this->addChild(f.overlay, 100);
        }
        return true;
    }

    // THIS is where GD 2.2 actually finishes populating m_objects!
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto& f = *m_fields.self();
        f.objectsIndexed = false;
        ensureObjectsIndexed();
        if (f.overlay) {
            f.overlay->ensureWorldContainer(m_objectLayer);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        f.simTime = 0.0;
        f.prevPlayerX = m_player1 ? m_player1->getPositionX() : 0.0f;
        f.recentClicks.clear();
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                f.lastDedupTime[i][j] = -1.0;
                f.lastDedupX[i][j] = -9999.0f;
            }
        }

        ensureObjectsIndexed();
        fwc::MarkerStore::get().resetTriggers();

        if (f.overlay) {
            f.overlay->resetAttempt(m_objectLayer);
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& f = *m_fields.self();
        if (!f.cfg.enabled || dt <= 0.0f) return;

        if (!f.objectsIndexed) {
            ensureObjectsIndexed();
        }

        double instTps = 1.0 / static_cast<double>(dt);
        if (instTps >= 20.0 && instTps <= 100000.0) {
            f.smoothTps = f.smoothTps * 0.90 + instTps * 0.10;
        }

        float curX = 0.0f;
        if (m_player1 && !m_player1->m_isDead) {
            f.simTime += static_cast<double>(dt);
            curX = m_player1->getPositionX();
            float dx = curX - f.prevPlayerX;
            if (dx > 0.01f && dt > 1e-6f) {
                float instSpeed = dx / dt;
                if (instSpeed > 80.0f && instSpeed < 1600.0f) {
                    f.measuredSpeedX = f.measuredSpeedX * 0.85f + instSpeed * 0.15f;
                }
            }
            f.prevPlayerX = curX;

            if (f.cfg.mode == fwc::CounterMode::Manual) {
                auto& markers = fwc::MarkerStore::get().markers();
                for (auto& mk : markers) {
                    if (!mk.triggered && curX >= mk.x) {
                        mk.triggered = true;
                        triggerFeedback(m_player1, mk.frames, mk.comment);
                    }
                }
            }
        }

        if (f.overlay) {
            f.overlay->ensureWorldContainer(m_objectLayer);
            f.overlay->stepSimulation(dt, curX);
        }
    }
};

// Hook 1: Standard input & bots that use handleButton
class $modify(FWCGJBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (button != 1) return;
        if (auto* pl = typeinfo_cast<PlayLayer*>(this)) {
            auto* fwcPl = static_cast<FWCPlayLayer*>(pl);
            PlayerObject* p = isPlayer1 ? pl->m_player1 : pl->m_player2;
            fwcPl->onPlayerClick(p, down, isPlayer1);
        }
    }
};

class $modify(FWCPauseLayer, PauseLayer) {
    void onOpenFrameCounter(CCObject*) {
        auto* pl = PlayLayer::get();
        CCPoint pos = ccp(0.0f, 30.0f);
        if (pl) {
            pos = static_cast<FWCPlayLayer*>(pl)->getCurrentPlayerPosSafe();
        }

        auto* popup = fwc::SettingsPopup::create(
            []() {
                if (auto* activePl = PlayLayer::get()) {
                    static_cast<FWCPlayLayer*>(activePl)->reloadFromSettings();
                }
            },
            pos.x,
            pos.y
        );
        if (popup) popup->show();
    }

    CCNode* makeSquarePauseIcon() {
        auto* sq = CCScale9Sprite::create("GJ_button_05.png");
        sq->setContentSize({ 34.0f, 34.0f });

        auto iconPath = (Mod::get()->getResourcesDir() / "fwc_icon.png").string();
        if (auto* icon = CCSprite::create(iconPath.c_str())) {
            float maxDim = std::max(icon->getContentSize().width, icon->getContentSize().height);
            if (maxDim > 1.0f) icon->setScale(22.0f / maxDim);
            icon->setPosition({ 17.0f, 17.0f });
            sq->addChild(icon, 1);
        } else {
            auto* lbl = CCLabelBMFont::create("FC", "bigFont.fnt");
            lbl->setScale(0.45f);
            lbl->setPosition({ 17.0f, 17.5f });
            sq->addChild(lbl, 1);
        }
        return sq;
    }

    void customSetup() {
        PauseLayer::customSetup();

        Ref<PauseLayer> selfRef = this;
        Loader::get()->queueInMainThread([selfRef]() {
            auto* self = static_cast<FWCPauseLayer*>(selfRef.data());
            if (!self || !self->getParent()) return;

            auto* btn = CCMenuItemSpriteExtra::create(
                self->makeSquarePauseIcon(),
                self,
                menu_selector(FWCPauseLayer::onOpenFrameCounter)
            );
            btn->setID("fwc-settings-button"_spr);

            if (auto* leftMenu = self->getChildByID("left-button-menu")) {
                leftMenu->addChild(btn);
                leftMenu->updateLayout();
                return;
            }
            if (auto* rightMenu = self->getChildByID("right-button-menu")) {
                rightMenu->addChild(btn);
                rightMenu->updateLayout();
                return;
            }

            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto* menu = CCMenu::create();
            menu->setPosition({ 0.0f, 0.0f });
            btn->setPosition({ 34.0f, winSize.height - 96.0f });
            menu->addChild(btn);
            self->addChild(menu, 50);
        });
    }
};
