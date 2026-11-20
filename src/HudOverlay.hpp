#pragma once

#include "Config.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace fwc {

struct RunStats {
    int totalClicks = 0;
    int fpClicks = 0;
    int sFrames = 0;
    int swifts = 0;
    int multiclicks = 0;
    double sumFrames = 0.0;
    double passProb = 1.0;
    double lastFrames = 0.0;
    double lastHz = 0.0;
    double lastMs = 0.0;
    ClickKind lastKind = ClickKind::Normal;
    bool hasLastClick = false;
    double liveTps = 240.0;

    void reset() {
        totalClicks = 0;
        fpClicks = 0;
        sFrames = 0;
        swifts = 0;
        multiclicks = 0;
        sumFrames = 0.0;
        passProb = 1.0;
        lastFrames = 0.0;
        lastHz = 0.0;
        lastMs = 0.0;
        lastKind = ClickKind::Normal;
        hasLastClick = false;
    }

    void recordClick(double frames, double refFps, ClickKind kind, bool isFp) {
        totalClicks++;
        sumFrames += frames;
        if (isFp) fpClicks++;
        if (frames < 1.0 || kind == ClickKind::SFrame) sFrames++;
        if (kind == ClickKind::Swift) swifts++;
        if (kind == ClickKind::Multiclick) multiclicks++;

        lastFrames = frames;
        lastMs = framesToMs(frames, refFps);
        lastHz = framesToHz(frames, refFps);
        lastKind = kind;
        hasLastClick = true;

        double pClick = std::clamp(lastMs / (lastMs + 1.35), 0.25, 0.996);
        passProb = std::max(0.0001, passProb * pClick);
    }
};

struct ActiveWorldCircle {
    CCSprite* ringSprite = nullptr;
    CCLabelBMFont* label = nullptr;
    CCPoint origin;
    float age = 0.0f;
    float lifetime = 0.65f;
    float baseScale = 1.0f;
};

class HudOverlay : public CCLayer {
private:
    ModConfig m_cfg;
    std::string m_levelName;
    RunStats m_stats;

    // World-space circle container attached directly to PlayLayer::m_objectLayer!
    CCNode* m_worldContainer = nullptr;
    std::vector<ActiveWorldCircle> m_circles;

    // Clean minimal bottom-left HUD
    CCNode* m_hudCard = nullptr;
    CCScale9Sprite* m_hudBg = nullptr;
    CCLabelBMFont* m_lblBigFrame = nullptr;
    CCLabelBMFont* m_lblMeta = nullptr;
    CCLabelBMFont* m_lblCounts = nullptr;
    CCLabelBMFont* m_lblSpecials = nullptr;
    CCLabelBMFont* m_lblPercents = nullptr;

    // Clean minimal intro banner
    CCNode* m_bannerRoot = nullptr;
    CCScale9Sprite* m_bannerBg = nullptr;
    std::vector<CCLabelBMFont*> m_bannerLabels;
    float m_bannerTimer = 0.0f;
    bool m_bannerActive = false;

    CCSprite* createRingSprite() {
        auto path = (Mod::get()->getResourcesDir() / "fwc_ring.png").string();
        auto* spr = CCSprite::create(path.c_str());
        if (!spr) {
            // Guaranteed vanilla GD fallback sprite
            spr = CCSprite::createWithSpriteFrameName("d_circle_02_001.png");
        }
        return spr;
    }

