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
    float baseY = 0.0f;
};

struct WorldCircle {
    CCSprite* ring = nullptr;
    CCLabelBMFont* label = nullptr;
    float x = 0.0f;
    float age = 0.0f;
};

class HudOverlay : public CCLayer {
private:
    ModConfig m_cfg;
    std::string m_levelName;
    std::vector<TierDef> m_tiers;

    Ref<CCNode> m_worldContainer;
    std::vector<WorldCircle> m_circles;

    CCNode* m_tableRoot = nullptr;
    std::vector<TierRowUI> m_rows;
    float m_countX = 76.0f;

    CCLabelBMFont* m_lblMs = nullptr;
    CCLabelBMFont* m_lblHz = nullptr;
    CCLabelBMFont* m_lblPercent = nullptr;
    CCLabelBMFont* m_lblScore = nullptr;
    CCLabelBMFont* m_lblWatermark = nullptr;

    CCLabelBMFont* m_lblCallout = nullptr;
    float m_calloutAge = 999.0f;
    float m_calloutTotal = 2.4f;

    CCNode* m_statusBanner = nullptr;
    CCLabelBMFont* m_statusTitleLbl = nullptr;
    CCLabelBMFont* m_statusHintLbl = nullptr;

    CCNode* m_introRoot = nullptr;
    CCLayerColor* m_introBg = nullptr;
    std::vector<CCLabelBMFont*> m_introLabels;
    float m_introTimer = 0.0f;
    bool m_introActive = false;
    bool m_introShownOnce = false;

    // Difficulty % state (NEVER tied linearly to level X%!)
    double m_lastMs = 0.0;
    double m_lastHz = 0.0;
    double m_rawPrecision = 0.0;
    double m_knownTotalDifficulty = 0.0; // From Manual/Scan markers OR previous completed run
    double m_displayDiffPct = 0.0;       // Only updates when a click occurs (or hits 100% on completion)

    double m_lastCalloutTime = -10.0;

    CCSprite* makeRingSprite() {
        std::string fname = "fwc_ring_bold.png";
        if (m_cfg.ringThickness == "thin") fname = "fwc_ring_thin.png";
        else if (m_cfg.ringThickness == "thick") fname = "fwc_ring_thick.png";

        auto path = (Mod::get()->getResourcesDir() / fname).string();
        auto* spr = CCSprite::create(path.c_str());
        if (!spr) spr = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
        return spr;
    }

    CCSprite* makeGlowSprite() {
        auto path = (Mod::get()->getResourcesDir() / "fwc_glow.png").string();
        auto* spr = CCSprite::create(path.c_str());
        if (spr) {
            spr->setBlendFunc(ccBlendFunc{ GL_SRC_ALPHA, GL_ONE });
        }
        return spr;
    }

    void buildStatusBanner() {
        if (m_statusBanner) {
            m_statusBanner->removeFromParent();
            m_statusBanner = nullptr;
            m_statusTitleLbl = nullptr;
            m_statusHintLbl = nullptr;
        }
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_statusBanner = CCNode::create();
        m_statusBanner->setPosition({ winSize.width * 0.5f, winSize.height - 46.0f });
        m_statusBanner->setVisible(false);
        this->addChild(m_statusBanner, 60);

        auto* bg = CCScale9Sprite::create("square02_001.png");
        bg->setOpacity(190);
        bg->setContentSize({ 308.0f, 34.0f });
        m_statusBanner->addChild(bg, 0);

        m_statusTitleLbl = CCLabelBMFont::create("WORKING...", "bigFont.fnt");
        m_statusTitleLbl->setScale(0.35f);
        m_statusTitleLbl->setColor(ccc3(120, 245, 165));
        m_statusTitleLbl->setPosition({ 0.0f, 5.5f });
        m_statusBanner->addChild(m_statusTitleLbl, 2);

        m_statusHintLbl = CCLabelBMFont::create("Press ESC to stop", "chatFont.fnt");
        m_statusHintLbl->setScale(0.54f);
        m_statusHintLbl->setColor(ccc3(215, 222, 235));
        m_statusHintLbl->setPosition({ 0.0f, -8.5f });
        m_statusBanner->addChild(m_statusHintLbl, 2);
    }

