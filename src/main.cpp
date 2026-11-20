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
#include <vector>

using namespace geode::prelude;

namespace {

enum class ObjKind : uint8_t {
    Solid = 0,
    Hazard = 1,
    Orb = 2
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
    float halfW = 14.0f;
    float halfH = 14.0f;
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
};

} // namespace

class $modify(FWCPlayLayer, PlayLayer) {
    struct Fields {
        fwc::ModConfig cfg;
        fwc::HudOverlay* overlay = nullptr;
        std::vector<SpatialObj> spatialCache;

        double simTime = 0.0;
        double smoothTps = 240.0;
        float prevPlayerX = 0.0f;
        float measuredSpeedX = 311.58f;

        double lastPressTime[2] = { -10.0, -10.0 };
        double lastClickEventTime[2] = { -10.0, -10.0 };
        bool isHolding[2] = { false, false };
    };

    void indexLevelObjects() {
        auto& f = *m_fields.self();
        f.spatialCache.clear();
        if (!m_objects) return;

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
            } else if (
                t == GameObjectType::YellowJumpRing ||
                t == GameObjectType::PinkJumpRing ||
                t == GameObjectType::RedJumpRing ||
                t == GameObjectType::GravityRing ||
                t == GameObjectType::GreenRing ||
                t == GameObjectType::DashRing ||
                t == GameObjectType::GravityDashRing ||
                t == GameObjectType::DropRing
            ) {
                kind = ObjKind::Orb;
                keep = true;
            }

            if (!keep) continue;

            CCRect r = obj->getObjectRect();
            if (r.size.width <= 0.1f || r.size.height <= 0.1f) continue;

            // Tighten hazard hitboxes slightly to match GD's forgiving spike inner hitbox
            float shrinkX = (kind == ObjKind::Hazard) ? (r.size.width * 0.22f) : 0.0f;
            float shrinkY = (kind == ObjKind::Hazard) ? (r.size.height * 0.22f) : 0.0f;