    void buildHudCard() {
        if (m_hudCard) {
            m_hudCard->removeFromParent();
            m_hudCard = nullptr;
        }

        m_hudCard = CCNode::create();
        m_hudCard->setPosition(ccp(10.0f, 10.0f));
        this->addChild(m_hudCard, 15);

        float cardW = 192.0f;
        float cardH = 54.0f;
        if (m_cfg.showSpecialStats) cardH += 13.0f;
        if (m_cfg.showPercentages) cardH += 13.0f;

        // Clean native GD rounded dark panel (zero neon slop borders)
        m_hudBg = CCScale9Sprite::create("square02_001.png");
        m_hudBg->setOpacity(155);
        m_hudBg->setContentSize({ cardW, cardH });
        m_hudBg->setAnchorPoint({ 0.0f, 0.0f });
        m_hudBg->setPosition({ 0.0f, 0.0f });
        m_hudCard->addChild(m_hudBg, 0);

        m_lblBigFrame = CCLabelBMFont::create("--F", "bigFont.fnt");
        m_lblBigFrame->setAnchorPoint({ 0.0f, 0.5f });
        m_lblBigFrame->setScale(0.45f);
        m_lblBigFrame->setPosition({ 10.0f, cardH - 14.0f });
        m_lblBigFrame->setColor(ccc3(245, 245, 245));
        m_hudCard->addChild(m_lblBigFrame, 2);

        m_lblMeta = CCLabelBMFont::create("READY", "bigFont.fnt");
        m_lblMeta->setAnchorPoint({ 1.0f, 0.5f });
        m_lblMeta->setScale(0.26f);
        m_lblMeta->setPosition({ cardW - 10.0f, cardH - 14.0f });
        m_lblMeta->setColor(ccc3(205, 210, 220));
        m_hudCard->addChild(m_lblMeta, 2);

        float y = cardH - 33.0f;

        m_lblCounts = CCLabelBMFont::create("Clicks: 0  |  FP: 0  |  Avg: --", "chatFont.fnt");
        m_lblCounts->setAnchorPoint({ 0.0f, 0.5f });
        m_lblCounts->setScale(0.64f);
        m_lblCounts->setPosition({ 10.0f, y });
        m_lblCounts->setColor(ccc3(235, 238, 244));
        m_hudCard->addChild(m_lblCounts, 2);
        y -= 13.0f;

        m_lblSpecials = nullptr;
        if (m_cfg.showSpecialStats) {
            m_lblSpecials = CCLabelBMFont::create("S-Fr: 0  |  Swifts: 0  |  Multi: 0", "chatFont.fnt");
            m_lblSpecials->setAnchorPoint({ 0.0f, 0.5f });
            m_lblSpecials->setScale(0.60f);
            m_lblSpecials->setPosition({ 10.0f, y });
            m_lblSpecials->setColor(ccc3(208, 214, 226));
            m_hudCard->addChild(m_lblSpecials, 2);
            y -= 13.0f;
        }

        m_lblPercents = nullptr;
        if (m_cfg.showPercentages) {
            m_lblPercents = CCLabelBMFont::create("FP: 0.0%  |  Est: 100.0%  |  TPS: 240", "chatFont.fnt");
            m_lblPercents->setAnchorPoint({ 0.0f, 0.5f });
            m_lblPercents->setScale(0.60f);
            m_lblPercents->setPosition({ 10.0f, y });
            m_lblPercents->setColor(ccc3(175, 220, 255));
            m_hudCard->addChild(m_lblPercents, 2);
        }

        m_hudCard->setVisible(m_cfg.enabled && m_cfg.showHud);
    }