    void buildLeftHud(bool preserveCounts) {
        std::vector<int> oldCounts;
        if (preserveCounts) {
            for (auto const& r : m_rows) oldCounts.push_back(r.count);
        }

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
        m_countX = (m_cfg.tableStyle == TableStyle::TopDown24) ? 76.0f : 64.0f;
        float topY = winSize.height - 16.0f;
        float rowStep = (m_tiers.size() > 7) ? 16.0f : 18.0f;
        float curY = topY;

        if (m_cfg.showTierTable) {
            for (size_t i = 0; i < m_tiers.size(); ++i) {
                auto const& t = m_tiers[i];
                TierRowUI row;
                row.baseY = curY;
                if (preserveCounts && oldCounts.size() == m_tiers.size()) {
                    row.count = oldCounts[i];
                }

                row.nameLbl = CCLabelBMFont::create(t.label.c_str(), "bigFont.fnt");
                row.nameLbl->setAnchorPoint({ 0.0f, 0.5f });
                row.nameLbl->setScale(0.38f);
                row.nameLbl->setColor(t.color);
                row.nameLbl->setPosition({ leftX, curY });
                m_tableRoot->addChild(row.nameLbl, 2);

                row.glowSpr = makeGlowSprite();
                if (row.glowSpr) {
                    row.glowSpr->setPosition({ m_countX + 7.0f, curY });
                    row.glowSpr->setScale(0.45f);
                    row.glowSpr->setOpacity(0);
                    m_tableRoot->addChild(row.glowSpr, 1);
                }

                row.countLbl = CCLabelBMFont::create(fmt::format("{}", row.count).c_str(), "bigFont.fnt");
                row.countLbl->setAnchorPoint({ 0.0f, 0.5f });
                row.countLbl->setScale(0.38f);
                row.countLbl->setColor(t.color);
                row.countLbl->setPosition({ m_countX, curY });
                m_tableRoot->addChild(row.countLbl, 2);

                m_rows.push_back(row);
                curY -= rowStep;
            }
        }

        if (m_cfg.showMsHz) {
            float msY = m_cfg.showTierTable ? (curY - 6.0f) : (winSize.height - 20.0f);

            m_lblMs = CCLabelBMFont::create(fmt::format("{:.2f}ms", m_lastMs).c_str(), "bigFont.fnt");
            m_lblMs->setAnchorPoint({ 0.0f, 0.5f });
            m_lblMs->setScale(0.40f);
            m_lblMs->setColor(ccc3(255, 255, 255));
            m_lblMs->setPosition({ leftX, msY });
            m_tableRoot->addChild(m_lblMs, 2);

            m_lblHz = CCLabelBMFont::create(fmt::format("{:.0f}Hz", std::min(m_lastHz, 9999.0)).c_str(), "bigFont.fnt");
            m_lblHz->setAnchorPoint({ 0.0f, 0.5f });
            m_lblHz->setScale(0.40f);
            m_lblHz->setColor(ccc3(255, 255, 255));
            m_lblHz->setPosition({ leftX, msY - 17.0f });
            m_tableRoot->addChild(m_lblHz, 2);
        }

        if (m_cfg.showBottomStats) {
            m_lblPercent = CCLabelBMFont::create("0.00%", "bigFont.fnt");
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
            updateBottomNumbers();
        }

        if (m_cfg.showWatermark && !m_cfg.watermarkText.empty()) {
            m_lblWatermark = CCLabelBMFont::create(m_cfg.watermarkText.c_str(), "bigFont.fnt");
            m_lblWatermark->setAnchorPoint({ 0.5f, 0.5f });
            m_lblWatermark->setScale(0.32f);
            m_lblWatermark->setColor(ccc3(255, 255, 255));
            m_lblWatermark->setOpacity(125);
            m_lblWatermark->setPosition({ winSize.width * 0.5f, winSize.height - 24.0f });
            m_tableRoot->addChild(m_lblWatermark, 2);
        }

        m_lblCallout = CCLabelBMFont::create("", "bigFont.fnt");
        m_lblCallout->setAnchorPoint({ 1.0f, 0.5f });
        m_lblCallout->setAlignment(kCCTextAlignmentRight);
        m_lblCallout->setScale(0.38f);
        m_lblCallout->setColor(ccc3(255, 255, 255));
        m_lblCallout->setOpacity(0);
        m_lblCallout->setPosition({ winSize.width - 14.0f, winSize.height - 56.0f });
        m_tableRoot->addChild(m_lblCallout, 4);
    }

