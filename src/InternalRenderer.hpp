#pragma once

#include "Config.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace fwc {

using namespace geode::prelude;

class InternalRenderer {
private:
    bool m_active = false;
    FILE* m_pipe = nullptr;
    Ref<CCRenderTexture> m_rt = nullptr;
    std::vector<uint8_t> m_pixelBuf;

    int m_texW = 0;
    int m_texH = 0;
    int m_fps = 60;
    int m_framesWritten = 0;
    std::string m_outputPath;
    std::string m_lastError;

    static std::string sanitizeFileName(std::string const& raw) {
        std::string out;
        for (char c : raw) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-') {
                out.push_back(c);
            } else if (c == ' ') {
                out.push_back('_');
            }
        }
        return out.empty() ? "showcase" : out;
    }

    std::string findFfmpegExecutable() const {
        std::error_code ec;
        auto localBin = getRendersDir() / "ffmpeg.exe";
        if (std::filesystem::exists(localBin, ec)) {
            return localBin.string();
        }
        auto cfgBin = Mod::get()->getConfigDir() / "ffmpeg.exe";
        if (std::filesystem::exists(cfgBin, ec)) {
            return cfgBin.string();
        }
        // Fallback to system PATH
        return "ffmpeg";
    }

public:
    static InternalRenderer& get() {
        static InternalRenderer instance;
        return instance;
    }

    bool isActive() const { return m_active; }
    int framesWritten() const { return m_framesWritten; }
    int fps() const { return m_fps; }
    std::string const& outputPath() const { return m_outputPath; }
    std::string const& lastError() const { return m_lastError; }

    std::filesystem::path getRendersDir() const {
        auto dir = Mod::get()->getConfigDir() / "renders";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    bool start(std::string const& levelName, ModConfig const& cfg) {
        stop();
        m_lastError.clear();

#ifdef GEODE_IS_WINDOWS
        auto* glView = CCEGLView::sharedOpenGLView();
        if (!glView) {
            m_lastError = "OpenGL view unavailable.";
            return false;
        }

        // Capture at exact current window pixel dimensions (guaranteed zero Cocos2d-x FBO clipping!)
        // and let FFmpeg scale cleanly to 720p / 1080p if selected.
        auto frameSz = glView->getFrameSize();
        m_texW = std::max(320, static_cast<int>(frameSz.width) & ~1);
        m_texH = std::max(240, static_cast<int>(frameSz.height) & ~1);
        m_fps = static_cast<int>(std::clamp<int64_t>(cfg.renderFps, 24, 120));

        std::string vf = "vflip";
        if (cfg.renderRes == "720p") {
            vf = "vflip,scale=1280:720:flags=lanczos";
        } else if (cfg.renderRes == "1080p") {
            vf = "vflip,scale=1920:1080:flags=lanczos";
        }

        auto rendersDir = getRendersDir();
        auto now = std::time(nullptr);
        std::string base = fmt::format("{}_{}.mp4", sanitizeFileName(levelName), static_cast<long long>(now));
        m_outputPath = (rendersDir / base).string();

        std::string ff = findFfmpegExecutable();
        std::string bitrate = cfg.renderBitrate.empty() ? "12M" : cfg.renderBitrate;

        // Lightweight x264 pipe (fast preset, yuv420p for YouTube/TikTok/Discord compatibility, ~15MB RAM!)
        std::string cmd = fmt::format(
            "\"{}\" -y -f rawvideo -pix_fmt rgba -s {}x{} -r {} -i - "
            "-vf \"{}\" -c:v libx264 -preset fast -b:v {} -pix_fmt yuv420p -movflags +faststart \"{}\"",
            ff, m_texW, m_texH, m_fps, vf, bitrate, m_outputPath
        );

        m_pipe = _popen(cmd.c_str(), "wb");
        if (!m_pipe) {
            m_lastError = "Could not launch ffmpeg. Place ffmpeg.exe in the Renders folder or add it to PATH.";
            return false;
        }

        // Create single reusable FBO
        m_rt = CCRenderTexture::create(m_texW, m_texH);
        if (!m_rt) {
            _pclose(m_pipe);
            m_pipe = nullptr;
            m_lastError = "Failed to allocate OpenGL render texture.";
            return false;
        }

        m_pixelBuf.resize(static_cast<size_t>(m_texW) * static_cast<size_t>(m_texH) * 4);
        m_framesWritten = 0;
        m_active = true;
        return true;
#else
        m_lastError = "Built-in FFmpeg render is currently supported on Windows only.";
        return false;
#endif
    }

    bool captureFrame(CCNode* targetLayer) {
        if (!m_active || !m_pipe || !m_rt || !targetLayer) return false;

#ifdef GEODE_IS_WINDOWS
        m_rt->beginWithClear(0.0f, 0.0f, 0.0f, 1.0f);
        targetLayer->visit();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, m_texW, m_texH, GL_RGBA, GL_UNSIGNED_BYTE, m_pixelBuf.data());
        m_rt->end();

        size_t written = std::fwrite(m_pixelBuf.data(), 1, m_pixelBuf.size(), m_pipe);
        if (written != m_pixelBuf.size()) {
            m_lastError = "FFmpeg pipe closed! Make sure ffmpeg.exe is in the Renders folder or Windows PATH.";
            stop();
            return false;
        }

        m_framesWritten++;
        return true;
#else
        return false;
#endif
    }

    void stop() {
#ifdef GEODE_IS_WINDOWS
        if (m_pipe) {
            std::fflush(m_pipe);
            _pclose(m_pipe);
            m_pipe = nullptr;
        }
#endif
        m_rt = nullptr;
        m_pixelBuf.clear();
        m_pixelBuf.shrink_to_fit();
        m_active = false;
    }
};

} // namespace fwc
