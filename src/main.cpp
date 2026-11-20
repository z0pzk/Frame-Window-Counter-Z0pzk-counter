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

struct SpatialObj {
    float x = 0.0f;
    float y = 0.0f;
    float halfW = 10.0f;
    float halfH = 10.0f;
    bool isOrb = false;
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

        // Click state tracking (Player 1 & Player 2)
        double lastPressTime[2] = { -10.0, -10.0 };
        double lastClickEventTime[2] = { -10.0, -10.0 };
        bool isHolding[2] = { false, false };
        int rapidClickChain[2] = { 0, 0 };
    };

    void indexLevelObjects() {
        auto& f = *m_fields.self();
        f.spatialCache.clear();
        if (!m_objects) return;

        for (auto* obj : CCArrayExt<GameObject*>(m_objects)) {
            if (!obj) continue;
            auto t = obj->m_objectType;
            bool isHazard = (t == GameObjectType::Hazard || t == GameObjectType::AnimatedHazard);
            bool isOrb = (
                t == GameObjectType::YellowJumpRing ||
                t == GameObjectType::PinkJumpRing ||
                t == GameObjectType::RedJumpRing ||
                t == GameObjectType::GravityRing ||
                t == GameObjectType::GreenRing ||
                t == GameObjectType::DashRing ||
                t == GameObjectType::GravityDashRing ||
                t == GameObjectType::DropRing
            );
            if (!isHazard && !isOrb) continue;

            CCRect rect = obj->getObjectRect();
            SpatialObj s;
            s.x = rect.getMidX();
            s.y = rect.getMidY();
            s.halfW = std::max(4.0f, rect.size.width * 0.5f);
            s.halfH = std::max(4.0f, rect.size.height * 0.5f);
            s.isOrb = isOrb;
            f.spatialCache.push_back(s);
        }

        std::sort(f.spatialCache.begin(), f.spatialCache.end(), [](SpatialObj const& a, SpatialObj const& b) {
            return a.x < b.x;
        });
    }

    CCPoint getPlayerScreenPos(PlayerObject* player) {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        if (!player) return ccp(winSize.width * 0.5f, winSize.height * 0.5f);
        if (m_objectLayer) {
            CCPoint world = m_objectLayer->convertToWorldSpace(player->getPosition());
            return world;
        }
        return ccp(winSize.width * 0.5f, winSize.height * 0.5f);
    }

    // Fast, zero-lag physics-proximity + timing frame window estimator for Auto Mode
    double estimateAutoWindowFrames(PlayerObject* player, double refFps, double holdOrGapSec, bool isSwift, bool isMulti) {
        auto& f = *m_fields.self();
        double speedX = std::max(120.0, static_cast<double>(f.measuredSpeedX));
        double estSec = 0.022; // baseline open click (~22ms)

        if (player && !f.spatialCache.empty()) {
            CCPoint pPos = player->getPosition();
            CCRect pRect = player->getObjectRect();
            float pHalfW = std::max(8.0f, pRect.size.width * 0.5f);
            float pHalfH = std::max(8.0f, pRect.size.height * 0.5f);

            // Binary search objects within +-125 GD units of player X
            auto it = std::lower_bound(
                f.spatialCache.begin(),
                f.spatialCache.end(),
                pPos.x - 125.0f,
                [](SpatialObj const& a, float val) { return a.x < val; }
            );

            float minClearanceUnits = 160.0f;
            bool nearOrb = false;

            for (; it != f.spatialCache.end() && it->x <= pPos.x + 125.0f; ++it) {
                float dy = std::fabs(it->y - pPos.y);
                if (dy > pHalfH + it->halfH + 46.0f) continue;

                float dx = std::fabs(it->x - pPos.x);
                if (it->isOrb && dx <= (pHalfW + it->halfW + 18.0f)) {
                    nearOrb = true;
                    float orbWindowUnits = std::max(1.5f, (pHalfW + it->halfW) * 0.45f - std::fabs(dx - 6.0f) * 0.25f);
                    minClearanceUnits = std::min(minClearanceUnits, orbWindowUnits);
                } else if (!it->isOrb) {
                    float edgeGapX = std::max(0.4f, dx - (pHalfW + it->halfW));
                    float edgeGapY = std::max(0.0f, dy - (pHalfH + it->halfH));
                    float effDist = std::sqrt(edgeGapX * edgeGapX + edgeGapY * edgeGapY * 0.65f);
                    if (effDist < minClearanceUnits) {
                        minClearanceUnits = effDist;
                    }
                }
            }

            if (minClearanceUnits < 150.0f) {
                // Convert spatial clearance into timing window in seconds
                double spatialSec = (static_cast<double>(minClearanceUnits) * (nearOrb ? 0.55 : 0.34)) / speedX;
                estSec = std::min(estSec, std::max(0.00015, spatialSec));
            }
        }

        // If this click was a Swift release or Multiclick spam, the real hold/gap constrains the window
        if ((isSwift || isMulti) && holdOrGapSec > 0.00005) {
            estSec = std::min(estSec, holdOrGapSec * 0.85);
        }

        estSec *= static_cast<double>(std::clamp(f.cfg.autoSensitivity, 0.25f, 3.0f));
        double frames = estSec * std::max(1.0, refFps);
        return std::clamp(frames, 0.10, refFps * 0.5);
    }

    void triggerFeedback(PlayerObject* player, double frames, fwc::ClickKind kind) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled) return;

        if (f.overlay) {
            f.overlay->onClickTriggered(getPlayerScreenPos(player), frames, kind);
        }

        if (f.cfg.soundEnabled) {
            auto tier = fwc::evaluateTier(frames, static_cast<double>(f.cfg.refFps));
            float pitch = f.cfg.soundPitch + (f.cfg.dynamicPitch ? tier.pitchOffset : 0.0f);
            fwc::AudioManager::get().playBell(f.cfg.soundPreset, f.cfg.soundVolume, pitch);
        }
    }

    void onInputEvent(bool down, int button, bool isPlayer1) {
        // Track primary jump button (button == 1)
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

            bool isMulti = (gapSec > 0.0 && (gapSec * 1000.0) <= static_cast<double>(f.cfg.multiclickWindowMs));
            if (isMulti) {
                f.rapidClickChain[pIdx]++;
            } else {
                f.rapidClickChain[pIdx] = 1;
            }

            if (f.cfg.mode == fwc::CounterMode::Auto || f.cfg.captureToManual) {
                double estFrames = estimateAutoWindowFrames(player, refFps, gapSec, false, isMulti);
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
            // Button release: detect Swift clicks (rapid press + release)
            if (f.isHolding[pIdx]) {
                f.isHolding[pIdx] = false;
                double holdSec = now - f.lastPressTime[pIdx];
                if (holdSec > 0.0 && (holdSec * 1000.0) <= static_cast<double>(f.cfg.swiftThresholdMs)) {
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

        fwc::MarkerStore::get().loadForLevel(level);
        indexLevelObjects();

        std::string levelName = level ? std::string(level->m_levelName) : "Unknown Level";
        f.overlay = fwc::HudOverlay::create(levelName);
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
        f.rapidClickChain[0] = f.rapidClickChain[1] = 0;

        fwc::MarkerStore::get().resetTriggers();

        if (f.overlay) {
            f.overlay->resetAttempt();
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& f = *m_fields.self();
        if (!f.cfg.enabled || dt <= 0.0f) return;

        // Track live physics TPS smoothly from game dt
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

            // Manual Mode: trigger markers cleanly when player reaches marker X (100% synced in renders & bots!)
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
        // Always call original FIRST so bots, CBF, and vanilla inputs behave identically
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

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // Sleek dark obsidian [FC] pill button in top-right of Pause Menu (zero NodeIDs dependency needed!)
        auto* btnRoot = CCNode::create();
        btnRoot->setContentSize({ 62.0f, 24.0f });

        auto* bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize({ 62.0f, 24.0f });
        bg->setColor(ccc3(16, 22, 33));
        bg->setOpacity(242);
        bg->setPosition({ 31.0f, 12.0f });
        btnRoot->addChild(bg, 0);

        auto* border = CCDrawNode::create();
        CCPoint pts[4] = { ccp(0, 0), ccp(62, 0), ccp(62, 24), ccp(0, 24) };
        border->drawPolygon(pts, 4, ccc4f(0, 0, 0, 0), 0.9f, ccc4f(0.22f, 0.74f, 0.97f, 0.95f));
        btnRoot->addChild(border, 1);

        auto* lbl = CCLabelBMFont::create("FWC", "bigFont.fnt");
        lbl->setScale(0.36f);
        lbl->setColor(ccc3(238, 246, 255));
        lbl->setPosition({ 31.0f, 12.5f });
        btnRoot->addChild(lbl, 2);

        auto* btn = CCMenuItemSpriteExtra::create(btnRoot, this, menu_selector(FWCPauseLayer::onOpenFrameCounter));
        btn->setPosition({ winSize.width - 42.0f, winSize.height - 22.0f });

        auto* menu = CCMenu::create();
        menu->setPosition({ 0.0f, 0.0f });
        menu->addChild(btn);
        this->addChild(menu, 50);
    }
};