    void buildBanner() {
        if (m_bannerRoot) {
            m_bannerRoot->removeFromParent();
            m_bannerRoot = nullptr;
        }
        m_bannerLabels.clear();

        if (!m_cfg.enabled || !m_cfg.showBanner) {
            m_bannerActive = false;
            return;
        }

        bool hasPrecision = m_cfg.showRawPrecision && !m_cfg.rawPrecisionText.empty();
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        float bw = 270.0f;
        float bh = hasPrecision ? 72.0f : 58.0f;

        m_bannerRoot = CCNode::create();
        m_bannerRoot->setPosition({ winSize.width * 0.5f - bw * 0.5f, winSize.height - bh - 12.0f });
        this->addChild(m_bannerRoot, 20);

        m_bannerBg = CCScale9Sprite::create("square02_001.png");
        m_bannerBg->setOpacity(175);
        m_bannerBg->setContentSize({ bw, bh });
        m_bannerBg->setAnchorPoint({ 0.0f, 0.0f });
        m_bannerRoot->addChild(m_bannerBg, 0);

        // Line 1: Level Name (+ optional custom comment ONLY if non-empty)
        std::string titleText = m_levelName.empty() ? "Unnamed Level" : m_levelName;
        if (!m_cfg.bannerComment.empty()) {
            titleText += "  -  " + m_cfg.bannerComment;
        }
        auto* lblTitle = CCLabelBMFont::create(titleText.c_str(), "bigFont.fnt");
        lblTitle->setAnchorPoint({ 0.5f, 0.5f });
        lblTitle->setScale(0.36f);
        lblTitle->limitLabelWidth(bw - 20.0f, 0.36f, 0.18f);
        lblTitle->setPosition({ bw * 0.5f, bh - 14.0f });
        lblTitle->setColor(ccc3(255, 255, 255));
        m_bannerRoot->addChild(lblTitle, 2);
        m_bannerLabels.push_back(lblTitle);

        // Line 2: Frame Counter | FPS | 60Hz scale
        double ref60Frames = static_cast<double>(m_cfg.refFps) / 60.0;
        std::string modeLabel = (m_cfg.mode == CounterMode::Manual) ? "MANUAL" : "AUTO";
        std::string metaStr = fmt::format(
            "Frame Counter ({})  |  {} FPS  |  {} (60Hz)",
            modeLabel, m_cfg.refFps, formatFrames(ref60Frames)
        );
        auto* lblMeta = CCLabelBMFont::create(metaStr.c_str(), "chatFont.fnt");
        lblMeta->setAnchorPoint({ 0.5f, 0.5f });
        lblMeta->setScale(0.62f);
        lblMeta->setPosition({ bw * 0.5f, bh - 30.0f });
        lblMeta->setColor(ccc3(195, 224, 255));
        m_bannerRoot->addChild(lblMeta, 2);
        m_bannerLabels.push_back(lblMeta);

        float yCursor = bh - 44.0f;

        if (hasPrecision) {
            auto* lblPrec = CCLabelBMFont::create(m_cfg.rawPrecisionText.c_str(), "chatFont.fnt");
            lblPrec->setAnchorPoint({ 0.5f, 0.5f });
            lblPrec->setScale(0.58f);
            lblPrec->setPosition({ bw * 0.5f, yCursor });
            lblPrec->setColor(ccc3(205, 210, 220));
            m_bannerRoot->addChild(lblPrec, 2);
            m_bannerLabels.push_back(lblPrec);
            yCursor -= 14.0f;
        }

        // Bottom legend line adapted to current Ref FPS
        int yellowThresh = std::max(1, static_cast<int>(std::ceil((12.50 / 1000.0) * static_cast<double>(m_cfg.refFps))) + 1);
        int redThresh = std::max(1, static_cast<int>(std::ceil((4.17 / 1000.0) * static_cast<double>(m_cfg.refFps))));
        std::string legendYellow = fmt::format("<{}F: ~80Hz FP (8.33-12.50ms)", yellowThresh);
        std::string legendRed = fmt::format("<={}F: 240Hz+", redThresh);

        auto* lblYellow = CCLabelBMFont::create(legendYellow.c_str(), "chatFont.fnt");
        lblYellow->setAnchorPoint({ 0.0f, 0.5f });
        lblYellow->setScale(0.58f);
        lblYellow->setPosition({ 12.0f, yCursor - 1.0f });
        lblYellow->setColor(ccc3(255, 224, 66));
        m_bannerRoot->addChild(lblYellow, 2);
        m_bannerLabels.push_back(lblYellow);

        auto* lblRed = CCLabelBMFont::create(legendRed.c_str(), "chatFont.fnt");
        lblRed->setAnchorPoint({ 1.0f, 0.5f });
        lblRed->setScale(0.58f);
        lblRed->setPosition({ bw - 12.0f, yCursor - 1.0f });
        lblRed->setColor(ccc3(255, 88, 98));
        m_bannerRoot->addChild(lblRed, 2);
        m_bannerLabels.push_back(lblRed);

        m_bannerTimer = 0.0f;
        m_bannerActive = true;
    }

