#pragma once

#include "Config.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace fwc {

struct TierRowUI {
    CCLabelBMFont* nameLbl = nullptr;
    CCLabelBMFont* countLbl = nullptr;
    CCSprite* glowSpr = nullptr;
    int count = 0;
    float glowTimer = 0.0f;
};

struct WorldCircle {
    CCSprite* ring = nullptr;
    CCLabelBMFont* label = nullptr;
    float x = 0.0f;
    float age = 0.0f;
    float targetScale = 1.0f;
    float labelTargetScale = 0.42f;
};

class HudOverlay : public CCLayer {
private:
    ModConfig m_cfg;
    std::string m_levelName;
    std::vector<TierDef> m_tiers;

    // World-space persistent circles attached to PlayLayer::m_objectLayer
    CCNode* m_worldContainer = nullptr;
    std::vector<WorldCircle> m_circles;

    // Top-left NaN-Counter-style tier table (NO ugly background box!)
    CCNode* m_tableRoot = nullptr;
    std::vector<TierRowUI> m_rows;

    // Reaction ms & Hz below the table
    CCLabelBMFont* m_lblMs = nullptr;
    CCLabelBMFont* m_lblHz = nullptr;

    // Bottom-left Percentage & Raw Precision Score (2 clean white lines)
    CCLabelBMFont* m_lblPercent = nullptr;
    CCLabelBMFont* m_lblScore = nullptr;

    // Top-right subtle Watermark
    CCLabelBMFont* m_lblWatermark = nullptr;

    // Right-side animated Comment Callout (pops in fast, fades slowly)
    CCLabelBMFont* m_lblCallout = nullptr;
    float m_calloutAge = 999.0f;
    float m_calloutTotal = 2.2f;

    // Intro Screen (NaN Counter full-screen dark splash with left title + right color legend)
    CCNode* m_introRoot = nullptr;
    CCLayerColor* m_introBg = nullptr;
    std::vector<CCLabelBMFont*> m_introLabels;
    float m_introTimer = 0.0f;
    bool m_introActive = false;
    bool m_introShownOnce = false;

    // Statistical Precision Tracking
    int m_totalClicks = 0;
    int m_tightClicks = 0;       // clicks <= 16.67ms (60Hz FP or tighter)
    double m_rawPrecision = 0.0; // cumulative raw precision / difficulty score
    double m_logPassProb = 0.0;  // sum of ln(p_click) using human timing sigma
    int m_240FpCount = 0;

    CCSprite* makeRingSprite() {
        auto path = (Mod::get()->getResourcesDir() / "fwc_ring.png").string();
        auto* spr = CCSprite::create(path.c_str());
        if (!spr) spr = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
        return spr;
    }

    CCSprite* makeGlowSprite() {
        auto path = (Mod::get()->getResourcesDir() / "fwc_glow.png").string();
        auto* spr = CCSprite::create(path.c_str());
        return spr;
    }

