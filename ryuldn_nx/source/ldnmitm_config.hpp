#pragma once

#include <stratosphere.hpp>
#include "interfaces/iconfig.hpp"
#include "debug.hpp"
#include <atomic>

namespace ams::mitm::ldn {

class LdnConfig {
public:
    static bool getEnabled();
    static bool getLoggerEnabled();
    static u32 getLoggerLevel();

protected:
    static std::atomic_bool LdnEnabled;
    static std::atomic_bool LdnLoggerEnabled;
    static std::atomic_uint32_t LdnLoggerLevel;

public:
    Result GetVersion(ams::sf::Out<LdnMitmVersion> version);
    Result GetLoggerEnabled(ams::sf::Out<u32> logger);
    Result SetLoggerEnabled(u32 logger);
    Result GetEnabled(ams::sf::Out<u32> enabled);
    Result SetEnabled(u32 enabled);
    Result GetLoggerLevel(ams::sf::Out<u32> level);
    Result SetLoggerLevel(u32 level);
};

} // namespace ams::mitm::ldn
