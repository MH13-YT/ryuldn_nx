#include <cstring>
#include "ldnmitm_config.hpp"
#include "debug.hpp"

namespace ams::mitm::ldn {

std::atomic_bool LdnConfig::LdnEnabled{true};
std::atomic_bool LdnConfig::LdnLoggerEnabled{false};
std::atomic_uint32_t LdnConfig::LdnLoggerLevel{1};


bool LdnConfig::getEnabled() {
    return LdnEnabled.load();
}

bool LdnConfig::getLoggerEnabled() {
    return LdnLoggerEnabled.load();
}

u32 LdnConfig::getLoggerLevel() {
    return LdnLoggerLevel.load();
}

Result LdnConfig::GetVersion(ams::sf::Out<LdnMitmVersion> version) {
    std::strcpy(version.GetPointer()->raw, GITDESCVER ?: "1.0.0");
    R_SUCCEED();
}

Result LdnConfig::GetEnabled(ams::sf::Out<u32> enabled) {
    enabled.SetValue(getEnabled() ? 1u : 0u);
    R_SUCCEED();
}

Result LdnConfig::SetEnabled(u32 enabled) {
    LdnEnabled.store(enabled != 0);
    R_SUCCEED();
}

Result LdnConfig::GetLoggerEnabled(ams::sf::Out<u32> logging) {
    logging.SetValue(getLoggerEnabled() ? 1u : 0u);
    R_SUCCEED();
}

Result LdnConfig::SetLoggerEnabled(u32 logger) {
    LdnLoggerEnabled.store(logger != 0);
    R_SUCCEED();
}

Result LdnConfig::GetLoggerLevel(ams::sf::Out<u32> level) {
    level.SetValue(getLoggerLevel());  // comme GetEnabled, mais avec u32
    R_SUCCEED();
}

Result LdnConfig::SetLoggerLevel(u32 level) {
    LdnLoggerLevel.store(level);
    R_SUCCEED();
}

} // namespace ams::mitm::ldn
