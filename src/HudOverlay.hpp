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
    double passProb = 1.0; // cumulative consistency estimate (0..1)
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

        // Smooth human-timing pass probability estimate per click:
        // Wide open clicks (> 25ms) ~ 99.6%, 60Hz (~16.6ms) ~ 97.5%, 120Hz (~8.3ms) ~ 90%, 240Hz (~4.1ms) ~ 76%, <1ms ~ 40%
        double pClick = std::clamp(lastMs / (lastMs + 1.35), 0.25, 0.996);
        passProb = std::max(0.0001, passProb * pClick);
    }
};

struct ActiveCircle {
    CCDrawNode* ringNode = nullptr;
    CCLabelBMFont* label = nullptr;
    CCPoint origin;
    ccColor4F baseColor;
    float age = 0.0f;
    float lifetime = 0.58f;
    float scale = 1.0f;
};

class HudOverlay : public CCLayer {
private:
    ModConfig m_cfg;
    std::string m_levelName;
    RunStats m_stats;

    // Layers
    CCNode* m_circleContainer = nullptr;
    std::vector<ActiveCircle> m_circles;

    // Bottom-left HUD Card
    CCNode* m_hudCard = nullptr;
    CCScale9Sprite* m_hudBg = nullptr;
    CCDrawNode* m_hudFrame = nullptr;
    CCLabelBMFont* m_lblBigFrame = nullptr;
    CCLabelBMFont* m_lblMeta = nullptr;
    CCLabelBMFont* m_lblCounts = nullptr;
    CCLabelBMFont* m_lblSpecials = nullptr;
    CCLabelBMFont* m_lblPercents = nullptr;
    ccColor4F m_hudAccentColor = ccc4f(0.22f, 0.74f, 0.97f, 1.0f);

    // Intro Showcase Banner
    CCNode* m_bannerRoot = nullptr;
    CCScale9Sprite* m_bannerBg = nullptr;
    CCDrawNode* m_bannerDecor = nullptr;
    std::vector<CCLabelBMFont*> m_bannerLabels;
    float m_bannerTimer = 0.0f;
    bool m_bannerActive = false;

    static void drawRing(CCDrawNode* node, float radius, float thickness, ccColor4F strokeColor, ccColor4F fillColor) {
        constexpr int kSegs = 36;
        CCPoint verts[kSegs];
        for (int i = 0; i < kSegs; ++i) {
            float angle = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(kSegs);
            verts[i] = ccp(std::cos(angle) * radius, std::sin(angle) * radius);
        }
        node->clear();
        node->drawPolygon(verts, kSegs, fillColor, thickness, strokeColor);
    }

    static void drawCardBorder(CCDrawNode* node, float w, float h, ccColor4F accentColor) {
        node->clear();
        ccColor4F borderCol = ccc4f(0.24f, 0.30f, 0.40f, 0.85f);
        CCPoint rect[4] = { ccp(0, 0), ccp(w, 0), ccp(w, h), ccp(0, h) };
        node->drawPolygon(rect, 4, ccc4f(0, 0, 0, 0), 0.75f, borderCol);

        // Crisp left vertical accent stripe
        CCPoint bar[4] = { ccp(0, 0), ccp(3.2f, 0), ccp(3.2f, h), ccp(0, h) };
        node->drawPolygon(bar, 4, accentColor, 0.0f, accentColor);

        // Subtle horizontal divider under header
        node->drawSegment(ccp(8.0f, h - 24.0f), ccp(w - 8.0f, h - 24.0f), 0.45f, ccc4f(0.25f, 0.32f, 0.44f, 0.65f));
    }

