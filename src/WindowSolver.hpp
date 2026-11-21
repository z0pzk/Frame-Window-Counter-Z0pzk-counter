#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fwc::sim {

enum class ObjKind : uint8_t {
    Solid = 0,
    Hazard = 1,
    Slope = 2,
    YellowOrb = 3,
    PinkOrb = 4,
    RedOrb = 5,
    BlueOrb = 6,
    GreenOrb = 7,
    BlackOrb = 8,
    DashOrb = 9
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
};

inline bool isOrbKind(ObjKind k) {
    return static_cast<uint8_t>(k) >= static_cast<uint8_t>(ObjKind::YellowOrb);
}

inline bool rectOverlaps(float ax0, float ax1, float ay0, float ay1, SpatialObj const& o) {
    return ax1 > o.minX && ax0 < o.maxX && ay1 > o.minY && ay0 < o.maxY;
}

inline bool hitsHazard(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool waveMode) {
    float px0 = cx - hw, px1 = cx + hw, py0 = cy - hh, py1 = cy + hh;
    for (auto const& o : local) {
        // NOTE: Slopes are NEVER treated as full-box wave walls (prevents false AABB crashes!)
        if (o.kind == ObjKind::Hazard || (waveMode && o.kind == ObjKind::Solid)) {
            if (rectOverlaps(px0, px1, py0, py1, o)) return true;
        }
    }
    return false;
}

inline bool hitsSolidCore(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh) {
    float px0 = cx - hw * 0.30f, px1 = cx + hw * 0.30f;
    float py0 = cy - hh * 0.30f, py1 = cy + hh * 0.30f;
    for (auto const& o : local) {
        if (o.kind == ObjKind::Solid) {
            if (rectOverlaps(px0, px1, py0, py1, o)) return true;
        }
    }
    return false;
}

inline bool hasGroundSupport(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown) {
    if (!upsideDown && cy - hh <= 6.0f) return true;
    float px0 = cx - hw * 0.95f, px1 = cx + hw * 0.95f;
    float py0 = upsideDown ? (cy + hh - 4.0f) : (cy - hh - 8.0f);
    float py1 = upsideDown ? (cy + hh + 8.0f) : (cy - hh + 4.0f);
    for (auto const& o : local) {
        if (o.kind == ObjKind::Solid || o.kind == ObjKind::Slope) {
            if (rectOverlaps(px0, px1, py0, py1, o)) return true;
        }
    }
    return false;
}

inline bool nearAnyGround(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown) {
    if (!upsideDown && cy - hh <= 28.0f) return true;
    float px0 = cx - hw - 10.0f, px1 = cx + hw + 10.0f;
    float py0 = upsideDown ? (cy + hh - 6.0f) : (cy - hh - 28.0f);
    float py1 = upsideDown ? (cy + hh + 28.0f) : (cy - hh + 6.0f);
    for (auto const& o : local) {
        if (o.kind == ObjKind::Solid || o.kind == ObjKind::Slope) {
            if (rectOverlaps(px0, px1, py0, py1, o)) return true;
        }
    }
    return false;
}

