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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

using namespace geode::prelude;

namespace {

struct RecordedClick {
    float x = 0.0f;
    float y = 30.0f;
    bool down = true;
    bool isP1 = true;
    bool releaseMatters = false;
};

struct PendingClick {
    CCPoint pos;
    double frames = 1.0;
    int ticksLeft = 2;
};

enum class ScanPhase : int {
    Idle = 0,
    ApproachCheckpoint = 1,
    MeasureBaseline = 2,
    TestSkip = 3,
    TestLate = 4,
    TestEarly = 5,
    Done = 6
};

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

template <typename PL>
bool safeCreateAndStoreCheckpoint(PL* pl) {
    if (!pl) return false;
    if constexpr (requires { pl->createCheckpoint(); pl->storeCheckpoint(pl->createCheckpoint()); }) {
        if (auto* cp = pl->createCheckpoint()) {
            pl->storeCheckpoint(cp);
            if constexpr (requires { pl->m_checkpointArray; }) {
                return pl->m_checkpointArray && pl->m_checkpointArray->count() > 0;
            }
            return true;
        }
    }
    if constexpr (requires { pl->markCheckpoint(); }) {
        pl->markCheckpoint();
        if constexpr (requires { pl->m_checkpointArray; }) {
            return pl->m_checkpointArray && pl->m_checkpointArray->count() > 0;
        }
        return true;
    }
    return false;
}

template <typename PL>
void safeRemoveAllCheckpoints(PL* pl) {
    if (!pl) return;
    if constexpr (requires { pl->m_checkpointArray; pl->removeLastCheckpoint(); }) {
        while (pl->m_checkpointArray && pl->m_checkpointArray->count() > 0) {
            pl->removeLastCheckpoint();
        }
    } else if constexpr (requires { pl->removeLastCheckpoint(); }) {
        for (int k = 0; k < 4; ++k) pl->removeLastCheckpoint();
    }
}

template <typename PL>
void safeResetFromStart(PL* pl) {
    if (!pl) return;
    if constexpr (requires { pl->resetLevelFromStart(); }) {
        pl->resetLevelFromStart();
    } else {
        pl->resetLevel();
    }
}

template <typename PL>
bool safeHasCompleted(PL* pl) {
    if (!pl) return false;
    if constexpr (requires { static_cast<bool>(pl->m_hasCompletedLevel); }) {
        return static_cast<bool>(pl->m_hasCompletedLevel);
    } else {
        return false;
    }
}

template <typename PL>
float safeGetLevelLength(PL* pl) {
    if (!pl) return 1000.0f;
    if constexpr (requires { static_cast<float>(pl->m_levelLength); }) {
        return std::max(100.0f, static_cast<float>(pl->m_levelLength));
    } else {
        return 1000.0f;
    }
}

inline uint8_t computeSlopeType(GameObject* obj) {
    float dx = 1.0f;
    float dy = -1.0f;
    if (obj->isFlipX()) dx = -dx;
    if (obj->isFlipY()) dy = -dy;
    float rotDeg = obj->getRotation();
    float rad = -rotDeg * 3.14159265f / 180.0f;
    float nx = dx * std::cos(rad) - dy * std::sin(rad);
    float ny = dx * std::sin(rad) + dy * std::cos(rad);
    if (nx >= 0.0f && ny <= 0.0f) return 0;
    if (nx <  0.0f && ny <= 0.0f) return 1;
    if (nx >= 0.0f && ny >  0.0f) return 2;
    return 3;
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
        bool isDeadThisAttempt = false;
        std::vector<PendingClick> pendingClicks;
        std::deque<double> recentClicks;

        std::vector<RecordedClick> currentRun;
        std::vector<RecordedClick> savedRun;

        bool isScanning = false;
        bool scanProbeDied = false;
        bool wasPracticeBeforeScan = false;
        ScanPhase scanPhase = ScanPhase::Idle;

        std::vector<RecordedClick> scanClicks;
        std::vector<bool> replayFired;
        size_t scanClickIdx = 0;
        size_t cpStartIdx = 0;
        bool hasActiveCp = false;

        int probeTick = 0;
        int baseClickTick = 0;
        int maxApproachTicks = 480;
        float baseEndX = 0.0f;
        CCPoint baseClickPos = ccp(0.0f, 30.0f);
        bool targetFiredThisProbe = false;

        int currentDelta = 0;
        int safeLate = 0;
        int safeEarly = 0;
        int totalSolved = 0;
        int diagSkip = 0;
        int diagFail = 0;
    };