    void buildHudCard() {
        if (m_hudCard) {
            m_hudCard->removeFromParent();
            m_hudCard = nullptr;
        }

        m_hudCard = CCNode::create();
        m_hudCard->setPosition(ccp(10.0f, 10.0f));
        this->addChild(m_hudCard, 15);

        float cardW = 196.0f;
        float cardH = 66.0f;
        if (m_cfg.showSpecialStats) cardH += 13.0f;
        if (m_cfg.showPercentages) cardH += 14.0f;

        m_hudBg = CCScale9Sprite::create("square02b_001.png");
        m_hudBg->setColor(ccc3(14, 17, 24));
        m_hudBg->setOpacity(225);
        m_hudBg->setContentSize({ cardW, cardH });
        m_hudBg->setAnchorPoint({ 0.0f, 0.0f });
        m_hudBg->setPosition({ 0.0f, 0.0f });
        m_hudCard->addChild(m_hudBg, 0);

        m_hudFrame = CCDrawNode::create();
        drawCardBorder(m_hudFrame, cardW, cardH, m_hudAccentColor);
        m_hudCard->addChild(m_hudFrame, 1);

        // Top headline: Frame count + Hz/ms metadata
        m_lblBigFrame = CCLabelBMFont::create("--F", "bigFont.fnt");
        m_lblBigFrame->setAnchorPoint({ 0.0f, 0.5f });
        m_lblBigFrame->setScale(0.42f);
        m_lblBigFrame->setPosition({ 10.0f, cardH - 12.5f });
        m_lblBigFrame->setColor(ccc3(240, 244, 252));
        m_hudCard->addChild(m_lblBigFrame, 2);

        m_lblMeta = CCLabelBMFont::create("READY  |  AUTO", "chatFont.fnt");
        m_lblMeta->setAnchorPoint({ 1.0f, 0.5f });
        m_lblMeta->setScale(0.60f);
        m_lblMeta->setPosition({ cardW - 8.0f, cardH - 12.5f });
        m_lblMeta->setColor(ccc3(175, 188, 210));
        m_hudCard->addChild(m_lblMeta, 2);

        float y = cardH - 34.0f;

        m_lblCounts = CCLabelBMFont::create("Clicks: 0   FP: 0   Avg: --F", "chatFont.fnt");
        m_lblCounts->setAnchorPoint({ 0.0f, 0.5f });
        m_lblCounts->setScale(0.62f);
        m_lblCounts->setPosition({ 10.0f, y });
        m_lblCounts->setColor(ccc3(226, 232, 244));
        m_hudCard->addChild(m_lblCounts, 2);
        y -= 13.5f;

        m_lblSpecials = nullptr;
        if (m_cfg.showSpecialStats) {
            m_lblSpecials = CCLabelBMFont::create("S-Fr: 0   Swifts: 0   Multi: 0", "chatFont.fnt");
            m_lblSpecials->setAnchorPoint({ 0.0f, 0.5f });
            m_lblSpecials->setScale(0.58f);
            m_lblSpecials->setPosition({ 10.0f, y });
            m_lblSpecials->setColor(ccc3(186, 200, 224));
            m_hudCard->addChild(m_lblSpecials, 2);
            y -= 13.5f;
        }

        m_lblPercents = nullptr;
        if (m_cfg.showPercentages) {
            m_lblPercents = CCLabelBMFont::create("FP: 0.0%   Est: 100.0%   TPS: 240", "chatFont.fnt");
            m_lblPercents->setAnchorPoint({ 0.0f, 0.5f });
            m_lblPercents->setScale(0.58f);
            m_lblPercents->setPosition({ 10.0f, y });
            m_lblPercents->setColor(ccc3(125, 211, 252));
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

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        float bw = 278.0f;
        float bh = m_cfg.showRawPrecision ? 82.0f : 69.0f;

        m_bannerRoot = CCNode::create();
        m_bannerRoot->setPosition({ winSize.width * 0.5f - bw * 0.5f, winSize.height - bh - 12.0f });
        this->addChild(m_bannerRoot, 20);

        m_bannerBg = CCScale9Sprite::create("square02b_001.png");
        m_bannerBg->setColor(ccc3(13, 16, 23));
        m_bannerBg->setOpacity(232);
        m_bannerBg->setContentSize({ bw, bh });
        m_bannerBg->setAnchorPoint({ 0.0f, 0.0f });
        m_bannerRoot->addChild(m_bannerBg, 0);
        
        m_bannerDecor = CCDrawNode::create();
        CCPoint rect[4] = { ccp(0, 0), ccp(bw, 0), ccp(bw, bh), ccp(0, bh) };
        m_bannerDecor->drawPolygon(rect, 4, ccc4f(0, 0, 0, 0), 0.8f, ccc4f(0.28f, 0.36f, 0.48f, 0.9f));
        // Sleek top accent line
        CCPoint topLine[4] = { ccp(0, bh - 2.4f), ccp(bw, bh - 2.4f), ccp(bw, bh), ccp(0, bh) };
        m_bannerDecor->drawPolygon(topLine, 4, ccc4f(0.98f, 0.80f, 0.08f, 0.95f), 0.0f, ccc4f(0.98f, 0.80f, 0.08f, 0.95f));
        m_bannerRoot->addChild(m_bannerDecor, 1);

        // Line 1: Level Name + Custom Comment
        std::string titleText = m_levelName.empty() ? "Unnamed Level" : m_levelName;
        if (!m_cfg.bannerComment.empty()) {
            titleText += "  |  " + m_cfg.bannerComment;
        }
        auto* lblTitle = CCLabelBMFont::create(titleText.c_str(), "bigFont.fnt");
        lblTitle->setAnchorPoint({ 0.0f, 0.5f });
        lblTitle->setScale(0.35f);
        if (lblTitle->getScaledContentSize().width > bw - 20.0f) {
            lblTitle->limitLabelWidth(bw - 20.0f, 0.35f, 0.18f);
        }
        lblTitle->setPosition({ 10.0f, bh - 14.0f });
        lblTitle->setColor(ccc3(244, 247, 255));
        m_bannerRoot->addChild(lblTitle, 2);
        m_bannerLabels.push_back(lblTitle);

        // Line 2: Frame Counter | Ref FPS | 60Hz Frame Scale (e.g. 100F @ 6000 FPS)
        double ref60Frames = static_cast<double>(m_cfg.refFps) / 60.0;
        std::string modeLabel = (m_cfg.mode == CounterMode::Manual) ? "MANUAL" : "AUTO";
        std::string metaStr = fmt::format(
            "Frame Counter [{}]   |   {} FPS   |   {} (60Hz)",
            modeLabel, m_cfg.refFps, formatFrames(ref60Frames)
        );
        auto* lblMeta = CCLabelBMFont::create(metaStr.c_str(), "chatFont.fnt");
        lblMeta->setAnchorPoint({ 0.0f, 0.5f });
        lblMeta->setScale(0.62f);
        lblMeta->setPosition({ 10.0f, bh - 29.0f });
        lblMeta->setColor(ccc3(148, 210, 255));
        m_bannerRoot->addChild(lblMeta, 2);
        m_bannerLabels.push_back(lblMeta);

        float yCursor = bh - 43.0f;

        // Optional Line 3: Raw Precision text
        if (m_cfg.showRawPrecision && !m_cfg.rawPrecisionText.empty()) {
            auto* lblPrec = CCLabelBMFont::create(m_cfg.rawPrecisionText.c_str(), "chatFont.fnt");
            lblPrec->setAnchorPoint({ 0.0f, 0.5f });
            lblPrec->setScale(0.58f);
            lblPrec->setPosition({ 10.0f, yCursor });
            lblPrec->setColor(ccc3(172, 184, 206));
            m_bannerRoot->addChild(lblPrec, 2);
            m_bannerLabels.push_back(lblPrec);
            yCursor -= 14.0f;
        }

        // Bottom Legend Line: Exact Yellow tier threshold for current Ref FPS (e.g. <76F @ 6000 FPS)
        int yellowThresh = static_cast<int>(std::ceil((12.50 / 1000.0) * static_cast<double>(m_cfg.refFps))) + 1;
        int redThresh = static_cast<int>(std::ceil((4.17 / 1000.0) * static_cast<double>(m_cfg.refFps)));
        std::string legendYellow = fmt::format("<{}F: ~80Hz FP (8.33-12.50ms)", yellowThresh);
        std::string legendRed = fmt::format("<{}F: 240Hz+", std::max(1, redThresh));

        auto* lblYellow = CCLabelBMFont::create(legendYellow.c_str(), "chatFont.fnt");
        lblYellow->setAnchorPoint({ 0.0f, 0.5f });
        lblYellow->setScale(0.58f);
        lblYellow->setPosition({ 10.0f, yCursor - 2.0f });
        lblYellow->setColor(ccc3(250, 204, 21));
        m_bannerRoot->addChild(lblYellow, 2);
        m_bannerLabels.push_back(lblYellow);

        auto* lblRed = CCLabelBMFont::create(legendRed.c_str(), "chatFont.fnt");
        lblRed->setAnchorPoint({ 1.0f, 0.5f });
        lblRed->setScale(0.58f);
        lblRed->setPosition({ bw - 10.0f, yCursor - 2.0f });
        lblRed->setColor(ccc3(244, 63, 94));
        m_bannerRoot->addChild(lblRed, 2);
        m_bannerLabels.push_back(lblRed);

        m_bannerTimer = 0.0f;
        m_bannerActive = true;
    }

    void refreshHudTexts() {
        if (!m_hudCard) return;

        if (m_stats.hasLastClick) {
            auto tier = evaluateTier(m_stats.lastFrames, static_cast<double>(m_cfg.refFps));
            m_hudAccentColor = tier.color4;

            std::string frameStr = formatFrames(m_stats.lastFrames);
            m_lblBigFrame->setString(frameStr.c_str());
            m_lblBigFrame->setColor(tier.color3);

            std::string kindTag = clickKindShort(m_stats.lastKind);
            std::string meta = fmt::format(
                "~{:.0f}Hz  |  {:.2f}ms{}",
                std::min(m_stats.lastHz, 9999.0),
                m_stats.lastMs,
                kindTag.empty() ? "" : ("  [" + kindTag + "]")
            );
            m_lblMeta->setString(meta.c_str());
            m_lblMeta->setColor(tier.color3);
        } else {
            m_hudAccentColor = ccc4f(0.22f, 0.74f, 0.97f, 1.0f);
            m_lblBigFrame->setString("--F");
            m_lblBigFrame->setColor(ccc3(235, 240, 250));
            std::string modeTxt = (m_cfg.mode == CounterMode::Manual) ? "READY  |  MANUAL" : "READY  |  AUTO";
            m_lblMeta->setString(modeTxt.c_str());
            m_lblMeta->setColor(ccc3(165, 180, 205));
        }

        if (m_hudBg && m_hudFrame) {
            auto sz = m_hudBg->getContentSize();
            drawCardBorder(m_hudFrame, sz.width, sz.height, m_hudAccentColor);
        }

        double avgF = (m_stats.totalClicks > 0) ? (m_stats.sumFrames / static_cast<double>(m_stats.totalClicks)) : 0.0;
        std::string countsStr = (m_stats.totalClicks > 0)
            ? fmt::format("Clicks: {}   FP: {}   Avg: {}", m_stats.totalClicks, m_stats.fpClicks, formatFrames(avgF))
            : "Clicks: 0   FP: 0   Avg: --F";
        m_lblCounts->setString(countsStr.c_str());

        if (m_lblSpecials) {
            std::string specStr = fmt::format(
                "S-Fr: {}   Swifts: {}   Multi: {}",
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
                "FP: {:.1f}%   Est: {:.1f}%   TPS: {:.0f}",
                fpPct, probPct, m_stats.liveTps
            );
            m_lblPercents->setString(pctStr.c_str());
        }
    }

public:
    static HudOverlay* create(std::string const& levelName) {
        auto* ret = new HudOverlay();
        if (ret && ret->initOverlay(levelName)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool initOverlay(std::string const& levelName) {
        if (!CCLayer::init()) return false;
        m_levelName = levelName;
        m_cfg = ModConfig::load();

        m_circleContainer = CCNode::create();
        this->addChild(m_circleContainer, 5);

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
                "FP: {:.1f}%   Est: {:.1f}%   TPS: {:.0f}",
                fpPct, probPct, m_stats.liveTps
            );
            m_lblPercents->setString(pctStr.c_str());
        }
    }

    void clearCircles() {
        for (auto& c : m_circles) {
            if (c.ringNode) c.ringNode->removeFromParent();
            if (c.label) c.label->removeFromParent();
        }
        m_circles.clear();
    }

    void onClickTriggered(CCPoint screenPos, double frames, ClickKind kind) {
        if (!m_cfg.enabled) return;

        auto tier = evaluateTier(frames, static_cast<double>(m_cfg.refFps));
        m_stats.recordClick(frames, static_cast<double>(m_cfg.refFps), kind, tier.isFramePerfect);
        refreshHudTexts();

        if (!m_cfg.showCircles || !m_circleContainer) return;

        // Keep circle pool clean under intense spam
        if (m_circles.size() > 48) {
            auto& oldest = m_circles.front();
            if (oldest.ringNode) oldest.ringNode->removeFromParent();
            if (oldest.label) oldest.label->removeFromParent();
            m_circles.erase(m_circles.begin());
        }

        ActiveCircle ac;
        ac.origin = screenPos;
        ac.baseColor = tier.color4;
        ac.age = 0.0f;
        ac.lifetime = 0.56f;
        ac.scale = std::clamp(m_cfg.circleScale, 0.4f, 2.5f);

        ac.ringNode = CCDrawNode::create();
        ac.ringNode->setPosition(screenPos);
        drawRing(
            ac.ringNode,
            10.0f * ac.scale,
            1.8f,
            ac.baseColor,
            ccc4f(ac.baseColor.r, ac.baseColor.g, ac.baseColor.b, 0.18f)
        );
        m_circleContainer->addChild(ac.ringNode, 1);

        if (m_cfg.showCircleLabel) {
            std::string tag = clickKindShort(kind);
            std::string text = tag.empty()
                ? formatFrames(frames)
                : fmt::format("{} {}", formatFrames(frames), tag);
            ac.label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
            ac.label->setScale(0.64f * std::min(1.3f, ac.scale));
            ac.label->setColor(tier.color3);
            ac.label->setPosition(ccp(screenPos.x, screenPos.y + 18.0f * ac.scale));
            m_circleContainer->addChild(ac.label, 2);
        }

        m_circles.push_back(ac);
    }

    // Driven strictly by game delta time (dt) -> 100% synced during bot slow-mo & renders!
    void stepSimulation(float dt) {
        if (!m_cfg.enabled || dt <= 0.0f) return;

        // Animate active click circles
        for (size_t i = 0; i < m_circles.size(); ) {
            auto& c = m_circles[i];
            c.age += dt;
            if (c.age >= c.lifetime) {
                if (c.ringNode) c.ringNode->removeFromParent();
                if (c.label) c.label->removeFromParent();
                m_circles.erase(m_circles.begin() + static_cast<ptrdiff_t>(i));
                continue;
            }

            float t = std::clamp(c.age / c.lifetime, 0.0f, 1.0f);
            float easeOut = 1.0f - (1.0f - t) * (1.0f - t);
            float alpha = 1.0f - t;
            float radius = (10.0f + 22.0f * easeOut) * c.scale;

            if (c.ringNode) {
                ccColor4F stroke = ccc4f(c.baseColor.r, c.baseColor.g, c.baseColor.b, alpha * 0.95f);
                ccColor4F fill = ccc4f(c.baseColor.r, c.baseColor.g, c.baseColor.b, alpha * 0.16f);
                drawRing(c.ringNode, radius, 1.6f * (1.0f - 0.45f * t), stroke, fill);
            }
            if (c.label) {
                c.label->setPosition(ccp(c.origin.x, c.origin.y + (18.0f + 16.0f * easeOut) * c.scale));
                c.label->setOpacity(static_cast<GLubyte>(std::clamp(alpha * 255.0f, 0.0f, 255.0f)));
            }
            ++i;
        }

        // Animate intro showcase banner fade-out on game time
        if (m_bannerActive && m_bannerRoot) {
            m_bannerTimer += dt;
            float holdEnd = std::max(0.5f, m_cfg.bannerDuration);
            float fadeDur = 0.65f;

            if (m_bannerTimer >= holdEnd + fadeDur) {
                m_bannerRoot->setVisible(false);
                m_bannerActive = false;
            } else if (m_bannerTimer > holdEnd) {
                float p = 1.0f - std::clamp((m_bannerTimer - holdEnd) / fadeDur, 0.0f, 1.0f);
                GLubyte op = static_cast<GLubyte>(p * 232.0f);
                GLubyte lblOp = static_cast<GLubyte>(p * 255.0f);
                if (m_bannerBg) m_bannerBg->setOpacity(op);
                for (auto* lbl : m_bannerLabels) {
                    if (lbl) lbl->setOpacity(lblOp);
                }
                if (m_bannerDecor) {
                    m_bannerDecor->setVisible(p > 0.18f);
                }
            }
        }
    }
};

} // namespace fwc
