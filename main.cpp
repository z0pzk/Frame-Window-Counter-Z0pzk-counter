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
#include <cstdint>
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
    bool isPress = true;
    bool checkGroundSupport = false;
};

inline bool isOrbKind(ObjKind k) {
    return static_cast<uint8_t>(k) >= static_cast<uint8_t>(ObjKind::YellowOrb);
}

// Binding-safe Jump-hold reader for GD 2.2081 (handles m_holdingButtons map/array/vector or m_isHolding)
template <typename P>
bool isPlayerHoldingJump(P* p) {
    if (!p) return false;
    if constexpr (requires { p->m_holdingButtons.find(PlayerButton::Jump)->second; }) {
        auto it = p->m_holdingButtons.find(PlayerButton::Jump);
        return it != p->m_holdingButtons.end() && static_cast<bool>(it->second);
    } else if constexpr (requires { p->m_holdingButtons.find(1)->second; }) {
        auto it = p->m_holdingButtons.find(1);
        return it != p->m_holdingButtons.end() && static_cast<bool>(it->second);
    } else if constexpr (requires { p->m_holdingButtons.size(); static_cast<bool>(p->m_holdingButtons[1]); }) {
        return p->m_holdingButtons.size() > 1 && static_cast<bool>(p->m_holdingButtons[1]);
    } else if constexpr (requires { static_cast<bool>(p->m_holdingButtons[1]); }) {
        return static_cast<bool>(p->m_holdingButtons[1]);
    } else if constexpr (requires { static_cast<bool>(p->m_isHolding); }) {
        return static_cast<bool>(p->m_isHolding);
    } else {
        return false;
    }
}

} // namespace

class $modify(FWCPlayLayer, PlayLayer) {
    struct Fields {
        fwc::ModConfig cfg;
        fwc::HudOverlay* overlay = nullptr;
        std::vector<SpatialObj> spatialCache;
        bool objectsIndexed = false;

        double simTime = 0.0;
        uint64_t updateTick = 0;
        float prevPlayerX = 0.0f;
        float measuredSpeedX = 311.58f;

        // Universal bot-safe input tracking (handleButton + jump-hold edge fallback)
        bool prevHolding[2] = { false, false };
        uint64_t lastHookTick[2][2] = { { 0, 0 }, { 0, 0 } }; // [pIdx][down?1:0]
        bool lastPlayerMoved = false;
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

    static bool rectOverlaps(float ax0, float ax1, float ay0, float ay1, SpatialObj const& o) {
        return ax1 > o.minX && ax0 < o.maxX && ay1 > o.minY && ay0 < o.maxY;
    }

    static bool hitsHazard(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool waveMode) {
        float px0 = cx - hw, px1 = cx + hw, py0 = cy - hh, py1 = cy + hh;
        for (auto const& o : local) {
            if (o.kind == ObjKind::Hazard || (waveMode && o.kind == ObjKind::Solid)) {
                if (rectOverlaps(px0, px1, py0, py1, o)) return true;
            }
        }
        return false;
    }

    static bool hitsSolidCore(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh) {
        float px0 = cx - hw * 0.30f, px1 = cx + hw * 0.30f;
        float py0 = cy - hh * 0.30f, py1 = cy + hh * 0.30f;
        for (auto const& o : local) {
            if (o.kind == ObjKind::Solid) {
                if (rectOverlaps(px0, px1, py0, py1, o)) return true;
            }
        }
        return false;
    }

    static bool hasGroundSupport(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown) {
        if (!upsideDown && cy - hh <= 6.0f) return true;
        float px0 = cx - hw * 0.95f, px1 = cx + hw * 0.95f;
        float py0 = upsideDown ? (cy + hh - 4.0f) : (cy - hh - 8.0f);
        float py1 = upsideDown ? (cy + hh + 8.0f) : (cy - hh + 4.0f);
        for (auto const& o : local) {
            if (o.kind == ObjKind::Solid) {
                if (rectOverlaps(px0, px1, py0, py1, o)) return true;
            }
        }
        return false;
    }

    static bool nearAnyGround(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown) {
        if (!upsideDown && cy - hh <= 28.0f) return true;
        float px0 = cx - hw - 10.0f, px1 = cx + hw + 10.0f;
        float py0 = upsideDown ? (cy + hh - 6.0f) : (cy - hh - 28.0f);
        float py1 = upsideDown ? (cy + hh + 28.0f) : (cy - hh + 6.0f);
        for (auto const& o : local) {
            if (o.kind == ObjKind::Solid) {
                if (rectOverlaps(px0, px1, py0, py1, o)) return true;
            }
        }
        return false;
    }

