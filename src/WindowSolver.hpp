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
    uint8_t slopeType = 0;
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

constexpr float kCubeVyNormal = 603.722f;
constexpr float kCubeVyMini   = 482.977f;
constexpr float kCubeGrav     = 3104.565f;

inline bool isOrbKind(ObjKind k) {
    return static_cast<uint8_t>(k) >= static_cast<uint8_t>(ObjKind::YellowOrb);
}

inline bool rectOverlaps(float ax0, float ax1, float ay0, float ay1, SpatialObj const& o) {
    return ax1 > o.minX && ax0 < o.maxX && ay1 > o.minY && ay0 < o.maxY;
}

inline bool boxHitsSlopeTriangle(float cx, float cy, float hw, float hh, SpatialObj const& o) {
    if (!rectOverlaps(cx - hw, cx + hw, cy - hh, cy + hh, o)) return false;
    float w = std::max(0.1f, o.maxX - o.minX);
    float h = std::max(0.1f, o.maxY - o.minY);
    switch (o.slopeType & 3) {
        case 0: return (((cx + hw) - o.minX) / w) - (((cy - hh) - o.minY) / h) > 0.02f;
        case 1: return (((cx - hw) - o.minX) / w) + (((cy - hh) - o.minY) / h) < 0.98f;
        case 2: return (((cx + hw) - o.minX) / w) + (((cy + hh) - o.minY) / h) > 1.02f;
        default: return (((cy + hh) - o.minY) / h) - (((cx - hw) - o.minX) / w) > 0.02f;
    }
}