    void buildLeftHud() {
        if (m_tableRoot) {
            m_tableRoot->removeFromParent();
            m_tableRoot = nullptr;
        }
        m_rows.clear();
        m_lblMs = nullptr;
        m_lblHz = nullptr;
        m_lblPercent = nullptr;
        m_lblScore = nullptr;
        m_lblWatermark = nullptr;
        m_lblCallout = nullptr;

        if (!m_cfg.enabled) return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_tableRoot = CCNode::create();
        this->addChild(m_tableRoot, 15);

        m_tiers = buildTiers(m_cfg.tableStyle, static_cast<double>(m_cfg.refFps));

        float leftX = 12.0f;
        float countX = 64.0f;
        float topY = winSize.height - 16.0f;
        float rowStep = (m_tiers.size() > 7) ? 16.0f : 18.0f;
        float curY = topY;

        if (m_cfg.showTierTable) {
            for (auto const& t : m_tiers) {
                TierRowUI row;

                row.nameLbl = CCLabelBMFont::create(t.label.c_str(), "bigFont.fnt");
                row.nameLbl->setAnchorPoint({ 0.0f, 0.5f });
                row.nameLbl->setScale(0.38f);
                row.nameLbl->setColor(t.color);
                row.nameLbl->setPosition({ leftX, curY });
                m_tableRoot->addChild(row.nameLbl, 2);

                row.glowSpr = makeGlowSprite();
                if (row.glowSpr) {
                    row.glowSpr->setPosition({ countX + 6.0f, curY });
                    row.glowSpr->setScale(0.32f);
                    row.glowSpr->setOpacity(0);
                    m_tableRoot->addChild(row.glowSpr, 1);
                }

                row.countLbl = CCLabelBMFont::create("0", "bigFont.fnt");
                row.countLbl->setAnchorPoint({ 0.0f, 0.5f });
                row.countLbl->setScale(0.38f);
                row.countLbl->setColor(t.color);
                row.countLbl->setPosition({ countX, curY });
                m_tableRoot->addChild(row.countLbl, 2);

                m_rows.push_back(row);
                curY -= rowStep;
            }
        }

        // Reaction time (ms) and Hz right underneath the tier table
        if (m_cfg.showMsHz) {
            float msY = m_cfg.showTierTable ? (curY - 6.0f) : (winSize.height - 20.0f);

            m_lblMs = CCLabelBMFont::create("0.00ms", "bigFont.fnt");
            m_lblMs->setAnchorPoint({ 0.0f, 0.5f });
            m_lblMs->setScale(0.40f);
            m_lblMs->setColor(ccc3(255, 255, 255));
            m_lblMs->setPosition({ leftX, msY });
            m_tableRoot->addChild(m_lblMs, 2);

            m_lblHz = CCLabelBMFont::create("0Hz", "bigFont.fnt");
            m_lblHz->setAnchorPoint({ 0.0f, 0.5f });
            m_lblHz->setScale(0.40f);
            m_lblHz->setColor(ccc3(255, 255, 255));
            m_lblHz->setPosition({ leftX, msY - 17.0f });
            m_tableRoot->addChild(m_lblHz, 2);
        }

        // Bottom-left: ONLY the 2 clean white numbers (Percentage & Raw Precision Score)
        if (m_cfg.showBottomStats) {
            m_lblPercent = CCLabelBMFont::create("100.00%", "bigFont.fnt");
            m_lblPercent->setAnchorPoint({ 0.0f, 0.5f });
            m_lblPercent->setScale(0.44f);
            m_lblPercent->setColor(ccc3(255, 255, 255));
            m_lblPercent->setPosition({ leftX, 34.0f });
            m_tableRoot->addChild(m_lblPercent, 2);

            m_lblScore = CCLabelBMFont::create("0.00", "bigFont.fnt");
            m_lblScore->setAnchorPoint({ 0.0f, 0.5f });
            m_lblScore->setScale(0.44f);
            m_lblScore->setColor(ccc3(255, 255, 255));
            m_lblScore->setPosition({ leftX, 15.0f });
            m_tableRoot->addChild(m_lblScore, 2);
        }

        // Top-right subtle Watermark (like NAN COUNTER)
        if (m_cfg.showWatermark && !m_cfg.watermarkText.empty()) {
            m_lblWatermark = CCLabelBMFont::create(m_cfg.watermarkText.c_str(), "bigFont.fnt");
            m_lblWatermark->setAnchorPoint({ 1.0f, 0.5f });
            m_lblWatermark->setScale(0.32f);
            m_lblWatermark->setColor(ccc3(255, 255, 255));
            m_lblWatermark->setOpacity(115);
            m_lblWatermark->setPosition({ winSize.width - 12.0f, winSize.height - 28.0f });
            m_tableRoot->addChild(m_lblWatermark, 2);
        }

        // Right-side Comment Callout (like "11TH 240HZ FRAME PERFECT")
        m_lblCallout = CCLabelBMFont::create("", "bigFont.fnt");
        m_lblCallout->setAnchorPoint({ 1.0f, 0.5f });
        m_lblCallout->setAlignment(kCCTextAlignmentRight);
        m_lblCallout->setScale(0.38f);
        m_lblCallout->setColor(ccc3(255, 255, 255));
        m_lblCallout->setOpacity(0);
        m_lblCallout->setPosition({ winSize.width - 14.0f, winSize.height - 62.0f });
        m_tableRoot->addChild(m_lblCallout, 4);
    }