    static ObjKind findOverlappingOrb(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, float margin, bool& found) {
        float px0 = cx - hw - margin, px1 = cx + hw + margin;
        float py0 = cy - hh - margin, py1 = cy + hh + margin;
        for (auto const& o : local) {
            if (isOrbKind(o.kind)) {
                if (rectOverlaps(px0, px1, py0, py1, o)) {
                    found = true;
                    return o.kind;
                }
            }
        }
        found = false;
        return ObjKind::Solid;
    }

    static float simulateSurvival(SimParams const& p, std::vector<SpatialObj> const& local, float deltaT, bool clickWasOrb, ObjKind orbKind) {
        constexpr float dt = 1.0f / 240.0f;
        float gravSign = p.upsideDown ? -1.0f : 1.0f;
        float xClick = p.x0 + p.vx * deltaT;
        float yClick = p.y0;

        float hazHW = p.isWave ? (p.halfW * 0.32f) : (p.halfW * 0.80f);
        float hazHH = p.isWave ? (p.halfH * 0.32f) : (p.halfH * 0.80f);

        if (p.isWave) {
            float slope = p.mini ? 2.0f : 1.0f;
            float preVy = (p.isPress ? -1.0f : 1.0f) * gravSign * p.vx * slope;
            yClick = p.y0 + preVy * deltaT;
            if (deltaT > 0.0f) {
                int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
                for (int s = 1; s <= preSteps; ++s) {
                    float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                    float tx = p.x0 + p.vx * ts;
                    float ty = p.y0 + preVy * ts;
                    if (ty < hazHH || hitsHazard(local, tx, ty, hazHW, hazHH, true)) {
                        return 0.0f;
                    }
                }
            }
        } else if (p.onGround && !clickWasOrb && !p.isShip && !p.isUfo) {
            if (p.checkGroundSupport && !hasGroundSupport(local, xClick, yClick, p.halfW, p.halfH, p.upsideDown)) {
                return 0.0f;
            }
            if (deltaT > 0.0f) {
                int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
                for (int s = 1; s <= preSteps; ++s) {
                    float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                    float tx = p.x0 + p.vx * ts;
                    if (hitsHazard(local, tx, yClick, hazHW, hazHH, false) ||
                        hitsSolidCore(local, tx, yClick, p.halfW, p.halfH)) {
                        return 0.0f;
                    }
                }
            }
        } else {
            float preG = (p.isShip ? -1050.0f : -3216.0f) * gravSign;
            yClick = p.y0 + p.vy0 * deltaT + 0.5f * preG * deltaT * deltaT;
        }

        if (clickWasOrb) {
            bool stillOnOrb = false;
            findOverlappingOrb(local, xClick, yClick, p.halfW, p.halfH, 4.0f, stillOnOrb);
            if (!stillOnOrb) return 0.0f;
        }

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
            vy = (p.mini ? 632.0f : 644.0f) * gravSign;
            g = (p.mini ? -3920.0f : -3216.0f) * gravSign;
        }

        constexpr int kSteps = 100;
        float maxDist = p.vx * (static_cast<float>(kSteps) * dt);
        bool startedInsideSolid = !p.isWave && hitsSolidCore(local, x, y, p.halfW, p.halfH);

        for (int step = 1; step <= kSteps; ++step) {
            x += p.vx * dt;
            vy += g * dt;
            y += vy * dt;

            if (y < p.halfH && !p.upsideDown) {
                if (p.isWave) return x - xClick;
                y = p.halfH;
                vy = 0.0f;
                if (!p.isShip && step > 14) return maxDist;
            }

            if (hitsHazard(local, x, y, hazHW, hazHH, p.isWave)) {
                return x - xClick;
            }
            if (!p.isWave && (!startedInsideSolid || step > 6) && hitsSolidCore(local, x, y, p.halfW, p.halfH)) {
                return x - xClick;
            }

            if (!p.isWave && !p.isShip && !p.isUfo && step > 16 && (vy * gravSign < 0.0f)) {
                if (hasGroundSupport(local, x, y, p.halfW, p.halfH, p.upsideDown)) {
                    return maxDist;
                }
            }
        }
        return maxDist;
    }

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

        if (!isPress && !p.isWave) return -1.0;

        std::vector<SpatialObj> local;
        gatherLocalSlice(p.x0 - 110.0f, p.x0 + p.vx * 0.46f + 110.0f, local);

        bool clickWasOrb = false;
        ObjKind orbKind = findOverlappingOrb(local, p.x0, p.y0, p.halfW, p.halfH, 10.0f, clickWasOrb);

