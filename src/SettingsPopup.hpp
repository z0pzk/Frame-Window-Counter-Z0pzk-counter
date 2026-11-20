#pragma once

#include "AudioManager.hpp"
#include "Config.hpp"
#include "MarkerStore.hpp"
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
    float m_currentPlayerX = 0.0f;
    double m_currentSimTime = 0.0;

    int m_activeTab = 0; // 0=General, 1=HUD & Banner, 2=Audio, 3=Manual
    int m_manualPage = 0;

    CCNode* m_panel = nullptr;
    CCNode* m_contentNode = nullptr;
    CCMenu* m_tabMenu = nullptr;

    static constexpr float kPanelW = 432.0f;
    static constexpr float kPanelH = 278.0f;

    void notifyLive() {
        m_cfg.save();
        if (m_onChanged) m_onChanged();
    }

    void applyAndRebuild() {
        notifyLive();
        rebuildContent();
    }

    CCLabelBMFont* addLabel(CCNode* parent, std::string const& text, float x, float y, float scale = 0.42f, const char* font = "bigFont.fnt", ccColor3B col = ccc3(255, 255, 255)) {
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
            0.70f
        );
        toggler->toggle(value);
        toggler->setTag(tag);
        toggler->setPosition({ x + 12.0f, y });
        menu->addChild(toggler);

        addLabel(parent, title, x + 28.0f, y, 0.36f, "bigFont.fnt", ccc3(245, 245, 245));
    }

    void addStepperRow(CCNode* parent, CCMenu* menu, std::string const& title, std::string const& valText, int minusTag, int plusTag, float x, float y, float rowW) {
        addLabel(parent, title, x, y, 0.36f, "bigFont.fnt", ccc3(245, 245, 245));

        float rightEdge = x + rowW;

        auto* rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        rSpr->setFlipX(true);
        rSpr->setScale(0.45f);
        auto* plusBtn = CCMenuItemSpriteExtra::create(rSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        plusBtn->setTag(plusTag);
        plusBtn->setPosition({ rightEdge - 10.0f, y });
        menu->addChild(plusBtn);

        auto* valLbl = CCLabelBMFont::create(valText.c_str(), "goldFont.fnt");
        valLbl->setScale(0.56f);
        valLbl->setPosition({ rightEdge - 46.0f, y });
        parent->addChild(valLbl, 3);

        auto* lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        lSpr->setScale(0.45f);
        auto* minusBtn = CCMenuItemSpriteExtra::create(lSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        minusBtn->setTag(minusTag);
        minusBtn->setPosition({ rightEdge - 82.0f, y });
        menu->addChild(minusBtn);
    }

    void rebuildTabs() {
        if (m_tabMenu) {
            m_tabMenu->removeFromParent();
            m_tabMenu = nullptr;
        }
        m_tabMenu = CCMenu::create();
        m_tabMenu->setPosition({ 0.0f, 0.0f });
        m_panel->addChild(m_tabMenu, 10);

        const char* names[4] = { "General", "HUD", "Audio", "Manual" };
        float tabW = 90.0f;
        float gap = 8.0f;
        float totalW = 4 * tabW + 3 * gap;
        float startX = (kPanelW - totalW) * 0.5f + tabW * 0.5f;
        float tabY = kPanelH - 46.0f;

        for (int i = 0; i < 4; ++i) {
            bool active = (m_activeTab == i);
            auto* spr = ButtonSprite::create(
                names[i],
                static_cast<int>(tabW),
                true,
                active ? "goldFont.fnt" : "bigFont.fnt",
                active ? "GJ_button_01.png" : "GJ_button_04.png",
                24.0f,
                active ? 0.62f : 0.44f
            );
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onTabSelect));
            btn->setTag(i);
            btn->setPosition({ startX + i * (tabW + gap), tabY });
            m_tabMenu->addChild(btn);
        }

        // Standard native GD Close button
        auto* closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.82f);
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

        // Soft native GD inset box for the tab body
        auto* innerBg = CCScale9Sprite::create("square02_001.png");
        innerBg->setOpacity(95);
        innerBg->setContentSize({ kPanelW - 28.0f, kPanelH - 80.0f });
        innerBg->setPosition({ kPanelW * 0.5f, 14.0f + (kPanelH - 80.0f) * 0.5f });
        m_contentNode->addChild(innerBg, 0);

        auto* menu = CCMenu::create();
        menu->setPosition({ 0.0f, 0.0f });
        m_contentNode->addChild(menu, 6);

        float leftX = 28.0f;
        float fullW = kPanelW - 56.0f;
        float colW = (fullW - 18.0f) * 0.5f;
        float rightX = leftX + colW + 18.0f;
        float topY = kPanelH - 82.0f;

        if (m_activeTab == 0) {
            // GENERAL TAB (Clean & focused)
            addCheckboxRow(m_contentNode, menu, "Enable Mod", m_cfg.enabled, 100, leftX, topY);
            addCheckboxRow(m_contentNode, menu, "Adapt Live TPS", m_cfg.autoTpsAdapt, 101, rightX, topY);

            // Mode buttons (Auto / Manual)
            addLabel(m_contentNode, "Mode:", leftX, topY - 34.0f, 0.38f, "goldFont.fnt");
            bool isAuto = (m_cfg.mode == CounterMode::Auto);

            auto* autoSpr = ButtonSprite::create("Auto", 66, true, "bigFont.fnt", isAuto ? "GJ_button_01.png" : "GJ_button_04.png", 24.0f, 0.48f);
            auto* autoBtn = CCMenuItemSpriteExtra::create(autoSpr, this, menu_selector(SettingsPopup::onModeBtn));
            autoBtn->setTag(0);
            autoBtn->setPosition({ leftX + 104.0f, topY - 34.0f });
            menu->addChild(autoBtn);

            auto* manSpr = ButtonSprite::create("Manual", 72, true, "bigFont.fnt", !isAuto ? "GJ_button_01.png" : "GJ_button_04.png", 24.0f, 0.45f);
            auto* manBtn = CCMenuItemSpriteExtra::create(manSpr, this, menu_selector(SettingsPopup::onModeBtn));
            manBtn->setTag(1);
            manBtn->setPosition({ leftX + 182.0f, topY - 34.0f });
            menu->addChild(manBtn);

            // Direct TextInput for Reference FPS!
            addLabel(m_contentNode, "Ref FPS:", rightX + 36.0f, topY - 34.0f, 0.38f, "goldFont.fnt");
            auto* fpsInput = geode::TextInput::create(78.0f, "240", "bigFont.fnt");
            fpsInput->setFilter("0123456789");
            fpsInput->setMaxCharCount(6);
            fpsInput->setString(fmt::format("{}", m_cfg.refFps));
            fpsInput->setPosition({ rightX + colW - 39.0f, topY - 34.0f });
            fpsInput->setCallback([this](std::string const& str) {
                if (str.empty()) return;
                try {
                    int64_t val = std::stoll(str);
                    if (val >= 30 && val <= 100000) {
                        m_cfg.refFps = val;
                        notifyLive();
                    }
                } catch (...) {}
            });
            m_contentNode->addChild(fpsInput, 8);

            // Click circles & labels
            addCheckboxRow(m_contentNode, menu, "Click Circles", m_cfg.showCircles, 102, leftX, topY - 72.0f);
            addCheckboxRow(m_contentNode, menu, "Circle Labels", m_cfg.showCircleLabel, 103, rightX, topY - 72.0f);

            // Circle Size stepper
            addStepperRow(m_contentNode, menu, "Circle Size", fmt::format("{:.1f}x", m_cfg.circleScale), 200, 201, leftX, topY - 106.0f, colW);

            // Optional Swift / Multiclick detection switches
            addCheckboxRow(m_contentNode, menu, "Detect Swifts", m_cfg.detectSwifts, 104, leftX, topY - 140.0f);
            addCheckboxRow(m_contentNode, menu, "Detect Multiclicks", m_cfg.detectMulticlicks, 105, rightX, topY - 140.0f);

            auto* tip = CCLabelBMFont::create(
                "Type any Ref FPS (e.g. 60, 240, 360, 6000). Circles spawn directly in level space.",
                "chatFont.fnt"
            );
            tip->setScale(0.58f);
            tip->setColor(ccc3(195, 202, 214));
            tip->setPosition({ kPanelW * 0.5f, 28.0f });
            m_contentNode->addChild(tip, 3);
        }
        else if (m_activeTab == 1) {
            // HUD & BANNER TAB (With direct text inputs, empty by default!)
            addCheckboxRow(m_contentNode, menu, "Bottom-Left HUD", m_cfg.showHud, 110, leftX, topY);
            addCheckboxRow(m_contentNode, menu, "Percentages (%)", m_cfg.showPercentages, 111, rightX, topY);

            addCheckboxRow(m_contentNode, menu, "Show S-Fr/Sw/Mul", m_cfg.showSpecialStats, 112, leftX, topY - 30.0f);
            addCheckboxRow(m_contentNode, menu, "Intro Banner", m_cfg.showBanner, 113, rightX, topY - 30.0f);

            addCheckboxRow(m_contentNode, menu, "Precision Line", m_cfg.showRawPrecision, 114, leftX, topY - 60.0f);
            addStepperRow(m_contentNode, menu, "Banner Time", fmt::format("{:.1f}s", m_cfg.bannerDuration), 210, 211, rightX, topY - 60.0f, colW);

            // Direct TextInput for Banner Comment
            addLabel(m_contentNode, "Comment:", leftX, topY - 100.0f, 0.40f, "goldFont.fnt");
            auto* commentInput = geode::TextInput::create(262.0f, "Empty (optional)...", "chatFont.fnt");
            commentInput->setMaxCharCount(48);
            commentInput->setString(m_cfg.bannerComment);
            commentInput->setPosition({ leftX + fullW - 131.0f, topY - 100.0f });
            commentInput->setCallback([this](std::string const& str) {
                m_cfg.bannerComment = str;
                notifyLive();
            });
            m_contentNode->addChild(commentInput, 8);

            // Direct TextInput for Precision Line
            addLabel(m_contentNode, "Precision:", leftX, topY - 136.0f, 0.40f, "goldFont.fnt");
            auto* precInput = geode::TextInput::create(262.0f, "Empty (optional)...", "chatFont.fnt");
            precInput->setMaxCharCount(48);
            precInput->setString(m_cfg.rawPrecisionText);
            precInput->setPosition({ leftX + fullW - 131.0f, topY - 136.0f });
            precInput->setCallback([this](std::string const& str) {
                m_cfg.rawPrecisionText = str;
                notifyLive();
            });
            m_contentNode->addChild(precInput, 8);

            auto* tip = CCLabelBMFont::create(
                "Leave Comment or Precision empty to hide them from the intro banner.",
                "chatFont.fnt"
            );
            tip->setScale(0.58f);
            tip->setColor(ccc3(195, 202, 214));
            tip->setPosition({ kPanelW * 0.5f, 28.0f });
            m_contentNode->addChild(tip, 3);
        }
        else if (m_activeTab == 2) {
            // AUDIO TAB (Classic bell + Custom sounds folder!)
            addCheckboxRow(m_contentNode, menu, "Bell Sound", m_cfg.soundEnabled, 120, leftX, topY);
            addCheckboxRow(m_contentNode, menu, "Auto Pitch", m_cfg.dynamicPitch, 121, rightX, topY);

            addStepperRow(m_contentNode, menu, "Base Pitch", fmt::format("{:.2f}x", m_cfg.soundPitch), 220, 221, leftX, topY - 34.0f, colW);
            addStepperRow(m_contentNode, menu, "Volume", fmt::format("{:.0f}%", m_cfg.soundVolume * 100.0f), 222, 223, rightX, topY - 34.0f, colW);

            addLabel(m_contentNode, "Preset:", leftX, topY - 70.0f, 0.40f, "goldFont.fnt");
            const char* pKeys[5] = { "classic", "fb", "chime", "tick", "custom" };
            const char* pLabels[5] = { "Classic", "FB Bell", "Chime", "Tick", "Custom" };
            for (int i = 0; i < 5; ++i) {
                bool sel = (m_cfg.soundPreset == pKeys[i]);
                auto* bSpr = ButtonSprite::create(
                    pLabels[i], 58, true, "bigFont.fnt",
                    sel ? "GJ_button_01.png" : "GJ_button_04.png",
                    22.0f, 0.38f
                );
                auto* bBtn = CCMenuItemSpriteExtra::create(bSpr, this, menu_selector(SettingsPopup::onSoundPresetBtn));
                bBtn->setTag(i);
                bBtn->setPosition({ leftX + 94.0f + i * 64.0f, topY - 70.0f });
                menu->addChild(bBtn);
            }

            // Custom sound selector + Open Folder button
            addLabel(m_contentNode, "Custom:", leftX, topY - 106.0f, 0.40f, "goldFont.fnt");
            auto* customInput = geode::TextInput::create(166.0f, "bell.wav / .mp3 / .ogg", "chatFont.fnt");
            customInput->setMaxCharCount(120);
            customInput->setString(m_cfg.customSoundFile);
            customInput->setPosition({ leftX + 150.0f, topY - 106.0f });
            customInput->setCallback([this](std::string const& str) {
                m_cfg.customSoundFile = str;
                if (!str.empty()) m_cfg.soundPreset = "custom";
                notifyLive();
            });
            m_contentNode->addChild(customInput, 8);

            auto* pickSpr = ButtonSprite::create("Next", 46, true, "bigFont.fnt", "GJ_button_05.png", 22.0f, 0.42f);
            auto* pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(SettingsPopup::onCycleCustomSoundBtn));
            pickBtn->setPosition({ leftX + 266.0f, topY - 106.0f });
            menu->addChild(pickBtn);

            auto* folderSpr = ButtonSprite::create("Folder", 64, true, "bigFont.fnt", "GJ_button_04.png", 22.0f, 0.42f);
            auto* folderBtn = CCMenuItemSpriteExtra::create(folderSpr, this, menu_selector(SettingsPopup::onOpenSoundsFolderBtn));
            folderBtn->setPosition({ leftX + 332.0f, topY - 106.0f });
            menu->addChild(folderBtn);

            // Play test button
            auto* testSpr = ButtonSprite::create("Test Bell", 110, true, "goldFont.fnt", "GJ_button_01.png", 26.0f, 0.62f);
            auto* testBtn = CCMenuItemSpriteExtra::create(testSpr, this, menu_selector(SettingsPopup::onTestBellBtn));
            testBtn->setPosition({ kPanelW * 0.5f, topY - 144.0f });
            menu->addChild(testBtn);

            auto* tip = CCLabelBMFont::create(
                "Drop any .wav/.mp3/.ogg into Folder, then press Next or type its filename.",
                "chatFont.fnt"
            );
            tip->setScale(0.56f);
            tip->setColor(ccc3(195, 202, 214));
            tip->setPosition({ kPanelW * 0.5f, 28.0f });
            m_contentNode->addChild(tip, 3);
        }
        else {
            // MANUAL MARKER EDITOR TAB
            auto& markers = MarkerStore::get().markers();

            addCheckboxRow(m_contentNode, menu, "Capture Clicks", m_cfg.captureToManual, 130, leftX, topY);

            auto* addSpr = ButtonSprite::create("+ Mark Here", 84, true, "bigFont.fnt", "GJ_button_01.png", 22.0f, 0.38f);
            auto* addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(SettingsPopup::onManualActionBtn));
            addBtn->setTag(1);
            addBtn->setPosition({ leftX + 236.0f, topY });
            menu->addChild(addBtn);

            auto* clrSpr = ButtonSprite::create("Clear", 58, true, "bigFont.fnt", "GJ_button_06.png", 22.0f, 0.40f);
            auto* clrBtn = CCMenuItemSpriteExtra::create(clrSpr, this, menu_selector(SettingsPopup::onManualActionBtn));
            clrBtn->setTag(2);
            clrBtn->setPosition({ leftX + fullW - 30.0f, topY });
            menu->addChild(clrBtn);

            constexpr int kPerPage = 5;
            int totalPages = std::max(1, static_cast<int>((markers.size() + kPerPage - 1) / kPerPage));
            m_manualPage = std::clamp(m_manualPage, 0, totalPages - 1);

            if (markers.empty()) {
                auto* emptyLbl = CCLabelBMFont::create(
                    "No manual markers for this level yet.\nEnable 'Capture Clicks' during a run or press '+ Mark Here'.",
                    "chatFont.fnt"
                );
                emptyLbl->setAlignment(kCCTextAlignmentCenter);
                emptyLbl->setScale(0.64f);
                emptyLbl->setColor(ccc3(205, 212, 224));
                emptyLbl->setPosition({ kPanelW * 0.5f, topY - 76.0f });
                m_contentNode->addChild(emptyLbl, 3);
            } else {
                int startIdx = m_manualPage * kPerPage;
                int endIdx = std::min(static_cast<int>(markers.size()), startIdx + kPerPage);
                for (int idx = startIdx; idx < endIdx; ++idx) {
                    int row = idx - startIdx;
                    float ry = topY - 30.0f - row * 23.0f;
                    auto const& mk = markers[static_cast<size_t>(idx)];
                    auto tier = evaluateTier(mk.frames, static_cast<double>(m_cfg.refFps));

                    std::string rowText = fmt::format(
                        "#{:<2} X:{:<5.0f} {} [{}]",
                        idx + 1, mk.x, formatFrames(mk.frames), clickKindFull(mk.kind)
                    );
                    auto* lbl = CCLabelBMFont::create(rowText.c_str(), "chatFont.fnt");
                    lbl->setAnchorPoint({ 0.0f, 0.5f });
                    lbl->setScale(0.60f);
                    lbl->setColor(tier.color3);
                    lbl->setPosition({ leftX, ry });
                    m_contentNode->addChild(lbl, 3);

                    auto makeOpBtn = [&](const char* txt, float bx, int bw, int op, const char* tex) {
                        auto* s = ButtonSprite::create(txt, bw, true, "bigFont.fnt", tex, 18.0f, 0.34f);
                        auto* b = CCMenuItemSpriteExtra::create(s, this, menu_selector(SettingsPopup::onMarkerRowBtn));
                        b->setTag(idx * 10 + op);
                        b->setPosition({ bx, ry });
                        menu->addChild(b);
                    };

                    float rx = leftX + fullW;
                    makeOpBtn("X",     rx - 10.0f,  18, 5, "GJ_button_06.png");
                    makeOpBtn("Type",  rx - 40.0f,  36, 4, "GJ_button_04.png");
                    makeOpBtn("+1",    rx - 74.0f,  24, 1, "GJ_button_05.png");
                    makeOpBtn("-1",    rx - 102.0f, 24, 0, "GJ_button_05.png");
                    makeOpBtn("+0.25", rx - 138.0f, 38, 3, "GJ_button_04.png");
                    makeOpBtn("-0.25", rx - 180.0f, 38, 2, "GJ_button_04.png");
                }
            }

            auto* pageLbl = CCLabelBMFont::create(
                fmt::format("Markers: {}  |  Page {}/{}", markers.size(), m_manualPage + 1, totalPages).c_str(),
                "chatFont.fnt"
            );
            pageLbl->setScale(0.62f);
            pageLbl->setColor(ccc3(210, 218, 230));
            pageLbl->setPosition({ kPanelW * 0.5f, 28.0f });
            m_contentNode->addChild(pageLbl, 3);

            auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            prevSpr->setScale(0.45f);
            auto* prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(SettingsPopup::onManualPageBtn));
            prevBtn->setTag(-1);
            prevBtn->setPosition({ leftX + 16.0f, 28.0f });
            menu->addChild(prevBtn);

            auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            nextSpr->setFlipX(true);
            nextSpr->setScale(0.45f);
            auto* nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(SettingsPopup::onManualPageBtn));
            nextBtn->setTag(1);
            nextBtn->setPosition({ leftX + fullW - 16.0f, 28.0f });
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
        applyAndRebuild();
    }

    void onToggleBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        switch (tag) {
            case 100: m_cfg.enabled = !m_cfg.enabled; break;
            case 101: m_cfg.autoTpsAdapt = !m_cfg.autoTpsAdapt; break;
            case 102: m_cfg.showCircles = !m_cfg.showCircles; break;
            case 103: m_cfg.showCircleLabel = !m_cfg.showCircleLabel; break;
            case 104: m_cfg.detectSwifts = !m_cfg.detectSwifts; break;
            case 105: m_cfg.detectMulticlicks = !m_cfg.detectMulticlicks; break;
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
        notifyLive();
    }

    void onStepperBtn(CCObject* sender) {
        switch (static_cast<CCNode*>(sender)->getTag()) {
            case 200: m_cfg.circleScale = std::max(0.4f, m_cfg.circleScale - 0.1f); break;
            case 201: m_cfg.circleScale = std::min(2.5f, m_cfg.circleScale + 0.1f); break;
            case 210: m_cfg.bannerDuration = std::max(1.0f, m_cfg.bannerDuration - 0.5f); break;
            case 211: m_cfg.bannerDuration = std::min(10.0f, m_cfg.bannerDuration + 0.5f); break;
            case 220: m_cfg.soundPitch = std::max(0.4f, m_cfg.soundPitch - 0.05f); break;
            case 221: m_cfg.soundPitch = std::min(2.5f, m_cfg.soundPitch + 0.05f); break;
            case 222: m_cfg.soundVolume = std::max(0.0f, m_cfg.soundVolume - 0.05f); break;
            case 223: m_cfg.soundVolume = std::min(1.0f, m_cfg.soundVolume + 0.05f); break;
            default: break;
        }
        applyAndRebuild();
    }

    void onSoundPresetBtn(CCObject* sender) {
        const char* pKeys[5] = { "classic", "fb", "chime", "tick", "custom" };
        int idx = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 4);
        m_cfg.soundPreset = pKeys[idx];
        applyAndRebuild();
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.customSoundFile, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onCycleCustomSoundBtn(CCObject*) {
        auto files = AudioManager::get().listCustomSounds();
        if (files.empty()) {
            FLAlertLayer::create(
                "No Custom Sounds",
                "Put any .wav, .mp3, or .ogg file into the sounds folder first (press <cg>Folder</c>).",
                "OK"
            )->show();
            return;
        }
        auto it = std::find(files.begin(), files.end(), m_cfg.customSoundFile);
        size_t next = (it == files.end()) ? 0 : (static_cast<size_t>(it - files.begin()) + 1) % files.size();
        m_cfg.customSoundFile = files[next];
        m_cfg.soundPreset = "custom";
        applyAndRebuild();
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.customSoundFile, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onOpenSoundsFolderBtn(CCObject*) {
        auto dir = AudioManager::get().getCustomSoundsDir();
        geode::utils::file::openFolder(dir);
    }

    void onTestBellBtn(CCObject*) {
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.customSoundFile, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onManualActionBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag == 1) {
            double defaultF = std::max(1.0, static_cast<double>(m_cfg.refFps) / 240.0);
            MarkerStore::get().addMarker(m_currentPlayerX, m_currentSimTime, defaultF, ClickKind::Normal);
        } else if (tag == 2) {
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
        if (!FLAlertLayer::init(165)) return false;

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

        // Clean native GD dark rounded panel (GJ_square05.png)
        auto* bg = CCScale9Sprite::create("GJ_square05.png");
        bg->setContentSize({ kPanelW, kPanelH });
        bg->setPosition({ kPanelW * 0.5f, kPanelH * 0.5f });
        m_panel->addChild(bg, 0);

        auto* title = CCLabelBMFont::create("Frame Window Counter", "goldFont.fnt");
        title->setScale(0.76f);
        title->setPosition({ kPanelW * 0.5f, kPanelH - 18.0f });
        m_panel->addChild(title, 3);

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
