#pragma once

#include "AudioManager.hpp"
#include "Config.hpp"
#include "InternalRenderer.hpp"
#include "MacroStore.hpp"
#include "MarkerStore.hpp"
#include "TierSoundStore.hpp"
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace fwc {

using namespace geode::prelude;

class SettingsPopup : public FLAlertLayer {
private:
    ModConfig m_cfg;
    std::function<void()> m_onChanged;
    std::function<void()> m_onStartExactScan;
    std::function<void()> m_onWatchMacro;
    std::function<void()> m_onToggleRecordMacro;
    std::function<void()> m_onToggleRender;
    std::string m_levelName;
    std::vector<DiscoveredMacro> m_discoveredMacros;
    int m_macroPage = 0;
    size_t m_recordedRunClicks = 0;
    bool m_isRenderingNow = false;
    bool m_isRecordingNow = false;
    float m_currentPlayerX = 0.0f;
    float m_currentPlayerY = 30.0f;

    int m_activeTab = 0; // 0=Counter, 1=Macro, 2=Look, 3=Settings, 4=EditClicks
    int m_manualPage = 0;

    CCNode* m_panel = nullptr;
    CCNode* m_contentNode = nullptr;
    CCMenu* m_tabMenu = nullptr;

    static constexpr float kPanelW = 476.0f;
    static constexpr float kPanelH = 304.0f;

    void notifyLive() {
        m_cfg.save();
        if (m_onChanged) m_onChanged();
    }

    void applyAndRebuild() {
        notifyLive();
        rebuildContent();
    }

    CCLabelBMFont* addLabel(CCNode* parent, std::string const& text, float x, float y, float scale = 0.34f, const char* font = "bigFont.fnt", ccColor3B col = ccc3(255, 255, 255)) {
        auto* lbl = CCLabelBMFont::create(text.c_str(), font);
        lbl->setAnchorPoint({ 0.0f, 0.5f });
        lbl->setScale(scale);
        lbl->setColor(col);
        lbl->setPosition({ x, y });
        parent->addChild(lbl, 3);
        return lbl;
    }

    void addCheckboxRow(CCNode* parent, CCMenu* menu, std::string const& title, bool value, int tag, float x, float y) {
        auto* toggler = CCMenuItemToggler::createWithStandardSprites(
            this,
            menu_selector(SettingsPopup::onToggleBtn),
            0.62f
        );
        toggler->toggle(value);
        toggler->setTag(tag);
        toggler->setPosition({ x + 10.0f, y });
        menu->addChild(toggler);
        addLabel(parent, title, x + 24.0f, y, 0.31f, "bigFont.fnt", ccc3(242, 246, 252));
    }

    void addStepperRow(CCNode* parent, CCMenu* menu, std::string const& title, std::string const& valText, int minusTag, int plusTag, float x, float y, float rowW) {
        addLabel(parent, title, x, y, 0.31f, "bigFont.fnt", ccc3(242, 246, 252));
        float rightEdge = x + rowW;

        auto* rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        rSpr->setFlipX(true);
        rSpr->setScale(0.38f);
        auto* plusBtn = CCMenuItemSpriteExtra::create(rSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        plusBtn->setTag(plusTag);
        plusBtn->setPosition({ rightEdge - 9.0f, y });
        menu->addChild(plusBtn);

        auto* valLbl = CCLabelBMFont::create(valText.c_str(), "goldFont.fnt");
        valLbl->setScale(0.48f);
        valLbl->setPosition({ rightEdge - 42.0f, y });
        parent->addChild(valLbl, 3);

        auto* lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        lSpr->setScale(0.38f);
        auto* minusBtn = CCMenuItemSpriteExtra::create(lSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        minusBtn->setTag(minusTag);
        minusBtn->setPosition({ rightEdge - 75.0f, y });
        menu->addChild(minusBtn);
    }

    CCNode* makeBtn(const char* text, float w, float h, const char* bgTex, float fontScale = 0.30f) {
        auto* root = CCNode::create();
        root->setContentSize({ w, h });
        auto* bg = CCScale9Sprite::create(bgTex);
        bg->setContentSize({ w, h });
        bg->setPosition({ w * 0.5f, h * 0.5f });
        root->addChild(bg, 0);
        auto* lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(fontScale);
        lbl->setPosition({ w * 0.5f, h * 0.5f + 0.5f });
        root->addChild(lbl, 1);
        return root;
    }

    void rebuildTabs() {
        if (m_tabMenu) {
            m_tabMenu->removeFromParent();
            m_tabMenu = nullptr;
        }
        m_tabMenu = CCMenu::create();
        m_tabMenu->setPosition({ 0.0f, 0.0f });
        m_panel->addChild(m_tabMenu, 10);

        const char* names[4] = { "COUNTER", "MACRO", "LOOK", "SETTINGS" };
        float tabW = 104.0f;
        float gap = 8.0f;
        float totalW = 4 * tabW + 3 * gap;
        float startX = (kPanelW - totalW) * 0.5f + tabW * 0.5f;
        float tabY = kPanelH - 44.0f;

        for (int i = 0; i < 4; ++i) {
            bool active = (m_activeTab == i) || (m_activeTab == 4 && i == 0);
            auto* spr = makeBtn(names[i], tabW, 23.0f, active ? "GJ_button_01.png" : "GJ_button_04.png", 0.30f);
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onTabSelect));
            btn->setTag(i);
            btn->setPosition({ startX + i * (tabW + gap), tabY });
            m_tabMenu->addChild(btn);
        }

        auto* closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.80f);
        auto* closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(SettingsPopup::onCloseBtn));
        closeBtn->setPosition({ 6.0f, kPanelH - 6.0f });
        m_tabMenu->addChild(closeBtn);
    }

    void rebuildContent() {
        rebuildTabs();

        if (m_contentNode) {
            m_contentNode->removeFromParent();
            m_contentNode = nullptr;
        }
        m_contentNode = CCNode::create();
        m_panel->addChild(m_contentNode, 5);

        auto* innerBg = CCScale9Sprite::create("square02_001.png");
        innerBg->setOpacity(140);
        innerBg->setContentSize({ kPanelW - 24.0f, kPanelH - 76.0f });
        innerBg->setPosition({ kPanelW * 0.5f, 12.0f + (kPanelH - 76.0f) * 0.5f });
        m_contentNode->addChild(innerBg, 0);

        auto* menu = CCMenu::create();
        menu->setPosition({ 0.0f, 0.0f });
        m_contentNode->addChild(menu, 6);

        float leftX = 22.0f;
        float fullW = kPanelW - 44.0f;
        float topY = kPanelH - 78.0f;

        if (m_activeTab == 0) {
            // =================================================================
            // TAB 0: COUNTER (FB Frame Counter V2 2-column layout)
            // =================================================================
            auto const& activeMac = MacroStore::get().active();
            bool hasMac = !activeMac.empty();
            size_t availClicks = hasMac ? activeMac.clickCount() : m_recordedRunClicks;
            auto const& markers = MarkerStore::get().markers();
            bool isScanned = !markers.empty();

            // Status strip across top
            std::string lvlShort = m_levelName.empty() ? "LEVEL" : m_levelName;
            if (lvlShort.size() > 16) lvlShort = lvlShort.substr(0, 14) + "..";
            addLabel(m_contentNode, lvlShort, leftX, topY + 3.0f, 0.34f, "bigFont.fnt", ccc3(255, 255, 255));

            std::string depthTag = (m_cfg.analyzeDepth == "fast") ? "FAST (+-6)" : "DEEP (+-12)";
            std::string metaStr = fmt::format("{} CLICKS  {} FPS  {}", availClicks, m_cfg.refFps, depthTag);
            auto* metaLbl = CCLabelBMFont::create(metaStr.c_str(), "chatFont.fnt");
            metaLbl->setScale(0.56f);
            metaLbl->setColor(ccc3(205, 214, 228));
            metaLbl->setPosition({ kPanelW * 0.5f + 8.0f, topY + 3.0f });
            m_contentNode->addChild(metaLbl, 3);

            auto* pillBg = CCScale9Sprite::create(isScanned ? "GJ_button_01.png" : "GJ_button_04.png");
            pillBg->setContentSize({ 76.0f, 17.0f });
            pillBg->setPosition({ leftX + fullW - 38.0f, topY + 3.0f });
            m_contentNode->addChild(pillBg, 2);
            auto* pillLbl = CCLabelBMFont::create(isScanned ? "SCANNED" : "NOT SCANNED", "bigFont.fnt");
            pillLbl->setScale(0.22f);
            pillLbl->setPosition({ leftX + fullW - 38.0f, topY + 3.5f });
            m_contentNode->addChild(pillLbl, 3);

            // Left dark box: Live Tier Counts + Precision
            float cardW = 166.0f;
            float cardH = 190.0f;
            float cardY = 20.0f;
            auto* leftCard = CCScale9Sprite::create("square02_001.png");
            leftCard->setOpacity(155);
            leftCard->setContentSize({ cardW, cardH });
            leftCard->setPosition({ leftX + cardW * 0.5f, cardY + cardH * 0.5f });
            m_contentNode->addChild(leftCard, 1);

            auto tiers = buildTiers(m_cfg.tableStyle, static_cast<double>(m_cfg.refFps));
            std::vector<int> counts(tiers.size(), 0);
            double totalPrec = 0.0;
            double fpsD = static_cast<double>(std::max<int64_t>(30, m_cfg.refFps));
            for (auto const& mk : markers) {
                double df = resolveDisplayFrames(mk.frames, m_cfg.cbfMode);
                if (df > 12.01 && m_cfg.tableStyle == TableStyle::TopDown24) continue;
                int ti = findTierIndex(tiers, df);
                if (ti >= 0 && ti < static_cast<int>(counts.size())) counts[static_cast<size_t>(ti)]++;
                totalPrec += clickPrecisionWeight(framesToMs(df, fpsD));
            }

            float rowY = cardY + cardH - 16.0f;
            float rowStep = std::min(21.0f, (cardH - 36.0f) / static_cast<float>(std::max<size_t>(1, tiers.size())));
            for (int idx = static_cast<int>(tiers.size()) - 1; idx >= 0; --idx) {
                auto const& td = tiers[static_cast<size_t>(idx)];
                int cnt = counts[static_cast<size_t>(idx)];
                addLabel(m_contentNode, td.label, leftX + 12.0f, rowY, 0.34f, "bigFont.fnt", td.color);
                auto* numLbl = CCLabelBMFont::create(cnt > 0 ? fmt::format("{}", cnt).c_str() : "-", "bigFont.fnt");
                numLbl->setAnchorPoint({ 1.0f, 0.5f });
                numLbl->setScale(0.34f);
                numLbl->setColor(cnt > 0 ? td.color : ccc3(145, 152, 165));
                numLbl->setPosition({ leftX + cardW - 14.0f, rowY });
                m_contentNode->addChild(numLbl, 3);
                rowY -= rowStep;
            }

            auto* precLbl = CCLabelBMFont::create(fmt::format("PRECISION  {:.2f}", totalPrec).c_str(), "goldFont.fnt");
            precLbl->setScale(0.46f);
            precLbl->setPosition({ leftX + cardW * 0.5f, cardY + 12.0f });
            m_contentNode->addChild(precLbl, 3);

            // Right column: Big ANALYZE + WATCH/RENDER + EDIT CLICKS/CLEAR + compact controls
            float rX = leftX + cardW + 14.0f;
            float rW = fullW - cardW - 14.0f;
            float halfW = (rW - 8.0f) * 0.5f;

            // Row 1: Big Green ANALYZE button
            auto* analyzeBtn = CCMenuItemSpriteExtra::create(
                makeBtn("ANALYZE", rW, 28.0f, availClicks > 0 ? "GJ_button_01.png" : "GJ_button_04.png", 0.44f),
                this, menu_selector(SettingsPopup::onStartScanBtn)
            );
            analyzeBtn->setPosition({ rX + rW * 0.5f, cardY + cardH - 16.0f });
            menu->addChild(analyzeBtn);

            // Row 2: WATCH | RENDER
            auto* watchBtn = CCMenuItemSpriteExtra::create(
                makeBtn("WATCH", halfW, 23.0f, hasMac ? "GJ_button_02.png" : "GJ_button_04.png", 0.34f),
                this, menu_selector(SettingsPopup::onWatchMacroBtn)
            );
            watchBtn->setPosition({ rX + halfW * 0.5f, cardY + cardH - 46.0f });
            menu->addChild(watchBtn);

            auto* renderBtn = CCMenuItemSpriteExtra::create(
                makeBtn(m_isRenderingNow ? "STOP RENDER" : "RENDER", halfW, 23.0f, m_isRenderingNow ? "GJ_button_06.png" : "GJ_button_04.png", 0.32f),
                this, menu_selector(SettingsPopup::onToggleRenderBtn)
            );
            renderBtn->setPosition({ rX + halfW + 8.0f + halfW * 0.5f, cardY + cardH - 46.0f });
            menu->addChild(renderBtn);

            // Row 3: EDIT CLICKS | CLEAR
            auto* editBtn = CCMenuItemSpriteExtra::create(
                makeBtn("EDIT CLICKS", halfW, 22.0f, "GJ_button_04.png", 0.30f),
                this, menu_selector(SettingsPopup::onOpenEditClicksBtn)
            );
            editBtn->setPosition({ rX + halfW * 0.5f, cardY + cardH - 73.0f });
            menu->addChild(editBtn);

            auto* clrBtn = CCMenuItemSpriteExtra::create(
                makeBtn("CLEAR COUNTS", halfW, 22.0f, isScanned ? "GJ_button_06.png" : "GJ_button_04.png", 0.28f),
                this, menu_selector(SettingsPopup::onClearCountsBtn)
            );
            clrBtn->setPosition({ rX + halfW + 8.0f + halfW * 0.5f, cardY + cardH - 73.0f });
            menu->addChild(clrBtn);

            // Row 4: SCAN DEPTH (< FAST +-6 | DEEP +-12 >)
            bool isFast = (m_cfg.analyzeDepth == "fast");
            addLabel(m_contentNode, "SCAN:", rX, cardY + cardH - 103.0f, 0.32f, "goldFont.fnt");
            auto* fBtn = CCMenuItemSpriteExtra::create(
                makeBtn("FAST +-6", 80.0f, 20.0f, isFast ? "GJ_button_01.png" : "GJ_button_04.png", 0.27f),
                this, menu_selector(SettingsPopup::onAnalyzeDepthBtn)
            );
            fBtn->setTag(0);
            fBtn->setPosition({ rX + rW - 130.0f, cardY + cardH - 103.0f });
            menu->addChild(fBtn);

            auto* dBtn = CCMenuItemSpriteExtra::create(
                makeBtn("DEEP +-12", 84.0f, 20.0f, !isFast ? "GJ_button_01.png" : "GJ_button_04.png", 0.27f),
                this, menu_selector(SettingsPopup::onAnalyzeDepthBtn)
            );
            dBtn->setTag(1);
            dBtn->setPosition({ rX + rW - 42.0f, cardY + cardH - 103.0f });
            menu->addChild(dBtn);

            // Row 5: MODE (Auto / Manual) + Ref FPS
            bool isAuto = (m_cfg.mode == CounterMode::Auto);
            addLabel(m_contentNode, "MODE:", rX, cardY + cardH - 130.0f, 0.32f, "goldFont.fnt");
            auto* autoB = CCMenuItemSpriteExtra::create(
                makeBtn("AUTO", 54.0f, 19.0f, isAuto ? "GJ_button_01.png" : "GJ_button_04.png", 0.27f),
                this, menu_selector(SettingsPopup::onModeBtn)
            );
            autoB->setTag(0);
            autoB->setPosition({ rX + 82.0f, cardY + cardH - 130.0f });
            menu->addChild(autoB);

            auto* manB = CCMenuItemSpriteExtra::create(
                makeBtn("MANUAL", 62.0f, 19.0f, !isAuto ? "GJ_button_01.png" : "GJ_button_04.png", 0.27f),
                this, menu_selector(SettingsPopup::onModeBtn)
            );
            manB->setTag(1);
            manB->setPosition({ rX + 144.0f, cardY + cardH - 130.0f });
            menu->addChild(manB);

            auto* fpsIn = geode::TextInput::create(56.0f, "240", "bigFont.fnt");
            fpsIn->setFilter("0123456789");
            fpsIn->setMaxCharCount(5);
            fpsIn->setString(fmt::format("{}", m_cfg.refFps));
            fpsIn->setPosition({ rX + rW - 28.0f, cardY + cardH - 130.0f });
            fpsIn->setCallback([this](std::string const& str) {
                if (str.empty()) return;
                try {
                    int64_t val = std::stoll(str);
                    if (val >= 30 && val <= 100000) { m_cfg.refFps = val; notifyLive(); }
                } catch (...) {}
            });
            m_contentNode->addChild(fpsIn, 8);

            // Row 6: Checkboxes
            addCheckboxRow(m_contentNode, menu, "CBF Decimals", m_cfg.cbfMode, 101, rX, cardY + cardH - 157.0f);
            addCheckboxRow(m_contentNode, menu, "Enable Mod", m_cfg.enabled, 100, rX + halfW + 8.0f, cardY + cardH - 157.0f);

            // Macro hint at bottom right
            std::string macLine = hasMac
                ? fmt::format("Macro: [{}] {} ({}c)", activeMac.sourceBot, activeMac.name, activeMac.clickCount())
                : "No macro loaded — open MACRO tab or press REC";
            if (macLine.size() > 44) macLine = macLine.substr(0, 41) + "...";
            auto* mLbl = CCLabelBMFont::create(macLine.c_str(), "chatFont.fnt");
            mLbl->setScale(0.48f);
            mLbl->setColor(hasMac ? ccc3(125, 245, 160) : ccc3(195, 204, 218));
            mLbl->setPosition({ rX + rW * 0.5f, cardY + 11.0f });
            m_contentNode->addChild(mLbl, 3);
        }
        else if (m_activeTab == 1) {
            // =================================================================
            // TAB 1: MACRO (FB Counter Macro Hub: RECORD / REFRESH / CLEAR + list)
            // =================================================================
            auto const& activeMac = MacroStore::get().active();
            bool hasMac = !activeMac.empty();

            std::string topInfo = hasMac
                ? fmt::format("ACTIVE: [{}] {} ({} CLICKS @ {:.0f} FPS)", activeMac.sourceBot, activeMac.name, activeMac.clickCount(), activeMac.sourceFps)
                : "NO MACRO LOADED — RECORD OR PICK ONE BELOW";
            if (topInfo.size() > 54) topInfo = topInfo.substr(0, 51) + "...";
            addLabel(m_contentNode, topInfo, leftX, topY + 3.0f, 0.28f, "goldFont.fnt",
                     hasMac ? ccc3(125, 245, 160) : ccc3(215, 222, 232));

            // 3 big FB-style buttons: RECORD | REFRESH | CLEAR
            float bW = (fullW - 16.0f) / 3.0f;
            float bY = topY - 22.0f;
            auto* recBtn = CCMenuItemSpriteExtra::create(
                makeBtn(m_isRecordingNow ? "STOP & SAVE" : "RECORD", bW, 24.0f, "GJ_button_06.png", 0.34f),
                this, menu_selector(SettingsPopup::onToggleRecordBtn)
            );
            recBtn->setPosition({ leftX + bW * 0.5f, bY });
            menu->addChild(recBtn);

            auto* refBtn = CCMenuItemSpriteExtra::create(
                makeBtn("REFRESH LIST", bW, 24.0f, "GJ_button_01.png", 0.32f),
                this, menu_selector(SettingsPopup::onMacroRefreshBtn)
            );
            refBtn->setPosition({ leftX + bW + 8.0f + bW * 0.5f, bY });
            menu->addChild(refBtn);

            auto* clrBtn = CCMenuItemSpriteExtra::create(
                makeBtn("CLEAR MACRO", bW, 24.0f, "GJ_button_04.png", 0.32f),
                this, menu_selector(SettingsPopup::onMacroClearBtn)
            );
            clrBtn->setPosition({ leftX + 2.0f * (bW + 8.0f) + bW * 0.5f, bY });
            menu->addChild(clrBtn);

            constexpr int kPerPage = 5;
            int total = static_cast<int>(m_discoveredMacros.size());
            int totalPages = std::max(1, (total + kPerPage - 1) / kPerPage);
            m_macroPage = std::clamp(m_macroPage, 0, totalPages - 1);

            if (total == 0) {
                auto* emp = CCLabelBMFont::create(
                    "No macros found in Silicate / xdBot / Eclipse / FWC folders.\nPress RECORD (or REC on Pause screen) to record your run.",
                    "chatFont.fnt"
                );
                emp->setScale(0.54f);
                emp->setColor(ccc3(210, 218, 232));
                emp->setPosition({ kPanelW * 0.5f, topY - 96.0f });
                m_contentNode->addChild(emp, 3);
            } else {
                int startIdx = m_macroPage * kPerPage;
                float rowY = topY - 52.0f;
                for (int r = 0; r < kPerPage && (startIdx + r) < total; ++r) {
                    auto const& dm = m_discoveredMacros[static_cast<size_t>(startIdx + r)];
                    ccColor3B tagCol = ccc3(120, 220, 255);
                    if (dm.botTag == "SLC") tagCol = ccc3(195, 130, 255);
                    else if (dm.botTag == "XDB") tagCol = ccc3(255, 195, 95);
                    else if (dm.botTag == "ECL") tagCol = ccc3(110, 240, 165);

                    addLabel(m_contentNode, fmt::format("[{}]", dm.botTag), leftX, rowY, 0.30f, "bigFont.fnt", tagCol);
                    std::string shortName = dm.displayName;
                    if (shortName.size() > 32) shortName = shortName.substr(0, 29) + "...";
                    addLabel(m_contentNode, shortName, leftX + 46.0f, rowY, 0.30f, "bigFont.fnt",
                             dm.matchesLevel ? ccc3(135, 250, 155) : ccc3(240, 244, 250));

                    bool isLoaded = (hasMac && activeMac.path == dm.fullPath);
                    auto* useBtn = CCMenuItemSpriteExtra::create(
                        makeBtn(isLoaded ? "ACTIVE" : "USE", 58.0f, 19.0f, isLoaded ? "GJ_button_01.png" : "GJ_button_02.png", 0.28f),
                        this, menu_selector(SettingsPopup::onMacroLoadBtn)
                    );
                    useBtn->setTag(startIdx + r);
                    useBtn->setPosition({ leftX + fullW - 30.0f, rowY });
                    menu->addChild(useBtn);
                    rowY -= 24.0f;
                }
            }

            if (totalPages > 1) {
                auto* prevB = CCMenuItemSpriteExtra::create(makeBtn("< Prev", 52.0f, 18.0f, "GJ_button_04.png", 0.26f), this, menu_selector(SettingsPopup::onMacroPageBtn));
                prevB->setTag(-1);
                prevB->setPosition({ leftX + 28.0f, 26.0f });
                menu->addChild(prevB);

                auto* pgLbl = CCLabelBMFont::create(fmt::format("Page {} / {}", m_macroPage + 1, totalPages).c_str(), "goldFont.fnt");
                pgLbl->setScale(0.45f);
                pgLbl->setPosition({ kPanelW * 0.5f, 26.0f });
                m_contentNode->addChild(pgLbl, 3);

                auto* nextB = CCMenuItemSpriteExtra::create(makeBtn("Next >", 52.0f, 18.0f, "GJ_button_04.png", 0.26f), this, menu_selector(SettingsPopup::onMacroPageBtn));
                nextB->setTag(1);
                nextB->setPosition({ leftX + fullW - 28.0f, 26.0f });
                menu->addChild(nextB);
            }
        }
        else if (m_activeTab == 2) {
            // =================================================================
            // TAB 2: LOOK (HUD, Tiers, Circles, Watermark)
            // =================================================================
            float colW = (fullW - 16.0f) * 0.5f;
            float rightX = leftX + colW + 16.0f;

            addLabel(m_contentNode, "Tiers:", leftX, topY, 0.32f, "goldFont.fnt");
            const char* tNames[3] = { "1..12", "0-1", "1/2/3" };
            for (int i = 0; i < 3; ++i) {
                bool sel = (static_cast<int>(m_cfg.tableStyle) == i);
                auto* tb = CCMenuItemSpriteExtra::create(makeBtn(tNames[i], 46.0f, 20.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.26f), this, menu_selector(SettingsPopup::onTableStyleBtn));
                tb->setTag(i);
                tb->setPosition({ leftX + 74.0f + i * 50.0f, topY });
                menu->addChild(tb);
            }

            addLabel(m_contentNode, "Ring:", rightX, topY, 0.32f, "goldFont.fnt");
            const char* rKeys[3] = { "thin", "bold", "thick" };
            const char* rLbls[3] = { "Thin", "Bold", "Thick" };
            for (int i = 0; i < 3; ++i) {
                bool sel = (m_cfg.ringThickness == rKeys[i]);
                auto* rb = CCMenuItemSpriteExtra::create(makeBtn(rLbls[i], 46.0f, 20.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.26f), this, menu_selector(SettingsPopup::onRingThicknessBtn));
                rb->setTag(i);
                rb->setPosition({ rightX + 74.0f + i * 50.0f, topY });
                menu->addChild(rb);
            }

            addStepperRow(m_contentNode, menu, "HUD Scale", fmt::format("{:.2f}x", m_cfg.hudScale), 214, 215, leftX, topY - 28.0f, colW);
            addStepperRow(m_contentNode, menu, "Circle Size", fmt::format("{:.1f}x", m_cfg.circleScale), 200, 201, rightX, topY - 28.0f, colW);

            addCheckboxRow(m_contentNode, menu, "Top-Left Table", m_cfg.showTierTable, 110, leftX, topY - 56.0f);
            addCheckboxRow(m_contentNode, menu, "Click Circles", m_cfg.showCircles, 102, rightX, topY - 56.0f);

            addCheckboxRow(m_contentNode, menu, "ms & Hz Readout", m_cfg.showMsHz, 111, leftX, topY - 82.0f);
            addCheckboxRow(m_contentNode, menu, "Keep Circles", m_cfg.persistentCircles, 103, rightX, topY - 82.0f);

            addCheckboxRow(m_contentNode, menu, "Diff % & Score", m_cfg.showBottomStats, 112, leftX, topY - 108.0f);
            addCheckboxRow(m_contentNode, menu, "Circle Numbers", m_cfg.showCircleLabel, 104, rightX, topY - 108.0f);

            addCheckboxRow(m_contentNode, menu, "Center Watermark", m_cfg.showWatermark, 113, leftX, topY - 134.0f);
            addCheckboxRow(m_contentNode, menu, "Freeze Frame 0", m_cfg.introFreeze, 114, rightX, topY - 134.0f);

            addLabel(m_contentNode, "Watermark:", leftX, topY - 166.0f, 0.32f, "goldFont.fnt");
            auto* wmInput = geode::TextInput::create(280.0f, "FWC COUNTER", "chatFont.fnt");
            wmInput->setMaxCharCount(32);
            wmInput->setString(m_cfg.watermarkText);
            wmInput->setPosition({ leftX + fullW - 140.0f, topY - 166.0f });
            wmInput->setCallback([this](std::string const& str) { m_cfg.watermarkText = str; notifyLive(); });
            m_contentNode->addChild(wmInput, 8);
        }
        else if (m_activeTab == 3) {
            // =================================================================
            // TAB 3: SETTINGS (Audio & Render)
            // =================================================================
            float colW = (fullW - 16.0f) * 0.5f;
            float rightX = leftX + colW + 16.0f;

            addCheckboxRow(m_contentNode, menu, "Click Sounds", m_cfg.soundEnabled, 120, leftX, topY);
            addCheckboxRow(m_contentNode, menu, "Dynamic Pitch", m_cfg.dynamicPitch, 121, rightX, topY);

            addLabel(m_contentNode, "Sound:", leftX, topY - 30.0f, 0.32f, "goldFont.fnt");
            const char* pKeys[4] = { "classic", "soft", "tick", "custom" };
            const char* pLbls[4] = { "Classic", "Soft", "Tick", "Custom" };
            for (int i = 0; i < 4; ++i) {
                bool sel = (m_cfg.soundPreset == pKeys[i]);
                auto* pb = CCMenuItemSpriteExtra::create(makeBtn(pLbls[i], 54.0f, 20.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.25f), this, menu_selector(SettingsPopup::onSoundPresetBtn));
                pb->setTag(i);
                pb->setPosition({ leftX + 86.0f + i * 58.0f, topY - 30.0f });
                menu->addChild(pb);
            }
            auto* testB = CCMenuItemSpriteExtra::create(makeBtn("TEST", 52.0f, 20.0f, "GJ_button_02.png", 0.27f), this, menu_selector(SettingsPopup::onTestBellBtn));
            testB->setPosition({ leftX + fullW - 28.0f, topY - 30.0f });
            menu->addChild(testB);

            addStepperRow(m_contentNode, menu, "Volume", fmt::format("{}%", static_cast<int>(std::round(m_cfg.soundVolume * 100.0f))), 220, 221, leftX, topY - 60.0f, colW);
            addStepperRow(m_contentNode, menu, "Pitch", fmt::format("{:.2f}x", m_cfg.soundPitch), 222, 223, rightX, topY - 60.0f, colW);

            addCheckboxRow(m_contentNode, menu, "Auto Comments", m_cfg.showAutoComments, 105, leftX, topY - 92.0f);
            addCheckboxRow(m_contentNode, menu, "Capture to Manual", m_cfg.captureToManual, 130, rightX, topY - 92.0f);

            addLabel(m_contentNode, "Render Res:", leftX, topY - 126.0f, 0.31f, "goldFont.fnt");
            const char* resK[3] = { "window", "720p", "1080p" };
            const char* resL[3] = { "Window", "720p", "1080p" };
            for (int i = 0; i < 3; ++i) {
                bool sel = (m_cfg.renderRes == resK[i]);
                auto* rb = CCMenuItemSpriteExtra::create(makeBtn(resL[i], 54.0f, 20.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.26f), this, menu_selector(SettingsPopup::onRenderResBtn));
                rb->setTag(i);
                rb->setPosition({ leftX + 126.0f + i * 60.0f, topY - 126.0f });
                menu->addChild(rb);
            }

            addLabel(m_contentNode, "Render FPS:", leftX, topY - 154.0f, 0.31f, "goldFont.fnt");
            int fpsOpts[3] = { 30, 60, 120 };
            for (int i = 0; i < 3; ++i) {
                bool sel = (m_cfg.renderFps == fpsOpts[i]);
                auto* fb = CCMenuItemSpriteExtra::create(makeBtn(fmt::format("{}", fpsOpts[i]).c_str(), 48.0f, 20.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.27f), this, menu_selector(SettingsPopup::onRenderFpsBtn));
                fb->setTag(fpsOpts[i]);
                fb->setPosition({ leftX + 122.0f + i * 54.0f, topY - 154.0f });
                menu->addChild(fb);
            }
        }
        else if (m_activeTab == 4) {
            // =================================================================
            // SUB-VIEW 4: EDIT CLICKS (FB Counter Edit Clicks list)
            // =================================================================
            auto* backB = CCMenuItemSpriteExtra::create(makeBtn("< BACK", 62.0f, 20.0f, "GJ_button_04.png", 0.27f), this, menu_selector(SettingsPopup::onBackToCounterBtn));
            backB->setPosition({ leftX + 32.0f, topY + 2.0f });
            menu->addChild(backB);

            auto const& markers = MarkerStore::get().markers();
            addLabel(m_contentNode, fmt::format("EDIT CLICKS ({})", markers.size()), leftX + 76.0f, topY + 2.0f, 0.34f, "goldFont.fnt");

            auto* addB = CCMenuItemSpriteExtra::create(makeBtn("+ ADD HERE", 82.0f, 20.0f, "GJ_button_01.png", 0.26f), this, menu_selector(SettingsPopup::onManualActionBtn));
            addB->setTag(1);
            addB->setPosition({ leftX + fullW - 42.0f, topY + 2.0f });
            menu->addChild(addB);

            constexpr int kPerPage = 5;
            int total = static_cast<int>(markers.size());
            int totalPages = std::max(1, (total + kPerPage - 1) / kPerPage);
            m_manualPage = std::clamp(m_manualPage, 0, totalPages - 1);

            if (total == 0) {
                auto* emp = CCLabelBMFont::create("No clicks scanned yet. Run ANALYZE first or press + ADD HERE.", "chatFont.fnt");
                emp->setScale(0.54f);
                emp->setPosition({ kPanelW * 0.5f, topY - 76.0f });
                m_contentNode->addChild(emp, 3);
            } else {
                int startIdx = m_manualPage * kPerPage;
                float rowY = topY - 26.0f;
                for (int r = 0; r < kPerPage && (startIdx + r) < total; ++r) {
                    int idx = startIdx + r;
                    auto const& mk = markers[static_cast<size_t>(idx)];
                    addLabel(m_contentNode, fmt::format("#{}  X:{:.0f}", idx + 1, mk.x), leftX, rowY, 0.30f, "bigFont.fnt", ccc3(230, 236, 245));
                    addLabel(m_contentNode, fmt::format("{:.2f}f", mk.frames), leftX + 128.0f, rowY, 0.32f, "goldFont.fnt");

                    struct OpBtn { const char* lbl; int op; float dx; const char* bg; };
                    const OpBtn ops[5] = {
                        { "-1", 0, 220.0f, "GJ_button_04.png" },
                        { "+1", 1, 256.0f, "GJ_button_04.png" },
                        { "-.1", 2, 296.0f, "GJ_button_04.png" },
                        { "+.1", 3, 336.0f, "GJ_button_04.png" },
                        { "DEL", 5, 392.0f, "GJ_button_06.png" }
                    };
                    for (auto const& ob : ops) {
                        auto* b = CCMenuItemSpriteExtra::create(makeBtn(ob.lbl, 32.0f, 18.0f, ob.bg, 0.25f), this, menu_selector(SettingsPopup::onMarkerRowBtn));
                        b->setTag(idx * 10 + ob.op);
                        b->setPosition({ leftX + ob.dx, rowY });
                        menu->addChild(b);
                    }
                    rowY -= 25.0f;
                }
            }

            if (totalPages > 1) {
                auto* prevB = CCMenuItemSpriteExtra::create(makeBtn("< Prev", 52.0f, 18.0f, "GJ_button_04.png", 0.26f), this, menu_selector(SettingsPopup::onManualPageBtn));
                prevB->setTag(-1);
                prevB->setPosition({ leftX + 28.0f, 26.0f });
                menu->addChild(prevB);

                auto* pgLbl = CCLabelBMFont::create(fmt::format("Page {} / {}", m_manualPage + 1, totalPages).c_str(), "goldFont.fnt");
                pgLbl->setScale(0.45f);
                pgLbl->setPosition({ kPanelW * 0.5f, 26.0f });
                m_contentNode->addChild(pgLbl, 3);

                auto* nextB = CCMenuItemSpriteExtra::create(makeBtn("Next >", 52.0f, 18.0f, "GJ_button_04.png", 0.26f), this, menu_selector(SettingsPopup::onManualPageBtn));
                nextB->setTag(1);
                nextB->setPosition({ leftX + fullW - 28.0f, 26.0f });
                menu->addChild(nextB);
            }
        }
    }

    // ---- Callbacks ----
    void onTabSelect(CCObject* sender) {
        m_activeTab = static_cast<CCNode*>(sender)->getTag();
        rebuildContent();
    }

    void onOpenEditClicksBtn(CCObject*) {
        m_activeTab = 4;
        rebuildContent();
    }

    void onBackToCounterBtn(CCObject*) {
        m_activeTab = 0;
        rebuildContent();
    }

    void onClearCountsBtn(CCObject*) {
        MarkerStore::get().clearAll();
        m_manualPage = 0;
        applyAndRebuild();
    }

    void onCloseBtn(CCObject*) {
        this->removeFromParent();
    }

    void onStartScanBtn(CCObject*) {
        bool hasMac = MacroStore::get().hasMacro();
        if (!hasMac && m_recordedRunClicks == 0) {
            FLAlertLayer::create(
                "No Macro Loaded",
                "Open the <cg>MACRO</c> tab to load a Silicate/xdBot/Eclipse macro, or press <cr>REC</c> to record your own run first!",
                "OK"
            )->show();
            return;
        }
        auto cb = m_onStartExactScan;
        this->removeFromParent();
        if (cb) cb();
    }

    void onWatchMacroBtn(CCObject*) {
        if (!MacroStore::get().hasMacro()) {
            FLAlertLayer::create("No Macro Loaded", "Load or record a macro in the <cg>MACRO</c> tab first.", "OK")->show();
            return;
        }
        auto cb = m_onWatchMacro;
        this->removeFromParent();
        if (cb) cb();
    }

    void onToggleRecordBtn(CCObject*) {
        auto cb = m_onToggleRecordMacro;
        this->removeFromParent();
        if (cb) cb();
    }

    void onToggleRenderBtn(CCObject*) {
        auto cb = m_onToggleRender;
        this->removeFromParent();
        if (cb) cb();
    }

    void onMacroRefreshBtn(CCObject*) {
        m_discoveredMacros = MacroStore::get().discoverMacros(m_levelName);
        m_macroPage = 0;
        rebuildContent();
    }

    void onMacroClearBtn(CCObject*) {
        MacroStore::get().clear();
        rebuildContent();
    }

    void onMacroLoadBtn(CCObject* sender) {
        int idx = static_cast<CCNode*>(sender)->getTag();
        if (idx < 0 || idx >= static_cast<int>(m_discoveredMacros.size())) return;
        auto const& dm = m_discoveredMacros[static_cast<size_t>(idx)];
        if (MacroStore::get().loadFromFile(dm.fullPath, dm.sourceBot)) {
            m_activeTab = 0; // Jump right back to COUNTER tab to press ANALYZE (just like FB Counter!)
            rebuildContent();
        } else {
            FLAlertLayer::create("Macro Load Failed", MacroStore::get().lastError().c_str(), "OK")->show();
        }
    }

    void onMacroPageBtn(CCObject* sender) {
        m_macroPage += static_cast<CCNode*>(sender)->getTag();
        rebuildContent();
    }

    void onAnalyzeDepthBtn(CCObject* sender) {
        m_cfg.analyzeDepth = (static_cast<CCNode*>(sender)->getTag() == 0) ? "fast" : "deep";
        applyAndRebuild();
    }

    void onModeBtn(CCObject* sender) {
        m_cfg.mode = (static_cast<CCNode*>(sender)->getTag() == 1) ? CounterMode::Manual : CounterMode::Auto;
        applyAndRebuild();
    }

    void onTableStyleBtn(CCObject* sender) {
        int v = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 2);
        m_cfg.tableStyle = static_cast<TableStyle>(v);
        applyAndRebuild();
    }

    void onRingThicknessBtn(CCObject* sender) {
        const char* rKeys[3] = { "thin", "bold", "thick" };
        int idx = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 2);
        m_cfg.ringThickness = rKeys[idx];
        applyAndRebuild();
    }

    void onSoundPresetBtn(CCObject* sender) {
        const char* pKeys[4] = { "classic", "soft", "tick", "custom" };
        int idx = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 3);
        m_cfg.soundPreset = pKeys[idx];
        applyAndRebuild();
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.customSoundFile, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onTestBellBtn(CCObject*) {
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.customSoundFile, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onRenderResBtn(CCObject* sender) {
        const char* resKeys[3] = { "window", "720p", "1080p" };
        int idx = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 2);
        m_cfg.renderRes = resKeys[idx];
        applyAndRebuild();
    }

    void onRenderFpsBtn(CCObject* sender) {
        m_cfg.renderFps = static_cast<int64_t>(static_cast<CCNode*>(sender)->getTag());
        applyAndRebuild();
    }

    void onToggleBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        switch (tag) {
            case 100: m_cfg.enabled = !m_cfg.enabled; break;
            case 101: m_cfg.cbfMode = !m_cfg.cbfMode; break;
            case 102: m_cfg.showCircles = !m_cfg.showCircles; break;
            case 103: m_cfg.persistentCircles = !m_cfg.persistentCircles; break;
            case 104: m_cfg.showCircleLabel = !m_cfg.showCircleLabel; break;
            case 105: m_cfg.showAutoComments = !m_cfg.showAutoComments; break;
            case 110: m_cfg.showTierTable = !m_cfg.showTierTable; break;
            case 111: m_cfg.showMsHz = !m_cfg.showMsHz; break;
            case 112: m_cfg.showBottomStats = !m_cfg.showBottomStats; break;
            case 113: m_cfg.showWatermark = !m_cfg.showWatermark; break;
            case 114: m_cfg.introFreeze = !m_cfg.introFreeze; break;
            case 120: m_cfg.soundEnabled = !m_cfg.soundEnabled; break;
            case 121: m_cfg.dynamicPitch = !m_cfg.dynamicPitch; break;
            case 130: m_cfg.captureToManual = !m_cfg.captureToManual; break;
            default: break;
        }
        notifyLive();
    }

    void onStepperBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        switch (tag) {
            case 200: m_cfg.circleScale = std::max(0.4f, m_cfg.circleScale - 0.1f); break;
            case 201: m_cfg.circleScale = std::min(2.5f, m_cfg.circleScale + 0.1f); break;
            case 214: m_cfg.hudScale = std::max(0.70f, m_cfg.hudScale - 0.05f); break;
            case 215: m_cfg.hudScale = std::min(2.00f, m_cfg.hudScale + 0.05f); break;
            case 220: m_cfg.soundVolume = std::max(0.0f, m_cfg.soundVolume - 0.05f); break;
            case 221: m_cfg.soundVolume = std::min(1.0f, m_cfg.soundVolume + 0.05f); break;
            case 222: m_cfg.soundPitch = std::max(0.4f, m_cfg.soundPitch - 0.05f); break;
            case 223: m_cfg.soundPitch = std::min(2.5f, m_cfg.soundPitch + 0.05f); break;
            default: break;
        }
        applyAndRebuild();
    }

    void onManualActionBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag == 1) {
            MarkerStore::get().addMarker(m_currentPlayerX, m_currentPlayerY, 1.0, "");
        }
        rebuildContent();
    }

    void onMarkerRowBtn(CCObject* sender) {
        int raw = static_cast<CCNode*>(sender)->getTag();
        int idx = raw / 10;
        int op = raw % 10;
        auto& markers = MarkerStore::get().markers();
        if (idx < 0 || idx >= static_cast<int>(markers.size())) return;
        auto& mk = markers[static_cast<size_t>(idx)];
        switch (op) {
            case 0: mk.frames = std::max(0.05, mk.frames - 1.0); break;
            case 1: mk.frames = std::min(12.0, mk.frames + 1.0); break;
            case 2: mk.frames = std::max(0.05, mk.frames - 0.1); break;
            case 3: mk.frames = std::min(12.0, mk.frames + 0.1); break;
            case 5: MarkerStore::get().removeAt(static_cast<size_t>(idx)); rebuildContent(); return;
            default: break;
        }
        MarkerStore::get().saveCurrent();
        rebuildContent();
    }

    void onManualPageBtn(CCObject* sender) {
        m_manualPage += static_cast<CCNode*>(sender)->getTag();
        rebuildContent();
    }

    void keyBackClicked() override {
        this->removeFromParent();
    }

    bool setup(
        std::function<void()> onChanged,
        std::function<void()> onStartExactScan,
        std::function<void()> onWatchMacro,
        std::function<void()> onToggleRecordMacro,
        std::function<void()> onToggleRender,
        std::string const& levelName,
        size_t recordedClicks,
        bool isRenderingNow,
        bool isRecordingNow,
        float playerX,
        float playerY
    ) {
        if (!FLAlertLayer::init(175)) return false;

        if (!m_buttonMenu) {
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({ 0.0f, 0.0f });
            m_mainLayer->addChild(m_buttonMenu, 10);
        }

        m_cfg = ModConfig::load();
        m_onChanged = std::move(onChanged);
        m_onStartExactScan = std::move(onStartExactScan);
        m_onWatchMacro = std::move(onWatchMacro);
        m_onToggleRecordMacro = std::move(onToggleRecordMacro);
        m_onToggleRender = std::move(onToggleRender);
        m_levelName = levelName;
        m_discoveredMacros = MacroStore::get().discoverMacros(m_levelName);
        m_recordedRunClicks = recordedClicks;
        m_isRenderingNow = isRenderingNow;
        m_isRecordingNow = isRecordingNow;
        m_currentPlayerX = playerX;
        m_currentPlayerY = playerY;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        m_panel = CCNode::create();
        m_panel->setContentSize({ kPanelW, kPanelH });
        m_panel->setPosition({ winSize.width * 0.5f - kPanelW * 0.5f, winSize.height * 0.5f - kPanelH * 0.5f });
        m_mainLayer->addChild(m_panel, 5);

        auto* bg = CCScale9Sprite::create("GJ_square05.png");
        bg->setContentSize({ kPanelW, kPanelH });
        bg->setPosition({ kPanelW * 0.5f, kPanelH * 0.5f });
        m_panel->addChild(bg, 0);

        auto* title = CCLabelBMFont::create("FWC COUNTER", "goldFont.fnt");
        title->setScale(0.74f);
        title->setPosition({ kPanelW * 0.5f, kPanelH - 18.0f });
        m_panel->addChild(title, 3);

        rebuildContent();
        this->setKeypadEnabled(true);
        this->setTouchEnabled(true);
        return true;
    }

public:
    static SettingsPopup* create(
        std::function<void()> onChanged,
        std::function<void()> onStartExactScan,
        std::function<void()> onWatchMacro,
        std::function<void()> onToggleRecordMacro,
        std::function<void()> onToggleRender,
        std::string const& levelName,
        size_t recordedClicks,
        bool isRenderingNow,
        bool isRecordingNow,
        float playerX,
        float playerY
    ) {
        auto* ret = new SettingsPopup();
        if (ret && ret->setup(std::move(onChanged), std::move(onStartExactScan), std::move(onWatchMacro), std::move(onToggleRecordMacro), std::move(onToggleRender), levelName, recordedClicks, isRenderingNow, isRecordingNow, playerX, playerY)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

} // namespace fwc