        if (!p.isWave && !p.isShip && !p.isUfo && !p.onGround && !clickWasOrb) {
            if (!nearAnyGround(local, p.x0, p.y0, p.halfW, p.halfH, p.upsideDown)) {
                return -1.0;
            }
            p.onGround = true;
        }

        p.checkGroundSupport = p.onGround && !clickWasOrb &&
            hasGroundSupport(local, p.x0, p.y0, p.halfW, p.halfH, p.upsideDown);

        if (local.empty()) {
            return std::clamp(0.046 * refFps, 1.0, refFps * 0.5);
        }

        constexpr int kHalfN = 24;
        constexpr int kTotal = 2 * kHalfN + 1;
        constexpr float kStep = 0.028f / static_cast<float>(kHalfN);

        float surv[kTotal];
        float bestSurv = 0.0f;

        for (int i = 0; i < kTotal; ++i) {
            float dT = static_cast<float>(i - kHalfN) * kStep;
            surv[i] = simulateSurvival(p, local, dT, clickWasOrb, orbKind);
            if (surv[i] > bestSurv) bestSurv = surv[i];
        }

        if (bestSurv < 8.0f) {
            return std::clamp(0.042 * refFps, 1.0, refFps * 0.5);
        }

        float thresh = bestSurv * 0.92f;

        int anchor = -1;
        for (int r = 0; r <= kHalfN; ++r) {
            if (surv[kHalfN + r] >= thresh) { anchor = kHalfN + r; break; }
            if (r > 0 && surv[kHalfN - r] >= thresh) { anchor = kHalfN - r; break; }
        }
        if (anchor < 0) {
            return std::clamp(0.042 * refFps, 1.0, refFps * 0.5);
        }

        int leftIdx = anchor;
        while (leftIdx > 0 && surv[leftIdx - 1] >= thresh) leftIdx--;

        int rightIdx = anchor;
        while (rightIdx < kTotal - 1 && surv[rightIdx + 1] >= thresh) rightIdx++;

        if (leftIdx == 0 && rightIdx == kTotal - 1) {
            return std::clamp(0.046 * refFps, 1.0, refFps * 0.5);
        }

        float leftT = static_cast<float>(leftIdx - kHalfN) * kStep;
        if (leftIdx > 0) {
            float badT = leftT - kStep;
            float goodT = leftT;
            for (int it = 0; it < 4; ++it) {
                float mid = 0.5f * (badT + goodT);
                if (simulateSurvival(p, local, mid, clickWasOrb, orbKind) >= thresh) goodT = mid;
                else badT = mid;
            }
            leftT = goodT;
        }

        float rightT = static_cast<float>(rightIdx - kHalfN) * kStep;
        if (rightIdx < kTotal - 1) {
            float goodT = rightT;
            float badT = rightT + kStep;
            for (int it = 0; it < 4; ++it) {
                float mid = 0.5f * (goodT + badT);
                if (simulateSurvival(p, local, mid, clickWasOrb, orbKind) >= thresh) goodT = mid;
                else badT = mid;
            }
            rightT = goodT;
        }