inline ObjKind findOverlappingOrb(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, float margin, bool& found) {
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

// Raycast helper for Wave: returns time in seconds along (vx, vy) from (xStart, yStart) until hitting a wall/spike/floor
inline float waveRayClearanceSec(
    std::vector<SpatialObj> const& local,
    float xStart, float yStart,
    float vx, float vy,
    float hw, float hh,
    float maxTimeSec
) {
    constexpr float stepT = 1.0f / 480.0f; // ~2.08ms step (~0.65-1.0 GD unit)
    int maxSteps = std::max(1, static_cast<int>(std::ceil(maxTimeSec / stepT)));
    for (int s = 1; s <= maxSteps; ++s) {
        float t = static_cast<float>(s) * stepT;
        float x = xStart + vx * t;
        float y = yStart + vy * t;
        if (y < hh || hitsHazard(local, x, y, hw, hh, true)) {
            // Refine hit edge with 4 binary steps (~0.13ms precision!)
            float lo = static_cast<float>(s - 1) * stepT;
            float hi = t;
            for (int b = 0; b < 4; ++b) {
                float mid = 0.5f * (lo + hi);
                float mx = xStart + vx * mid;
                float my = yStart + vy * mid;
                if (my < hh || hitsHazard(local, mx, my, hw, hh, true)) hi = mid;
                else lo = mid;
            }
            return lo;
        }
    }
    return maxTimeSec;
}

// =========================================================================
// WAVE WINDOW SOLVER (Geometric Corridor Raycast — ZERO fake 1F clicks!)
// =========================================================================
inline double solveWaveWindowSec(SimParams const& p, std::vector<SpatialObj> const& local, bool ignoreSafe) {
    float gravSign = p.upsideDown ? -1.0f : 1.0f;
    float slope = p.mini ? 2.0f : 1.0f;
    // GD Wave hitbox is famously tiny (~3.2 units half-size for normal wave, ~2.0 for mini)
    float hw = p.mini ? 2.2f : 3.4f;
    float hh = p.mini ? 2.2f : 3.4f;

    float preVy  = (p.isPress ? -1.0f :  1.0f) * gravSign * p.vx * slope;
    float postVy = (p.isPress ?  1.0f : -1.0f) * gravSign * p.vx * slope;

    // If player is currently overlapping a solid (e.g. D-block slide or Noclip inside wall), ignore!
    if (hitsHazard(local, p.x0, p.y0, hw, hh, true)) {
        return -1.0;
    }

    // 1) Danger test: how long could we continue WITHOUT clicking?
    constexpr float kMaxRay = 0.14f; // 140ms lookahead
    float tLate = waveRayClearanceSec(local, p.x0, p.y0, p.vx, preVy, hw, hh, kMaxRay);

    // 2) How much EARLIER could we have clicked along the pre-click ray without colliding behind?
    float tBack = waveRayClearanceSec(local, p.x0, p.y0, -p.vx, -preVy, hw, hh, kMaxRay);

    // 3) Actual post-click clearance from (x0, y0)
    float tPost0 = waveRayClearanceSec(local, p.x0, p.y0, p.vx, postVy, hw, hh, kMaxRay);

    // If continuing without clicking is completely safe for > 65ms AND there is wide room behind (> 50ms):
    // This click is in a wide-open tunnel / easy spam -> NO DANGER!
    if (ignoreSafe && tLate >= 0.065f && (tLate + tBack) >= 0.095f) {
        return -1.0;
    }

    // Required post-click clearance for a shifted turn point to still clear the local gap
    // (Must NOT demand beating the opposite wall in a wide corridor — capped at 32ms or 50% of actual tPost0!)
    float reqPost = std::min(tPost0 * 0.42f, 0.030f);
    if (reqPost < 0.0005f) reqPost = 0.0005f;

    auto isWaveShiftValid = [&](float dT) -> bool {
        if (dT > tLate || -dT > tBack) return false;
        float cx = p.x0 + p.vx * dT;
        float cy = p.y0 + preVy * dT;
        if (cy < hh || hitsHazard(local, cx, cy, hw, hh, true)) return false;
        float postClear = waveRayClearanceSec(local, cx, cy, p.vx, postVy, hw, hh, reqPost + 0.002f);
        return postClear >= reqPost;
    };

    // Measure valid early shift [-earlyEdge, 0] and valid late shift [0, +lateEdge]
    constexpr float kScanMax = 0.065f;
    constexpr float kStep = 1.0f / 480.0f;

    float maxL = std::min(tLate, kScanMax);
    float lateEdge = 0.0f;
    for (float t = kStep; t <= maxL + 1e-5f; t += kStep) {
        float tc = std::min(t, maxL);
        if (isWaveShiftValid(tc)) lateEdge = tc;
        else {
            float lo = lateEdge, hi = tc;
            for (int b = 0; b < 4; ++b) {
                float mid = 0.5f * (lo + hi);
                if (isWaveShiftValid(mid)) lo = mid; else hi = mid;
            }
            lateEdge = lo;
            break;
        }
    }

    float maxE = std::min(tBack, kScanMax);
    float earlyEdge = 0.0f;
    for (float t = kStep; t <= maxE + 1e-5f; t += kStep) {
        float tc = std::min(t, maxE);
        if (isWaveShiftValid(-tc)) earlyEdge = tc;
        else {
            float lo = earlyEdge, hi = tc;
            for (int b = 0; b < 4; ++b) {
                float mid = 0.5f * (lo + hi);
                if (isWaveShiftValid(-mid)) lo = mid; else hi = mid;
            }
            earlyEdge = lo;
            break;
        }
    }

    double winSec = static_cast<double>(earlyEdge + lateEdge);

    // If the safe window is >= 50ms (>= 12 frames at 240Hz), it's a wide-open easy wave -> ignore if ignoreSafe!
    if (ignoreSafe && winSec >= 0.062) {
        return -1.0;
    }

    return std::max(0.0005, winSec);
}

// =========================================================================
// NON-WAVE SOLVER (Cube / Robot / Ball / UFO / Ship / Spider / Orbs)
// Compares ABSOLUTE X REACHED (fixes the linear drift fake-1F bug!)
// =========================================================================
inline float simulateAbsoluteEndX(
    SimParams const& p,
    std::vector<SpatialObj> const& local,
    float deltaT,
    bool clickWasOrb,
    ObjKind orbKind,
    bool checkGroundSupp,
    bool doClick,
    float horizonSec
) {
    constexpr float dt = 1.0f / 240.0f;
    float gravSign = p.upsideDown ? -1.0f : 1.0f;
    float hazHW = p.halfW * 0.80f;
    float hazHH = p.halfH * 0.80f;

    float targetEndX = p.x0 + p.vx * horizonSec;

    if (!doClick) {
        // Simulate NOT clicking at all from (x0, y0)
        float x = p.x0;
        float y = p.y0;
        float vy = p.onGround ? 0.0f : p.vy0;
        float g = (p.isShip ? -1050.0f : -3216.0f) * gravSign;
        bool onG = p.onGround && !p.isShip && !p.isUfo;
        bool startedInSolid = hitsSolidCore(local, x, y, p.halfW, p.halfH);

        int steps = std::max(1, static_cast<int>(std::ceil(horizonSec / dt)));
        for (int s = 1; s <= steps; ++s) {
            x += p.vx * dt;
            if (onG) {
                if (checkGroundSupp && !hasGroundSupport(local, x, y, p.halfW, p.halfH, p.upsideDown)) {
                    onG = false; // Walked off ledge -> start falling
                }
            }
            if (!onG) {
                vy += g * dt;
                y += vy * dt;
                if (y < p.halfH && !p.upsideDown) {
                    y = p.halfH;
                    vy = 0.0f;
                    onG = !p.isShip && !p.isUfo;
                }
            }
            if (hitsHazard(local, x, y, hazHW, hazHH, false)) return x;
            if ((!startedInSolid || s > 6) && hitsSolidCore(local, x, y, p.halfW, p.halfH)) return x;
        }
        return targetEndX;
    }

    // 1) Pre-click motion from x0 to xClick = x0 + vx * deltaT
    float xClick = p.x0 + p.vx * deltaT;
    float yClick = p.y0;

    if (p.onGround && !clickWasOrb && !p.isShip && !p.isUfo) {
        if (checkGroundSupp && !hasGroundSupport(local, xClick, yClick, p.halfW, p.halfH, p.upsideDown)) {
            return xClick; // Walked off ledge before clicking!
        }
        if (deltaT > 0.0f) {
            int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
            for (int s = 1; s <= preSteps; ++s) {
                float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                float tx = p.x0 + p.vx * ts;
                if (hitsHazard(local, tx, yClick, hazHW, hazHH, false) ||
                    hitsSolidCore(local, tx, yClick, p.halfW, p.halfH)) {
                    return tx;
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
        if (!stillOnOrb) return xClick; // Missed the orb!
    }

    // 2) Post-click trajectory up to targetEndX (SAME absolute end X for every shift!)
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
    } else if (p.isShip) {
        vy = p.vy0 + 150.0f * gravSign;
        g = 1150.0f * gravSign;
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
            if (hitsHazard(local, x, ty, hazHW, hazHH, false)) return x;
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

    bool startedInSolid = hitsSolidCore(local, x, y, p.halfW, p.halfH);
    int maxSteps = std::max(1, static_cast<int>(std::ceil((targetEndX - x) / (p.vx * dt)))) + 2;

    for (int s = 1; s <= maxSteps && x < targetEndX; ++s) {
        x += p.vx * dt;
        vy += g * dt;
        y += vy * dt;

        if (y < p.halfH && !p.upsideDown) {
            y = p.halfH;
            vy = 0.0f;
            if (!p.isShip && s > 14) return targetEndX; // Landed safely on main floor
        }

        if (hitsHazard(local, x, y, hazHW, hazHH, false)) return x;
        if ((!startedInSolid || s > 6) && hitsSolidCore(local, x, y, p.halfW, p.halfH)) return x;

        if (!p.isShip && !p.isUfo && s > 16 && (vy * gravSign < 0.0f)) {
            if (hasGroundSupport(local, x, y, p.halfW, p.halfH, p.upsideDown)) {
                return targetEndX; // Landed safely on a platform!
            }
        }
    }
    return targetEndX;
}

// Unified Auto Window Solver: returns window in Ref-FPS frames, or < 0.0 if click should be IGNORED!
inline double solveAutoWindowFrames(SimParams p, std::vector<SpatialObj> const& local, double refFps, bool ignoreSafe) {
    // Releases only matter on Wave
    if (!p.isPress && !p.isWave) return -1.0;

    // 1) WAVE MODE: dedicated geometric corridor solver
    if (p.isWave) {
        if (local.empty()) return ignoreSafe ? -1.0 : std::clamp(0.048 * refFps, 1.0, refFps * 0.5);
        double sec = solveWaveWindowSec(p, local, ignoreSafe);
        if (sec < 0.0) return -1.0;
        return std::clamp(sec * std::max(30.0, refFps), 0.10, std::max(15.0, refFps * 0.065));
    }

    // 2) NON-WAVE MODES
    bool clickWasOrb = false;
    ObjKind orbKind = findOverlappingOrb(local, p.x0, p.y0, p.halfW, p.halfH, 10.0f, clickWasOrb);

    if (!p.isShip && !p.isUfo && !p.onGround && !clickWasOrb) {
        if (!nearAnyGround(local, p.x0, p.y0, p.halfW, p.halfH, p.upsideDown)) {
            return -1.0; // Pure mid-air spam click
        }
        p.onGround = true;
    }

    if (local.empty()) {
        // Zero hazards/blocks nearby -> completely safe click!
        return ignoreSafe ? -1.0 : std::clamp(0.048 * refFps, 1.0, refFps * 0.5);
    }

    bool checkGroundSupp = p.onGround && !clickWasOrb &&
        hasGroundSupport(local, p.x0, p.y0, p.halfW, p.halfH, p.upsideDown);

    // Shorter horizon for Ship/UFO so clicking in a tunnel doesn't crash into a distant ceiling
    float horizonSec = (p.isShip || p.isUfo) ? 0.18f : 0.42f;
    float targetEndX = p.x0 + p.vx * horizonSec;

    // Step A: DANGER TEST — Does NOT clicking survive cleanly over the local horizon?
    if (ignoreSafe && !clickWasOrb) {
        float noClickEndX = simulateAbsoluteEndX(p, local, 0.0f, false, ObjKind::Solid, checkGroundSupp, false, horizonSec);
        if (noClickEndX >= targetEndX - 2.0f) {
            // Even if you didn't click at all, you wouldn't hit anything -> ZERO DANGER -> IGNORE!
            return -1.0;
        }
    }

    // Step B: Scan [-28ms, +28ms] comparing ABSOLUTE X REACHED!
    constexpr int kHalfN = 28;
    constexpr int kTotal = 2 * kHalfN + 1;
    constexpr float kStep = 0.035f / static_cast<float>(kHalfN);

    float endX[kTotal];
    float bestX = p.x0;

    for (int i = 0; i < kTotal; ++i) {
        float dT = static_cast<float>(i - kHalfN) * kStep;
        endX[i] = simulateAbsoluteEndX(p, local, dT, clickWasOrb, orbKind, checkGroundSupp, true, horizonSec);
        if (endX[i] > bestX) bestX = endX[i];
    }

    // If every shift died almost immediately (e.g. Noclip inside wall), ignore if ignoreSafe
    if (bestX - p.x0 < 12.0f) {
        return ignoreSafe ? -1.0 : std::clamp(0.042 * refFps, 1.0, refFps * 0.5);
    }

    // A shift is ALIVE if it reaches within 16 GD units (~half a block) of the best absolute X!
    float aliveThreshX = bestX - 16.0f;

    int anchor = -1;
    for (int r = 0; r <= kHalfN; ++r) {
        if (endX[kHalfN + r] >= aliveThreshX) { anchor = kHalfN + r; break; }
        if (r > 0 && endX[kHalfN - r] >= aliveThreshX) { anchor = kHalfN - r; break; }
    }
    if (anchor < 0) {
        return ignoreSafe ? -1.0 : std::clamp(0.042 * refFps, 1.0, refFps * 0.5);
    }

    int leftIdx = anchor;
    while (leftIdx > 0 && endX[leftIdx - 1] >= aliveThreshX) leftIdx--;

    int rightIdx = anchor;
    while (rightIdx < kTotal - 1 && endX[rightIdx + 1] >= aliveThreshX) rightIdx++;

    // Entire [-28ms, +28ms] range is alive -> window >= 56ms (> 13F @ 240Hz) -> safe click!
    if (leftIdx == 0 && rightIdx == kTotal - 1) {
        return ignoreSafe ? -1.0 : std::clamp(0.048 * refFps, 1.0, refFps * 0.5);
    }

    // Binary-search refine both edges to ~0.07ms precision!
    float leftT = static_cast<float>(leftIdx - kHalfN) * kStep;
    if (leftIdx > 0) {
        float badT = leftT - kStep;
        float goodT = leftT;
        for (int it = 0; it < 4; ++it) {
            float mid = 0.5f * (badT + goodT);
            if (simulateAbsoluteEndX(p, local, mid, clickWasOrb, orbKind, checkGroundSupp, true, horizonSec) >= aliveThreshX)
                goodT = mid;
            else
                badT = mid;
        }
        leftT = goodT;
    }

    float rightT = static_cast<float>(rightIdx - kHalfN) * kStep;
    if (rightIdx < kTotal - 1) {
        float goodT = rightT;
        float badT = rightT + kStep;
        for (int it = 0; it < 4; ++it) {
            float mid = 0.5f * (goodT + badT);
            if (simulateAbsoluteEndX(p, local, mid, clickWasOrb, orbKind, checkGroundSupp, true, horizonSec) >= aliveThreshX)
                goodT = mid;
            else
                badT = mid;
        }
        rightT = goodT;
    }

    double winSec = std::max(0.0005, static_cast<double>(rightT - leftT));

    if (ignoreSafe && winSec >= 0.062) {
        return -1.0; // Wide open (> 12F @ 240Hz) -> not dangerous -> ignore!
    }

    double frames = winSec * std::max(30.0, refFps);
    return std::clamp(frames, 0.10, std::max(15.0, refFps * 0.065));
}

} // namespace fwc::sim