    bool isScanning() { return m_fields->isScanning; }

    size_t getRecordedClickCount() {
        auto& f = *m_fields.self();
        return std::max(f.currentRun.size(), f.savedRun.size());
    }

    void syncPlannedPrecisionToOverlay() {
        auto& f = *m_fields.self();
        if (!f.overlay) return;
        double fps = static_cast<double>(f.cfg.refFps);
        double total = 0.0;
        for (auto const& mk : fwc::MarkerStore::get().markers()) {
            double df = fwc::resolveDisplayFrames(mk.frames, f.cfg.cbfMode);
            total += fwc::clickPrecisionWeight(fwc::framesToMs(df, fps));
        }
        f.overlay->setPlannedTotalPrecision(total);
    }

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
            if (kind == fwc::sim::ObjKind::Slope) {
                s.slopeType = computeSlopeType(obj);
            }
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
        p.vy0 = static_cast<float>(player->m_yVelocity) * 54.0f;
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
        gatherLocalSlice(p.x0 - 150.0f, p.x0 + p.vx * 0.54f + 130.0f, local);

        return fwc::sim::solveAutoWindowFrames(p, local, static_cast<double>(f.cfg.refFps));
    }

    void triggerFeedbackAt(CCPoint pos, double frames, std::string const& manualComment) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled || f.isScanning || f.isDeadThisAttempt) return;
        if (m_player1 && m_player1->m_isDead) return;

        f.recentClicks.push_back(f.simTime);
        while (!f.recentClicks.empty() && (f.simTime - f.recentClicks.front()) > 1.0) {
            f.recentClicks.pop_front();
        }
        double liveCps = static_cast<double>(f.recentClicks.size());

        if (f.overlay) {
            f.overlay->onClickTriggered(m_objectLayer, pos, frames, manualComment, f.simTime, liveCps);
        }

        if (f.cfg.soundEnabled) {
            double fps = static_cast<double>(f.cfg.refFps);
            double displayFrames = fwc::resolveDisplayFrames(frames, f.cfg.cbfMode);
            auto tiers = fwc::buildTiers(f.cfg.tableStyle, fps);
            int ti = fwc::findTierIndex(tiers, displayFrames);
            fwc::AudioManager::get().playForTier(f.cfg, tiers[static_cast<size_t>(ti)]);
        }
    }

    void recordInputForScan(PlayerObject* player, bool down, bool isPlayer1) {
        auto& f = *m_fields.self();
        if (f.isScanning || f.isDeadThisAttempt || !player || player->m_isDead) return;

        CCPoint pos = player->getPosition();
        for (int k = static_cast<int>(f.currentRun.size()) - 1; k >= 0; --k) {
            if (f.currentRun[static_cast<size_t>(k)].isP1 == isPlayer1) {
                if (f.currentRun[static_cast<size_t>(k)].down == down &&
                    std::fabs(f.currentRun[static_cast<size_t>(k)].x - pos.x) < 1.0f) {
                    return;
                }
                break;
            }
        }
        bool relMatters = player->m_isDart || player->m_isShip || player->m_isRobot || player->m_isSwing;
        if (f.currentRun.size() < 3000) {
            f.currentRun.push_back({ pos.x, pos.y, down, isPlayer1, relMatters });
            if (f.currentRun.size() >= f.savedRun.size()) {
                f.savedRun = f.currentRun;
            }
        }
    }

    void handleResolvedClick(PlayerObject* player, bool down, bool isPlayer1, bool fromHook) {
        auto& f = *m_fields.self();
        if (!f.cfg.enabled || f.isScanning || f.isDeadThisAttempt) return;
        if (!player) player = isPlayer1 ? m_player1 : m_player2;
        if (!player || player->m_isDead) return;

        int pIdx = isPlayer1 ? 0 : 1;
        int dIdx = down ? 1 : 0;

        if (fromHook) {
            f.lastHookTick[pIdx][dIdx] = f.updateTick;
            f.prevHolding[pIdx] = down;
        } else {
            if (f.lastHookTick[pIdx][dIdx] == f.updateTick) return;
        }

        recordInputForScan(player, down, isPlayer1);

        if (f.cfg.mode == fwc::CounterMode::Auto || f.cfg.captureToManual) {
            double estFrames = computeAutoWindowFrames(player, down);
            if (estFrames < 0.0) return;

            auto pos = player->getPosition();
            if (f.cfg.captureToManual) {
                fwc::MarkerStore::get().addMarker(pos.x, pos.y, estFrames, "");
            }

            if (f.cfg.mode == fwc::CounterMode::Auto) {
                f.pendingClicks.push_back({ pos, estFrames, 2 });
            }
        }
    }

    void resetProbeState(std::function<void(bool, int, bool)> const& runBaseBtn) {
        auto& f = *m_fields.self();
        runBaseBtn(false, 1, true);
        runBaseBtn(false, 1, false);

        f.scanProbeDied = false;
        f.probeTick = 0;
        f.targetFiredThisProbe = false;
        std::fill(f.replayFired.begin(), f.replayFired.end(), false);
        for (size_t j = 0; j < f.cpStartIdx && j < f.replayFired.size(); ++j) {
            f.replayFired[j] = true;
        }
        if (f.hasActiveCp) {
            PlayLayer::resetLevel();
        } else {
            safeResetFromStart(this);
        }
        f.scanProbeDied = false;
    }

    void startExactScan() {
        auto& f = *m_fields.self();
        auto const& source = (f.currentRun.size() >= f.savedRun.size() && !f.currentRun.empty())
            ? f.currentRun : f.savedRun;
        if (source.empty()) return;

        f.scanClicks = source;
        std::sort(f.scanClicks.begin(), f.scanClicks.end(), [](RecordedClick const& a, RecordedClick const& b) {
            return a.x < b.x;
        });

        f.replayFired.assign(f.scanClicks.size(), false);
        f.isScanning = true;
        f.scanProbeDied = false;
        f.scanClickIdx = 0;
        f.cpStartIdx = 0;
        f.hasActiveCp = false;
        f.totalSolved = 0;
        f.diagSkip = 0;
        f.diagFail = 0;
        f.pendingClicks.clear();
        f.scanPhase = ScanPhase::ApproachCheckpoint;

        f.wasPracticeBeforeScan = m_isPracticeMode;
        if (!m_isPracticeMode) {
            this->togglePracticeMode(true);
        }
        safeRemoveAllCheckpoints(this);
        fwc::MarkerStore::get().clearAll();

        if (f.overlay) {
            f.overlay->clearCircles();
            f.overlay->setScanStatus(true, fmt::format("SCANNING 1 / {}...", f.scanClicks.size()));
        }

        safeResetFromStart(this);
        f.scanProbeDied = false;
    }

    void finishOrCancelScan(bool completed) {
        auto& f = *m_fields.self();
        if (!f.isScanning) return;

        f.isScanning = false;
        f.scanPhase = ScanPhase::Idle;

        safeRemoveAllCheckpoints(this);
        if (!f.wasPracticeBeforeScan && m_isPracticeMode) {
            this->togglePracticeMode(false);
        }

        if (f.overlay) {
            f.overlay->setScanStatus(false, "");
        }

        if (completed && f.totalSolved > 0) {
            f.cfg.mode = fwc::CounterMode::Manual;
            f.cfg.save();
            syncPlannedPrecisionToOverlay();
            if (f.overlay) {
                f.overlay->reloadConfig(m_objectLayer);
            }
        }

        safeResetFromStart(this);

        if (completed && f.overlay) {
            if (f.totalSolved > 0) {
                f.overlay->showCallout(
                    fmt::format("SCAN COMPLETE\n{} EXACT CLICKS", f.totalSolved),
                    ccc3(118, 244, 132)
                );
            } else {
                f.overlay->showCallout(
                    fmt::format("0 CLICKS FOUND\nSKIP:{} FAIL:{}", f.diagSkip, f.diagFail),
                    ccc3(255, 118, 92)
                );
            }
        }
    }

    void stepScannerTick(
        std::function<void(float)> const& runBaseUpdate,
        std::function<void(bool, int, bool)> const& runBaseBtn
    ) {
        auto& f = *m_fields.self();
        if (!f.isScanning) return;

        constexpr float kFixedDt = 1.0f / 240.0f;
        constexpr int kLookaheadTicks = 150;
        constexpr int kMaxShift = 24;

        auto tStart = std::chrono::steady_clock::now();
        int stepsDone = 0;

        auto didDie = [&]() -> bool {
            return f.scanProbeDied || (m_player1 && m_player1->m_isDead);
        };

        while (f.isScanning && stepsDone < 280) {
            if ((stepsDone & 7) == 7) {
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - tStart
                ).count();
                if (elapsedMs >= 7) break;
            }
            stepsDone++;

            while (f.scanClickIdx < f.scanClicks.size() &&
                   !f.scanClicks[f.scanClickIdx].down &&
                   !f.scanClicks[f.scanClickIdx].releaseMatters) {
                f.scanClickIdx++;
            }

            if (f.scanClickIdx >= f.scanClicks.size()) {
                finishOrCancelScan(true);
                return;
            }

            size_t i = f.scanClickIdx;
            float curX = m_player1 ? m_player1->getPositionX() : 0.0f;

            auto fireReplayRange = [&](size_t fromIdx, size_t toIdx, bool skipTarget) {
                for (size_t j = fromIdx; j < toIdx && j < f.scanClicks.size(); ++j) {
                    if (skipTarget && j == i) continue;
                    if (!f.replayFired[j] && curX >= f.scanClicks[j].x) {
                        f.replayFired[j] = true;
                        runBaseBtn(f.scanClicks[j].down, 1, f.scanClicks[j].isP1);
                    }
                }
            };

            if (f.scanPhase == ScanPhase::ApproachCheckpoint) {
                float targetCpX = std::max(0.0f, f.scanClicks[i].x - 75.0f);

                if (targetCpX <= 12.0f) {
                    f.hasActiveCp = false;
                    f.cpStartIdx = 0;
                    f.maxApproachTicks = 480;
                    f.scanPhase = ScanPhase::MeasureBaseline;
                    f.baseClickTick = -1;
                    f.baseEndX = 0.0f;
                    resetProbeState(runBaseBtn);
                    continue;
                }

                if (curX >= targetCpX) {
                    safeRemoveAllCheckpoints(this);
                    bool stored = safeCreateAndStoreCheckpoint(this);
                    f.hasActiveCp = stored;
                    if (stored) {
                        f.cpStartIdx = 0;
                        while (f.cpStartIdx < i && f.replayFired[f.cpStartIdx]) {
                            f.cpStartIdx++;
                        }
                        f.maxApproachTicks = 420;
                    } else {
                        f.cpStartIdx = 0;
                        f.maxApproachTicks = std::max(600, static_cast<int>((f.scanClicks[i].x / 140.0f) * 240.0f) + 240);
                    }
                    f.scanPhase = ScanPhase::MeasureBaseline;
                    f.baseClickTick = -1;
                    f.baseEndX = 0.0f;
                    resetProbeState(runBaseBtn);
                    continue;
                }

                fireReplayRange(f.cpStartIdx, i, false);
                runBaseUpdate(kFixedDt);

                if (didDie() || safeHasCompleted(this) || f.probeTick > 36000) {
                    finishOrCancelScan(true);
                    return;
                }
                f.probeTick++;
            }
            else if (f.scanPhase == ScanPhase::MeasureBaseline) {
                if (!f.targetFiredThisProbe && curX >= f.scanClicks[i].x) {
                    f.targetFiredThisProbe = true;
                    f.baseClickTick = f.probeTick;
                    f.baseClickPos = m_player1 ? m_player1->getPosition() : ccp(f.scanClicks[i].x, f.scanClicks[i].y);
                    f.replayFired[i] = true;
                    runBaseBtn(f.scanClicks[i].down, 1, f.scanClicks[i].isP1);
                }
                fireReplayRange(f.cpStartIdx, f.scanClicks.size(), true);
                runBaseUpdate(kFixedDt);
                f.probeTick++;

                bool finishedLookahead = f.targetFiredThisProbe && ((f.probeTick - f.baseClickTick) >= kLookaheadTicks);
                bool failedBeforeTarget = !f.targetFiredThisProbe && (didDie() || f.probeTick > f.maxApproachTicks);

                if (failedBeforeTarget) {
                    f.diagFail++;
                    f.scanClickIdx++;
                    f.scanPhase = ScanPhase::ApproachCheckpoint;
                    resetProbeState(runBaseBtn);
                    continue;
                }

                if (finishedLookahead || didDie() || safeHasCompleted(this)) {
                    f.baseEndX = m_player1 ? m_player1->getPositionX() : curX;
                    if (f.baseClickTick < 0) f.baseClickTick = f.probeTick;
                    f.scanPhase = ScanPhase::TestSkip;
                    resetProbeState(runBaseBtn);
                }
            }
            else if (f.scanPhase == ScanPhase::TestSkip) {
                fireReplayRange(f.cpStartIdx, f.scanClicks.size(), true);
                runBaseUpdate(kFixedDt);
                f.probeTick++;

                bool doneTicks = (f.probeTick - f.baseClickTick) >= kLookaheadTicks;
                if (didDie() || doneTicks || safeHasCompleted(this)) {
                    float endX = m_player1 ? m_player1->getPositionX() : curX;
                    bool survivedWithoutClick = !didDie() && (endX >= f.baseEndX - 8.0f);

                    if (survivedWithoutClick) {
                        f.diagSkip++;
                        f.scanClickIdx++;
                        f.scanPhase = ScanPhase::ApproachCheckpoint;
                        if (f.overlay && f.scanClickIdx < f.scanClicks.size()) {
                            f.overlay->setScanStatus(true, fmt::format("SCANNING {} / {}...", f.scanClickIdx + 1, f.scanClicks.size()));
                        }
                        resetProbeState(runBaseBtn);
                    } else {
                        f.safeLate = 0;
                        f.safeEarly = 0;
                        f.currentDelta = 1;
                        f.scanPhase = ScanPhase::TestLate;
                        resetProbeState(runBaseBtn);
                    }
                }
            }
            else if (f.scanPhase == ScanPhase::TestLate || f.scanPhase == ScanPhase::TestEarly) {
                int targetTick = f.baseClickTick + f.currentDelta;
                if (targetTick < 0) {
                    f.scanPhase = ScanPhase::Done;
                    continue;
                }

                if (!f.targetFiredThisProbe && f.probeTick == targetTick) {
                    f.targetFiredThisProbe = true;
                    f.replayFired[i] = true;
                    runBaseBtn(f.scanClicks[i].down, 1, f.scanClicks[i].isP1);
                }
                fireReplayRange(f.cpStartIdx, f.scanClicks.size(), true);
                runBaseUpdate(kFixedDt);
                f.probeTick++;

                bool doneTicks = (f.probeTick - f.baseClickTick) >= kLookaheadTicks;
                if (didDie() || doneTicks || safeHasCompleted(this)) {
                    float endX = m_player1 ? m_player1->getPositionX() : curX;
                    bool survived = !didDie() && (endX >= f.baseEndX - 10.0f);

                    if (f.scanPhase == ScanPhase::TestLate) {
                        if (survived && f.currentDelta < kMaxShift) {
                            f.safeLate = f.currentDelta;
                            f.currentDelta++;
                            resetProbeState(runBaseBtn);
                        } else {
                            if (survived) f.safeLate = f.currentDelta;
                            f.currentDelta = -1;
                            f.scanPhase = ScanPhase::TestEarly;
                            resetProbeState(runBaseBtn);
                        }
                    } else {
                        if (survived && (-f.currentDelta) < kMaxShift) {
                            f.safeEarly = -f.currentDelta;
                            f.currentDelta--;
                            resetProbeState(runBaseBtn);
                        } else {
                            if (survived) f.safeEarly = -f.currentDelta;
                            f.scanPhase = ScanPhase::Done;
                        }
                    }
                }
            }
            else if (f.scanPhase == ScanPhase::Done) {
                double frames240 = static_cast<double>(1 + f.safeEarly + f.safeLate);
                double framesScaled = frames240 * (static_cast<double>(f.cfg.refFps) / 240.0);
                fwc::MarkerStore::get().addMarker(f.baseClickPos.x, f.baseClickPos.y, framesScaled, "");
                f.totalSolved++;

                f.scanClickIdx++;
                if (f.scanClickIdx >= f.scanClicks.size()) {
                    finishOrCancelScan(true);
                    return;
                }
                if (f.overlay) {
                    f.overlay->setScanStatus(true, fmt::format("SCANNING {} / {}...", f.scanClickIdx + 1, f.scanClicks.size()));
                }
                f.scanPhase = ScanPhase::ApproachCheckpoint;
                resetProbeState(runBaseBtn);
            }
        }
    }

    void reloadFromSettings() {
        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        syncPlannedPrecisionToOverlay();
        if (f.overlay) {
            f.overlay->reloadConfig(m_objectLayer);
        }
    }

    CCPoint getCurrentPlayerPosSafe() {
        return m_player1 ? m_player1->getPosition() : ccp(0.0f, 30.0f);
    }

    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", -100000);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& f = *m_fields.self();
        bool isAnticheat = false;
        if constexpr (requires { this->m_anticheatSpike; }) {
            isAnticheat = (object != nullptr && object == this->m_anticheatSpike);
        }

        if (f.isScanning) {
            if (!isAnticheat) {
                f.scanProbeDied = true;
            }
            return;
        }

        if (!isAnticheat) {
            f.pendingClicks.clear();
        }

        PlayLayer::destroyPlayer(player, object);

        if (!isAnticheat && m_player1 && m_player1->m_isDead) {
            f.isDeadThisAttempt = true;
        }
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto& f = *m_fields.self();
        f.cfg = fwc::ModConfig::load();
        f.simTime = 0.0;
        f.updateTick = 1;
        f.objectsIndexed = false;
        f.isScanning = false;
        f.isDeadThisAttempt = false;
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
            syncPlannedPrecisionToOverlay();
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

    void performNormalReset() {
        auto& f = *m_fields.self();
        if (f.currentRun.size() >= f.savedRun.size() && !f.currentRun.empty()) {
            f.savedRun = f.currentRun;
        }
        f.currentRun.clear();
        f.pendingClicks.clear();
        f.recentClicks.clear();

        f.cfg = fwc::ModConfig::load();
        f.simTime = 0.0;
        f.updateTick++;
        f.isDeadThisAttempt = false;
        f.prevPlayerX = m_player1 ? m_player1->getPositionX() : 0.0f;
        f.prevHolding[0] = false;
        f.prevHolding[1] = false;
        f.lastHookTick[0][0] = f.lastHookTick[0][1] = 0;
        f.lastHookTick[1][0] = f.lastHookTick[1][1] = 0;

        ensureObjectsIndexed();
        fwc::MarkerStore::get().resetTriggers();
        syncPlannedPrecisionToOverlay();

        if (f.overlay) {
            f.overlay->resetAttempt(m_objectLayer);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->isScanning) {
            m_fields->scanProbeDied = false;
            return;
        }
        performNormalReset();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& f = *m_fields.self();
        if (!f.cfg.enabled || f.isScanning || dt <= 0.0f) return;

        if (!f.objectsIndexed) {
            ensureObjectsIndexed();
        }

        float curX = 0.0f;
        if (m_player1) {
            if (m_player1->m_isDead) {
                f.isDeadThisAttempt = true;
                f.pendingClicks.clear();
            }

            curX = m_player1->getPositionX();
            float dx = curX - f.prevPlayerX;

            if (curX < 45.0f && f.prevPlayerX > 85.0f) {
                performNormalReset();
                f.prevPlayerX = curX;
                return;
            }

            if (dx < -15.0f) {
                f.isDeadThisAttempt = false;
                f.pendingClicks.clear();
                f.prevHolding[0] = isPlayerHoldingJump(m_player1);
                f.prevHolding[1] = isPlayerHoldingJump(m_player2);
                fwc::MarkerStore::get().resetTriggersAfter(curX);
                if (f.overlay) {
                    f.overlay->pruneCirclesAfter(curX);
                }
            } else if (dx > 0.01f && !f.isDeadThisAttempt) {
                f.simTime += static_cast<double>(dt);
                if (dt > 1e-6f) {
                    float instSpeed = dx / dt;
                    if (instSpeed > 80.0f && instSpeed < 1600.0f) {
                        f.measuredSpeedX = f.measuredSpeedX * 0.85f + instSpeed * 0.15f;
                    }
                }
            }
            f.prevPlayerX = curX;

            if (!f.isDeadThisAttempt && !m_player1->m_isDead) {
                for (size_t k = 0; k < f.pendingClicks.size(); ) {
                    f.pendingClicks[k].ticksLeft--;
                    if (f.pendingClicks[k].ticksLeft <= 0) {
                        auto pc = f.pendingClicks[k];
                        f.pendingClicks.erase(f.pendingClicks.begin() + static_cast<ptrdiff_t>(k));
                        triggerFeedbackAt(pc.pos, pc.frames, "");
                    } else {
                        ++k;
                    }
                }

                if (f.overlay) {
                    float len = safeGetLevelLength(this);
                    double pct = safeHasCompleted(this) ? 100.0 : (static_cast<double>(curX) * 100.0 / static_cast<double>(len));
                    f.overlay->setLevelProgress(pct);
                }

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
                            triggerFeedbackAt(m_player1->getPosition(), mk.frames, mk.comment);
                        } else if (!mk.comment.empty() && f.overlay) {
                            auto tiers = fwc::buildTiers(f.cfg.tableStyle, static_cast<double>(f.cfg.refFps));
                            int ti = fwc::findTierIndex(tiers, fwc::resolveDisplayFrames(mk.frames, f.cfg.cbfMode));
                            f.overlay->showCallout(mk.comment, tiers[static_cast<size_t>(ti)].color);
                        }
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

    void update(float dt) {
        if (auto* pl = typeinfo_cast<PlayLayer*>(this)) {
            auto* fwcPl = static_cast<FWCPlayLayer*>(pl);
            if (fwcPl->isScanning()) {
                fwcPl->stepScannerTick(
                    [this](float stepDt) { GJBaseGameLayer::update(stepDt); },
                    [this](bool d, int b, bool p1) { GJBaseGameLayer::handleButton(d, b, p1); }
                );
                return;
            }
        }
        GJBaseGameLayer::update(dt);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (auto* pl = typeinfo_cast<PlayLayer*>(this)) {
            auto* fwcPl = static_cast<FWCPlayLayer*>(pl);
            if (fwcPl->isScanning()) {
                return;
            }
            if (button == 1) {
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
        size_t recCount = 0;
        if (pl) {
            auto* fwcPl = static_cast<FWCPlayLayer*>(pl);
            pos = fwcPl->getCurrentPlayerPosSafe();
            recCount = fwcPl->getRecordedClickCount();
        }

        Ref<PauseLayer> pauseRef = this;
        auto* popup = fwc::SettingsPopup::create(
            []() {
                if (auto* activePl = PlayLayer::get()) {
                    static_cast<FWCPlayLayer*>(activePl)->reloadFromSettings();
                }
            },
            [pauseRef]() {
                if (auto* activePl = PlayLayer::get()) {
                    static_cast<FWCPlayLayer*>(activePl)->startExactScan();
                }
                if (pauseRef && pauseRef->getParent()) {
                    pauseRef->onResume(nullptr);
                }
            },
            recCount,
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

        if (auto* pl = PlayLayer::get()) {
            auto* fwcPl = static_cast<FWCPlayLayer*>(pl);
            if (fwcPl->isScanning()) {
                fwcPl->finishOrCancelScan(false);
            }
        }

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
