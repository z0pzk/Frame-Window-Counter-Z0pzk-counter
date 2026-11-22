#pragma once

#include "Config.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

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
    std::atomic<bool> m_downloadingFfmpeg{ false };

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

    static std::string escapePsSingleQuote(std::string const& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "''";
            else out.push_back(c);
        }
        return out;
    }

public:
    static InternalRenderer& get() {
        static InternalRenderer instance;
        return instance;
    }

    bool isActive() const { return m_active; }
    bool isDownloadingFfmpeg() const { return m_downloadingFfmpeg.load(); }
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

    std::string findFfmpegExecutable() const {
        std::error_code ec;
        auto localBin = getRendersDir() / "ffmpeg.exe";
        if (std::filesystem::exists(localBin, ec)) return localBin.string();
        auto cfgBin = Mod::get()->getConfigDir() / "ffmpeg.exe";
        if (std::filesystem::exists(cfgBin, ec)) return cfgBin.string();
        return "ffmpeg";
    }

    bool isFfmpegReady() const {
        std::error_code ec;
        if (std::filesystem::exists(getRendersDir() / "ffmpeg.exe", ec)) return true;
        if (std::filesystem::exists(Mod::get()->getConfigDir() / "ffmpeg.exe", ec)) return true;
#ifdef GEODE_IS_WINDOWS
        char buf[MAX_PATH];
        if (SearchPathA(nullptr, "ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
            return true;
        }
#endif
        return false;
    }

    void startDownloadFfmpeg(std::function<void(bool, std::string)> onDone) {
        if (m_downloadingFfmpeg.exchange(true)) return;

        auto targetDir = getRendersDir();
        auto targetExe = targetDir / "ffmpeg.exe";

        std::thread([this, targetDir, targetExe, onDone = std::move(onDone)]() {
            bool ok = false;
            std::string msg;
#ifdef GEODE_IS_WINDOWS
            auto tempZip = targetDir / "_ffmpeg_tmp.zip";
            auto tempExt = targetDir / "_ffmpeg_ext";
            std::string zipPs = escapePsSingleQuote(tempZip.string());
            std::string extPs = escapePsSingleQuote(tempExt.string());
            std::string exePs = escapePsSingleQuote(targetExe.string());

            std::string psScript =
                "$ErrorActionPreference='Stop'; "
                "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
                "$url='https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip'; "
                "if (Get-Command curl.exe -ErrorAction SilentlyContinue) { "
                "  & curl.exe -L --silent --show-error -o '" + zipPs + "' $url "
                "} else { "
                "  Invoke-WebRequest -Uri $url -OutFile '" + zipPs + "' -UseBasicParsing "
                "}; "
                "if (Test-Path '" + extPs + "') { Remove-Item -Recurse -Force '" + extPs + "' }; "
                "Expand-Archive -Path '" + zipPs + "' -DestinationPath '" + extPs + "' -Force; "
                "$bin = Get-ChildItem -Path '" + extPs + "' -Filter 'ffmpeg.exe' -Recurse | Select-Object -First 1; "
                "if ($bin) { Copy-Item -Path $bin.FullName -Destination '" + exePs + "' -Force }; "
                "Remove-Item -Force '" + zipPs + "' -ErrorAction SilentlyContinue; "
                "Remove-Item -Recurse -Force '" + extPs + "' -ErrorAction SilentlyContinue;";

            std::string cmdLine = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"" + psScript + "\"";

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};

            std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
            mutableCmd.push_back('\0');

            if (CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }

            std::error_code ec;
            if (std::filesystem::exists(targetExe, ec) && std::filesystem::file_size(targetExe, ec) > 1000000) {
                ok = true;
                msg = "FFmpeg downloaded and installed automatically into the Renders folder! You can now press Start Render.";
            } else {
                ok = false;
                msg = "Auto-download failed (check internet/VPN). You can also drop ffmpeg.exe manually into the Renders Folder.";
            }
#else
            ok = false;
            msg = "Automatic FFmpeg download is supported on Windows only.";
#endif
            m_downloadingFfmpeg.store(false);
            Loader::get()->queueInMainThread([onDone, ok, msg]() {
                if (onDone) onDone(ok, msg);
            });
        }).detach();
    }

    bool start(std::string const& levelName, ModConfig const& cfg) {
        stop();
        m_lastError.clear();

#ifdef GEODE_IS_WINDOWS
        if (!isFfmpegReady()) {
            m_lastError = "FFmpeg not found! Press 'Download FFmpeg' in the Render tab first.";
            return false;
        }

        auto* glView = CCEGLView::sharedOpenGLView();
        if (!glView) {
            m_lastError = "OpenGL view unavailable.";
            return false;
        }

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

        std::string cmd = fmt::format(
            "\"{}\" -y -f rawvideo -pix_fmt rgba -s {}x{} -r {} -i - "
            "-vf \"{}\" -c:v libx264 -preset fast -b:v {} -pix_fmt yuv420p -movflags +faststart \"{}\"",
            ff, m_texW, m_texH, m_fps, vf, bitrate, m_outputPath
        );

        m_pipe = _popen(cmd.c_str(), "wb");
        if (!m_pipe) {
            m_lastError = "Could not launch ffmpeg. Press 'Download FFmpeg' in the Render tab.";
            return false;
        }

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
            m_lastError = "FFmpeg pipe closed unexpectedly.";
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