            SpatialObj s;
            s.minX = r.getMinX() + shrinkX;
            s.maxX = r.getMaxX() - shrinkX;
            s.minY = r.getMinY() + shrinkY;
            s.maxY = r.getMaxY() - shrinkY;
            s.kind = kind;
            f.spatialCache.push_back(s);
        }

        std::sort(f.spatialCache.begin(), f.spatialCache.end(), [](SpatialObj const& a, SpatialObj const& b) {
            return a.minX < b.minX;
        });
    }

    // Gather local slice of objects around [xMin, xMax] for ultra-fast probing
    void gatherLocalSlice(float xMin, float xMax, std::vector<SpatialObj>& out) {
        auto const& cache = m_fields->spatialCache;
        out.clear();
        if (cache.empty()) return;

        auto it = std::lower_bound(
            cache.begin(), cache.end(), xMin - 60.0f,
            [](SpatialObj const& a, float val) { return a.minX < val; }
        );
        for (; it != cache.end() && it->minX <= xMax; ++it) {
            if (it->maxX >= xMin) {
                out.push_back(*it);
            }
        }
    }

    static bool hitsHazardAt(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool waveMode) {
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

    static bool hitsSolidSideAt(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh) {
        // Inner core collision against solid blocks (side wall crash)
        float px0 = cx - hw * 0.55f;
        float px1 = cx + hw * 0.55f;
        float py0 = cy - hh * 0.35f;
        float py1 = cy + hh * 0.35f;
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
        if (!upsideDown && cy - hh <= 6.0f) return true; // GD floor y=0
        float px0 = cx - hw;
        float px1 = cx + hw;
        float probeY0 = upsideDown ? (cy + hh - 2.0f) : (cy - hh - 6.0f);
        float probeY1 = upsideDown ? (cy + hh + 6.0f) : (cy - hh + 2.0f);
        for (auto const& o : local) {
            if (o.kind == ObjKind::Solid) {
                if (px1 > o.minX && px0 < o.maxX && probeY1 >= o.minY && probeY0 <= o.maxY) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool overlapsOrbAt(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh) {
        float px0 = cx - hw - 4.0f;
        float px1 = cx + hw + 4.0f;
        float py0 = cy - hh - 4.0f;
        float py1 = cy + hh + 4.0f;
        for (auto const& o : local) {
            if (o.kind == ObjKind::Orb) {
                if (px1 > o.minX && px0 < o.maxX && py1 > o.minY && py0 < o.maxY) {
                    return true;
                }
            }
        }
        return false;
    }

    // Simulate a candidate click shifted by deltaT seconds (negative = earlier, positive = later)
    static bool simulateCandidateClick(SimParams const& p, std::vector<SpatialObj> const& local, float deltaT, bool clickWasOrb) {
        constexpr float dt = 1.0f / 240.0f;
        float gravSign = p.upsideDown ? -1.0f : 1.0f;
        float sizeScale = p.mini ? 0.62f : 1.0f;

        // 1) Compute shifted click position (xClick, yClick)
        float xClick = p.x0 + p.vx * deltaT;
        float yClick = p.y0;

        if (p.isWave) {
            float slope = p.mini ? 2.0f : 1.0f;
            float preVy = -gravSign * p.vx * slope;
            yClick = p.y0 + preVy * deltaT;
            // Check path from x0 to xClick if delayed
            if (deltaT > 0.0f) {
                int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
                for (int s = 1; s <= preSteps; ++s) {
                    float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                    if (hitsHazardAt(local, p.x0 + p.vx * ts, p.y0 + preVy * ts, p.halfW * 0.45f, p.halfH * 0.45f, true)) {
                        return false;
                    }
                }
            }
        } else {
            // For ground modes (Cube/Robot/Ball/Spider), if clicking on ground and NOT an orb:
            if (p.onGround && !clickWasOrb && !p.isShip && !p.isUfo) {
                if (!hasGroundSupport(local, xClick, yClick, p.halfW, p.halfH, p.upsideDown)) {
                    return false; // Fell off the ledge before clicking!
                }
                if (deltaT > 0.0f) {
                    // Check we didn't run into a spike before jumping
                    int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
                    for (int s = 1; s <= preSteps; ++s) {
                        float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                        float tx = p.x0 + p.vx * ts;
                        if (hitsHazardAt(local, tx, yClick, p.halfW * 0.78f, p.halfH * 0.78f, false) ||
                            hitsSolidSideAt(local, tx, yClick, p.halfW, p.halfH)) {
                            return false;
                        }
                    }
                }
            } else {
                // Airborne click (Ship, UFO, or Orb)
                float preG = (p.isShip ? -950.0f : -2750.0f) * gravSign;
                yClick = p.y0 + p.vy0 * deltaT + 0.5f * preG * deltaT * deltaT;
            }
        }

        if (clickWasOrb) {
            if (!overlapsOrbAt(local, xClick, yClick, p.halfW, p.halfH)) {
                return false; // Missed the orb hitbox!
            }
        }

        // 2) Simulate post-click trajectory for ~0.46s forward
        float x = xClick;
        float y = yClick;
        float vy = 0.0f;
        float g = -2780.0f * gravSign;

        if (p.isWave) {
            float slope = p.mini ? 2.0f : 1.0f;
            vy = gravSign * p.vx * slope;
            g = 0.0f;
        } else if (p.isShip) {
            vy = p.vy0 + 180.0f * gravSign;
            g = 1100.0f * gravSign;
        } else if (p.isUfo) {
            vy = 475.0f * gravSign * (p.mini ? 0.85f : 1.0f);
            g = -2550.0f * gravSign;
        } else if (p.isBall) {
            vy = -220.0f * gravSign;
            g = 2200.0f * gravSign;
        } else if (p.isSpider) {
            // Instant gravity teleport check
            vy = 0.0f;
            g = 0.0f;
        } else {
            // Cube / Robot jump arc (standard GD 1x cube jump ~11.18 * 54 = ~603 u/s, g ~ -2790 u/s^2)
            vy = (p.mini ? 485.0f : 604.0f) * gravSign;
            g = (p.mini ? -3050.0f : -2790.0f) * gravSign;
        }

        constexpr int kSteps = 105; // ~0.4375 sec at 240Hz
        float checkHW = p.isWave ? (p.halfW * 0.42f) : (p.halfW * 0.78f);
        float checkHH = p.isWave ? (p.halfH * 0.42f) : (p.halfH * 0.78f);
        (void)sizeScale;

        for (int step = 1; step <= kSteps; ++step) {
            x += p.vx * dt;
            vy += g * dt;
            y += vy * dt;

            if (y < p.halfH && !p.upsideDown) {
                y = p.halfH;
                vy = 0.0f;
                if (!p.isWave && step > 15) break; // Landed safely on floor
            }

            if (hitsHazardAt(local, x, y, checkHW, checkHH, p.isWave)) {
                return false;
            }
            if (!p.isWave && hitsSolidSideAt(local, x, y, p.halfW, p.halfH)) {
                return false;
            }

            // If cube/robot lands on a solid platform after apex, stop checking further
            if (!p.isWave && !p.isShip && !p.isUfo && step > 18 && (vy * gravSign < 0.0f)) {
                if (hasGroundSupport(local, x, y, p.halfW, p.halfH, p.upsideDown)) {
                    break;
                }
            }
        }
        return true;
    }

    // Physics-probed Auto Mode frame window estimator
    double estimateAutoWindowFrames(PlayerObject* player, double refFps) {
        auto& f = *m_fields.self();
        double liveTps = std::clamp(f.smoothTps, 60.0, 2400.0);
        double tickSec = 1.0 / std::max(240.0, liveTps);

        if (!player || f.spatialCache.empty()) {
            return std::max(1.0, (1.0 / 60.0) * refFps);
        }

        SimParams p;
        CCPoint pos = player->getPosition();
        CCRect rect = player->getObjectRect();
        p.x0 = pos.x;
        p.y0 = pos.y;
        p.halfW = std::max(6.0f, rect.size.width * 0.5f);
        p.halfH = std::max(6.0f, rect.size.height * 0.5f);
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

        std::vector<SpatialObj> local;
        gatherLocalSlice(p.x0 - 90.0f, p.x0 + p.vx * 0.50f + 90.0f, local);

        if (local.empty()) {
            return std::clamp(0.025 * refFps, 1.0, refFps * 0.5);
        }

        bool clickWasOrb = overlapsOrbAt(local, p.x0, p.y0, p.halfW, p.halfH);

        // Probe late bound (+deltaT) and early bound (-deltaT) in fine steps (0.5 tick = ~2.08ms)
        float stepSec = static_cast<float>(tickSec * 0.5);
        constexpr int kMaxHalfSteps = 22; // up to ~45ms each direction

        int safeLate = 0;
        for (int k = 1; k <= kMaxHalfSteps; ++k) {
            if (simulateCandidateClick(p, local, stepSec * static_cast<float>(k), clickWasOrb)) {
                safeLate = k;
            } else {
                break;
            }
        }

        int safeEarly = 0;
        for (int k = 1; k <= kMaxHalfSteps; ++k) {
            if (simulateCandidateClick(p, local, -stepSec * static_cast<float>(k), clickWasOrb)) {
                safeEarly = k;
            } else {
                break;
            }
        }

        // Binary-search refine the exact boundary for sub-frame precision!
        float lateEdge = stepSec * static_cast<float>(safeLate);
        if (safeLate < kMaxHalfSteps) {
            float lo = lateEdge;
            float hi = lateEdge + stepSec;
            for (int iter = 0; iter < 4; ++iter) {
                float mid = 0.5f * (lo + hi);
                if (simulateCandidateClick(p, local, mid, clickWasOrb)) lo = mid;
                else hi = mid;
            }
            lateEdge = lo;
        }

        float earlyEdge = stepSec * static_cast<float>(safeEarly);
        if (safeEarly < kMaxHalfSteps) {
            float lo = earlyEdge;
            float hi = earlyEdge + stepSec;
            for (int iter = 0; iter < 4; ++iter) {
                float mid = 0.5f * (lo + hi);
                if (simulateCandidateClick(p, local, -mid, clickWasOrb)) lo = mid;
                else hi = mid;
            }
            earlyEdge = lo;
        }

        // Total surviving timing window in seconds
        double windowSec = static_cast<double>(earlyEdge + lateEdge);

        // If the click itself is strictly tighter than half a probe step, clamp around a tight 1-frame / sub-frame window
        if (windowSec < tickSec * 0.25) {
            windowSec = tickSec * 0.85;
        } else if (safeEarly == kMaxHalfSteps && safeLate == kMaxHalfSteps) {
            // Totally unobstructed open jump
            windowSec = 0.028;
        }

        double frames = windowSec * std::max(1.0, refFps);
        return std::clamp(frames, 0.10, refFps * 0.5);
    }

    void triggerFeedback(PlayerObject* player, double frames, fwc::ClickKind kind) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled) return;

        if (f.overlay && player) {
            f.overlay->onClickTriggered(player->getPosition(), frames, kind);
        }

        if (f.cfg.soundEnabled) {
            auto tier = fwc::evaluateTier(frames, static_cast<double>(f.cfg.refFps));
            float pitch = f.cfg.soundPitch + (f.cfg.dynamicPitch ? tier.pitchOffset : 0.0f);
            fwc::AudioManager::get().playBell(f.cfg.soundPreset, f.cfg.customSoundFile, f.cfg.soundVolume, pitch);
        }
    }

    void onInputEvent(bool down, int button, bool isPlayer1) {
        if (button != 1) return;

        auto& f = *m_fields.self();
        if (!f.cfg.enabled) return;
        if (m_player1 && m_player1->m_isDead) return;

        int pIdx = isPlayer1 ? 0 : 1;
        PlayerObject* player = isPlayer1 ? m_player1 : m_player2;
        if (!player) player = m_player1;

        double now = f.simTime;
        double refFps = static_cast<double>(f.cfg.refFps);

        if (down) {
            double gapSec = now - f.lastClickEventTime[pIdx];
            f.lastPressTime[pIdx] = now;
            f.lastClickEventTime[pIdx] = now;
            f.isHolding[pIdx] = true;

            bool isMulti = f.cfg.detectMulticlicks && (gapSec > 0.0 && (gapSec * 1000.0) <= 130.0);

            if (f.cfg.mode == fwc::CounterMode::Auto || f.cfg.captureToManual) {
                double estFrames = estimateAutoWindowFrames(player, refFps);
                fwc::ClickKind kind = fwc::ClickKind::Normal;
                if (estFrames < 1.0) kind = fwc::ClickKind::SFrame;
                else if (isMulti) kind = fwc::ClickKind::Multiclick;

                if (f.cfg.captureToManual && player) {
                    fwc::MarkerStore::get().addMarker(player->getPositionX(), now, estFrames, kind);
                }

                if (f.cfg.mode == fwc::CounterMode::Auto) {
                    triggerFeedback(player, estFrames, kind);
                }
            }
        } else {
            if (f.isHolding[pIdx]) {
                f.isHolding[pIdx] = false;
                if (f.cfg.detectSwifts) {
                    double holdSec = now - f.lastPressTime[pIdx];
                    if (holdSec > 0.0 && (holdSec * 1000.0) <= 33.33) {
                        double swiftFrames = std::max(0.10, holdSec * refFps);
                        fwc::ClickKind kind = (swiftFrames < 1.0) ? fwc::ClickKind::SFrame : fwc::ClickKind::Swift;

                        if (f.cfg.captureToManual && player) {
                            fwc::MarkerStore::get().addMarker(player->getPositionX(), now, swiftFrames, kind);
                        }
                        if (f.cfg.mode == fwc::CounterMode::Auto) {
                            triggerFeedback(player, swiftFrames, kind);
                        }
                    }
                }
            }
        }
    }

    void reloadFromSettings() {
        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        if (f.overlay) {
            f.overlay->reloadConfig();
        }
    }

    float getCurrentPlayerXSafe() {
        return m_player1 ? m_player1->getPositionX() : 0.0f;
    }

    double getCurrentSimTimeSafe() {
        return m_fields->simTime;
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        f.simTime = 0.0;
        f.prevPlayerX = m_player1 ? m_player1->getPositionX() : 0.0f;

        // Ensure custom sounds directory exists on startup
        (void)fwc::AudioManager::get().getCustomSoundsDir();

        fwc::MarkerStore::get().loadForLevel(level);
        indexLevelObjects();

        std::string levelName = level ? std::string(level->m_levelName) : "Unknown Level";
        // Attach circles directly inside m_objectLayer so they ALWAYS show up right on the player!
        f.overlay = fwc::HudOverlay::create(levelName, m_objectLayer);
        if (f.overlay) {
            this->addChild(f.overlay, 100);
        }

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        f.simTime = 0.0;
        f.prevPlayerX = m_player1 ? m_player1->getPositionX() : 0.0f;
        f.lastPressTime[0] = f.lastPressTime[1] = -10.0;
        f.lastClickEventTime[0] = f.lastClickEventTime[1] = -10.0;
        f.isHolding[0] = f.isHolding[1] = false;

        fwc::MarkerStore::get().resetTriggers();

        if (f.overlay) {
            f.overlay->resetAttempt();
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& f = *m_fields.self();
        if (!f.cfg.enabled || dt <= 0.0f) return;

        if (f.cfg.autoTpsAdapt) {
            double instTps = 1.0 / static_cast<double>(dt);
            if (instTps >= 20.0 && instTps <= 100000.0) {
                f.smoothTps = f.smoothTps * 0.90 + instTps * 0.10;
            }
            if (f.overlay) {
                f.overlay->setLiveTps(f.smoothTps);
            }
        }

        if (m_player1 && !m_player1->m_isDead) {
            f.simTime += static_cast<double>(dt);

            float curX = m_player1->getPositionX();
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
                        triggerFeedback(m_player1, mk.frames, mk.kind);
                    }
                }
            }
        }

        if (f.overlay) {
            f.overlay->stepSimulation(dt);
        }
    }
};

