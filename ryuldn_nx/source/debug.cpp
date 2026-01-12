// Clean FS-based logging implementation
#include <stratosphere.hpp>
#include "debug.hpp"
#include "ryuldnnx_config.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <switch.h>

namespace ams::log {
    std::atomic<u32> gLogLevel{3};
}

namespace ams::log {
    static constexpr size_t LOG_BUFFER_SIZE = 1024;
    static char g_logBuffer[LOG_BUFFER_SIZE];
    static ams::os::SdkMutex g_logMutex;
    static constexpr const char* LOG_FILE_PATH = "sdmc:/config/ryuldn_nx/ryuldn_nx.log";

    void LogFormatImpl(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);

        std::scoped_lock lk(g_logMutex);

        int len = std::vsnprintf(g_logBuffer, LOG_BUFFER_SIZE, fmt, args);
        va_end(args);

        if (len > 0) {
            std::fputs(g_logBuffer, stdout);
            std::fflush(stdout);
            svcOutputDebugString(g_logBuffer, static_cast<u64>(len));

            if (ams::mitm::ldn::LdnConfig::IsLoggingEnabled()) {
                fs::FileHandle fh{};
                if (R_SUCCEEDED(fs::OpenFile(&fh, LOG_FILE_PATH, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) {
                    s64 offset = 0;
                    (void)fs::GetFileSize(&offset, fh);
                    (void)fs::WriteFile(fh, offset, g_logBuffer, len, fs::WriteOption::Flush);
                    fs::CloseFile(fh);
                }
            }
        }
    }

    void LogHexImpl(const void *data, int size) {
        if (!data || size <= 0) return;

        std::scoped_lock lk(g_logMutex);

        const u8 *bytes = static_cast<const u8*>(data);
        constexpr int BYTES_PER_LINE = 16;

        for (int i = 0; i < size; i += BYTES_PER_LINE) {
            std::printf("%08x: ", i);
            for (int j = 0; j < BYTES_PER_LINE; ++j) {
                if (i + j < size) {
                    std::printf("%02x ", bytes[i + j]);
                } else {
                    std::printf("   ");
                }
                if (j == 7) std::printf(" ");
            }
            std::printf(" |");
            for (int j = 0; j < BYTES_PER_LINE && i + j < size; ++j) {
                u8 c = bytes[i + j];
                std::printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            std::printf("|\n");
        }

        std::fflush(stdout);
    }

    Result Initialize() {
        #ifdef DEBUG
        consoleInit(nullptr);
        #endif

        // Don't set gLogLevel here - it will be loaded from config by LdnConfig::Initialize()
        // Keep default value of 3 (INFO) until config is loaded

        (void)ams::fs::EnsureDirectory("sdmc:/config");
        (void)ams::fs::EnsureDirectory("sdmc:/config/ryuldn_nx");

        bool has_file = false;
        if (R_SUCCEEDED(fs::HasFile(&has_file, LOG_FILE_PATH)) && !has_file) {
            (void)fs::CreateFile(LOG_FILE_PATH, 0);
        }

        fs::FileHandle fh{};
        if (R_SUCCEEDED(fs::OpenFile(&fh, LOG_FILE_PATH, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) {
            s64 offset = 0;
            (void)fs::GetFileSize(&offset, fh);
            const char* hdr = "\n=== RyuLDN Log Session Started ===\n";
            (void)fs::WriteFile(fh, offset, hdr, std::strlen(hdr), fs::WriteOption::Flush);
            fs::CloseFile(fh);
        }

        return ResultSuccess();
    }

    void Finalize() {
        fs::FileHandle fh{};
        if (R_SUCCEEDED(fs::OpenFile(&fh, LOG_FILE_PATH, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) {
            s64 offset = 0;
            (void)fs::GetFileSize(&offset, fh);
            const char* ftr = "=== RyuLDN Log Session Ended ===\n\n";
            (void)fs::WriteFile(fh, offset, ftr, std::strlen(ftr), fs::WriteOption::Flush);
            fs::CloseFile(fh);
        }

        #ifdef DEBUG
        consoleExit(nullptr);
        #endif
    }

    void LogHeapUsage([[maybe_unused]] const char* tag) {
        const u32 el = gLogLevel.load(std::memory_order_relaxed);
        if (el < 3) return;
        LogFormatImpl("[%s] Heap usage check\n", tag);
    }
}