    void refreshHudTexts() {
        if (!m_hudCard) return;

        if (m_stats.hasLastClick) {
            auto tier = evaluateTier(m_stats.lastFrames, static_cast<double>(m_cfg.refFps));
            m_lblBigFrame->setString(formatFrames(m_stats.lastFrames).c_str());
            m_lblBigFrame->setColor(tier.color3);

            std::string kindTag = clickKindShort(m_stats.lastKind);
            std::string meta = fmt::format(
                "{:.0f}Hz | {:.2f}ms{}",
                std::min(m_stats.lastHz, 9999.0),
                m_stats.lastMs,
                kindTag.empty() ? "" : (" " + kindTag)
            );
            m_lblMeta->setString(meta.c_str());
            m_lblMeta->setColor(tier.color3);
        } else {
            m_lblBigFrame->setString("--F");
            m_lblBigFrame->setColor(ccc3(242, 244, 248));
            std::string modeTxt = (m_cfg.mode == CounterMode::Manual) ? "MANUAL" : "AUTO";
            m_lblMeta->setString(modeTxt.c_str());
            m_lblMeta->setColor(ccc3(190, 198, 210));
        }

        double avgF = (m_stats.totalClicks > 0) ? (m_stats.sumFrames / static_cast<double>(m_stats.totalClicks)) : 0.0;
        std::string countsStr = (m_stats.totalClicks > 0)
            ? fmt::format("Clicks: {}  |  FP: {}  |  Avg: {}", m_stats.totalClicks, m_stats.fpClicks, formatFrames(avgF))
            : "Clicks: 0  |  FP: 0  |  Avg: --";
        m_lblCounts->setString(countsStr.c_str());

        if (m_lblSpecials) {
            std::string specStr = fmt::format(
                "S-Fr: {}  |  Swifts: {}  |  Multi: {}",
                m_stats.sFrames, m_stats.swifts, m_stats.multiclicks
            );
            m_lblSpecials->setString(specStr.c_str());
        }

        if (m_lblPercents) {
            double fpPct = (m_stats.totalClicks > 0)
                ? (static_cast<double>(m_stats.fpClicks) * 100.0 / static_cast<double>(m_stats.totalClicks))
                : 0.0;
            double probPct = m_stats.passProb * 100.0;
            std::string pctStr = fmt::format(
                "FP: {:.1f}%  |  Est: {:.1f}%  |  TPS: {:.0f}",
                fpPct, probPct, m_stats.liveTps
            );
            m_lblPercents->setString(pctStr.c_str());
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

        // Attach click circle container DIRECTLY into m_objectLayer (world space)
        // so circles appear right on the player and stay pinned at the exact click spot!
        m_worldContainer = CCNode::create();
        if (worldParent) {
            worldParent->addChild(m_worldContainer, 1500);
        } else {
            this->addChild(m_worldContainer, 5);
        }

        buildHudCard();
        buildBanner();
        refreshHudTexts();
        return true;
    }

    void reloadConfig() {
        m_cfg = ModConfig::load();
        buildHudCard();
        buildBanner();
        refreshHudTexts();
    }

    void resetAttempt() {
        m_stats.reset();
        clearCircles();
        m_cfg = ModConfig::load();
        buildBanner();
        refreshHudTexts();
    }

    void setLiveTps(double tps) {
        m_stats.liveTps = std::clamp(tps, 1.0, 100000.0);
        if (m_lblPercents && m_cfg.enabled && m_cfg.showHud) {
            double fpPct = (m_stats.totalClicks > 0)
                ? (static_cast<double>(m_stats.fpClicks) * 100.0 / static_cast<double>(m_stats.totalClicks))
                : 0.0;
            double probPct = m_stats.passProb * 100.0;
            std::string pctStr = fmt::format(
                "FP: {:.1f}%  |  Est: {:.1f}%  |  TPS: {:.0f}",
                fpPct, probPct, m_stats.liveTps
            );
            m_lblPercents->setString(pctStr.c_str());
        }
    }

    void clearCircles() {
        for (auto& c : m_circles) {
            if (c.ringSprite) c.ringSprite->removeFromParent();
            if (c.label) c.label->removeFromParent();
        }
        m_circles.clear();
    }

    // levelPos is player->getPosition() inside m_objectLayer!
    void onClickTriggered(CCPoint levelPos, double frames, ClickKind kind) {
        if (!m_cfg.enabled) return;

        auto tier = evaluateTier(frames, static_cast<double>(m_cfg.refFps));
        m_stats.recordClick(frames, static_cast<double>(m_cfg.refFps), kind, tier.isFramePerfect);
        refreshHudTexts();

        if (!m_cfg.showCircles || !m_worldContainer) return;

        if (m_circles.size() > 64) {
            auto& oldest = m_circles.front();
            if (oldest.ringSprite) oldest.ringSprite->removeFromParent();
            if (oldest.label) oldest.label->removeFromParent();
            m_circles.erase(m_circles.begin());
        }

        ActiveWorldCircle ac;
        ac.origin = levelPos;
        ac.age = 0.0f;
        ac.lifetime = 0.65f;
        ac.baseScale = std::clamp(m_cfg.circleScale, 0.4f, 2.5f);

        ac.ringSprite = createRingSprite();
        if (ac.ringSprite) {
            float texW = std::max(16.0f, ac.ringSprite->getContentSize().width);
            float initWorldDiameter = 34.0f * ac.baseScale; // slightly larger than the 30x30 player cube!
            ac.ringSprite->setPosition(levelPos);
            ac.ringSprite->setColor(tier.color3);
            ac.ringSprite->setOpacity(245);
            ac.ringSprite->setScale(initWorldDiameter / texW);
            m_worldContainer->addChild(ac.ringSprite, 10);
        }

        if (m_cfg.showCircleLabel) {
            std::string tag = clickKindShort(kind);
            std::string text = tag.empty()
                ? formatFrames(frames)
                : fmt::format("{} {}", formatFrames(frames), tag);
            ac.label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            ac.label->setScale(0.32f * std::min(1.4f, ac.baseScale));
            ac.label->setColor(tier.color3);
            ac.label->setOpacity(255);
            ac.label->setPosition(ccp(levelPos.x, levelPos.y + 22.0f * ac.baseScale));
            m_worldContainer->addChild(ac.label, 11);
        }

        m_circles.push_back(ac);
    }

    // Driven purely by game dt -> synced in slow-mo, bots, frame-stepping, and renders
    void stepSimulation(float dt) {
        if (!m_cfg.enabled || dt <= 0.0f) return;

        for (size_t i = 0; i < m_circles.size(); ) {
            auto& c = m_circles[i];
            c.age += dt;
            if (c.age >= c.lifetime) {
                if (c.ringSprite) c.ringSprite->removeFromParent();
                if (c.label) c.label->removeFromParent();
                m_circles.erase(m_circles.begin() + static_cast<ptrdiff_t>(i));
                continue;
            }

            float t = std::clamp(c.age / c.lifetime, 0.0f, 1.0f);
            float easeOut = 1.0f - (1.0f - t) * (1.0f - t);
            float alpha = 1.0f - t * t;

            if (c.ringSprite) {
                float texW = std::max(16.0f, c.ringSprite->getContentSize().width);
                float curDiameter = (34.0f + 28.0f * easeOut) * c.baseScale;
                c.ringSprite->setScale(curDiameter / texW);
                c.ringSprite->setOpacity(static_cast<GLubyte>(std::clamp(alpha * 245.0f, 0.0f, 245.0f)));
            }
            if (c.label) {
                c.label->setPosition(ccp(c.origin.x, c.origin.y + (22.0f + 14.0f * easeOut) * c.baseScale));
                c.label->setOpacity(static_cast<GLubyte>(std::clamp(alpha * 255.0f, 0.0f, 255.0f)));
            }
            ++i;
        }

        if (m_bannerActive && m_bannerRoot) {
            m_bannerTimer += dt;
            float holdEnd = std::max(0.5f, m_cfg.bannerDuration);
            float fadeDur = 0.60f;

            if (m_bannerTimer >= holdEnd + fadeDur) {
                m_bannerRoot->setVisible(false);
                m_bannerActive = false;
            } else if (m_bannerTimer > holdEnd) {
                float p = 1.0f - std::clamp((m_bannerTimer - holdEnd) / fadeDur, 0.0f, 1.0f);
                if (m_bannerBg) m_bannerBg->setOpacity(static_cast<GLubyte>(p * 175.0f));
                GLubyte lblOp = static_cast<GLubyte>(p * 255.0f);
                for (auto* lbl : m_bannerLabels) {
                    if (lbl) lbl->setOpacity(lblOp);
                }
            }
        }
    }
};

} // namespace fwc