    void buildIntroScreen(bool isReset) {
        if (m_introRoot) {
            m_introRoot->removeFromParent();
            m_introRoot = nullptr;
        }
        m_introLabels.clear();
        m_introActive = false;

        if (!m_cfg.enabled || m_cfg.bannerMode == BannerMode::Off) return;
        if (m_cfg.bannerMode == BannerMode::FirstOnly && m_introShownOnce && isReset) return;

        m_introShownOnce = true;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_introRoot = CCNode::create();
        this->addChild(m_introRoot, 40);

        m_introBg = CCLayerColor::create(ccc4(0, 0, 0, 215), winSize.width, winSize.height);
        m_introRoot->addChild(m_introBg, 0);

        // Left column: Level Name, optional Subtitle, Counter Title, optional Precision line
        float lx = 26.0f;

        auto addIntroLbl = [&](std::string const& txt, float x, float y, float scale, ccColor3B col, bool rightAlign) {
            auto* l = CCLabelBMFont::create(txt.c_str(), "bigFont.fnt");
            l->setAnchorPoint({ rightAlign ? 1.0f : 0.0f, 0.5f });
            l->setScale(scale);
            l->setColor(col);
            l->setPosition({ x, y });
            m_introRoot->addChild(l, 2);
            m_introLabels.push_back(l);
            return l;
        };

        auto* lvlLbl = addIntroLbl(m_levelName.empty() ? "LEVEL" : m_levelName, lx, winSize.height - 52.0f, 0.62f, ccc3(255, 255, 255), false);
        lvlLbl->limitLabelWidth(winSize.width * 0.44f, 0.62f, 0.28f);

        if (!m_cfg.bannerComment.empty()) {
            auto* sub = addIntroLbl(m_cfg.bannerComment, lx, winSize.height - 80.0f, 0.44f, ccc3(235, 238, 245), false);
            sub->limitLabelWidth(winSize.width * 0.44f, 0.44f, 0.22f);
        }

        std::string titleMode = m_cfg.cbfMode ? "CBF WINDOW\nCOUNTER" : "FRAME WINDOW\nCOUNTER";
        addIntroLbl(titleMode, lx, winSize.height * 0.5f + 6.0f, 0.52f, ccc3(255, 255, 255), false);

        std::string fpsTag = m_cfg.cbfMode
            ? fmt::format("@{}FPS (SUBFRAME)", m_cfg.refFps)
            : fmt::format("@{}FPS", m_cfg.refFps);
        addIntroLbl(fpsTag, lx, winSize.height * 0.5f - 36.0f, 0.44f, ccc3(220, 235, 255), false);

        if (!m_cfg.rawPrecisionText.empty()) {
            auto* pLbl = addIntroLbl(m_cfg.rawPrecisionText, lx, 34.0f, 0.34f, ccc3(220, 224, 232), false);
            pLbl->limitLabelWidth(winSize.width * 0.46f, 0.34f, 0.18f);
        }

        // Right column: Exact color legend of tiers
        float rx = winSize.width - 22.0f;
        std::string header = m_cfg.cbfMode
            ? fmt::format("{}HZ \"FRAMES\" W/ CBF", m_cfg.refFps)
            : fmt::format("{}HZ FRAME WINDOWS", m_cfg.refFps);
        addIntroLbl(header, rx, winSize.height - 24.0f, 0.38f, ccc3(255, 255, 255), true);

        float ry = winSize.height - 46.0f;
        float stepY = (winSize.height - 64.0f) / static_cast<float>(std::max<size_t>(1, m_tiers.size()));
        stepY = std::min(stepY, 32.0f);

        for (auto const& t : m_tiers) {
            if (t.maxF > 1e6 && m_tiers.size() > 8) continue; // Keep legend crisp & uncrowded like NaN Counter!
            addIntroLbl(t.introDesc, rx, ry, 0.34f, t.color, true);
            addIntroLbl(t.introMs, rx, ry - 12.0f, 0.30f, t.color, true);
            ry -= stepY;
        }

        m_introTimer = 0.0f;
        m_introActive = true;
    }

