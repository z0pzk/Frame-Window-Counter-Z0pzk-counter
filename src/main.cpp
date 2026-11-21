#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "AudioManager.hpp"
#include "Config.hpp"
#include "HudOverlay.hpp"
#include "MarkerStore.hpp"
#include "SettingsPopup.hpp"
#include "TierSoundStore.hpp"
#include "WindowSolver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace geode::prelude;

namespace {

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
        std::vector<fwc::sim::SpatialObj> spatialCache;
        bool objectsIndexed = false;

        double simTime = 0.0;
        uint64_t updateTick = 0;
        float prevPlayerX = 0.0f;
        float measuredSpeedX = 311.58f;

        bool prevHolding[2] = { false, false };
        uint64_t lastHookTick[2][2] = { { 0, 0 }, { 0, 0 } };
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

            fwc::sim::ObjKind kind = fwc::sim::ObjKind::Solid;
            bool keep = false;

            if (t == GameObjectType::Hazard || t == GameObjectType::AnimatedHazard) {
                kind = fwc::sim::ObjKind::Hazard;
                keep = true;
            } else if (t == GameObjectType::Solid) {
                kind = fwc::sim::ObjKind::Solid;
                keep = true;
            } else if (t == GameObjectType::Slope) {
                // Separate Slope from Solid so Wave never crashes into empty AABB corners of slopes!
                kind = fwc::sim::ObjKind::Slope;
                keep = true;
            } else if (t == GameObjectType::YellowJumpRing) {
                kind = fwc::sim::ObjKind::YellowOrb; keep = true;
            } else if (t == GameObjectType::PinkJumpRing) {
                kind = fwc::sim::ObjKind::PinkOrb; keep = true;
            } else if (t == GameObjectType::RedJumpRing) {
                kind = fwc::sim::ObjKind::RedOrb; keep = true;
            } else if (t == GameObjectType::GravityRing) {
                kind = fwc::sim::ObjKind::BlueOrb; keep = true;
            } else if (t == GameObjectType::GreenRing) {
                kind = fwc::sim::ObjKind::GreenOrb; keep = true;
            } else if (t == GameObjectType::DropRing) {
                kind = fwc::sim::ObjKind::BlackOrb; keep = true;
            } else if (t == GameObjectType::DashRing || t == GameObjectType::GravityDashRing) {
                kind = fwc::sim::ObjKind::DashOrb; keep = true;
            }

            if (!keep) continue;

            CCRect r = obj->getObjectRect();
            if (r.size.width <= 0.05f || r.size.height <= 0.05f) continue;

            fwc::sim::SpatialObj s;
            s.minX = r.getMinX();
            s.maxX = r.getMaxX();
            s.minY = r.getMinY();
            s.maxY = r.getMaxY();
            s.kind = kind;
            f.spatialCache.push_back(s);
        }

        std::sort(f.spatialCache.begin(), f.spatialCache.end(), [](fwc::sim::SpatialObj const& a, fwc::sim::SpatialObj const& b) {
            return a.minX < b.minX;
        });
        f.objectsIndexed = true;
    }

    void gatherLocalSlice(float xMin, float xMax, std::vector<fwc::sim::SpatialObj>& out) {
        auto const& cache = m_fields->spatialCache;
        out.clear();
        if (cache.empty()) return;

        auto it = std::lower_bound(
            cache.begin(), cache.end(), xMin - 90.0f,
            [](fwc::sim::SpatialObj const& a, float val) { return a.minX < val; }
        );
        for (; it != cache.end() && it->minX <= xMax; ++it) {
            if (it->maxX >= xMin) {
                out.push_back(*it);
            }
        }
    }

    double computeAutoWindowFrames(PlayerObject* player, bool isPress) {
        auto& f = *m_fields.self();
        ensureObjectsIndexed();
        if (!player) return -1.0;

        fwc::sim::SimParams p;
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

        std::vector<fwc::sim::SpatialObj> local;
        gatherLocalSlice(p.x0 - 140.0f, p.x0 + p.vx * 0.50f + 120.0f, local);

        return fwc::sim::solveAutoWindowFrames(
            p, local, static_cast<double>(f.cfg.refFps), f.cfg.ignoreSafeClicks
        );
    }

    void triggerFeedback(PlayerObject* player, double frames, std::string const& manualComment) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled || !player) return;

        if (f.overlay) {
            f.overlay->onClickTriggered(m_objectLayer, player->getPosition(), frames, manualComment);
        }

        if (f.cfg.soundEnabled) {
            double fps = static_cast<double>(f.cfg.refFps);
            double displayFrames = f.cfg.cbfMode ? frames : std::max(1.0, std::round(frames));
            auto tiers = fwc::buildTiers(f.cfg.tableStyle, fps);
            int ti = fwc::findTierIndex(tiers, displayFrames);
            fwc::AudioManager::get().playForTier(f.cfg, tiers[static_cast<size_t>(ti)]);
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
            double estFrames = computeAutoWindowFrames(player, down);
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
        fwc::TierSoundStore::get().ensureLoaded();
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
