#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fwc::sim {

enum class ObjKind : uint8_t {
    Solid = 0,
    Hazard = 1,
    Slope = 2,
    DBlock = 3,
    YellowOrb = 4,
    PinkOrb = 5,
    RedOrb = 6,
    BlueOrb = 7,
    GreenOrb = 8,
    BlackOrb = 9,
    DashOrb = 10,
    SpiderOrb = 11,
    TriggerOrb = 12,
    GravPortalNormal = 13,
    GravPortalFlip   = 14,
    GravPortalToggle = 15,
    YellowPad        = 16,
    PinkPad          = 17,
    RedPad           = 18,
    BluePad          = 19,
    ModePortal       = 20,
    TeleportPortal   = 21,
    SizePortalNormal = 22,
    SizePortalMini   = 23
};

struct SpatialObj {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    ObjKind kind = ObjKind::Solid;
    uint8_t slopeType = 0;
    bool hasDBlock = false;
    bool isCircular = false;
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
constexpr float kCubeGrav     = 2794.108f;

inline bool isOrbKind(ObjKind k) {
    uint8_t v = static_cast<uint8_t>(k);
    return v >= static_cast<uint8_t>(ObjKind::YellowOrb) &&
           v <= static_cast<uint8_t>(ObjKind::TriggerOrb);
}

inline bool isPortalOrPadKind(ObjKind k) {
    return static_cast<uint8_t>(k) >= static_cast<uint8_t>(ObjKind::GravPortalNormal);
}

inline bool rectOverlaps(float ax0, float ax1, float ay0, float ay1, SpatialObj const& o) {
    return ax1 > o.minX && ax0 < o.maxX && ay1 > o.minY && ay0 < o.maxY;
}

inline bool aabbHitsCircleHazard(float px0, float px1, float py0, float py1, SpatialObj const& o) {
    if (!rectOverlaps(px0, px1, py0, py1, o)) return false;
    float ocx = 0.5f * (o.minX + o.maxX);
    float ocy = 0.5f * (o.minY + o.maxY);
    float rad = 0.40f * std::min(o.maxX - o.minX, o.maxY - o.minY);
    float closestX = std::clamp(ocx, px0, px1);
    float closestY = std::clamp(ocy, py0, py1);
    float dx = ocx - closestX;
    float dy = ocy - closestY;
    return (dx * dx + dy * dy) < (rad * rad);
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

inline bool insideAnyDBlock(std::vector<SpatialObj> const& local, float cx, float cy, float margin) {
    float px0 = cx - margin, px1 = cx + margin, py0 = cy - margin, py1 = cy + margin;
    for (auto const& o : local) {
        if (o.kind == ObjKind::DBlock && rectOverlaps(px0, px1, py0, py1, o)) {
            return true;
        }
    }
    return false;
}

inline bool hitsHazard(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool waveMode) {
    float px0 = cx - hw, px1 = cx + hw, py0 = cy - hh, py1 = cy + hh;
    for (auto const& o : local) {
        if (o.kind == ObjKind::Hazard) {
            if (o.isCircular) {
                if (aabbHitsCircleHazard(px0, px1, py0, py1, o)) return true;
            } else {
                if (rectOverlaps(px0, px1, py0, py1, o)) return true;
            }
        } else if (waveMode && !o.hasDBlock && o.kind == ObjKind::Solid) {
            if (rectOverlaps(px0, px1, py0, py1, o)) return true;
        } else if (waveMode && !o.hasDBlock && o.kind == ObjKind::Slope) {
            if (boxHitsSlopeTriangle(cx, cy, hw, hh, o)) return true;
        }
    }
    return false;
}

inline bool hitsSolidCore(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh) {
    float px0 = cx - hw * 0.28f, px1 = cx + hw * 0.28f;
    float py0 = cy - hh * 0.28f, py1 = cy + hh * 0.28f;
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
    float py0 = upsideDown ? (cy + hh - 9.5f) : (cy - hh - 6.0f);
    float py1 = upsideDown ? (cy + hh + 6.0f) : (cy - hh + 9.5f);
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

inline bool findSpiderDestY(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, bool upsideDown, float& outCenterY) {
    float px0 = cx - hw * 0.92f, px1 = cx + hw * 0.92f;
    bool found = false;
    float bestY = cy;

    if (upsideDown) {
        bestY = hh;
        found = true;
        for (auto const& o : local) {
            if (o.kind != ObjKind::Solid && o.kind != ObjKind::Slope) continue;
            if (o.maxX <= px0 || o.minX >= px1) continue;
            if (o.maxY <= cy - hh + 4.0f) {
                float cand = o.maxY + hh;
                if (cand > bestY) {
                    bestY = cand;
                    found = true;
                }
            }
        }
    } else {
        for (auto const& o : local) {
            if (o.kind != ObjKind::Solid && o.kind != ObjKind::Slope) continue;
            if (o.maxX <= px0 || o.minX >= px1) continue;
            if (o.minY >= cy + hh - 4.0f && o.minY <= cy + 480.0f) {
                float cand = o.minY - hh;
                if (!found || cand < bestY) {
                    bestY = cand;
                    found = true;
                }
            }
        }
    }
    if (found) outCenterY = bestY;
    return found;
}

inline int findActiveOrbIndex(std::vector<SpatialObj> const& local, float cx, float cy, float hw, float hh, float margin) {
    float px0 = cx - hw - margin, px1 = cx + hw + margin;
    float py0 = cy - hh - margin, py1 = cy + hh + margin;
    int bestIdx = -1;
    float bestDistSq = 1e18f;
    for (size_t i = 0; i < local.size(); ++i) {
        auto const& o = local[i];
        if (!isOrbKind(o.kind)) continue;
        if (rectOverlaps(px0, px1, py0, py1, o)) {
            float ox = 0.5f * (o.minX + o.maxX);
            float oy = 0.5f * (o.minY + o.maxY);
            float d2 = (ox - cx) * (ox - cx) + (oy - cy) * (oy - cy);
            if (d2 < bestDistSq) {
                bestDistSq = d2;
                bestIdx = static_cast<int>(i);
            }
        }
    }
    return bestIdx;
}

inline bool touchesOtherOrb(std::vector<SpatialObj> const& local, int excludeIdx, float cx, float cy, float hw, float hh) {
    float px0 = cx - hw - 5.0f, px1 = cx + hw + 5.0f;
    float py0 = cy - hh - 5.0f, py1 = cy + hh + 5.0f;
    for (size_t i = 0; i < local.size(); ++i) {
        if (static_cast<int>(i) == excludeIdx) continue;
        auto const& o = local[i];
        if (isOrbKind(o.kind) && rectOverlaps(px0, px1, py0, py1, o)) {
            return true;
        }
    }
    return false;
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

        {
            float px0 = x - hw, px1 = x + hw, py0 = y - hh, py1 = y + hh;
            for (auto const& o : local) {
                if (o.kind == ObjKind::TeleportPortal && rectOverlaps(px0, px1, py0, py1, o)) {
                    return maxTimeSec;
                }
                if (s >= 4 && (o.kind == ObjKind::ModePortal || o.kind == ObjKind::DBlock) && rectOverlaps(px0, px1, py0, py1, o)) {
                    return maxTimeSec;
                }
            }
        }

        if (y < hh || hitsHazard(local, x, y, hw, hh, true)) {
            float lo = static_cast<float>(s - 1) * stepT;
            float hi = t;
            for (int b = 0; b < 5; ++b) {
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
    if (p.onGround || insideAnyDBlock(local, p.x0, p.y0, 14.0f)) {
        return -1.0;
    }

    float gravSign = p.upsideDown ? -1.0f : 1.0f;
    float slope = p.mini ? 2.0f : 1.0f;
    float hw = p.mini ? 2.2f : 3.4f;
    float hh = p.mini ? 2.2f : 3.4f;

    if (hitsHazard(local, p.x0, p.y0, hw, hh, true)) {
        return -1.0;
    }

    float preVy  = (p.isPress ? -1.0f :  1.0f) * gravSign * p.vx * slope;
    float postVy = (p.isPress ?  1.0f : -1.0f) * gravSign * p.vx * slope;

    constexpr float kMaxRay = 0.14f;
    float tLate  = waveRayClearanceSec(local, p.x0, p.y0,  p.vx,  preVy, hw, hh, kMaxRay);
    float tBack  = waveRayClearanceSec(local, p.x0, p.y0, -p.vx, -preVy, hw, hh, kMaxRay);
    float tPost0 = waveRayClearanceSec(local, p.x0, p.y0,  p.vx, postVy, hw, hh, kMaxRay);

    if (tLate >= 0.095f && (tLate + tBack) >= 0.135f) {
        return -1.0;
    }

    float reqPost = std::min(tPost0 * 0.52f, 0.022f);
    if (reqPost < 0.001f) reqPost = 0.001f;

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
    if (winSec >= 0.095) return -1.0;
    return std::max(0.00025, winSec);
}

inline double solveShipWindowSec(SimParams const& p, std::vector<SpatialObj> const& local) {
    float gravSign = p.upsideDown ? -1.0f : 1.0f;
    float hh = p.halfH * 0.90f;

    float upClear = 120.0f;
    float downClear = 120.0f;
    for (auto const& o : local) {
        if (o.kind != ObjKind::Hazard && o.kind != ObjKind::Solid) continue;
        if (o.maxX < p.x0 - 10.0f || o.minX > p.x0 + 65.0f) continue;

        if (o.minY >= p.y0) {
            float gap = (o.minY - (p.y0 + hh));
            if (gap < upClear) upClear = std::max(0.0f, gap);
        } else if (o.maxY <= p.y0) {
            float gap = ((p.y0 - hh) - o.maxY);
            if (gap < downClear) downClear = std::max(0.0f, gap);
        }
    }

    float totalVertGap = upClear + downClear;
    if (totalVertGap > 75.0f) return -1.0;

    float activeGap = (gravSign > 0.0f) ? std::min(downClear + upClear * 0.45f, totalVertGap) : std::min(upClear + downClear * 0.45f, totalVertGap);
    double tSec = std::sqrt((2.0 * std::max(0.8, static_cast<double>(activeGap))) / 1050.0) * 0.42;
    if (tSec >= 0.085) return -1.0;
    return std::clamp(tSec, 0.0010, 0.080);
}

struct ProbeOutcome {
    float endX = 0.0f;
    bool died = false;
};

inline bool processPortalsAndPads(
    std::vector<SpatialObj> const& local,
    int& lastPortalIdx,
    float x, float y, float hw, float hh,
    bool& mini,
    float& gravSign,
    float& vy,
    float& g,
    bool& onG,
    int stepNum
) {
    float px0 = x - hw, px1 = x + hw, py0 = y - hh, py1 = y + hh;
    float baseGravMag = std::fabs(g);
    if (baseGravMag < 100.0f) baseGravMag = kCubeGrav;

    for (size_t i = 0; i < local.size(); ++i) {
        if (static_cast<int>(i) == lastPortalIdx) continue;
        auto const& o = local[i];
        if (!isPortalOrPadKind(o.kind)) continue;
        if (!rectOverlaps(px0, px1, py0, py1, o)) continue;

        lastPortalIdx = static_cast<int>(i);
        switch (o.kind) {
            case ObjKind::GravPortalNormal:
                if (gravSign < 0.0f) {
                    gravSign = 1.0f;
                    g = -baseGravMag;
                    vy = std::clamp(vy, -240.0f, 240.0f);
                    onG = false;
                }
                break;
            case ObjKind::GravPortalFlip:
                if (gravSign > 0.0f) {
                    gravSign = -1.0f;
                    g = baseGravMag;
                    vy = std::clamp(vy, -240.0f, 240.0f);
                    onG = false;
                }
                break;
            case ObjKind::GravPortalToggle:
                gravSign = -gravSign;
                g = -baseGravMag * gravSign;
                vy = std::clamp(vy, -240.0f, 240.0f);
                onG = false;
                break;
            case ObjKind::YellowPad:
                vy = (mini ? 640.0f : 795.0f) * gravSign;
                onG = false;
                break;
            case ObjKind::PinkPad:
                vy = (mini ? 430.0f : 535.0f) * gravSign;
                onG = false;
                break;
            case ObjKind::RedPad:
                vy = (mini ? 820.0f : 990.0f) * gravSign;
                onG = false;
                break;
            case ObjKind::BluePad:
                gravSign = -gravSign;
                g = -baseGravMag * gravSign;
                vy = -210.0f * gravSign;
                onG = false;
                break;
            case ObjKind::TeleportPortal:
                return true;
            case ObjKind::ModePortal:
                if (stepNum >= 2) return true;
                break;
            case ObjKind::SizePortalNormal:
                mini = false;
                break;
            case ObjKind::SizePortalMini:
                mini = true;
                break;
            default:
                break;
        }
    }
    return false;
}

inline ProbeOutcome simulateCandidate(
    SimParams const& p,
    std::vector<SpatialObj> const& local,
    float deltaT,
    int orbIdx,
    bool checkGroundSupp,
    bool doClick,
    float horizonSec,
    float hitboxScale
) {
    constexpr float dt = 1.0f / 240.0f;
    float gravSign = p.upsideDown ? -1.0f : 1.0f;
    float hazHW = p.halfW * hitboxScale;
    float hazHH = p.halfH * hitboxScale;
    bool clickWasOrb = (orbIdx >= 0 && orbIdx < static_cast<int>(local.size()));
    ObjKind orbKind = clickWasOrb ? local[static_cast<size_t>(orbIdx)].kind : ObjKind::Solid;

    float targetEndX = p.x0 + p.vx * horizonSec;

    if (!doClick) {
        float x = p.x0, y = p.y0;
        float vy = p.onGround ? 0.0f : p.vy0;
        float g = -kCubeGrav * gravSign;
        bool onG = p.onGround && !p.isUfo;
        bool curMini = p.mini;
        bool startedInSolid = hitsSolidCore(local, x, y, p.halfW, p.halfH);
        int lastPortal = -1;

        int steps = std::max(1, static_cast<int>(std::ceil(horizonSec / dt)));
        for (int s = 1; s <= steps; ++s) {
            x += p.vx * dt;
            if (processPortalsAndPads(local, lastPortal, x, y, p.halfW, p.halfH, curMini, gravSign, vy, g, onG, s)) {
                return { targetEndX, false };
            }
            bool curUp = (gravSign < 0.0f);
            if (onG) {
                float supY = y;
                if (checkGroundSupp && !findSupportY(local, x, y, p.halfW, p.halfH, curUp, supY)) {
                    onG = false;
                }
            }
            if (!onG) {
                y += vy * dt;
                vy += g * dt;
                if (y < p.halfH && !curUp) {
                    y = p.halfH; vy = 0.0f;
                    onG = !p.isUfo;
                }
            }
            if (hitsHazard(local, x, y, hazHW, hazHH, false)) return { x, true };
            if ((!startedInSolid || s > 6) && hitsSolidCore(local, x, y, p.halfW, p.halfH)) return { x, true };
        }
        return { targetEndX, false };
    }

    float xClick = p.x0 + p.vx * deltaT;
    float yClick = p.y0;

    if (p.onGround && !clickWasOrb && !p.isUfo) {
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
        float preG = -kCubeGrav * gravSign;
        yClick = p.y0 + p.vy0 * deltaT + 0.5f * preG * deltaT * deltaT;
        if (hitsHazard(local, xClick, yClick, hazHW, hazHH, false)) {
            return { xClick, true };
        }
    }

    if (clickWasOrb) {
        auto const& orbObj = local[static_cast<size_t>(orbIdx)];
        float ox0 = orbObj.minX - 6.0f, ox1 = orbObj.maxX + 6.0f;
        float oy0 = orbObj.minY - 6.0f, oy1 = orbObj.maxY + 6.0f;
        bool stillOnOrb = (xClick + p.halfW > ox0) && (xClick - p.halfW < ox1) &&
                          (yClick + p.halfH > oy0) && (yClick - p.halfH < oy1);
        if (!stillOnOrb) return { xClick, true };
    }

    float x = xClick, y = yClick, vy = 0.0f;
    float g = -kCubeGrav * gravSign;
    bool onG = false;
    bool curMini = p.mini;
    int lastPortal = -1;

    if (clickWasOrb) {
        switch (orbKind) {
            case ObjKind::YellowOrb: vy = (curMini ? 483.0f : 603.7f) * gravSign; break;
            case ObjKind::PinkOrb:   vy = (curMini ? 362.9f : 453.6f) * gravSign; break;
            case ObjKind::RedOrb:    vy = (curMini ? 639.4f : 799.2f) * gravSign; break;
            case ObjKind::BlueOrb:   gravSign = -gravSign; g = -kCubeGrav * gravSign; vy = -210.0f * gravSign; break;
            case ObjKind::GreenOrb:  gravSign = -gravSign; g = -kCubeGrav * gravSign; vy = -(curMini ? 483.0f : 603.7f) * gravSign; break;
            case ObjKind::BlackOrb:  vy = -864.0f * gravSign; break;
            case ObjKind::DashOrb:   vy = 0.0f; g = 0.0f; break;
            case ObjKind::SpiderOrb: {
                float destY = y;
                if (!findSpiderDestY(local, x, y, p.halfW, p.halfH, gravSign < 0.0f, destY)) {
                    return { x, true };
                }
                y = destY;
                if (hitsHazard(local, x, y, hazHW, hazHH, false)) return { x, true };
                gravSign = -gravSign;
                vy = 0.0f;
                g = 0.0f;
                onG = true;
                break;
            }
            case ObjKind::TriggerOrb:
                // Square/Toggle/Teleport orb triggers custom level actions (scale/launch/teleport):
                // valid window is the safe overlap interval with the orb.
                return { targetEndX, false };
            default:                 vy = 603.7f * gravSign; break;
        }
    } else if (p.isUfo) {
        vy = (curMini ? 388.0f : 432.0f) * gravSign;
        g = (curMini ? -2790.0f : -2385.0f) * gravSign;
    } else if (p.isBall) {
        gravSign = -gravSign;
        vy = -135.0f * gravSign;
        g = -2385.0f * gravSign;
    } else if (p.isSpider) {
        float destY = y;
        if (!findSpiderDestY(local, x, y, p.halfW, p.halfH, p.upsideDown, destY)) {
            return { x, true };
        }
        y = destY;
        if (hitsHazard(local, x, y, hazHW, hazHH, false)) return { x, true };
        gravSign = -gravSign;
        vy = 0.0f;
        g = 0.0f;
        onG = true;
    } else {
        vy = (curMini ? kCubeVyMini : kCubeVyNormal) * gravSign;
        g = -kCubeGrav * gravSign;
    }

    if (processPortalsAndPads(local, lastPortal, x, y, p.halfW, p.halfH, curMini, gravSign, vy, g, onG, 0)) {
        return { targetEndX, false };
    }

    bool startedInSolid = hitsSolidCore(local, x, y, p.halfW, p.halfH);
    int maxSteps = std::max(1, static_cast<int>(std::ceil((targetEndX - x) / (p.vx * dt)))) + 2;
    int landedTicks = 0;

    for (int s = 1; s <= maxSteps && x < targetEndX; ++s) {
        x += p.vx * dt;

        if (processPortalsAndPads(local, lastPortal, x, y, p.halfW, p.halfH, curMini, gravSign, vy, g, onG, s)) {
            return { targetEndX, false };
        }

        bool curUpsideDown = (gravSign < 0.0f);

        if (!onG) {
            y += vy * dt;
            vy += g * dt;

            if (s >= 4 && touchesOtherOrb(local, orbIdx, x, y, p.halfW, p.halfH)) {
                return { targetEndX, false };
            }

            if (y <= p.halfH && !curUpsideDown) {
                y = p.halfH;
                vy = 0.0f;
                if (s >= 4) {
                    onG = true;
                }
            } else if (s >= 3 && (vy * gravSign < 0.0f)) {
                float supY = y;
                if (findSupportY(local, x, y, p.halfW, p.halfH, curUpsideDown, supY)) {
                    y = supY;
                    vy = 0.0f;
                    onG = true;
                }
            }
        } else {
            landedTicks++;
            if (landedTicks >= 2) {
                return { targetEndX, false };
            }
        }

        if (hitsHazard(local, x, y, hazHW, hazHH, false)) return { x, true };
        if ((!startedInSolid || s > 6) && hitsSolidCore(local, x, y, p.halfW, p.halfH)) return { x, true };
    }
    return { targetEndX, false };
}

inline double scanWindowPass(
    SimParams const& p,
    std::vector<SpatialObj> const& local,
    int orbIdx,
    bool checkGroundSupp,
    float horizonSec,
    float hitboxScale,
    double refFps,
    bool& outAnySurvived,
    bool& outAllSurvived
) {
    outAnySurvived = false;
    outAllSurvived = false;

    constexpr int kCoarseHalf = 23;
    constexpr int kCoarseN = 2 * kCoarseHalf + 1;
    constexpr float kCoarseStep = 1.0f / 240.0f;

    ProbeOutcome out[kCoarseN];

    for (int i = 0; i < kCoarseN; ++i) {
        float dT = static_cast<float>(i - kCoarseHalf) * kCoarseStep;
        out[i] = simulateCandidate(p, local, dT, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale);
        if (!out[i].died) outAnySurvived = true;
    }

    float fineAnchorT = 0.0f;
    bool foundFineSurvive = false;
    if (!outAnySurvived) {
        constexpr float kFineStep = 1.0f / 960.0f;
        for (int r = 0; r <= 12; ++r) {
            for (int sign = 1; sign >= -1; sign -= 2) {
                if (r == 0 && sign == -1) continue;
                float dT = static_cast<float>(r * sign) * kFineStep;
                auto po = simulateCandidate(p, local, dT, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale);
                if (!po.died) {
                    outAnySurvived = true;
                    foundFineSurvive = true;
                    fineAnchorT = dT;
                    break;
                }
            }
            if (foundFineSurvive) break;
        }
    }

    if (foundFineSurvive) {
        constexpr float kFineStep = 1.0f / 960.0f;
        float rightT = fineAnchorT;
        for (int k = 1; k <= 32; ++k) {
            float t = fineAnchorT + static_cast<float>(k) * kFineStep;
            if (!simulateCandidate(p, local, t, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale).died) {
                rightT = t;
            } else {
                float lo = rightT, hi = t;
                for (int b = 0; b < 4; ++b) {
                    float mid = 0.5f * (lo + hi);
                    if (!simulateCandidate(p, local, mid, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale).died) lo = mid;
                    else hi = mid;
                }
                rightT = lo;
                break;
            }
        }
        float leftT = fineAnchorT;
        for (int k = 1; k <= 32; ++k) {
            float t = fineAnchorT - static_cast<float>(k) * kFineStep;
            if (!simulateCandidate(p, local, t, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale).died) {
                leftT = t;
            } else {
                float hi = leftT, lo = t;
                for (int b = 0; b < 4; ++b) {
                    float mid = 0.5f * (lo + hi);
                    if (!simulateCandidate(p, local, mid, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale).died) hi = mid;
                    else lo = mid;
                }
                leftT = hi;
                break;
            }
        }
        double winSec = std::max(0.0004, static_cast<double>(rightT - leftT));
        return winSec * std::max(30.0, refFps);
    }

    if (!outAnySurvived) return -1.0;

    auto isGood = [&](ProbeOutcome const& po) -> bool {
        return !po.died;
    };

    int anchor = -1;
    for (int r = 0; r <= kCoarseHalf; ++r) {
        if (isGood(out[kCoarseHalf + r])) { anchor = kCoarseHalf + r; break; }
        if (r > 0 && isGood(out[kCoarseHalf - r])) { anchor = kCoarseHalf - r; break; }
    }
    if (anchor < 0) return -1.0;

    int leftIdx = anchor;
    while (leftIdx > 0 && isGood(out[leftIdx - 1])) leftIdx--;

    int rightIdx = anchor;
    while (rightIdx < kCoarseN - 1 && isGood(out[rightIdx + 1])) rightIdx++;

    if (leftIdx == 0 && rightIdx == kCoarseN - 1) {
        outAllSurvived = true;
        return -1.0;
    }

    float leftT = static_cast<float>(leftIdx - kCoarseHalf) * kCoarseStep;
    if (leftIdx > 0) {
        float badT = leftT - kCoarseStep;
        float goodT = leftT;
        for (int it = 0; it < 5; ++it) {
            float mid = 0.5f * (badT + goodT);
            if (isGood(simulateCandidate(p, local, mid, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale)))
                goodT = mid;
            else
                badT = mid;
        }
        leftT = goodT;
    }

    float rightT = static_cast<float>(rightIdx - kCoarseHalf) * kCoarseStep;
    if (rightIdx < kCoarseN - 1) {
        float goodT = rightT;
        float badT = rightT + kCoarseStep;
        for (int it = 0; it < 5; ++it) {
            float mid = 0.5f * (goodT + badT);
            if (isGood(simulateCandidate(p, local, mid, orbIdx, checkGroundSupp, true, horizonSec, hitboxScale)))
                goodT = mid;
            else
                badT = mid;
        }
        rightT = goodT;
    }

    double winSec = std::max(0.0004, static_cast<double>(rightT - leftT));
    return winSec * std::max(30.0, refFps);
}

inline double solveAutoWindowFrames(SimParams p, std::vector<SpatialObj> const& local, double refFps) {
    if (!p.isPress && !p.isWave) return -1.0;

    if (p.isWave) {
        if (local.empty()) return -1.0;
        double sec = solveWaveWindowSec(p, local);
        if (sec < 0.0) return -1.0;
        return std::clamp(sec * std::max(30.0, refFps), 0.10, std::max(24.0, refFps * 0.10));
    }

    if (p.isShip) {
        if (local.empty()) return -1.0;
        double sec = solveShipWindowSec(p, local);
        if (sec < 0.0) return -1.0;
        return std::clamp(sec * std::max(30.0, refFps), 0.50, std::max(24.0, refFps * 0.10));
    }

    if (local.empty()) return -1.0;

    int orbIdx = findActiveOrbIndex(local, p.x0, p.y0, p.halfW, p.halfH, 8.0f);

    if (orbIdx < 0 && !p.isUfo && !p.onGround) {
        constexpr float dt = 1.0f / 240.0f;
        float gravSign = p.upsideDown ? -1.0f : 1.0f;
        float g = -kCubeGrav * gravSign;
        float tx = p.x0, ty = p.y0, tvy = p.vy0;
        bool projected = false;
        for (int s = 1; s <= 34; ++s) {
            tx += p.vx * dt;
            ty += tvy * dt;
            tvy += g * dt;
            int futureOrb = findActiveOrbIndex(local, tx, ty, p.halfW, p.halfH, 4.0f);
            if (futureOrb >= 0) {
                p.x0 = tx; p.y0 = ty; p.vy0 = tvy;
                orbIdx = futureOrb;
                projected = true;
                break;
            }
            float supY = ty;
            if ((tvy * gravSign <= 0.0f) && findSupportY(local, tx, ty, p.halfW, p.halfH, p.upsideDown, supY)) {
                p.x0 = tx; p.y0 = supY; p.vy0 = 0.0f; p.onGround = true;
                projected = true;
                break;
            }
        }
        if (!projected) return -1.0;
    }

    bool clickWasOrb = (orbIdx >= 0);
    bool checkGroundSupp = p.onGround && !clickWasOrb &&
        hasGroundSupport(local, p.x0, p.y0, p.halfW, p.halfH, p.upsideDown);

    float horizonSec = 0.48f;
    if (clickWasOrb) horizonSec = 0.26f;
    else if (p.isUfo) horizonSec = p.mini ? 0.13f : 0.16f;
    else if (p.isBall || p.isSpider) horizonSec = 0.22f;

    if (!clickWasOrb) {
        auto noClick = simulateCandidate(p, local, 0.0f, -1, checkGroundSupp, false, horizonSec, 1.0f);
        if (!noClick.died) {
            return -1.0;
        }
    }

    bool anySurv = false, allSurv = false;
    double frames = scanWindowPass(p, local, orbIdx, checkGroundSupp, horizonSec, 1.0f, refFps, anySurv, allSurv);
    if (allSurv) return -1.0;

    bool isPureCubeGround = p.onGround && !clickWasOrb && !p.isBall && !p.isSpider && !p.isUfo && !p.isShip && !p.isWave;
    if (frames < 0.0 && !anySurv && isPureCubeGround) {
        bool anySurvRelax = false, allSurvRelax = false;
        double fRelax = scanWindowPass(p, local, -1, checkGroundSupp, horizonSec, 0.90f, refFps, anySurvRelax, allSurvRelax);
        if (anySurvRelax && fRelax > 0.0) {
            frames = std::clamp(fRelax * 0.35, 0.45 * (refFps / 240.0), 1.65 * (refFps / 240.0));
        }
    }

    if (frames < 0.0) return -1.0;
    return std::clamp(frames, 0.15, std::max(28.0, refFps * 0.12));
}

} // namespace fwc::sim