        double windowSec = std::max(0.0004, static_cast<double>(rightT - leftT));
        double frames = windowSec * std::max(30.0, refFps);
        return std::clamp(frames, 0.10, std::max(12.0, refFps * 0.055));
    }

    void triggerFeedback(PlayerObject* player, double frames, std::string const& manualComment) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled || !player) return;

        if (f.overlay) {
            f.overlay->onClickTriggered(m_objectLayer, player->getPosition(), frames, manualComment);
        }

        if (f.cfg.soundEnabled) {
            double fps = static_cast<double>(f.cfg.refFps);
            auto tiers = fwc::buildTiers(f.cfg.tableStyle, fps);
            int ti = fwc::findTierIndex(tiers, frames);
            float pitch = f.cfg.soundPitch + (f.cfg.dynamicPitch ? tiers[static_cast<size_t>(ti)].pitchOffset : 0.0f);
            fwc::AudioManager::get().playBell(f.cfg.soundPreset, f.cfg.customSoundFile, f.cfg.soundVolume, pitch);
        }
    }

    void handleResolvedClick(PlayerObject* player, bool down, bool isPlayer1, bool fromHook) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled) return;
        if (!player) player = isPlayer1 ? m_player1 : m_player2;
        if (!player) return;
        if (player->m_isDead && !f.lastPlayerMoved) return;

        int pIdx = isPlayer1 ? 0 : 1;
        int dIdx = down ? 1 : 0;

        if (fromHook) {
            f.lastHookTick[pIdx][dIdx] = f.updateTick;
            f.prevHolding[pIdx] = down;
        } else {
            if (f.lastHookTick[pIdx][dIdx] == f.updateTick) return;
        }

        if (f.cfg.mode == fwc::CounterMode::Auto || f.cfg.captureToManual) {
            double estFrames = computeAutoWindowFrames(player, down, static_cast<double>(f.cfg.refFps));
            if (estFrames < 0.0) return;

            if (f.cfg.captureToManual) {
                auto pos = player->getPosition();
                fwc::MarkerStore::get().addMarker(pos.x, pos.y, estFrames, "");
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
        f.updateTick = 1;
        f.objectsIndexed = false;
        f.prevPlayerX = m_player1 ? m_player1->getPositionX() : 0.0f;
        f.prevHolding[0] = isPlayerHoldingJump(m_player1);
        f.prevHolding[1] = isPlayerHoldingJump(m_player2);

        (void)fwc::AudioManager::get().getCustomSoundsDir();
        fwc::MarkerStore::get().loadForLevel(level);

        std::string levelName = level ? std::string(level->m_levelName) : "LEVEL";
        f.overlay = fwc::HudOverlay::create(levelName, m_objectLayer);
        if (f.overlay) {
            this->addChild(f.overlay, 100);
        }
        return true;
    }

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
        f.updateTick++;
        f.prevPlayerX = m_player1 ? m_player1->getPositionX() : 0.0f;
        f.prevHolding[0] = false;
        f.prevHolding[1] = false;
        f.lastHookTick[0][0] = f.lastHookTick[0][1] = 0;
        f.lastHookTick[1][0] = f.lastHookTick[1][1] = 0;
        f.lastPlayerMoved = false;

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

        float curX = 0.0f;
        if (m_player1) {
            curX = m_player1->getPositionX();
            float dx = curX - f.prevPlayerX;

            if (dx < -15.0f) {
                f.prevHolding[0] = isPlayerHoldingJump(m_player1);
                f.prevHolding[1] = isPlayerHoldingJump(m_player2);
                fwc::MarkerStore::get().resetTriggersAfter(curX);
                if (f.overlay) {
                    f.overlay->pruneCirclesAfter(curX);
                }
            } else if (dx > 0.01f) {
                f.lastPlayerMoved = true;
                f.simTime += static_cast<double>(dt);
                if (dt > 1e-6f) {
                    float instSpeed = dx / dt;
                    if (instSpeed > 80.0f && instSpeed < 1600.0f) {
                        f.measuredSpeedX = f.measuredSpeedX * 0.85f + instSpeed * 0.15f;
                    }
                }
            } else {
                f.lastPlayerMoved = false;
            }
            f.prevPlayerX = curX;

            // Universal Bot Fallback Detector (binding-safe m_holdingButtons check)
            auto checkPlayerHoldEdge = [&](PlayerObject* p, bool isP1) {
                if (!p) return;
                int pIdx = isP1 ? 0 : 1;
                bool curHold = isPlayerHoldingJump(p);
                if (curHold != f.prevHolding[pIdx]) {
                    f.prevHolding[pIdx] = curHold;
                    handleResolvedClick(p, curHold, isP1, false);
                }
            };
            checkPlayerHoldEdge(m_player1, true);
            if (m_player2 && m_player2->isVisible()) {
                checkPlayerHoldEdge(m_player2, false);
            }

            // Trigger Manual markers & manual comments
            auto& markers = fwc::MarkerStore::get().markers();
            for (auto& mk : markers) {
                if (!mk.triggered && curX >= mk.x) {
                    mk.triggered = true;
                    if (f.cfg.mode == fwc::CounterMode::Manual) {
                        triggerFeedback(m_player1, mk.frames, mk.comment);
                    } else if (!mk.comment.empty() && f.overlay) {
                        auto tiers = fwc::buildTiers(f.cfg.tableStyle, static_cast<double>(f.cfg.refFps));
                        int ti = fwc::findTierIndex(tiers, mk.frames);
                        f.overlay->showManualCallout(mk.comment, tiers[static_cast<size_t>(ti)].color);
                    }
                }
            }
        }

        if (f.overlay) {
            f.overlay->ensureWorldContainer(m_objectLayer);
            f.overlay->stepSimulation(dt, curX);
        }

        f.updateTick++;
    }
};

// Late-priority handleButton hook (runs AFTER Silicate/xdBot/Eclipse filters & receives their injected bot clicks!)
class $modify(FWCGJBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", 10000);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (button == 1) {
            if (auto* pl = typeinfo_cast<PlayLayer*>(this)) {
                auto* fwcPl = static_cast<FWCPlayLayer*>(pl);
                PlayerObject* p = isPlayer1 ? pl->m_player1 : pl->m_player2;
                fwcPl->handleResolvedClick(p, down, isPlayer1, true);
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
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