inline bool hitsHazard(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool waveMode) {
    float px0 = cx - hw, px1 = cx + hw, py0 = cy - hh, py1 = cy + hh;
    for (auto const& o : local) {
        if (o.kind == ObjKind::Hazard) {
            if (rectOverlaps(px0, px1, py0, py1, o)) return true;
        } else if (waveMode && o.kind == ObjKind::Solid) {
            if (rectOverlaps(px0, px1, py0, py1, o)) return true;
        } else if (waveMode && o.kind == ObjKind::Slope) {
            if (boxHitsSlopeTriangle(cx, cy, hw, hh, o)) return true;
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

inline bool findSupportY(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown, float& outCenterY) {
    if (!upsideDown && cy - hh <= 4.5f) {
        outCenterY = hh;
        return true;
    }
    float px0 = cx - hw * 0.96f, px1 = cx + hw * 0.96f;
    float py0 = upsideDown ? (cy + hh - 9.0f) : (cy - hh - 6.0f);
    float py1 = upsideDown ? (cy + hh + 6.0f) : (cy - hh + 9.0f);
    bool found = false;
    float bestY = cy;
    for (auto const& o : local) {
        if (o.kind == ObjKind::Solid || o.kind == ObjKind::Slope) {
            if (rectOverlaps(px0, px1, py0, py1, o)) {
                float cand = upsideDown ? (o.minY - hh) : (o.maxY + hh);
                if (!found || (upsideDown ? (cand < bestY) : (cand > bestY))) {
                    bestY = cand;
                    found = true;
                }
            }
        }
    }
    if (found) outCenterY = bestY;
    return found;
}

inline bool hasGroundSupport(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown) {
    float dummy = cy;
    return findSupportY(local, cx, cy, hw, hh, upsideDown, dummy);
}

inline bool nearAnyGround(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown) {
    if (!upsideDown && cy - hh <= 26.0f) return true;
    float px0 = cx - hw - 10.0f, px1 = cx + hw + 10.0f;
    float py0 = upsideDown ? (cy + hh - 6.0f) : (cy - hh - 26.0f);
    float py1 = upsideDown ? (cy + hh + 26.0f) : (cy - hh + 6.0f);
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

inline float waveRayClearanceSec(
    std::vector<SpatialObj> const& local,
    float xStart, float yStart,
    float vx, float vy,
    float hw, float hh,
    float maxTimeSec
) {
    constexpr float stepT = 1.0f / 480.0f;
    int maxSteps = std::max(1, static_cast<int>(std::ceil(maxTimeSec / stepT)));
    for (int s = 1; s <= maxSteps; ++s) {
        float t = static_cast<float>(s) * stepT;
        float x = xStart + vx * t;
        float y = yStart + vy * t;
        if (y < hh || hitsHazard(local, x, y, hw, hh, true)) {
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

inline double solveWaveWindowSec(SimParams const& p, std::vector<SpatialObj> const& local) {
    float gravSign = p.upsideDown ? -1.0f : 1.0f;
    float slope = p.mini ? 2.0f : 1.0f;
    float hw = p.mini ? 2.4f : 3.8f;
    float hh = p.mini ? 2.4f : 3.8f;

    float preVy  = (p.isPress ? -1.0f :  1.0f) * gravSign * p.vx * slope;
    float postVy = (p.isPress ?  1.0f : -1.0f) * gravSign * p.vx * slope;

    constexpr float kMaxRay = 0.14f;
    float tLate  = waveRayClearanceSec(local, p.x0, p.y0,  p.vx,  preVy, hw, hh, kMaxRay);
    float tBack  = waveRayClearanceSec(local, p.x0, p.y0, -p.vx, -preVy, hw, hh, kMaxRay);
    float tPost0 = waveRayClearanceSec(local, p.x0, p.y0,  p.vx, postVy, hw, hh, kMaxRay);

    if (tLate >= 0.095f && (tLate + tBack) >= 0.135f) {
        return -1.0;
    }

    float reqPost = std::min(tPost0 * 0.85f, 0.048f);
    if (reqPost < 0.002f) reqPost = 0.002f;

    auto isWaveShiftValid = [&](float dT) -> bool {
        if (dT > tLate || -dT > tBack) return false;
        float cx = p.x0 + p.vx * dT;
        float cy = p.y0 + preVy * dT;
        if (cy < hh || hitsHazard(local, cx, cy, hw, hh, true)) return false;
        float postClear = waveRayClearanceSec(local, cx, cy, p.vx, postVy, hw, hh, reqPost + 0.001f);
        return postClear >= reqPost;
    };

    constexpr float kScanMax = 0.085f;
    constexpr float kStep = 1.0f / 480.0f;

    float maxL = std::min(tLate, kScanMax);
    float lateEdge = 0.0f;
    for (float t = kStep; t <= maxL + 1e-5f; t += kStep) {
        float tc = std::min(t, maxL);
        if (isWaveShiftValid(tc)) lateEdge = tc;
        else {
            float lo = lateEdge, hi = tc;
            for (int b = 0; b < 5; ++b) {
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
            for (int b = 0; b < 5; ++b) {
                float mid = 0.5f * (lo + hi);
                if (isWaveShiftValid(-mid)) lo = mid; else hi = mid;
            }
            earlyEdge = lo;
            break;
        }
    }

    double winSec = static_cast<double>(earlyEdge + lateEdge);
    if (winSec >= 0.098) return -1.0;
    return std::max(0.0004, winSec);
}

struct ProbeOutcome {
    float endX = 0.0f;
    bool died = false;
};

inline ProbeOutcome simulateCandidate(
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
    float hazHW = p.halfW;
    float hazHH = p.halfH;

    float targetEndX = p.x0 + p.vx * horizonSec;

    if (!doClick) {
        float x = p.x0, y = p.y0;
        float vy = p.onGround ? 0.0f : p.vy0;
        float g = (p.isShip ? -950.0f : -kCubeGrav) * gravSign;
        bool onG = p.onGround && !p.isShip && !p.isUfo;
        bool startedInSolid = hitsSolidCore(local, x, y, p.halfW, p.halfH);

        int steps = std::max(1, static_cast<int>(std::ceil(horizonSec / dt)));
        for (int s = 1; s <= steps; ++s) {
            x += p.vx * dt;
            if (onG) {
                float supY = y;
                if (checkGroundSupp && !findSupportY(local, x, y, p.halfW, p.halfH, p.upsideDown, supY)) {
                    onG = false;
                }
            }
            if (!onG) {
                vy += g * dt;
                y += vy * dt;
                if (y < p.halfH && !p.upsideDown) {
                    y = p.halfH; vy = 0.0f;
                    onG = !p.isShip && !p.isUfo;
                }
            }
            if (hitsHazard(local, x, y, hazHW, hazHH, false)) return { x, true };
            if ((!startedInSolid || s > 6) && hitsSolidCore(local, x, y, p.halfW, p.halfH)) return { x, true };
        }
        return { targetEndX, false };
    }

    float xClick = p.x0 + p.vx * deltaT;
    float yClick = p.y0;

    if (p.onGround && !clickWasOrb && !p.isShip && !p.isUfo) {
        if (checkGroundSupp && !hasGroundSupport(local, xClick, yClick, p.halfW, p.halfH, p.upsideDown)) {
            return { xClick, true };
        }
        if (deltaT > 0.0f) {
            int preSteps = std::max(1, static_cast<int>(std::ceil(deltaT / dt)));
            for (int s = 1; s <= preSteps; ++s) {
                float ts = (deltaT * static_cast<float>(s)) / static_cast<float>(preSteps);
                float tx = p.x0 + p.vx * ts;
                if (hitsHazard(local, tx, yClick, hazHW, hazHH, false) ||
                    hitsSolidCore(local, tx, yClick, p.halfW, p.halfH)) {
                    return { tx, true };
                }
            }
        } else if (deltaT < 0.0f) {
            if (hitsHazard(local, xClick, yClick, hazHW, hazHH, false) ||
                hitsSolidCore(local, xClick, yClick, p.halfW, p.halfH)) {
                return { xClick, true };
            }
        }
    } else {
        float preG = (p.isShip ? -950.0f : -kCubeGrav) * gravSign;
        yClick = p.y0 + p.vy0 * deltaT + 0.5f * preG * deltaT * deltaT;
    }

    if (clickWasOrb) {
        bool stillOnOrb = false;
        findOverlappingOrb(local, xClick, yClick, p.halfW, p.halfH, 2.0f, stillOnOrb);
        if (!stillOnOrb) return { xClick, true };
    }

    float x = xClick, y = yClick, vy = 0.0f;
    float g = -kCubeGrav * gravSign;
    bool onG = false;

    if (clickWasOrb) {
        switch (orbKind) {
            case ObjKind::YellowOrb: vy = (p.mini ? 483.0f : 603.7f) * gravSign; break;
            case ObjKind::PinkOrb:   vy = (p.mini ? 362.9f : 453.6f) * gravSign; break;
            case ObjKind::RedOrb:    vy = (p.mini ? 639.4f : 799.2f) * gravSign; break;
            case ObjKind::BlueOrb:   gravSign = -gravSign; g = -g; vy = 216.0f * gravSign; break;
            case ObjKind::GreenOrb:  gravSign = -gravSign; g = -g; vy = 603.7f * gravSign; break;
            case ObjKind::BlackOrb:  vy = -864.0f * gravSign; break;
            case ObjKind::DashOrb:   vy = 0.0f; g = 0.0f; break;
            default:                 vy = 603.7f * gravSign; break;
        }
    } else if (p.isShip) {
        vy = p.vy0 + 135.0f * gravSign;
        g = 1050.0f * gravSign;
    } else if (p.isUfo) {
        vy = (p.mini ? 388.0f : 432.0f) * gravSign;
        g = (p.mini ? -2790.0f : -2385.0f) * gravSign;
    } else if (p.isBall) {
        gravSign = -gravSign;
        vy = 145.0f * gravSign;
        g = -2385.0f * gravSign;
    } else if (p.isSpider) {
        float dirY = gravSign;
        bool landed = false;
        for (int k = 1; k <= 160; ++k) {
            float ty = y + dirY * 3.0f * static_cast<float>(k);
            if (!p.upsideDown && ty > y + 480.0f) break;
            if (p.upsideDown && ty <= p.halfH) {
                y = p.halfH;
                landed = true;
                break;
            }
            if (hitsHazard(local, x, ty, hazHW, hazHH, false)) return { x, true };
            float supY = ty;
            if (findSupportY(local, x, ty, p.halfW, p.halfH, !p.upsideDown, supY)) {
                y = supY;
                landed = true;
                break;
            }
        }
        if (!landed) return { x, true };
        gravSign = -gravSign;
        vy = 0.0f;
        g = 0.0f;
        onG = true;
    } else {
        vy = (p.mini ? kCubeVyMini : kCubeVyNormal) * gravSign;
        g = -kCubeGrav * gravSign;
    }

    bool startedInSolid = hitsSolidCore(local, x, y, p.halfW, p.halfH);
    int maxSteps = std::max(1, static_cast<int>(std::ceil((targetEndX - x) / (p.vx * dt)))) + 2;
    int landedTicks = 0;

    for (int s = 1; s <= maxSteps && x < targetEndX; ++s) {
        x += p.vx * dt;

        if (!onG) {
            vy += g * dt;
            y += vy * dt;

            if (y <= p.halfH && !p.upsideDown) {
                y = p.halfH;
                vy = 0.0f;
                if (!p.isShip && !p.isUfo && s > 10) {
                    onG = true;
                }
            } else if (!p.isShip && !p.isUfo && s > 14 && (vy * gravSign < 0.0f)) {
                float supY = y;
                if (findSupportY(local, x, y, p.halfW, p.halfH, p.upsideDown, supY)) {
                    y = supY;
                    vy = 0.0f;
                    onG = true;
                }
            }
        } else {
            landedTicks++;
            if (landedTicks >= 6) {
                return { targetEndX, false };
            }
        }

        if (hitsHazard(local, x, y, hazHW, hazHH, false)) return { x, true };
        if ((!startedInSolid || s > 6) && hitsSolidCore(local, x, y, p.halfW, p.halfH)) return { x, true };
    }
    return { targetEndX, false };
}

inline double solveAutoWindowFrames(SimParams p, std::vector<SpatialObj> const& local, double refFps) {
    if (!p.isPress && !p.isWave) return -1.0;

    if (p.isWave) {
        if (local.empty()) return -1.0;
        double sec = solveWaveWindowSec(p, local);
        if (sec < 0.0) return -1.0;
        return std::clamp(sec * std::max(30.0, refFps), 0.10, std::max(24.0, refFps * 0.10));
    }

    bool clickWasOrb = false;
    ObjKind orbKind = findOverlappingOrb(local, p.x0, p.y0, p.halfW, p.halfH, 8.0f, clickWasOrb);

    if (!p.isShip && !p.isUfo && !p.onGround && !clickWasOrb) {
        if (!nearAnyGround(local, p.x0, p.y0, p.halfW, p.halfH, p.upsideDown)) {
            return -1.0;
        }
        p.onGround = true;
    }

    if (local.empty()) return -1.0;

    bool checkGroundSupp = p.onGround && !clickWasOrb &&
        hasGroundSupport(local, p.x0, p.y0, p.halfW, p.halfH, p.upsideDown);

    float horizonSec = (p.isShip || p.isUfo) ? 0.18f : 0.44f;

    if (!clickWasOrb) {
        auto noClick = simulateCandidate(p, local, 0.0f, false, ObjKind::Solid, checkGroundSupp, false, horizonSec);
        if (!noClick.died) {
            return -1.0;
        }
    }

    constexpr int kHalfN = 24;
    constexpr int kTotal = 2 * kHalfN + 1;
    constexpr float kStep = 1.0f / 480.0f;

    ProbeOutcome out[kTotal];
    bool anySurvived = false;
    float bestX = p.x0;

    for (int i = 0; i < kTotal; ++i) {
        float dT = static_cast<float>(i - kHalfN) * kStep;
        out[i] = simulateCandidate(p, local, dT, clickWasOrb, orbKind, checkGroundSupp, true, horizonSec);
        if (!out[i].died) anySurvived = true;
        if (out[i].endX > bestX) bestX = out[i].endX;
    }

    auto isGood = [&](ProbeOutcome const& po) -> bool {
        if (anySurvived) return !po.died;
        return (po.endX >= bestX - 2.5f) && (bestX - p.x0 > 12.0f);
    };

    int anchor = -1;
    for (int r = 0; r <= kHalfN; ++r) {
        if (isGood(out[kHalfN + r])) { anchor = kHalfN + r; break; }
        if (r > 0 && isGood(out[kHalfN - r])) { anchor = kHalfN - r; break; }
    }
    if (anchor < 0) return -1.0;

    int leftIdx = anchor;
    while (leftIdx > 0 && isGood(out[leftIdx - 1])) leftIdx--;

    int rightIdx = anchor;
    while (rightIdx < kTotal - 1 && isGood(out[rightIdx + 1])) rightIdx++;

    if (leftIdx == 0 && rightIdx == kTotal - 1) return -1.0;

    float leftT = static_cast<float>(leftIdx - kHalfN) * kStep;
    if (leftIdx > 0) {
        float badT = leftT - kStep;
        float goodT = leftT;
        for (int it = 0; it < 5; ++it) {
            float mid = 0.5f * (badT + goodT);
            if (isGood(simulateCandidate(p, local, mid, clickWasOrb, orbKind, checkGroundSupp, true, horizonSec)))
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
        for (int it = 0; it < 5; ++it) {
            float mid = 0.5f * (goodT + badT);
            if (isGood(simulateCandidate(p, local, mid, clickWasOrb, orbKind, checkGroundSupp, true, horizonSec)))
                goodT = mid;
            else
                badT = mid;
        }
        rightT = goodT;
    }

    double winSec = std::max(0.0004, static_cast<double>(rightT - leftT));
    double frames = winSec * std::max(30.0, refFps);
    return std::clamp(frames, 0.10, std::max(24.0, refFps * 0.10));
}

} // namespace fwc::sim