    void buildIntroScreen(bool isReset, bool forceShow = false) {
        if (m_introRoot) {
            m_introRoot->removeFromParent();
            m_introRoot = nullptr;
        }
        m_introLabels.clear();
        m_introActive = false;

        if (!m_cfg.enabled) return;
        if (!forceShow) {
            if (m_cfg.bannerMode == BannerMode::Off) return;
            if (m_cfg.bannerMode == BannerMode::FirstOnly && m_introShownOnce && isReset) return;
        } else {
            if (m_cfg.bannerMode == BannerMode::Off) return;
        }

        m_introShownOnce = true;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_introRoot = CCNode::create();
        this->addChild(m_introRoot, 40);

        // Full black splash screen while Intro Freeze holds Frame 0!
        m_introBg = CCLayerColor::create(ccc4(0, 0, 0, 238), winSize.width, winSize.height);
        m_introRoot->addChild(m_introBg, 0);

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

        float rx = winSize.width - 22.0f;
        std::string header = m_cfg.cbfMode
            ? fmt::format("{}HZ \"FRAMES\" W/ CBF", m_cfg.refFps)
            : fmt::format("{}HZ FRAME WINDOWS", m_cfg.refFps);
        addIntroLbl(header, rx, winSize.height - 24.0f, 0.38f, ccc3(255, 255, 255), true);

        float ry = winSize.height - 46.0f;
        float stepY = (winSize.height - 64.0f) / static_cast<float>(std::max<size_t>(1, m_tiers.size()));
        stepY = std::min(stepY, 30.0f);

        for (auto const& t : m_tiers) {
            if (t.maxF > 1e6 && m_tiers.size() > 8 && m_cfg.tableStyle == TableStyle::CbfRanges) continue;
            addIntroLbl(t.introDesc, rx, ry, 0.32f, t.color, true);
            addIntroLbl(t.introMs, rx, ry - 11.0f, 0.28f, t.color, true);
            ry -= stepY;
        }

        m_introTimer = 0.0f;
        m_introActive = true;
    }

