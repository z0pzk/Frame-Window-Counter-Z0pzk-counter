#pragma once

#include "AudioManager.hpp"
#include "Config.hpp"
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
    float m_currentPlayerX = 0.0f;
    float m_currentPlayerY = 30.0f;

    int m_activeTab = 0;       // 0=General, 1=HUD, 2=Audio, 3=Manual
    int m_audioSubTab = 0;     // 0=Default (All), 1=Per-Tier Overrides
    int m_selectedTierIdx = 0; // index into buildTiers() (defaults to tightest tier like 0-1 or 1!)
    bool m_tierIdxInited = false;
    int m_manualPage = 0;
    std::string m_draftMarkerComment;

    CCNode* m_panel = nullptr;
    CCNode* m_contentNode = nullptr;
    CCMenu* m_tabMenu = nullptr;

    static constexpr float kPanelW = 446.0f;
    static constexpr float kPanelH = 296.0f;

    void notifyLive() {
        m_cfg.save();
        if (m_onChanged) m_onChanged();
    }

    void applyAndRebuild() {
        notifyLive();
        rebuildContent();
    }

    std::vector<TierDef> currentTiers() const {
        return buildTiers(m_cfg.tableStyle, static_cast<double>(m_cfg.refFps));
    }

    TierDef activeTierDef() {
        auto tiers = currentTiers();
        if (!m_tierIdxInited && !tiers.empty()) {
            // Default to the tightest tier (e.g. "0-1" or "1") because that's what people override first!
            m_selectedTierIdx = static_cast<int>(tiers.size()) - 1;
            m_tierIdxInited = true;
        }
        m_selectedTierIdx = std::clamp(m_selectedTierIdx, 0, static_cast<int>(tiers.size()) - 1);
        return tiers[static_cast<size_t>(m_selectedTierIdx)];
    }

    CCLabelBMFont* addLabel(CCNode* parent, std::string const& text, float x, float y, float scale = 0.36f, const char* font = "bigFont.fnt", ccColor3B col = ccc3(255, 255, 255)) {
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
            0.66f
        );
        toggler->toggle(value);
        toggler->setTag(tag);
        toggler->setPosition({ x + 11.0f, y });
        menu->addChild(toggler);

        addLabel(parent, title, x + 26.0f, y, 0.34f, "bigFont.fnt", ccc3(245, 245, 245));
    }

    void addStepperRow(CCNode* parent, CCMenu* menu, std::string const& title, std::string const& valText, int minusTag, int plusTag, float x, float y, float rowW) {
        addLabel(parent, title, x, y, 0.34f, "bigFont.fnt", ccc3(245, 245, 245));

        float rightEdge = x + rowW;

        auto* rSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        rSpr->setFlipX(true);
        rSpr->setScale(0.42f);
        auto* plusBtn = CCMenuItemSpriteExtra::create(rSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        plusBtn->setTag(plusTag);
        plusBtn->setPosition({ rightEdge - 10.0f, y });
        menu->addChild(plusBtn);

        auto* valLbl = CCLabelBMFont::create(valText.c_str(), "goldFont.fnt");
        valLbl->setScale(0.54f);
        valLbl->setPosition({ rightEdge - 44.0f, y });
        parent->addChild(valLbl, 3);

        auto* lSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        lSpr->setScale(0.42f);
        auto* minusBtn = CCMenuItemSpriteExtra::create(lSpr, this, menu_selector(SettingsPopup::onStepperBtn));
        minusBtn->setTag(minusTag);
        minusBtn->setPosition({ rightEdge - 78.0f, y });
        menu->addChild(minusBtn);
    }

    CCNode* makeMiniBtn(const char* text, float w, float h, const char* bgTex) {
        auto* root = CCNode::create();
        root->setContentSize({ w, h });
        auto* bg = CCScale9Sprite::create(bgTex);
        bg->setContentSize({ w, h });
        bg->setPosition({ w * 0.5f, h * 0.5f });
        root->addChild(bg, 0);
        auto* lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.30f);
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

        const char* names[4] = { "General", "HUD", "Audio", "Manual" };
        float tabW = 92.0f;
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
                active ? 0.60f : 0.42f
            );
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onTabSelect));
            btn->setTag(i);
            btn->setPosition({ startX + i * (tabW + gap), tabY });
            m_tabMenu->addChild(btn);
        }

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

        auto* innerBg = CCScale9Sprite::create("square02_001.png");
        innerBg->setOpacity(95);
        innerBg->setContentSize({ kPanelW - 26.0f, kPanelH - 78.0f });
        innerBg->setPosition({ kPanelW * 0.5f, 13.0f + (kPanelH - 78.0f) * 0.5f });
        m_contentNode->addChild(innerBg, 0);

        auto* menu = CCMenu::create();
        menu->setPosition({ 0.0f, 0.0f });
        m_contentNode->addChild(menu, 6);

        float leftX = 24.0f;
        float fullW = kPanelW - 48.0f;
        float colW = (fullW - 16.0f) * 0.5f;
        float rightX = leftX + colW + 16.0f;
        float topY = kPanelH - 80.0f;

        if (m_activeTab == 0) {
            // GENERAL TAB
            addCheckboxRow(m_contentNode, menu, "Enable Mod", m_cfg.enabled, 100, leftX, topY);
            addCheckboxRow(m_contentNode, menu, "Ignore Safe", m_cfg.ignoreSafeClicks, 105, rightX, topY);

            addLabel(m_contentNode, "Mode:", leftX, topY - 30.0f, 0.34f, "goldFont.fnt");
            bool isAuto = (m_cfg.mode == CounterMode::Auto);

            auto* autoBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn("Auto", 56.0f, 21.0f, isAuto ? "GJ_button_01.png" : "GJ_button_04.png"),
                this, menu_selector(SettingsPopup::onModeBtn)
            );
            autoBtn->setTag(0);
            autoBtn->setPosition({ leftX + 84.0f, topY - 30.0f });
            menu->addChild(autoBtn);

            auto* manBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn("Manual", 62.0f, 21.0f, !isAuto ? "GJ_button_01.png" : "GJ_button_04.png"),
                this, menu_selector(SettingsPopup::onModeBtn)
            );
            manBtn->setTag(1);
            manBtn->setPosition({ leftX + 148.0f, topY - 30.0f });
            menu->addChild(manBtn);

            addLabel(m_contentNode, "Ref FPS:", rightX + 10.0f, topY - 30.0f, 0.34f, "goldFont.fnt");
            auto* fpsInput = geode::TextInput::create(78.0f, "240", "bigFont.fnt");
            fpsInput->setFilter("0123456789");
            fpsInput->setMaxCharCount(6);
            fpsInput->setString(fmt::format("{}", m_cfg.refFps));
            fpsInput->setPosition({ rightX + colW - 39.0f, topY - 30.0f });
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

            addLabel(m_contentNode, "Tiers:", leftX, topY - 62.0f, 0.34f, "goldFont.fnt");
            bool isCbfTbl = (m_cfg.tableStyle == TableStyle::CbfRanges);

            auto* cbfTblBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn("0-1 / 1-2", 70.0f, 21.0f, isCbfTbl ? "GJ_button_01.png" : "GJ_button_04.png"),
                this, menu_selector(SettingsPopup::onTableStyleBtn)
            );
            cbfTblBtn->setTag(0);
            cbfTblBtn->setPosition({ leftX + 92.0f, topY - 62.0f });
            menu->addChild(cbfTblBtn);

            auto* discTblBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn("1 / 2 / 3", 70.0f, 21.0f, !isCbfTbl ? "GJ_button_01.png" : "GJ_button_04.png"),
                this, menu_selector(SettingsPopup::onTableStyleBtn)
            );
            discTblBtn->setTag(1);
            discTblBtn->setPosition({ leftX + 168.0f, topY - 62.0f });
            menu->addChild(discTblBtn);

            addLabel(m_contentNode, "Ring:", rightX, topY - 62.0f, 0.34f, "goldFont.fnt");
            const char* rKeys[3] = { "thin", "bold", "thick" };
            const char* rLbls[3] = { "Thin", "Bold", "Thick" };
            for (int i = 0; i < 3; ++i) {
                bool sel = (m_cfg.ringThickness == rKeys[i]);
                auto* rb = CCMenuItemSpriteExtra::create(
                    makeMiniBtn(rLbls[i], 44.0f, 21.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png"),
                    this, menu_selector(SettingsPopup::onRingThicknessBtn)
                );
                rb->setTag(i);
                rb->setPosition({ rightX + 74.0f + i * 48.0f, topY - 62.0f });
                menu->addChild(rb);
            }

            addCheckboxRow(m_contentNode, menu, "CBF Decimals", m_cfg.cbfMode, 101, leftX, topY - 96.0f);
            addCheckboxRow(m_contentNode, menu, "Click Circles", m_cfg.showCircles, 102, rightX, topY - 96.0f);

            addCheckboxRow(m_contentNode, menu, "Keep Circles", m_cfg.persistentCircles, 103, leftX, topY - 126.0f);
            addCheckboxRow(m_contentNode, menu, "Circle Numbers", m_cfg.showCircleLabel, 104, rightX, topY - 126.0f);

            addStepperRow(m_contentNode, menu, "Circle Size", fmt::format("{:.1f}x", m_cfg.circleScale), 200, 201, leftX, topY - 156.0f, colW);

            auto* tip = CCLabelBMFont::create(
                "Ignore Safe skips harmless spam & open clicks. Works with Silicate, xdBot, CBF & Frame Advance.",
                "chatFont.fnt"
            );
            tip->setScale(0.52f);
            tip->setColor(ccc3(195, 202, 214));
            tip->setPosition({ kPanelW * 0.5f, 24.0f });
            m_contentNode->addChild(tip, 3);
        }
        else if (m_activeTab == 1) {
            // HUD & INTRO SCREEN TAB
            addCheckboxRow(m_contentNode, menu, "Top-Left Table", m_cfg.showTierTable, 110, leftX, topY);
            addCheckboxRow(m_contentNode, menu, "ms & Hz Readout", m_cfg.showMsHz, 111, rightX, topY);

            addCheckboxRow(m_contentNode, menu, "Bottom % / Score", m_cfg.showBottomStats, 112, leftX, topY - 28.0f);
            addCheckboxRow(m_contentNode, menu, "Center Watermark", m_cfg.showWatermark, 113, rightX, topY - 28.0f);

            addLabel(m_contentNode, "Intro:", leftX, topY - 60.0f, 0.36f, "goldFont.fnt");
            const char* bNames[3] = { "Off", "1st Try", "Always" };
            for (int i = 0; i < 3; ++i) {
                bool sel = (static_cast<int>(m_cfg.bannerMode) == i);
                auto* b = CCMenuItemSpriteExtra::create(
                    makeMiniBtn(bNames[i], 54.0f, 21.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png"),
                    this, menu_selector(SettingsPopup::onBannerModeBtn)
                );
                b->setTag(i);
                b->setPosition({ leftX + 76.0f + i * 58.0f, topY - 60.0f });
                menu->addChild(b);
            }
            addStepperRow(m_contentNode, menu, "Cmt Fade", fmt::format("{:.1f}s", m_cfg.commentFadeSec), 210, 211, rightX + 20.0f, topY - 60.0f, colW - 20.0f);

            addLabel(m_contentNode, "Watermark:", leftX, topY - 94.0f, 0.36f, "goldFont.fnt");
            auto* wmInput = geode::TextInput::create(262.0f, "FWC COUNTER", "chatFont.fnt");
            wmInput->setMaxCharCount(32);
            wmInput->setString(m_cfg.watermarkText);
            wmInput->setPosition({ leftX + fullW - 131.0f, topY - 94.0f });
            wmInput->setCallback([this](std::string const& str) {
                m_cfg.watermarkText = str;
                notifyLive();
            });
            m_contentNode->addChild(wmInput, 8);

            addLabel(m_contentNode, "Subtitle:", leftX, topY - 126.0f, 0.36f, "goldFont.fnt");
            auto* subInput = geode::TextInput::create(262.0f, "Empty (optional)...", "chatFont.fnt");
            subInput->setMaxCharCount(48);
            subInput->setString(m_cfg.bannerComment);
            subInput->setPosition({ leftX + fullW - 131.0f, topY - 126.0f });
            subInput->setCallback([this](std::string const& str) {
                m_cfg.bannerComment = str;
                notifyLive();
            });
            m_contentNode->addChild(subInput, 8);

            addLabel(m_contentNode, "Bottom:", leftX, topY - 158.0f, 0.36f, "goldFont.fnt");
            auto* precInput = geode::TextInput::create(262.0f, "Empty (optional)...", "chatFont.fnt");
            precInput->setMaxCharCount(48);
            precInput->setString(m_cfg.rawPrecisionText);
            precInput->setPosition({ leftX + fullW - 131.0f, topY - 158.0f });
            precInput->setCallback([this](std::string const& str) {
                m_cfg.rawPrecisionText = str;
                notifyLive();
            });
            m_contentNode->addChild(precInput, 8);
        }
        else if (m_activeTab == 2) {
            // AUDIO TAB (Sub-tabs: Default (All) vs Per-Tier Independent Overrides!)
            auto tiers = currentTiers();
            int ovCount = TierSoundStore::get().countActiveOverrides(tiers);

            auto* defBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn("Default (All)", 118.0f, 22.0f, m_audioSubTab == 0 ? "GJ_button_01.png" : "GJ_button_04.png"),
                this, menu_selector(SettingsPopup::onAudioSubTabBtn)
            );
            defBtn->setTag(0);
            defBtn->setPosition({ leftX + 64.0f, topY + 2.0f });
            menu->addChild(defBtn);

            std::string ovBtnLabel = ovCount > 0
                ? fmt::format("Per-Frame ({})", ovCount)
                : "Per-Frame (0)";
            auto* ovBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn(ovBtnLabel.c_str(), 126.0f, 22.0f, m_audioSubTab == 1 ? "GJ_button_01.png" : "GJ_button_04.png"),
                this, menu_selector(SettingsPopup::onAudioSubTabBtn)
            );
            ovBtn->setTag(1);
            ovBtn->setPosition({ leftX + 194.0f, topY + 2.0f });
            menu->addChild(ovBtn);

            addCheckboxRow(m_contentNode, menu, "Sound On", m_cfg.soundEnabled, 120, rightX + 68.0f, topY + 2.0f);

            const char* pKeys[5] = { "classic", "fb", "chime", "tick", "custom" };
            const char* pLabels[5] = { "Classic", "FB Bell", "Chime", "Tick", "Custom" };

            if (m_audioSubTab == 0) {
                // DEFAULT SOUND FOR ALL NON-OVERRIDDEN TIERS
                addStepperRow(m_contentNode, menu, "Base Pitch", fmt::format("{:.2f}x", m_cfg.soundPitch), 220, 221, leftX, topY - 30.0f, colW);
                addStepperRow(m_contentNode, menu, "Volume", fmt::format("{:.0f}%", m_cfg.soundVolume * 100.0f), 222, 223, rightX, topY - 30.0f, colW);

                addCheckboxRow(m_contentNode, menu, "Auto Pitch by Tightness", m_cfg.dynamicPitch, 121, leftX, topY - 60.0f);

                addLabel(m_contentNode, "Sound:", leftX, topY - 90.0f, 0.34f, "goldFont.fnt");
                for (int i = 0; i < 5; ++i) {
                    bool sel = (m_cfg.soundPreset == pKeys[i]);
                    auto* bBtn = CCMenuItemSpriteExtra::create(
                        makeMiniBtn(pLabels[i], 60.0f, 21.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png"),
                        this, menu_selector(SettingsPopup::onSoundPresetBtn)
                    );
                    bBtn->setTag(i);
                    bBtn->setPosition({ leftX + 94.0f + i * 64.0f, topY - 90.0f });
                    menu->addChild(bBtn);
                }

                addLabel(m_contentNode, "File:", leftX, topY - 122.0f, 0.34f, "goldFont.fnt");
                auto* customInput = geode::TextInput::create(176.0f, "bell.wav / .mp3 / .ogg", "chatFont.fnt");
                customInput->setMaxCharCount(120);
                customInput->setString(m_cfg.customSoundFile);
                customInput->setPosition({ leftX + 138.0f, topY - 122.0f });
                customInput->setCallback([this](std::string const& str) {
                    m_cfg.customSoundFile = str;
                    if (!str.empty()) m_cfg.soundPreset = "custom";
                    notifyLive();
                });
                m_contentNode->addChild(customInput, 8);

                auto* pickBtn = CCMenuItemSpriteExtra::create(
                    makeMiniBtn("Next", 48.0f, 21.0f, "GJ_button_05.png"),
                    this, menu_selector(SettingsPopup::onCycleCustomSoundBtn)
                );
                pickBtn->setPosition({ leftX + 260.0f, topY - 122.0f });
                menu->addChild(pickBtn);

                auto* folderBtn = CCMenuItemSpriteExtra::create(
                    makeMiniBtn("Folder", 64.0f, 21.0f, "GJ_button_04.png"),
                    this, menu_selector(SettingsPopup::onOpenSoundsFolderBtn)
                );
                folderBtn->setPosition({ leftX + 324.0f, topY - 122.0f });
                menu->addChild(folderBtn);

                auto* testSpr = ButtonSprite::create("Test Default", 118, true, "goldFont.fnt", "GJ_button_01.png", 24.0f, 0.56f);
                auto* testBtn = CCMenuItemSpriteExtra::create(testSpr, this, menu_selector(SettingsPopup::onTestBellBtn));
                testBtn->setPosition({ kPanelW * 0.5f, topY - 156.0f });
                menu->addChild(testBtn);

                auto* tip = CCLabelBMFont::create(
                    "Plays for every tier without a custom override in 'Per-Frame'.",
                    "chatFont.fnt"
                );
                tip->setScale(0.54f);
                tip->setColor(ccc3(195, 202, 214));
                tip->setPosition({ kPanelW * 0.5f, 24.0f });
                m_contentNode->addChild(tip, 3);
            } else {
                // PER-FRAME INDEPENDENT OVERRIDES (pick any 1 or multiple tiers!)
                TierDef td = activeTierDef();
                TierSoundOverride ov = TierSoundStore::get().getForTier(td.key);

                // Tier picker row: <  [0-1]  >   +   [x] Override This Tier
                addLabel(m_contentNode, "Tier:", leftX, topY - 30.0f, 0.34f, "goldFont.fnt");

                auto* lArr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
                lArr->setScale(0.45f);
                auto* prevT = CCMenuItemSpriteExtra::create(lArr, this, menu_selector(SettingsPopup::onSelectTierBtn));
                prevT->setTag(-1);
                prevT->setPosition({ leftX + 58.0f, topY - 30.0f });
                menu->addChild(prevT);

                auto* tierLbl = CCLabelBMFont::create(td.key.c_str(), "bigFont.fnt");
                tierLbl->setScale(0.44f);
                tierLbl->setColor(td.color);
                tierLbl->setPosition({ leftX + 100.0f, topY - 30.0f });
                m_contentNode->addChild(tierLbl, 3);

                auto* rArr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
                rArr->setFlipX(true);
                rArr->setScale(0.45f);
                auto* nextT = CCMenuItemSpriteExtra::create(rArr, this, menu_selector(SettingsPopup::onSelectTierBtn));
                nextT->setTag(1);
                nextT->setPosition({ leftX + 142.0f, topY - 30.0f });
                menu->addChild(nextT);

                addCheckboxRow(m_contentNode, menu, "Use Custom Sound", ov.enabled, 125, rightX, topY - 30.0f);

                // Independent Pitch & Independent Volume (zero link to global!)
                addStepperRow(m_contentNode, menu, "Own Pitch", fmt::format("{:.2f}x", ov.pitch), 230, 231, leftX, topY - 62.0f, colW);
                addStepperRow(m_contentNode, menu, "Own Volume", fmt::format("{:.0f}%", ov.volume * 100.0f), 232, 233, rightX, topY - 62.0f, colW);

                addLabel(m_contentNode, "Sound:", leftX, topY - 94.0f, 0.34f, "goldFont.fnt");
                for (int i = 0; i < 5; ++i) {
                    bool sel = (ov.preset == pKeys[i]);
                    auto* bBtn = CCMenuItemSpriteExtra::create(
                        makeMiniBtn(pLabels[i], 60.0f, 21.0f, sel ? "GJ_button_01.png" : "GJ_button_04.png"),
                        this, menu_selector(SettingsPopup::onTierSoundPresetBtn)
                    );
                    bBtn->setTag(i);
                    bBtn->setPosition({ leftX + 94.0f + i * 64.0f, topY - 94.0f });
                    menu->addChild(bBtn);
                }

                addLabel(m_contentNode, "File:", leftX, topY - 126.0f, 0.34f, "goldFont.fnt");
                auto* tFileInput = geode::TextInput::create(176.0f, "custom.wav / .mp3 / .ogg", "chatFont.fnt");
                tFileInput->setMaxCharCount(120);
                tFileInput->setString(ov.customFile);
                tFileInput->setPosition({ leftX + 138.0f, topY - 126.0f });
                tFileInput->setCallback([this](std::string const& str) {
                    auto tdNow = activeTierDef();
                    auto cur = TierSoundStore::get().getForTier(tdNow.key);
                    cur.customFile = str;
                    if (!str.empty()) {
                        cur.preset = "custom";
                        cur.enabled = true;
                    }
                    TierSoundStore::get().setForTier(tdNow.key, cur);
                });
                m_contentNode->addChild(tFileInput, 8);

                auto* tPickBtn = CCMenuItemSpriteExtra::create(
                    makeMiniBtn("Next", 48.0f, 21.0f, "GJ_button_05.png"),
                    this, menu_selector(SettingsPopup::onCycleTierCustomSoundBtn)
                );
                tPickBtn->setPosition({ leftX + 260.0f, topY - 126.0f });
                menu->addChild(tPickBtn);

                auto* tFolderBtn = CCMenuItemSpriteExtra::create(
                    makeMiniBtn("Folder", 64.0f, 21.0f, "GJ_button_04.png"),
                    this, menu_selector(SettingsPopup::onOpenSoundsFolderBtn)
                );
                tFolderBtn->setPosition({ leftX + 324.0f, topY - 126.0f });
                menu->addChild(tFolderBtn);

                auto* testTierSpr = ButtonSprite::create("Test Tier", 110, true, "goldFont.fnt", "GJ_button_01.png", 24.0f, 0.56f);
                auto* testTierBtn = CCMenuItemSpriteExtra::create(testTierSpr, this, menu_selector(SettingsPopup::onTestTierBellBtn));
                testTierBtn->setPosition({ kPanelW * 0.5f, topY - 158.0f });
                menu->addChild(testTierBtn);

                auto* tip = CCLabelBMFont::create(
                    "Own Pitch & Volume are 100% independent of Default. Unchecked tiers use Default.",
                    "chatFont.fnt"
                );
                tip->setScale(0.52f);
                tip->setColor(ccc3(195, 202, 214));
                tip->setPosition({ kPanelW * 0.5f, 24.0f });
                m_contentNode->addChild(tip, 3);
            }
        }
        else {
            // MANUAL MARKER & COMMENT TAB
            auto& markers = MarkerStore::get().markers();

            addCheckboxRow(m_contentNode, menu, "Capture Clicks", m_cfg.captureToManual, 130, leftX, topY);

            auto* addBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn("+ Mark Here", 84.0f, 22.0f, "GJ_button_01.png"),
                this, menu_selector(SettingsPopup::onManualActionBtn)
            );
            addBtn->setTag(1);
            addBtn->setPosition({ leftX + 242.0f, topY });
            menu->addChild(addBtn);

            auto* clrBtn = CCMenuItemSpriteExtra::create(
                makeMiniBtn("Clear All", 70.0f, 22.0f, "GJ_button_06.png"),
                this, menu_selector(SettingsPopup::onManualActionBtn)
            );
            clrBtn->setTag(2);
            clrBtn->setPosition({ leftX + fullW - 35.0f, topY });
            menu->addChild(clrBtn);

            addLabel(m_contentNode, "Comment:", leftX, topY - 28.0f, 0.34f, "goldFont.fnt");
            auto* cmtInput = geode::TextInput::create(302.0f, "Optional manual comment...", "chatFont.fnt");
            cmtInput->setMaxCharCount(64);
            cmtInput->setString(m_draftMarkerComment);
            cmtInput->setPosition({ leftX + fullW - 151.0f, topY - 28.0f });
            cmtInput->setCallback([this](std::string const& str) {
                m_draftMarkerComment = str;
            });
            m_contentNode->addChild(cmtInput, 8);

            constexpr int kPerPage = 4;
            int totalPages = std::max(1, static_cast<int>((markers.size() + kPerPage - 1) / kPerPage));
            m_manualPage = std::clamp(m_manualPage, 0, totalPages - 1);

            if (markers.empty()) {
                auto* emptyLbl = CCLabelBMFont::create(
                    "No manual markers for this level yet.\nEnable 'Capture Clicks' or press '+ Mark Here'.",
                    "chatFont.fnt"
                );
                emptyLbl->setAlignment(kCCTextAlignmentCenter);
                emptyLbl->setScale(0.60f);
                emptyLbl->setColor(ccc3(205, 212, 224));
                emptyLbl->setPosition({ kPanelW * 0.5f, topY - 96.0f });
                m_contentNode->addChild(emptyLbl, 3);
            } else {
                auto tiers = currentTiers();
                int startIdx = m_manualPage * kPerPage;
                int endIdx = std::min(static_cast<int>(markers.size()), startIdx + kPerPage);

                for (int idx = startIdx; idx < endIdx; ++idx) {
                    int row = idx - startIdx;
                    float ry = topY - 58.0f - row * 24.0f;
                    auto const& mk = markers[static_cast<size_t>(idx)];
                    int ti = findTierIndex(tiers, mk.frames);
                    ccColor3B col = tiers[static_cast<size_t>(ti)].color;

                    std::string cTag = mk.comment.empty() ? "" : " [C]";
                    std::string rowText = fmt::format(
                        "#{:<2} X:{:<5.0f} {}{}",
                        idx + 1, mk.x, formatCircleNumber(mk.frames, true), cTag
                    );
                    auto* lbl = CCLabelBMFont::create(rowText.c_str(), "chatFont.fnt");
                    lbl->setAnchorPoint({ 0.0f, 0.5f });
                    lbl->setScale(0.60f);
                    lbl->setColor(col);
                    lbl->setPosition({ leftX, ry });
                    m_contentNode->addChild(lbl, 3);

                    auto addOp = [&](const char* txt, float cx, float bw, int op, const char* tex) {
                        auto* b = CCMenuItemSpriteExtra::create(
                            makeMiniBtn(txt, bw, 19.0f, tex),
                            this, menu_selector(SettingsPopup::onMarkerRowBtn)
                        );
                        b->setTag(idx * 10 + op);
                        b->setPosition({ cx, ry });
                        menu->addChild(b);
                    };

                    float rx = leftX + fullW;
                    addOp("X",    rx -  11.0f, 22.0f, 5, "GJ_button_06.png");
                    addOp("C",    rx -  37.0f, 22.0f, 4, "GJ_button_01.png");
                    addOp("+1",   rx -  66.0f, 28.0f, 1, "GJ_button_05.png");
                    addOp("-1",   rx -  98.0f, 28.0f, 0, "GJ_button_05.png");
                    addOp("+0.1", rx - 134.0f, 36.0f, 3, "GJ_button_04.png");
                    addOp("-0.1", rx - 174.0f, 36.0f, 2, "GJ_button_04.png");
                }
            }

            auto* pageLbl = CCLabelBMFont::create(
                fmt::format("Markers: {}  |  Page {}/{}  ('C' sets Comment)", markers.size(), m_manualPage + 1, totalPages).c_str(),
                "chatFont.fnt"
            );
            pageLbl->setScale(0.56f);
            pageLbl->setColor(ccc3(210, 218, 230));
            pageLbl->setPosition({ kPanelW * 0.5f, 24.0f });
            m_contentNode->addChild(pageLbl, 3);

            auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            prevSpr->setScale(0.45f);
            auto* prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(SettingsPopup::onManualPageBtn));
            prevBtn->setTag(-1);
            prevBtn->setPosition({ leftX + 16.0f, 24.0f });
            menu->addChild(prevBtn);

            auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            nextSpr->setFlipX(true);
            nextSpr->setScale(0.45f);
            auto* nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(SettingsPopup::onManualPageBtn));
            nextBtn->setTag(1);
            nextBtn->setPosition({ leftX + fullW - 16.0f, 24.0f });
            menu->addChild(nextBtn);
        }
    }

    // Callbacks
    void onTabSelect(CCObject* sender) {
        m_activeTab = static_cast<CCNode*>(sender)->getTag();
        rebuildContent();
    }

    void onAudioSubTabBtn(CCObject* sender) {
        m_audioSubTab = static_cast<CCNode*>(sender)->getTag();
        rebuildContent();
    }

    void onSelectTierBtn(CCObject* sender) {
        auto tiers = currentTiers();
        int n = static_cast<int>(tiers.size());
        if (n <= 0) return;
        int delta = static_cast<CCNode*>(sender)->getTag();
        m_selectedTierIdx = (m_selectedTierIdx + delta + n) % n;
        rebuildContent();
    }

    void onCloseBtn(CCObject*) {
        this->removeFromParent();
    }

    void onModeBtn(CCObject* sender) {
        m_cfg.mode = (static_cast<CCNode*>(sender)->getTag() == 1) ? CounterMode::Manual : CounterMode::Auto;
        applyAndRebuild();
    }

    void onTableStyleBtn(CCObject* sender) {
        m_cfg.tableStyle = (static_cast<CCNode*>(sender)->getTag() == 1) ? TableStyle::Discrete : TableStyle::CbfRanges;
        m_tierIdxInited = false;
        applyAndRebuild();
    }

    void onRingThicknessBtn(CCObject* sender) {
        const char* rKeys[3] = { "thin", "bold", "thick" };
        int idx = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 2);
        m_cfg.ringThickness = rKeys[idx];
        applyAndRebuild();
    }

    void onBannerModeBtn(CCObject* sender) {
        int v = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 2);
        m_cfg.bannerMode = static_cast<BannerMode>(v);
        applyAndRebuild();
    }

    void onToggleBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag == 125) {
            auto td = activeTierDef();
            auto ov = TierSoundStore::get().getForTier(td.key);
            ov.enabled = !ov.enabled;
            TierSoundStore::get().setForTier(td.key, ov);
            rebuildContent();
            return;
        }
        switch (tag) {
            case 100: m_cfg.enabled = !m_cfg.enabled; break;
            case 101: m_cfg.cbfMode = !m_cfg.cbfMode; break;
            case 102: m_cfg.showCircles = !m_cfg.showCircles; break;
            case 103: m_cfg.persistentCircles = !m_cfg.persistentCircles; break;
            case 104: m_cfg.showCircleLabel = !m_cfg.showCircleLabel; break;
            case 105: m_cfg.ignoreSafeClicks = !m_cfg.ignoreSafeClicks; break;
            case 110: m_cfg.showTierTable = !m_cfg.showTierTable; break;
            case 111: m_cfg.showMsHz = !m_cfg.showMsHz; break;
            case 112: m_cfg.showBottomStats = !m_cfg.showBottomStats; break;
            case 113: m_cfg.showWatermark = !m_cfg.showWatermark; break;
            case 120: m_cfg.soundEnabled = !m_cfg.soundEnabled; break;
            case 121: m_cfg.dynamicPitch = !m_cfg.dynamicPitch; break;
            case 130: m_cfg.captureToManual = !m_cfg.captureToManual; break;
            default: break;
        }
        notifyLive();
    }

    void onStepperBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag >= 230 && tag <= 233) {
            auto td = activeTierDef();
            auto ov = TierSoundStore::get().getForTier(td.key);
            switch (tag) {
                case 230: ov.pitch = std::max(0.4f, ov.pitch - 0.05f); break;
                case 231: ov.pitch = std::min(2.5f, ov.pitch + 0.05f); break;
                case 232: ov.volume = std::max(0.0f, ov.volume - 0.05f); break;
                case 233: ov.volume = std::min(1.5f, ov.volume + 0.05f); break;
                default: break;
            }
            ov.enabled = true;
            TierSoundStore::get().setForTier(td.key, ov);
            rebuildContent();
            return;
        }

        switch (tag) {
            case 200: m_cfg.circleScale = std::max(0.4f, m_cfg.circleScale - 0.1f); break;
            case 201: m_cfg.circleScale = std::min(2.5f, m_cfg.circleScale + 0.1f); break;
            case 210: m_cfg.commentFadeSec = std::max(0.5f, m_cfg.commentFadeSec - 0.3f); break;
            case 211: m_cfg.commentFadeSec = std::min(8.0f, m_cfg.commentFadeSec + 0.3f); break;
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

    void onTierSoundPresetBtn(CCObject* sender) {
        const char* pKeys[5] = { "classic", "fb", "chime", "tick", "custom" };
        int idx = std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 4);
        auto td = activeTierDef();
        auto ov = TierSoundStore::get().getForTier(td.key);
        ov.preset = pKeys[idx];
        ov.enabled = true;
        TierSoundStore::get().setForTier(td.key, ov);
        rebuildContent();
        AudioManager::get().playBell(ov.preset, ov.customFile, ov.volume, ov.pitch);
    }

    void onCycleCustomSoundBtn(CCObject*) {
        auto files = AudioManager::get().listCustomSounds();
        if (files.empty()) {
            FLAlertLayer::create(
                "No Custom Sounds",
                "Drop any .wav, .mp3, or .ogg file into the sounds folder first (press Folder).",
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

    void onCycleTierCustomSoundBtn(CCObject*) {
        auto files = AudioManager::get().listCustomSounds();
        if (files.empty()) {
            FLAlertLayer::create(
                "No Custom Sounds",
                "Drop any .wav, .mp3, or .ogg file into the sounds folder first (press Folder).",
                "OK"
            )->show();
            return;
        }
        auto td = activeTierDef();
        auto ov = TierSoundStore::get().getForTier(td.key);
        auto it = std::find(files.begin(), files.end(), ov.customFile);
        size_t next = (it == files.end()) ? 0 : (static_cast<size_t>(it - files.begin()) + 1) % files.size();
        ov.customFile = files[next];
        ov.preset = "custom";
        ov.enabled = true;
        TierSoundStore::get().setForTier(td.key, ov);
        rebuildContent();
        AudioManager::get().playBell(ov.preset, ov.customFile, ov.volume, ov.pitch);
    }

    void onOpenSoundsFolderBtn(CCObject*) {
        geode::utils::file::openFolder(AudioManager::get().getCustomSoundsDir());
    }

    void onTestBellBtn(CCObject*) {
        AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.customSoundFile, m_cfg.soundVolume, m_cfg.soundPitch);
    }

    void onTestTierBellBtn(CCObject*) {
        auto td = activeTierDef();
        auto ov = TierSoundStore::get().getForTier(td.key);
        if (ov.enabled) {
            AudioManager::get().playBell(ov.preset, ov.customFile, ov.volume, ov.pitch);
        } else {
            float pitch = m_cfg.soundPitch + (m_cfg.dynamicPitch ? td.pitchOffset : 0.0f);
            AudioManager::get().playBell(m_cfg.soundPreset, m_cfg.customSoundFile, m_cfg.soundVolume, pitch);
        }
    }

    void onManualActionBtn(CCObject* sender) {
        int tag = static_cast<CCNode*>(sender)->getTag();
        if (tag == 1) {
            MarkerStore::get().addMarker(m_currentPlayerX, m_currentPlayerY, 1.0, m_draftMarkerComment);
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
            case 2: mk.frames = std::max(0.05, mk.frames - 0.1); break;
            case 3: mk.frames = std::min(10000.0, mk.frames + 0.1); break;
            case 4:
                MarkerStore::get().setCommentAt(static_cast<size_t>(idx), m_draftMarkerComment);
                rebuildContent();
                return;
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

    bool setup(std::function<void()> onChanged, float playerX, float playerY) {
        if (!FLAlertLayer::init(165)) return false;

        if (!m_buttonMenu) {
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({ 0.0f, 0.0f });
            m_mainLayer->addChild(m_buttonMenu, 10);
        }

        m_cfg = ModConfig::load();
        m_onChanged = std::move(onChanged);
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
    static SettingsPopup* create(std::function<void()> onChanged, float playerX, float playerY) {
        auto* ret = new SettingsPopup();
        if (ret && ret->setup(std::move(onChanged), playerX, playerY)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

} // namespace fwc