class $modify(FWCGJBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (auto* pl = typeinfo_cast<PlayLayer*>(this)) {
            static_cast<FWCPlayLayer*>(pl)->onInputEvent(down, button, isPlayer1);
        }
    }
};

class $modify(FWCPauseLayer, PauseLayer) {
    void onOpenFrameCounter(CCObject*) {
        auto* pl = PlayLayer::get();
        float px = 0.0f;
        double simT = 0.0;
        if (pl) {
            auto* fwcPl = static_cast<FWCPlayLayer*>(pl);
            px = fwcPl->getCurrentPlayerXSafe();
            simT = fwcPl->getCurrentSimTimeSafe();
        }

        auto* popup = fwc::SettingsPopup::create(
            []() {
                if (auto* activePl = PlayLayer::get()) {
                    static_cast<FWCPlayLayer*>(activePl)->reloadFromSettings();
                }
            },
            px,
            simT
        );
        if (popup) {
            popup->show();
        }
    }

    CCNode* makeSquarePauseIcon() {
        // Proper native GD square button (34x34) with crisp bundled icon
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

        // Queue one frame so geode.node-ids and all other mods finish setting up left-button-menu / right-button-menu
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

            // Attach cleanly into Geode's standard mod side menu (auto-stacks next to other mods!)
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

            // Fallback if Node IDs isn't installed: neat square button on the left edge
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto* menu = CCMenu::create();
            menu->setPosition({ 0.0f, 0.0f });
            btn->setPosition({ 34.0f, winSize.height - 96.0f });
            menu->addChild(btn);
            self->addChild(menu, 50);
        });
    }
};