    void updateBottomNumbers() {
        if (m_lblPercent) {
            m_lblPercent->setString(fmt::format("{:.2f}%", std::clamp(m_displayDiffPct, 0.0, 100.0)).c_str());
        }
        if (m_lblScore) {
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
        buildLeftHud(false);
        buildStatusBanner();
        buildIntroScreen(false);
        return true;
    }

    void onExit() override {
        clearCircles();
        if (m_worldContainer) {
            m_worldContainer->removeFromParent();
            m_worldContainer = nullptr;
        }
        CCLayer::onExit();
    }

    void ensureWorldContainer(CCNode* worldParent) {
        if (!worldParent) return;
        if (m_worldContainer && m_worldContainer->getParent() == worldParent) return;

        if (m_worldContainer) {
            m_worldContainer->removeAllChildrenWithCleanup(true);
            m_worldContainer->removeFromParent();
            m_worldContainer = nullptr;
        }
        m_circles.clear();

        auto* node = CCNode::create();
        node->setID("fwc-world-circles"_spr);
        worldParent->addChild(node, 2000);
        m_worldContainer = node;
    }

    bool isIntroFreezing() const {
        return m_cfg.enabled && m_cfg.introFreeze && m_introActive;
    }

    double currentRawPrecision() const {
        return m_rawPrecision;
    }

    void setStatusBanner(bool active, std::string const& title, std::string const& hint) {
        if (!m_statusBanner) return;
        m_statusBanner->setVisible(active);
        if (active) {
            if (m_statusTitleLbl) m_statusTitleLbl->setString(title.c_str());
            if (m_statusHintLbl) m_statusHintLbl->setString(hint.c_str());
        }
    }

    void setStatusBannerTemporaryHidden(bool hidden) {
        if (m_statusBanner) {
            m_statusBanner->setVisible(!hidden);
        }
    }

    void setKnownTotalDifficulty(double totalScore) {
        m_knownTotalDifficulty = std::max(0.0, totalScore);
    }

    void markLevelCompleted() {
        if (m_rawPrecision > 0.01) {
            m_displayDiffPct = 100.0;
            updateBottomNumbers();
        }
    }

    void reloadConfig(CCNode* worldParent) {
        m_cfg = ModConfig::load();
        ensureWorldContainer(worldParent);
        buildLeftHud(true);
    }

    void resetAttempt(CCNode* worldParent, bool forceShowIntro = false) {
        m_cfg = ModConfig::load();
        ensureWorldContainer(worldParent);
        clearCircles();

        m_lastMs = 0.0;
        m_lastHz = 0.0;
        m_rawPrecision = 0.0;
        m_displayDiffPct = 0.0;
        m_lastCalloutTime = -10.0;
        m_calloutAge = 999.0f;

        for (auto& r : m_rows) {
            r.count = 0;
            r.glowTimer = 0.0f;
            if (r.countLbl) {
                r.countLbl->setString("0");
                r.countLbl->setScale(0.38f);
            }
            if (r.glowSpr) r.glowSpr->setOpacity(0);
        }
        if (m_lblMs) m_lblMs->setString("0.00ms");
        if (m_lblHz) m_lblHz->setString("0Hz");
        if (m_lblCallout) m_lblCallout->setOpacity(0);

        updateBottomNumbers();
        buildIntroScreen(true, forceShowIntro);
    }

    void clearCircles() {
        m_circles.clear();
        if (m_worldContainer) {
            m_worldContainer->removeAllChildrenWithCleanup(true);
        }
    }

    void pruneCirclesAfter(float x) {
        for (size_t i = 0; i < m_circles.size(); ) {
            if (m_circles[i].x >= x - 2.0f) {
                if (m_circles[i].ring) m_circles[i].ring->removeFromParent();
                if (m_circles[i].label) m_circles[i].label->removeFromParent();
                m_circles.erase(m_circles.begin() + static_cast<ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    void showCallout(std::string const& text, ccColor3B color) {
        if (!m_cfg.enabled || !m_lblCallout || text.empty()) return;
        m_lblCallout->setString(text.c_str());
        m_lblCallout->setColor(color);
        m_lblCallout->setOpacity(255);
        m_calloutAge = 0.0f;
        m_calloutTotal = std::max(0.6f, m_cfg.commentFadeSec);
    }

    // hazardFractionPassed is (hazards up to player X) / (total hazards in level) in [0.01 .. 1.0]
    void onClickTriggered(
        CCNode* worldParent,
        CCPoint levelPos,
        double frames,
        std::string const& manualComment,
        double simTime,
        double liveCps,
        double hazardFractionPassed
    ) {
        if (!m_cfg.enabled) return;
        ensureWorldContainer(worldParent);

        // Keep overlay & circles visible even if an external renderer hides other UI
        this->setVisible(true);
        if (m_worldContainer) m_worldContainer->setVisible(true);

        double fps = static_cast<double>(m_cfg.refFps);
        double displayFrames = resolveDisplayFrames(frames, m_cfg.cbfMode);
        double rawMs = framesToMs(frames, fps);
        m_lastMs = framesToMs(displayFrames, fps);
        m_lastHz = framesToHz(displayFrames, fps);

        if (m_tiers.empty()) {
            m_tiers = buildTiers(m_cfg.tableStyle, fps);
        }
        int tierIdx = findTierIndex(m_tiers, displayFrames);
        auto const& tier = m_tiers[static_cast<size_t>(tierIdx)];

        // 1) Update Tier Row + Bright Additive White Glow
        if (tierIdx >= 0 && tierIdx < static_cast<int>(m_rows.size())) {
            auto& row = m_rows[static_cast<size_t>(tierIdx)];
            row.count++;
            if (row.countLbl) {
                row.countLbl->setString(fmt::format("{}", row.count).c_str());
                row.countLbl->setScale(0.47f);
                float centerX = m_countX + row.countLbl->getScaledContentSize().width * 0.42f;
                if (row.glowSpr) {
                    row.glowSpr->setPosition({ centerX, row.baseY });
                }
            }
            row.glowTimer = 0.85f;
            if (row.glowSpr) {
                row.glowSpr->setOpacity(255);
                row.glowSpr->setScale(0.62f);
            }
        }

        // 2) Update ms & Hz
        if (m_lblMs) m_lblMs->setString(fmt::format("{:.2f}ms", m_lastMs).c_str());
        if (m_lblHz) m_lblHz->setString(fmt::format("{:.0f}Hz", std::min(m_lastHz, 9999.0)).c_str());

        // 3) Difficulty-Based % & Raw Precision Score!
        double clickWeight = clickPrecisionWeight(m_lastMs);
        m_rawPrecision += clickWeight;

        if (m_knownTotalDifficulty > 0.05) {
            // Exact share of total level difficulty!
            m_displayDiffPct = std::clamp((m_rawPrecision / m_knownTotalDifficulty) * 100.0, 0.0, 100.0);
        } else {
            // First-ever attempt (not completed yet): estimate remaining difficulty from remaining hazard share!
            double hFrac = std::clamp(hazardFractionPassed, 0.02, 1.0);
            double estRemaining = (1.0 - hFrac) * std::max(m_rawPrecision / hFrac, 35.0);
            double estTotal = m_rawPrecision + estRemaining;
            double estPct = (estTotal > 1e-4) ? ((m_rawPrecision / estTotal) * 100.0) : 0.0;
            // Monotonic rise purely on clicks (jumps big on tight 1F clicks, tiny on easy clicks!)
            m_displayDiffPct = std::clamp(std::max(m_displayDiffPct, estPct), 0.0, 99.95);
        }
        updateBottomNumbers();

        // 4) Comments (Manual + clean non-numbered Auto Comments)
        if (!manualComment.empty()) {
            showCallout(manualComment, tier.color);
            m_lastCalloutTime = simTime;
        } else if (m_cfg.showAutoComments) {
            if (liveCps >= 11.0 && (simTime - m_lastCalloutTime) > 1.4) {
                showCallout(
                    fmt::format("{:.1f} CPS SPAM\n(WINDOW {})", liveCps, formatCircleNumber(displayFrames, m_cfg.cbfMode)),
                    tier.color
                );
                m_lastCalloutTime = simTime;
            } else if (rawMs <= 5.50 && (simTime - m_lastCalloutTime) > 1.2) {
                double loEst = std::max(0.1, frames - 0.35);
                double hiEst = frames + 0.35;
                showCallout(
                    fmt::format("CAN BE FROM ~({:.1f}-{:.1f})", loEst, hiEst),
                    tier.color
                );
                m_lastCalloutTime = simTime;
            }
        }

        // 5) Spawn STATIC Bold Circle directly in m_objectLayer
        auto* container = m_worldContainer.data();
        if (!m_cfg.showCircles || !container) return;

        if (m_circles.size() > 240) {
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
            float diameter = 36.0f * sc;
            wc.ring->setPosition(levelPos);
            wc.ring->setColor(tier.color);
            wc.ring->setOpacity(255);
            wc.ring->setScale(diameter / texW);
            container->addChild(wc.ring, 10);
        }

        if (m_cfg.showCircleLabel) {
            std::string numStr = formatCircleNumber(displayFrames, m_cfg.cbfMode);
            wc.label = CCLabelBMFont::create(numStr.c_str(), "bigFont.fnt");
            wc.label->setAnchorPoint({ 1.0f, 0.5f });
            wc.label->setScale(0.44f * std::min(1.4f, sc));
            wc.label->setColor(tier.color);
            wc.label->setOpacity(255);
            wc.label->setPosition(ccp(levelPos.x - 21.0f * sc, levelPos.y));
            container->addChild(wc.label, 11);
        }

        m_circles.push_back(wc);
    }

    // Advance ONLY the Intro screen timer & fade (used while game is frozen on Frame 0!)
    void stepIntroOnly(float dt) {
        if (!m_introActive || !m_introRoot || dt <= 0.0f) return;
        m_introTimer += dt;
        float holdEnd = std::max(0.4f, m_cfg.bannerDuration);
        float fadeDur = 0.55f;
        if (m_introTimer >= holdEnd + fadeDur) {
            m_introRoot->setVisible(false);
            m_introActive = false;
        } else if (m_introTimer > holdEnd) {
            float p = 1.0f - std::clamp((m_introTimer - holdEnd) / fadeDur, 0.0f, 1.0f);
            if (m_introBg) m_introBg->setOpacity(static_cast<GLubyte>(p * 238.0f));
            GLubyte lblOp = static_cast<GLubyte>(p * 255.0f);
            for (auto* l : m_introLabels) {
                if (l) l->setOpacity(lblOp);
            }
        }
    }

    void stepSimulation(float dt, float currentPlayerX) {
        if (!m_cfg.enabled || dt <= 0.0f) return;

        for (auto& r : m_rows) {
            if (r.glowTimer > 0.0f) {
                r.glowTimer = std::max(0.0f, r.glowTimer - dt);
                float p = r.glowTimer / 0.85f;
                if (r.glowSpr) {
                    float eased = p * (0.45f + 0.55f * p);
                    r.glowSpr->setOpacity(static_cast<GLubyte>(std::clamp(eased * 255.0f, 0.0f, 255.0f)));
                    r.glowSpr->setScale(0.44f + 0.20f * p);
                }
                if (r.countLbl) {
                    float elapsed = 0.85f - r.glowTimer;
                    if (elapsed < 0.16f) {
                        float sp = elapsed / 0.16f;
                        r.countLbl->setScale(0.47f - 0.09f * sp);
                    } else {
                        r.countLbl->setScale(0.38f);
                    }
                }
            }
        }

        if (m_lblCallout && m_calloutAge < m_calloutTotal) {
            m_calloutAge += dt;
            if (m_calloutAge >= m_calloutTotal) {
                m_lblCallout->setOpacity(0);
            } else {
                float holdTime = m_calloutTotal * 0.42f;
                if (m_calloutAge <= holdTime) {
                    m_lblCallout->setOpacity(255);
                } else {
                    float fadeP = 1.0f - (m_calloutAge - holdTime) / std::max(0.1f, m_calloutTotal - holdTime);
                    m_lblCallout->setOpacity(static_cast<GLubyte>(std::clamp(fadeP * 255.0f, 0.0f, 255.0f)));
                }
            }
        }

        for (size_t i = 0; i < m_circles.size(); ) {
            auto& c = m_circles[i];
            c.age += dt;

            if (currentPlayerX - c.x > 1000.0f) {
                if (c.ring) c.ring->removeFromParent();
                if (c.label) c.label->removeFromParent();
                m_circles.erase(m_circles.begin() + static_cast<ptrdiff_t>(i));
                continue;
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

        // If Intro Freeze is OFF, still fade the intro normally during gameplay
        if (!m_cfg.introFreeze && m_introActive) {
            stepIntroOnly(dt);
        }
    }
};

} // namespace fwc
