#pragma once
#include <stratosphere.hpp>
#include <switch.h>
#include <atomic>
#include <cstdarg>

enum ComponentId {
    COMP_MAIN = 0, 
    COMP_SVC, 
    COMP_LDN_ICOM, 
    COMP_RLDN_PROTOCOL, 
    COMP_RLDN_PROXY,
    COMP_RLDN_PROXY_SOC, 
    COMP_RLDN_P2P_SRV, 
    COMP_RLDN_P2P_CLI, 
    COMP_RLDN_P2P_SES,
    COMP_RLDN_UPNP, 
    COMP_CONFIG, 
    COMP_LDN_MONITOR, 
    COMP_IP_UTILS, 
    COMP_BSD_MITM_SVC,
    COMPCOUNT
};

namespace ams::log {
    void LogFormatImpl(const char *fmt, ...);
    void LogHexImpl(const void *data, int size);
    Result Initialize();
    void Finalize();
    void LogHeapUsage(const char* tag);
    
    extern std::atomic<u32> gLogLevel;
    
    inline constexpr const char *const gCompPrefixes[COMPCOUNT] = {
        "[MAIN] ", "[SVC] ", "[LDN-ICOMM] ", "[RLDN-PROTOCOL] ", "[RLDN-PROXY] ",
        "[RLDN-PROXY-SOC] ", "[RLDN-P2P-SRV] ", "[RLDN-P2P-CLI] ", "[RLDN-P2P-SES] ",
        "[RLDN-UPNP] ", "[CONFIG] ", "[LDN-MONITOR] ", "[IP-UTILS] ", "[BSD-MITM-SVC] "
    };
    
    inline constexpr const char *const gLevelPrefixes[] = {
        "", "[ERR] ", "[WARN] ", "[INFO] ", "[DBG] ", "[TRC] "
    };
}

// Macro de base sans arguments
#define LOG_COMP(comp, lvl, fmt)                                              \
    do {                                                                       \
        const u32 el = ams::log::gLogLevel.load(std::memory_order_relaxed);   \
        if (el >= static_cast<u32>(lvl)) [[likely]] {                         \
            ams::log::LogFormatImpl("%s%s" fmt "\n",                          \
                                    ams::log::gCompPrefixes[comp],            \
                                    ams::log::gLevelPrefixes[lvl]);           \
        }                                                                      \
    } while (0)

// Macro avec arguments variadiques
#define LOG_COMP_ARGS(comp, lvl, fmt, ...)                                    \
    do {                                                                       \
        const u32 el = ams::log::gLogLevel.load(std::memory_order_relaxed);   \
        if (el >= static_cast<u32>(lvl)) [[likely]] {                         \
            ams::log::LogFormatImpl("%s%s" fmt "\n",                          \
                                    ams::log::gCompPrefixes[comp],            \
                                    ams::log::gLevelPrefixes[lvl],            \
                                    __VA_ARGS__);                             \
        }                                                                      \
    } while (0)

// Macros sans arguments
#define LOG_ERR(comp, fmt)         LOG_COMP(comp, 1, fmt)
#define LOG_WARN(comp, fmt)        LOG_COMP(comp, 2, fmt)
#define LOG_INFO(comp, fmt)        LOG_COMP(comp, 3, fmt)
#define LOG_DBG(comp, fmt)         LOG_COMP(comp, 4, fmt)
#define LOG_TRACE(comp, fmt)       LOG_COMP(comp, 5, fmt)

// Macros avec arguments
#define LOG_ERR_ARGS(comp, fmt, ...)      LOG_COMP_ARGS(comp, 1, fmt, __VA_ARGS__)
#define LOG_WARN_ARGS(comp, fmt, ...)     LOG_COMP_ARGS(comp, 2, fmt, __VA_ARGS__)
#define LOG_INFO_ARGS(comp, fmt, ...)     LOG_COMP_ARGS(comp, 3, fmt, __VA_ARGS__)
#define LOG_DBG_ARGS(comp, fmt, ...)      LOG_COMP_ARGS(comp, 4, fmt, __VA_ARGS__)
#define LOG_TRACE_ARGS(comp, fmt, ...)    LOG_COMP_ARGS(comp, 5, fmt, __VA_ARGS__)

// Macro pour dump hexadécimal
#define LOG_HEX(comp, data, size)                                             \
    do {                                                                       \
        const u32 el = ams::log::gLogLevel.load(std::memory_order_relaxed);   \
        if (el >= 4) {                                                         \
            ams::log::LogFormatImpl("%sHex dump (%d bytes):\n",              \
                                    ams::log::gCompPrefixes[comp], size);     \
            ams::log::LogHexImpl(data, size);                                 \
        }                                                                      \
    } while (0)

#define LOG_HEAP(comp, tag) \
    do { \
        const u32 el = ams::log::gLogLevel.load(std::memory_order_relaxed); \
        if (el >= 3) { \
            ams::log::LogHeapUsage(tag); \
        } \
    } while (0)

#define LogFormat(fmt, ...) ams::log::LogFormatImpl(fmt "\n", ##__VA_ARGS__)