    void updateBottomNumbers() {
        if (m_lblPercent) {
            // Live estimated pass consistency % (starts at 100%, drops naturally on tight frame perfects)
            double consistencyPct = std::exp(m_logPassProb) * 100.0;
            m_lblPercent->setString(fmt::format("{:.2f}%", std::clamp(consistencyPct, 0.0, 100.0)).c_str());
        }
        if (m_lblScore) {
            // Raw Precision difficulty accumulation score (like NaN Counter's bottom-left number)
            m_lblScore->setString(fmt::format("{:.2f}", m_rawPrecision).c_str());
        }
    }

public:
    static HudOverlay* create(std::string const& levelName, CCNode* worldParent) {
        auto* ret = new HudOverlay();
        if (ret && ret->initOverlay(levelName, worldParent)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool initOverlay(std::string const& levelName, CCNode* worldParent) {
        if (!CCLayer::init()) return false;
        m_levelName = levelName;
        m_cfg = ModConfig::load();

        ensureWorldContainer(worldParent);
        buildLeftHud();
        buildIntroScreen(false);
        return true;
    }

    // Lazily guarantee world container is ALWAYS attached to active PlayLayer::m_objectLayer!
    void ensureWorldContainer(CCNode* worldParent) {
        if (!worldParent) return;
        if (m_worldContainer && m_worldContainer->getParent() == worldParent) return;

        m_circles.clear();
        m_worldContainer = CCNode::create();
        worldParent->addChild(m_worldContainer, 2000);
    }

    void reloadConfig(CCNode* worldParent) {
        m_cfg = ModConfig::load();
        ensureWorldContainer(worldParent);
        buildLeftHud();
        updateBottomNumbers();
    }

    void resetAttempt(CCNode* worldParent) {
        m_cfg = ModConfig::load();
        ensureWorldContainer(worldParent);
        clearCircles();

        m_totalClicks = 0;
        m_tightClicks = 0;
        m_rawPrecision = 0.0;
        m_logPassProb = 0.0;
        m_240FpCount = 0;
        m_calloutAge = 999.0f;

        for (auto& r : m_rows) {
            r.count = 0;
            r.glowTimer = 0.0f;
            if (r.countLbl) r.countLbl->setString("0");
            if (r.glowSpr) r.glowSpr->setOpacity(0);
        }
        if (m_lblMs) m_lblMs->setString("0.00ms");
        if (m_lblHz) m_lblHz->setString("0Hz");
        if (m_lblCallout) m_lblCallout->setOpacity(0);

        updateBottomNumbers();
        buildIntroScreen(true);
    }

    void clearCircles() {
        for (auto& c : m_circles) {
            if (c.ring) c.ring->removeFromParent();
            if (c.label) c.label->removeFromParent();
        }
        m_circles.clear();
    }

    void showCallout(std::string const& text, ccColor3B color) {
        if (!m_cfg.enabled || !m_lblCallout || text.empty()) return;
        m_lblCallout->setString(text.c_str());
        m_lblCallout->setColor(color);
        m_lblCallout->setOpacity(255);
        m_calloutAge = 0.0f;
        m_calloutTotal = std::max(0.6f, m_cfg.commentFadeSec);
    }

    void onClickTriggered(CCNode* worldParent, CCPoint levelPos, double frames, std::string const& customComment, double liveCps) {
        if (!m_cfg.enabled) return;
        ensureWorldContainer(worldParent);

        double fps = static_cast<double>(m_cfg.refFps);
        double displayFrames = m_cfg.cbfMode ? frames : std::max(1.0, std::round(frames));
        double ms = framesToMs(displayFrames, fps);
        double hz = framesToHz(displayFrames, fps);

        if (m_tiers.empty()) {
            m_tiers = buildTiers(m_cfg.tableStyle, fps);
        }
        int tierIdx = findTierIndex(m_tiers, displayFrames);
        auto const& tier = m_tiers[static_cast<size_t>(tierIdx)];

        // 1) Update Tier Table + Trigger Crisp White Glow Highlight!
        if (tierIdx >= 0 && tierIdx < static_cast<int>(m_rows.size())) {
            auto& row = m_rows[static_cast<size_t>(tierIdx)];
            row.count++;
            if (row.countLbl) {
                row.countLbl->setString(fmt::format("{}", row.count).c_str());
            }
            row.glowTimer = 0.68f;
            if (row.glowSpr) {
                row.glowSpr->setOpacity(245);
                row.glowSpr->setScale(0.42f);
            }
        }

        // 2) Update Reaction ms & Hz
        if (m_lblMs) m_lblMs->setString(fmt::format("{:.2f}ms", ms).c_str());
        if (m_lblHz) m_lblHz->setString(fmt::format("{:.0f}Hz", std::min(hz, 9999.0)).c_str());

        // 3) Update Bottom-Left Consistency % & Raw Precision Difficulty Score
        m_totalClicks++;
        if (ms <= 16.7) m_tightClicks++;

        // Human click timing normal distribution (sigma = 8.0 ms)
        // Probability of hitting a window of width `ms`: erf(ms / (2 * sqrt(2) * sigma))
        double z = ms / (2.0 * 1.41421356 * 8.0);
        double pHit = std::clamp(std::erf(z), 0.03, 0.999);
        m_logPassProb += std::log(pHit);

        // Raw Precision Difficulty Weight: tighter clicks contribute exponentially more!
        // e.g. 22ms -> ~0.35, 12.5ms -> ~1.1, 8.3ms -> ~2.4, 4.17ms (240Hz FP) -> ~9.6, 2ms -> ~38
        double weight = std::pow(13.0 / std::max(0.6, ms), 2.0);
        m_rawPrecision += weight;
        updateBottomNumbers();

        // 4) Callout Comment (manual comment > 240Hz FP auto-comment > high CPS spam comment)
        if (!customComment.empty()) {
            showCallout(customComment, tier.color);
        } else if (m_cfg.showAutoComments) {
            if (ms <= 4.25) {
                m_240FpCount++;
                std::string ord = fmt::format("{}TH", m_240FpCount);
                if (m_240FpCount % 100 < 11 || m_240FpCount % 100 > 13) {
                    if (m_240FpCount % 10 == 1) ord = fmt::format("{}ST", m_240FpCount);
                    else if (m_240FpCount % 10 == 2) ord = fmt::format("{}ND", m_240FpCount);
                    else if (m_240FpCount % 10 == 3) ord = fmt::format("{}RD", m_240FpCount);
                }
                showCallout(
                    fmt::format("{} 240HZ FRAME PERFECT\n({})", ord, formatCircleNumber(displayFrames, m_cfg.cbfMode)),
                    tier.color
                );
            } else if (liveCps >= 10.0) {
                showCallout(
                    fmt::format("{:.1f} CPS SPAM\n(WINDOW {})", liveCps, formatCircleNumber(displayFrames, m_cfg.cbfMode)),
                    tier.color
                );
            }
        }

        // 5) Spawn BOLD NaN-Counter Circle directly in m_objectLayer!
        if (!m_cfg.showCircles || !m_worldContainer) return;

        // Keep memory clean if a huge level has hundreds of clicks
        if (m_circles.size() > 220) {
            auto& old = m_circles.front();
            if (old.ring) old.ring->removeFromParent();
            if (old.label) old.label->removeFromParent();
            m_circles.erase(m_circles.begin());
        }

        float sc = std::clamp(m_cfg.circleScale, 0.4f, 2.5f);
        WorldCircle wc;
        wc.x = levelPos.x;
        wc.age = 0.0f;

        wc.ring = makeRingSprite();
        if (wc.ring) {
            float texW = std::max(16.0f, wc.ring->getContentSize().width);
            float diameter = 36.0f * sc; // bold & clearly visible around player
            wc.targetScale = diameter / texW;
            wc.ring->setPosition(levelPos);
            wc.ring->setColor(tier.color);
            wc.ring->setOpacity(255);
            wc.ring->setScale(wc.targetScale * 1.28f); // punchy pop-in!
            m_worldContainer->addChild(wc.ring, 10);
        }

        if (m_cfg.showCircleLabel) {
            std::string numStr = formatCircleNumber(displayFrames, m_cfg.cbfMode);
            wc.label = CCLabelBMFont::create(numStr.c_str(), "bigFont.fnt");
            wc.label->setAnchorPoint({ 1.0f, 0.5f }); // Right-aligned to the LEFT of the circle!
            wc.labelTargetScale = 0.44f * std::min(1.4f, sc);
            wc.label->setScale(wc.labelTargetScale * 1.18f);
            wc.label->setColor(tier.color);
            wc.label->setOpacity(255);
            wc.label->setPosition(ccp(levelPos.x - 21.0f * sc, levelPos.y));
            m_worldContainer->addChild(wc.label, 11);
        }

        m_circles.push_back(wc);
    }

    // Driven purely by game dt -> 100% synced in renders, bots, and slow-mo!
    void stepSimulation(float dt, float currentPlayerX) {
        if (!m_cfg.enabled || dt <= 0.0f) return;

        // Animate table glow highlights
        for (auto& r : m_rows) {
            if (r.glowTimer > 0.0f) {
                r.glowTimer = std::max(0.0f, r.glowTimer - dt);
                float p = r.glowTimer / 0.68f;
                if (r.glowSpr) {
                    r.glowSpr->setOpacity(static_cast<GLubyte>(p * p * 240.0f));
                    r.glowSpr->setScale(0.30f + 0.14f * p);
                }
            }
        }

        // Animate right-side callout comment (sharp pop-in, slow smooth fade-out)
        if (m_lblCallout && m_calloutAge < m_calloutTotal) {
            m_calloutAge += dt;
            if (m_calloutAge >= m_calloutTotal) {
                m_lblCallout->setOpacity(0);
            } else {
                float holdTime = m_calloutTotal * 0.35f;
                if (m_calloutAge <= holdTime) {
                    m_lblCallout->setOpacity(255);
                } else {
                    float fadeP = 1.0f - (m_calloutAge - holdTime) / std::max(0.1f, m_calloutTotal - holdTime);
                    m_lblCallout->setOpacity(static_cast<GLubyte>(std::clamp(fadeP * 255.0f, 0.0f, 255.0f)));
                }
            }
        }

        // Animate circles (pop-in settle; persistent unless Persistent Circles is OFF)
        for (size_t i = 0; i < m_circles.size(); ) {
            auto& c = m_circles[i];
            c.age += dt;

            // Prune circles far behind the camera (> 900 GD units behind player) to keep FPS maxed
            if (currentPlayerX - c.x > 950.0f) {
                if (c.ring) c.ring->removeFromParent();
                if (c.label) c.label->removeFromParent();
                m_circles.erase(m_circles.begin() + static_cast<ptrdiff_t>(i));
                continue;
            }

            // Crisp 0.11s pop-in settle animation
            if (c.age < 0.11f) {
                float p = c.age / 0.11f;
                float ease = 1.0f - (1.0f - p) * (1.0f - p);
                if (c.ring) c.ring->setScale(c.targetScale * (1.28f - 0.28f * ease));
                if (c.label) c.label->setScale(c.labelTargetScale * (1.18f - 0.18f * ease));
            } else {
                if (c.ring) c.ring->setScale(c.targetScale);
                if (c.label) c.label->setScale(c.labelTargetScale);
            }

            if (!m_cfg.persistentCircles) {
                float life = 0.85f;
                if (c.age >= life) {
                    if (c.ring) c.ring->removeFromParent();
                    if (c.label) c.label->removeFromParent();
                    m_circles.erase(m_circles.begin() + static_cast<ptrdiff_t>(i));
                    continue;
                }
                if (c.age > 0.35f) {
                    float fade = 1.0f - (c.age - 0.35f) / (life - 0.35f);
                    GLubyte op = static_cast<GLubyte>(std::clamp(fade * 255.0f, 0.0f, 255.0f));
                    if (c.ring) c.ring->setOpacity(op);
                    if (c.label) c.label->setOpacity(op);
                }
            }

            ++i;
        }

        // Animate Intro Screen fade-out on game dt
        if (m_introActive && m_introRoot) {
            m_introTimer += dt;
            float holdEnd = std::max(0.4f, m_cfg.bannerDuration);
            float fadeDur = 0.55f;
            if (m_introTimer >= holdEnd + fadeDur) {
                m_introRoot->setVisible(false);
                m_introActive = false;
            } else if (m_introTimer > holdEnd) {
                float p = 1.0f - std::clamp((m_introTimer - holdEnd) / fadeDur, 0.0f, 1.0f);
                if (m_introBg) m_introBg->setOpacity(static_cast<GLubyte>(p * 215.0f));
                GLubyte lblOp = static_cast<GLubyte>(p * 255.0f);
                for (auto* l : m_introLabels) {
                    if (l) l->setOpacity(lblOp);
                }
            }
        }
    }
};

} // namespace fwc
