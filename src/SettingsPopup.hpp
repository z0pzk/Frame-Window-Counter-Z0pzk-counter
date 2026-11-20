#pragma once

#include "AudioManager.hpp"
#include "Config.hpp"
#include "MarkerStore.hpp"
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
    float m_currentPlayerX = 0.0f;
    double m_currentSimTime = 0.0;

    int m_activeTab = 0; // 0=General, 1=HUD/Banner, 2=Audio, 3=Manual
    int m_manualPage = 0;

    CCNode* m_panel = nullptr;
    CCNode* m_contentNode = nullptr;
    CCMenu* m_tabMenu = nullptr;

    static constexpr float kPanelW = 436.0f;
    static constexpr float kPanelH = 282.0f;

    CCNode* makeStyledButtonSprite(std::string const& text, float w, float h, ccColor3B bgCol, ccColor3B borderCol, ccColor3B textCol, float fontScale = 0.62f) {
        auto* root = CCNode::create();
        root->setContentSize({ w, h });

        auto* bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize({ w, h });
        bg->setColor(bgCol);
        bg->setOpacity(240);
        bg->setPosition({ w * 0.5f, h * 0.5f });
        root->addChild(bg, 0);

        auto* border = CCDrawNode::create();
        CCPoint pts[4] = { ccp(0, 0), ccp(w, 0), ccp(w, h), ccp(0, h) };
        ccColor4F bc = ccc4f(borderCol.r / 255.0f, borderCol.g / 255.0f, borderCol.b / 255.0f, 0.9f);
        border->drawPolygon(pts, 4, ccc4f(0, 0, 0, 0), 0.7f, bc);
        root->addChild(border, 1);

        auto* lbl = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
        lbl->setScale(fontScale);
        lbl->setColor(textCol);
        lbl->setPosition({ w * 0.5f, h * 0.5f });
        root->addChild(lbl, 2);

        return root;
    }

    void addRowLabel(CCNode* parent, std::string const& title, float x, float y, ccColor3B col = ccc3(222, 230, 244)) {
        auto* lbl = CCLabelBMFont::create(title.c_str(), "chatFont.fnt");
        lbl->setAnchorPoint({ 0.0f, 0.5f });
        lbl->setScale(0.68f);
        lbl->setColor(col);
        lbl->setPosition({ x, y });
        parent->addChild(lbl, 2);
    }

    void addToggleRow(CCNode* parent, CCMenu* menu, std::string const& title, bool value, int tag, float x, float y, float rowW) {
        addRowLabel(parent, title, x, y);
        auto bgCol = value ? ccc3(22, 95, 76) : ccc3(32, 37, 49);
        auto bdCol = value ? ccc3(74, 222, 128) : ccc3(74, 86, 108);
        auto txCol = value ? ccc3(220, 255, 234) : ccc3(165, 178, 200);
        auto* spr = makeStyledButtonSprite(value ? "ON" : "OFF", 46.0f, 20.0f, bgCol, bdCol, txCol, 0.62f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onToggleBtn));
        btn->setTag(tag);
        btn->setPosition({ x + rowW - 23.0f, y });
        menu->addChild(btn);
    }

    void addStepperRow(CCNode* parent, CCMenu* menu, std::string const& title, std::string const& valText, int minusTag, int plusTag, float x, float y, float rowW) {
        addRowLabel(parent, title, x, y);

        float rightEdge = x + rowW;
        auto* plusSpr = makeStyledButtonSprite("+", 22.0f, 20.0f, ccc3(28, 35, 48), ccc3(82, 106, 138), ccc3(235, 244, 255), 0.72f);
        auto* plusBtn = CCMenuItemSpriteExtra::create(plusSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        plusBtn->setTag(plusTag);
        plusBtn->setPosition({ rightEdge - 11.0f, y });
        menu->addChild(plusBtn);

        auto* valLbl = CCLabelBMFont::create(valText.c_str(), "chatFont.fnt");
        valLbl->setScale(0.66f);
        valLbl->setColor(ccc3(125, 211, 252));
        valLbl->setPosition({ rightEdge - 52.0f, y });
        parent->addChild(valLbl, 2);

        auto* minusSpr = makeStyledButtonSprite("-", 22.0f, 20.0f, ccc3(28, 35, 48), ccc3(82, 106, 138), ccc3(235, 244, 255), 0.72f);
        auto* minusBtn = CCMenuItemSpriteExtra::create(minusSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        minusBtn->setTag(minusTag);
        minusBtn->setPosition({ rightEdge - 93.0f, y });
        menu->addChild(minusBtn);
    }

    void applyAndNotify() {
        m_cfg.save();
        if (m_onChanged) m_onChanged();
        rebuildContent();
    }

    void rebuildTabs() {
        if (m_tabMenu) {
            m_tabMenu->removeFromParent();
            m_tabMenu = nullptr;
        }
        m_tabMenu = CCMenu::create();
        m_tabMenu->setPosition({ 0.0f, 0.0f });
        m_panel->addChild(m_tabMenu, 5);

        const char* names[4] = { "GENERAL", "HUD & BANNER", "AUDIO", "MANUAL" };
        float startX = 16.0f;
        float tabW = 96.0f;
        float gap = 6.0f;
        float tabY = kPanelH - 48.0f;

        for (int i = 0; i < 4; ++i) {
            bool active = (m_activeTab == i);
            auto bgCol = active ? ccc3(27, 56, 84) : ccc3(22, 26, 36);
            auto bdCol = active ? ccc3(56, 189, 248) : ccc3(58, 68, 88);
            auto txCol = active ? ccc3(240, 248, 255) : ccc3(160, 174, 198);
            auto* spr = makeStyledButtonSprite(names[i], tabW, 22.0f, bgCol, bdCol, txCol, 0.60f);
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onTabSelect));
            btn->setTag(i);
            btn->setPosition({ startX + tabW * 0.5f + i * (tabW + gap), tabY });
            m_tabMenu->addChild(btn);
        }

        // Close [X] button top-right
        auto* closeSpr = makeStyledButtonSprite("X", 24.0f, 20.0f, ccc3(78, 25, 36), ccc3(244, 63, 94), ccc3(255, 226, 232), 0.68f);
        auto* closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(SettingsPopup::onCloseBtn));
        closeBtn->setPosition({ kPanelW - 22.0f, kPanelH - 18.0f });
        m_tabMenu->addChild(closeBtn);
    }

    void rebuildContent() {
        rebuildTabs();

        if (m_contentNode) {
            m_contentNode->removeFromParent();
            m_contentNode = nullptr;
        }
        m_contentNode = CCNode::create();
        m_panel->addChild(m_contentNode, 4);

        auto* menu = CCMenu::create();
        menu->setPosition({ 0.0f, 0.0f });
        m_contentNode->addChild(menu, 5);

        float leftX = 18.0f;
        float fullW = kPanelW - 36.0f;
        float colW = (fullW - 18.0f) * 0.5f;
        float rightX = leftX + colW + 18.0f;
        float topY = kPanelH - 80.0f;

        if (m_activeTab == 0) {
            // GENERAL TAB
            addToggleRow(m_contentNode, menu, "Enable Mod", m_cfg.enabled, 100, leftX, topY, colW);
            addToggleRow(m_contentNode, menu, "Adapt Live TPS", m_cfg.autoTpsAdapt, 101, rightX, topY, colW);

            // Mode switcher row
            addRowLabel(m_contentNode, "Counter Mode", leftX, topY - 28.0f);
            bool isAuto = (m_cfg.mode == CounterMode::Auto);
            auto* autoSpr = makeStyledButtonSprite(
                "AUTO", 62.0f, 20.0f,
                isAuto ? ccc3(25, 80, 110) : ccc3(28, 32, 44),
                isAuto ? ccc3(56, 189, 248) : ccc3(64, 74, 94),
                isAuto ? ccc3(240, 250, 255) : ccc3(158, 170, 192)
            );
            auto* autoBtn = CCMenuItemSpriteExtra::create(autoSpr, this, menu_selector(SettingsPopup::onModeBtn));
            autoBtn->setTag(0);
            autoBtn->setPosition({ rightX + 31.0f, topY - 28.0f });
            menu->addChild(autoBtn);

            auto* manSpr = makeStyledButtonSprite(
                "MANUAL", 68.0f, 20.0f,
                !isAuto ? ccc3(104, 62, 16) : ccc3(28, 32, 44),
                !isAuto ? ccc3(250, 204, 21) : ccc3(64, 74, 94),
                !isAuto ? ccc3(255, 248, 220) : ccc3(158, 170, 192)
            );
            auto* manBtn = CCMenuItemSpriteExtra::create(manSpr, this, menu_selector(SettingsPopup::onModeBtn));
            manBtn->setTag(1);
            manBtn->setPosition({ rightX + 104.0f, topY - 28.0f });
            menu->addChild(manBtn);

            // Reference FPS presets
            addRowLabel(m_contentNode, fmt::format("Reference FPS: {}", m_cfg.refFps), leftX, topY - 58.0f, ccc3(250, 204, 21));
            const int64_t presets[5] = { 60, 240, 360, 1000, 6000 };
            for (int i = 0; i < 5; ++i) {
                bool sel = (m_cfg.refFps == presets[i]);
                auto* pSpr = makeStyledButtonSprite(
                    fmt::format("{}", presets[i]), 44.0f, 19.0f,
                    sel ? ccc3(34, 68, 98) : ccc3(25, 30, 42),
                    sel ? ccc3(56, 189, 248) : ccc3(64, 76, 98),
                    sel ? ccc3(240, 250, 255) : ccc3(180, 192, 214),
                    0.58f
                );
                auto* pBtn = CCMenuItemSpriteExtra::create(pSpr, this, menu_selector(SettingsPopup::onFpsPresetBtn));
                pBtn->setTag(static_cast<int>(presets[i]));
                pBtn->setPosition({ leftX + 178.0f + i * 48.0f, topY - 58.0f });
                menu->addChild(pBtn);
            }

            addToggleRow(m_contentNode, menu, "Click Circles", m_cfg.showCircles, 102, leftX, topY - 90.0f, colW);
            addToggleRow(m_contentNode, menu, "Circle Labels", m_cfg.showCircleLabel, 103, rightX, topY - 90.0f, colW);

            addStepperRow(m_contentNode, menu, "Circle Scale", fmt::format("{:.2f}x", m_cfg.circleScale), 200, 201, leftX, topY - 120.0f, colW);
            addStepperRow(m_contentNode, menu, "Auto Sens.", fmt::format("{:.2f}x", m_cfg.autoSensitivity), 202, 203, rightX, topY - 120.0f, colW);

            addStepperRow(m_contentNode, menu, "Swift Max (ms)", fmt::format("{:.0f}ms", m_cfg.swiftThresholdMs), 204, 205, leftX, topY - 150.0f, colW);
            addStepperRow(m_contentNode, menu, "Multi Gap (ms)", fmt::format("{:.0f}ms", m_cfg.multiclickWindowMs), 206, 207, rightX, topY - 150.0f, colW);

            auto* tip = CCLabelBMFont::create(
                "Tip: At 6000 FPS, 100F = 60Hz FP and <76F = ~80Hz FP (8.33-12.50ms). Bot & slow-mo safe.",
                "chatFont.fnt"
            );
            tip->setScale(0.56f);
            tip->setColor(ccc3(145, 160, 186));
            tip->setPosition({ kPanelW * 0.5f, 22.0f });
            m_contentNode->addChild(tip, 2);
        }
        else if (m_activeTab == 1) {
            // HUD & BANNER TAB
            addToggleRow(m_contentNode, menu, "Bottom-Left HUD", m_cfg.showHud, 110, leftX, topY, colW);
            addToggleRow(m_contentNode, menu, "Percentages (%)", m_cfg.showPercentages, 111, rightX, topY, colW);

            addToggleRow(m_contentNode, menu, "S-Fr/Swifts/Multi", m_cfg.showSpecialStats, 112, leftX, topY - 30.0f, colW);
            addToggleRow(m_contentNode, menu, "Intro Banner", m_cfg.showBanner, 113, rightX, topY - 30.0f, colW);

            addToggleRow(m_contentNode, menu, "Raw Precision", m_cfg.showRawPrecision, 114, leftX, topY - 60.0f, colW);
            addStepperRow(m_contentNode, menu, "Banner Time", fmt::format("{:.1f}s", m_cfg.bannerDuration), 210, 211, rightX, topY - 60.0f, colW);

            // Banner Comment cycle
            addRowLabel(m_contentNode, "Banner Comment:", leftX, topY - 96.0f, ccc3(250, 204, 21));
            auto* commentLbl = CCLabelBMFont::create(m_cfg.bannerComment.c_str(), "chatFont.fnt");
            commentLbl->setAnchorPoint({ 0.0f, 0.5f });
            commentLbl->setScale(0.64f);
            commentLbl->setColor(ccc3(235, 242, 255));
            commentLbl->setPosition({ leftX + 104.0f, topY - 96.0f });
            m_contentNode->addChild(commentLbl, 2);

            auto* cycComSpr = makeStyledButtonSprite("NEXT PRESET", 86.0f, 20.0f, ccc3(28, 38, 54), ccc3(82, 112, 150), ccc3(228, 240, 255), 0.56f);
            auto* cycComBtn = CCMenuItemSpriteExtra::create(cycComSpr, this, menu_selector(SettingsPopup::onCyclePresetBtn));
            cycComBtn->setTag(1);
            cycComBtn->setPosition({ leftX + fullW - 43.0f, topY - 96.0f });
            menu->addChild(cycComBtn);

            // Precision text cycle
            addRowLabel(m_contentNode, "Precision Line:", leftX, topY - 128.0f, ccc3(148, 210, 255));
            auto* precLbl = CCLabelBMFont::create(m_cfg.rawPrecisionText.c_str(), "chatFont.fnt");
            precLbl->setAnchorPoint({ 0.0f, 0.5f });
            precLbl->setScale(0.62f);
            precLbl->setColor(ccc3(220, 232, 248));
            precLbl->setPosition({ leftX + 94.0f, topY - 128.0f });
            m_contentNode->addChild(precLbl, 2);

            auto* cycPrecSpr = makeStyledButtonSprite("NEXT PRESET", 86.0f, 20.0f, ccc3(28, 38, 54), ccc3(82, 112, 150), ccc3(228, 240, 255), 0.56f);
            auto* cycPrecBtn = CCMenuItemSpriteExtra::create(cycPrecSpr, this, menu_selector(SettingsPopup::onCyclePresetBtn));
            cycPrecBtn->setTag(2);
            cycPrecBtn->setPosition({ leftX + fullW - 43.0f, topY - 128.0f });
            menu->addChild(cycPrecBtn);

            auto* note = CCLabelBMFont::create(
                "Need custom text? Edit 'Banner Comment' or 'Raw Precision Text' in Geode Mod Settings.",
                "chatFont.fnt"
            );
            note->setScale(0.56f);
            note->setColor(ccc3(145, 160, 186));
            note->setPosition({ kPanelW * 0.5f, 24.0f });
            m_contentNode->addChild(note, 2);
        }
        else if (m_activeTab == 2) {
            // AUDIO TAB
            addToggleRow(m_contentNode, menu, "Enable Bell Chime", m_cfg.soundEnabled, 120, leftX, topY, colW);
            addToggleRow(m_contentNode, menu, "Dynamic Pitch", m_cfg.dynamicPitch, 121, rightX, topY, colW);

            addStepperRow(m_contentNode, menu, "Base Pitch", fmt::format("{:.2f}x", m_cfg.soundPitch), 220, 221, leftX, topY - 34.0f, colW);
            addStepperRow(m_contentNode, menu, "Volume", fmt::format("{:.0f}%", m_cfg.soundVolume * 100.0f), 222, 223, rightX, topY - 34.0f, colW);

            // Sound preset selector
            addRowLabel(m_contentNode, "Bell Sound:", leftX, topY - 72.0f, ccc3(250, 204, 21));
            const char* pKeys[4] = { "crystal", "soft", "tick", "deep" };
            const char* pLabels[4] = { "CRYSTAL", "SOFT", "TICK", "DEEP" };
            for (int i = 0; i < 4; ++i) {
                bool sel = (m_cfg.soundPreset == pKeys[i]);
                auto* bSpr = makeStyledButtonSprite(
                    pLabels[i], 68.0f, 22.0f,
                    sel ? ccc3(28, 72, 102) : ccc3(24, 29, 40),
                    sel ? ccc3(56, 189, 248) : ccc3(64, 76, 98),
                    sel ? ccc3(242, 250, 255) : ccc3(178, 190, 212),
                    0.60f
                );
                auto* bBtn = CCMenuItemSpriteExtra::create(bSpr, this, menu_selector(SettingsPopup::onSoundPresetBtn));
                bBtn->setTag(i);
                bBtn->setPosition({ leftX + 124.0f + i * 74.0f, topY - 72.0f });
                menu->addChild(bBtn);
            }

            // Test Sound button
            auto* testSpr = makeStyledButtonSprite(
                "PLAY TEST BELL", 142.0f, 26.0f,
                ccc3(25, 86, 68), ccc3(74, 222, 128), ccc3(230, 255, 240), 0.68f
            );
            auto* testBtn = CCMenuItemSpriteExtra::create(testSpr, this, menu_selector(SettingsPopup::onTestBellBtn));
            testBtn->setPosition({ kPanelW * 0.5f, topY - 122.0f });
            menu->addChild(testBtn);

            auto* tip = CCLabelBMFont::create(
                "Dynamic Pitch raises the bell tone automatically on tighter frame perfects & sub-frames.",
                "chatFont.fnt"
            );
            tip->setScale(0.56f);
            tip->setColor(ccc3(145, 160, 186));
            tip->setPosition({ kPanelW * 0.5f, 24.0f });
            m_contentNode->addChild(tip, 2);
        }
        else {
            // MANUAL MARKER EDITOR TAB
            auto& markers = MarkerStore::get().markers();

            addToggleRow(m_contentNode, menu, "Capture Run Clicks", m_cfg.captureToManual, 130, leftX, topY, 174.0f);

            auto* addSpr = makeStyledButtonSprite("+ MARK HERE", 84.0f, 20.0f, ccc3(24, 78, 108), ccc3(56, 189, 248), ccc3(235, 248, 255), 0.56f);
            auto* addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(SettingsPopup::onManualActionBtn));
            addBtn->setTag(1);
            addBtn->setPosition({ leftX + 244.0f, topY });
            menu->addChild(addBtn);

            auto* clrSpr = makeStyledButtonSprite("CLEAR ALL", 72.0f, 20.0f, ccc3(78, 26, 36), ccc3(244, 63, 94), ccc3(255, 226, 232), 0.56f);
            auto* clrBtn = CCMenuItemSpriteExtra::create(clrSpr, this, menu_selector(SettingsPopup::onManualActionBtn));
            clrBtn->setTag(2);
            clrBtn->setPosition({ leftX + fullW - 36.0f, topY });
            menu->addChild(clrBtn);

            constexpr int kPerPage = 5;
            int totalPages = std::max(1, static_cast<int>((markers.size() + kPerPage - 1) / kPerPage));
            m_manualPage = std::clamp(m_manualPage, 0, totalPages - 1);

            if (markers.empty()) {
                auto* emptyLbl = CCLabelBMFont::create(
                    "No manual markers for this level yet.\nTurn on 'Capture Run Clicks' or press '+ MARK HERE' while playing!",
                    "chatFont.fnt"
                );
                emptyLbl->setAlignment(kCCTextAlignmentCenter);
                emptyLbl->setScale(0.64f);
                emptyLbl->setColor(ccc3(165, 178, 202));
                emptyLbl->setPosition({ kPanelW * 0.5f, topY - 76.0f });
                m_contentNode->addChild(emptyLbl, 2);
            } else {
                int startIdx = m_manualPage * kPerPage;
                int endIdx = std::min(static_cast<int>(markers.size()), startIdx + kPerPage);
                for (int idx = startIdx; idx < endIdx; ++idx) {
                    int row = idx - startIdx;
                    float ry = topY - 30.0f - row * 24.0f;
                    auto const& mk = markers[static_cast<size_t>(idx)];
                    auto tier = evaluateTier(mk.frames, static_cast<double>(m_cfg.refFps));

                    std::string rowText = fmt::format(
                        "#{:<2}  X:{:<5.0f}  {}  [{}]",
                        idx + 1, mk.x, formatFrames(mk.frames), clickKindFull(mk.kind)
                    );
                    auto* lbl = CCLabelBMFont::create(rowText.c_str(), "chatFont.fnt");
                    lbl->setAnchorPoint({ 0.0f, 0.5f });
                    lbl->setScale(0.60f);
                    lbl->setColor(tier.color3);
                    lbl->setPosition({ leftX, ry });
                    m_contentNode->addChild(lbl, 2);

                    // Encode idx and action into tag: (idx * 10 + op)
                    // op: 0 = -1F, 1 = +1F, 2 = -0.25F, 3 = +0.25F, 4 = cycle kind, 5 = delete
                    auto makeOpBtn = [&](const char* txt, float bx, float bw, int op, ccColor3B bg, ccColor3B bd) {
                        auto* s = makeStyledButtonSprite(txt, bw, 18.0f, bg, bd, ccc3(235, 242, 255), 0.52f);
                        auto* b = CCMenuItemSpriteExtra::create(s, this, menu_selector(SettingsPopup::onMarkerRowBtn));
                        b->setTag(idx * 10 + op);
                        b->setPosition({ bx, ry });
                        menu->addChild(b);
                    };

                    float rx = leftX + fullW;
                    makeOpBtn("X",     rx - 10.0f, 18.0f, 5, ccc3(76, 24, 34), ccc3(244, 63, 94));
                    makeOpBtn("TYPE",  rx - 42.0f, 36.0f, 4, ccc3(34, 42, 60), ccc3(100, 122, 160));
                    makeOpBtn("+1F",   rx - 78.0f, 26.0f, 1, ccc3(28, 38, 52), ccc3(78, 104, 138));
                    makeOpBtn("-1F",   rx - 108.0f, 26.0f, 0, ccc3(28, 38, 52), ccc3(78, 104, 138));
                    makeOpBtn("+0.25", rx - 144.0f, 36.0f, 3, ccc3(26, 34, 46), ccc3(70, 92, 124));
                    makeOpBtn("-0.25", rx - 184.0f, 36.0f, 2, ccc3(26, 34, 46), ccc3(70, 92, 124));
                }
            }

            // Pagination footer
            auto* pageLbl = CCLabelBMFont::create(
                fmt::format("Markers: {}  |  Page {}/{}", markers.size(), m_manualPage + 1, totalPages).c_str(),
                "chatFont.fnt"
            );
            pageLbl->setScale(0.60f);
            pageLbl->setColor(ccc3(175, 190, 214));
            pageLbl->setPosition({ kPanelW * 0.5f, 22.0f });
            m_contentNode->addChild(pageLbl, 2);

            auto* prevSpr = makeStyledButtonSprite("< PREV", 56.0f, 19.0f, ccc3(26, 32, 44), ccc3(74, 92, 122), ccc3(220, 232, 248), 0.56f);
            auto* prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(SettingsPopup::onManualPageBtn));
            prevBtn->setTag(-1);
            prevBtn->setPosition({ leftX + 28.0f, 22.0f });
            menu->addChild(prevBtn);

            auto* nextSpr = makeStyledButtonSprite("NEXT >", 56.0f, 19.0f, ccc3(26, 32, 44), ccc3(74, 92, 122), ccc3(220, 232, 248), 0.56f);
            auto* nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(SettingsPopup::onManualPageBtn));
            nextBtn->setTag(1);
            nextBtn->setPosition({ leftX + fullW - 28.0f, 22.0f });
            menu->addChild(nextBtn);
        }
    }

    // Callbacks
    void onTabSelect(CCObject* sender) {
        m_activeTab = static_cast<CCNode*>(sender)->getTag();
        rebuildContent();
    }

    void onCloseBtn(CCObject*) {
        this->removeFromParent();
    }

    void onModeBtn(CCObject* sender) {
        m_cfg.mode = (static_cast<CCNode*>(sender)->getTag() == 1) ? CounterMode::Manual : CounterMode::Auto;
        applyAndNotify();
    }

    void onFpsPresetBtn(CCObject* sender) {
        m_cfg.refFps = static_cast<int64_t>(static_cast<CCNode*>(sender)->getTag());
        applyAndNotify();
    }

    void onToggleBtn(CCObject* sender) {
        switch (static_cast<CCNode*>(sender)->getTag()) {
            case 100: m_cfg.enabled = !m_cfg.enabled; break;
            case 101: m_cfg.autoTpsAdapt = !m_cfg.autoTpsAdapt; break;
            case 102: m_cfg.showCircles = !m_cfg.showCircles; break;
            case 103: m_cfg.showCircleLabel = !m_cfg.showCircleLabel; break;
            case 110: m_cfg.showHud = !m_cfg.showHud; break;
            case 111: m_cfg.showPercentages = !m_cfg.showPercentages; break;
            case 112: m_cfg.showSpecialStats = !m_cfg.showSpecialStats; break;
            case 113: m_cfg.showBanner = !m_cfg.showBanner; break;
            case 114: m_cfg.showRawPrecision = !m_cfg.showRawPrecision; break;
            case 120: m_cfg.soundEnabled = !m_cfg.soundEnabled; break;
            case 121: m_cfg.dynamicPitch = !m_cfg.dynamicPitch; break;
            case 130: m_cfg.captureToManual = !m_cfg.captureToManual; break;
            default: break;
        }
        applyAndNotify();
    }

    void onStepperBtn(CCObject* sender) {
        switch (static_cast<CCNode*>(sender)->getTag()) {
            case 200: m_cfg.circleScale = std::max(0.4f, m_cfg.circleScale - 0.1f); break;
            case 201: m_cfg.circleScale = std::min(2.5f, m_cfg.circleScale + 0.1f); break;
            case 202: m_cfg.autoSensitivity = std::max(0.25f, m_cfg.autoSensitivity - 0.1f); break;
            case 203: m_cfg.autoSensitivity = std::min(3.0f, m_cfg.autoSensitivity + 0.1f); break;
            case 204: m_cfg.swiftThresholdMs = std::max(5.0f, m_cfg.swiftThresholdMs - 5.0f); break;
            case 205: m_cfg.swiftThresholdMs = std::min(120.0f, m_cfg.swiftThresholdMs + 5.0f); break;
            case 206: m_cfg.multiclickWindowMs = std::max(20.0f, m_cfg.multiclickWindowMs - 10.0f); break;
            case 207: m_cfg.multiclickWindowMs = std::min(350.0f, m_cfg.multiclickWindowMs + 10.0f); break;
            case 210: m_cfg.bannerDuration = std::max(1.0f, m_cfg.bannerDuration - 0.5f); break;
            case 211: m_cfg.bannerDuration = std::min(10.0f, m_cfg.bannerDuration + 0.5f); break;
            case 220: m_cfg.soundPitch = std::max(0.4f, m_cfg.soundPitch - 0.05f); break;
            case 221: m_cfg.soundPitch = std::min(2.5f, m_cfg.soundPitch + 0.05f); break;
            case 222: m_cfg.soundVolume = std::max(0.0f, m_cfg.soundVolume - 0.05f); break;
            case 223: m_cfg.soundVolume = std::min(1.0f, m_cfg.soundVolume + 0.05f); break;
            default: break;
        }
        applyAndNotify();
    }

    void onCyclePresetBtn(CCObject* sender) {
        if (static_cast<CCNode*>(sender)->getTag() == 1) {
            static const std::vector<std::string> kComments = {
                "Open Window Top 1",
                "ILL Showcase",
                "Frame Perfect Run",
                "Extreme Demon Verify",
                ""
            };
            auto it = std::find(kComments.begin(), kComments.end(), m_cfg.bannerComment);
            size_t next = (it == kComments.end()) ? 0 : (static_cast<size_t>(it - kComments.begin()) + 1) % kComments.size();
            m_cfg.bannerComment = kComments[next];
        } else {
            static const std::vector<std::string> kPrecs = {
                "Raw Precision: 240Hz in-game",
                "Raw Precision: 240Hz CBF",
                "Raw Precision: 6000Hz Bot",
                "Raw Precision: 360Hz TPS"
            };
            auto it = std::find(kPrecs.begin(), kPrecs.end(), m_cfg.rawPrecisionText);
            size_t next = (it == kPrecs.end()) ? 0 : (static_cast<size_t>(it - kPrecs.begin()) + 1) % kPrecs.size();
            m_cfg.rawPrecisionText = kPrecs[next];
        }
        applyAndNotify();
    }

    void onSoundPresetBtn(CCObject* sender) {
        const char* pKeys[4] = { "crystal", "soft", "tick", "deep" };
        int idx = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 3);
        m_cfg.soundPreset = pKeys[idx];
        applyAndNotify();
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onTestBellBtn(CCObject*) {
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onManualActionBtn(CCObject* sender) {
        if (static_cast<CCNode*>(sender)->getTag() == 1) {
            double defaultF = std::max(1.0, static_cast<double>(m_cfg.refFps) / 80.0);
            MarkerStore::get().addMarker(m_currentPlayerX, m_currentSimTime, defaultF, ClickKind::Normal);
        } else if (static_cast<CCNode*>(sender)->getTag() == 2) {
            MarkerStore::get().clearAll();
            m_manualPage = 0;
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
            case 1: mk.frames = std::min(10000.0, mk.frames + 1.0); break;
            case 2: mk.frames = std::max(0.05, mk.frames - 0.25); break;
            case 3: mk.frames = std::min(10000.0, mk.frames + 0.25); break;
            case 4: {
                int nextK = (static_cast<int>(mk.kind) + 1) % 5;
                mk.kind = static_cast<ClickKind>(nextK);
                break;
            }
            case 5:
                MarkerStore::get().removeAt(static_cast<size_t>(idx));
                rebuildContent();
                return;
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

    bool setup(std::function<void()> onChanged, float playerX, double simTime) {
        if (!FLAlertLayer::init(175)) return false;

        if (!m_buttonMenu) {
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({ 0.0f, 0.0f });
            m_mainLayer->addChild(m_buttonMenu, 10);
        }

        m_cfg = ModConfig::load();
        m_onChanged = std::move(onChanged);
        m_currentPlayerX = playerX;
        m_currentSimTime = simTime;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        m_panel = CCNode::create();
        m_panel->setContentSize({ kPanelW, kPanelH });
        m_panel->setPosition({ winSize.width * 0.5f - kPanelW * 0.5f, winSize.height * 0.5f - kPanelH * 0.5f });
        m_mainLayer->addChild(m_panel, 5);

        // Main dark obsidian card background (solid, crisp, clean)
        auto* bg = CCScale9Sprite::create("square02b_001.png");
        bg->setColor(ccc3(15, 19, 27));
        bg->setOpacity(246);
        bg->setContentSize({ kPanelW, kPanelH });
        bg->setAnchorPoint({ 0.0f, 0.0f });
        m_panel->addChild(bg, 0);

        // Header strip + crisp border
        auto* decor = CCDrawNode::create();
        CCPoint headerRect[4] = { ccp(0, kPanelH - 34.0f), ccp(kPanelW, kPanelH - 34.0f), ccp(kPanelW, kPanelH), ccp(0, kPanelH) };
        decor->drawPolygon(headerRect, 4, ccc4f(0.09f, 0.12f, 0.18f, 1.0f), 0.0f, ccc4f(0, 0, 0, 0));

        CCPoint topAccent[4] = { ccp(0, kPanelH - 2.5f), ccp(kPanelW, kPanelH - 2.5f), ccp(kPanelW, kPanelH), ccp(0, kPanelH) };
        decor->drawPolygon(topAccent, 4, ccc4f(0.22f, 0.74f, 0.97f, 1.0f), 0.0f, ccc4f(0, 0, 0, 0));

        CCPoint outerBorder[4] = { ccp(0, 0), ccp(kPanelW, 0), ccp(kPanelW, kPanelH), ccp(0, kPanelH) };
        decor->drawPolygon(outerBorder, 4, ccc4f(0, 0, 0, 0), 1.0f, ccc4f(0.26f, 0.34f, 0.46f, 0.95f));

        decor->drawSegment(ccp(12.0f, kPanelH - 63.0f), ccp(kPanelW - 12.0f, kPanelH - 63.0f), 0.5f, ccc4f(0.24f, 0.31f, 0.42f, 0.8f));
        m_panel->addChild(decor, 1);

        auto* title = CCLabelBMFont::create("FRAME WINDOW COUNTER", "bigFont.fnt");
        title->setAnchorPoint({ 0.0f, 0.5f });
        title->setScale(0.42f);
        title->setColor(ccc3(240, 246, 255));
        title->setPosition({ 16.0f, kPanelH - 18.0f });
        m_panel->addChild(title, 2);

        auto* ver = CCLabelBMFont::create("v1.0  |  GD 2.2081", "chatFont.fnt");
        ver->setAnchorPoint({ 0.0f, 0.5f });
        ver->setScale(0.56f);
        ver->setColor(ccc3(110, 180, 230));
        ver->setPosition({ 196.0f, kPanelH - 18.5f });
        m_panel->addChild(ver, 2);

        rebuildContent();
        this->setKeypadEnabled(true);
        this->setTouchEnabled(true);
        return true;
    }

public:
    static SettingsPopup* create(std::function<void()> onChanged, float playerX, double simTime) {
        auto* ret = new SettingsPopup();
        if (ret && ret->setup(std::move(onChanged), playerX, simTime)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

} // namespace fwc
