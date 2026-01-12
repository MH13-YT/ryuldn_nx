#pragma once

#include <stratosphere.hpp>
#include <functional>
#include "interfaces/iconfig.hpp"
#include "debug.hpp"
#include <atomic>

namespace ams::mitm::ldn {

class LdnConfig {
private:
    static RyuLdnConfig config;  // Single unified config for storage
    static std::function<void(const char*, u32)> PassphraseUpdateHandler;
    static std::atomic_bool enabled;
    static std::atomic_bool logging_enabled;
    static std::atomic_uint32_t logging_level;  // 1-5
    
    // Helper functions for ini file management
    static void LoadConfigFromIni();
    static void SaveConfigToIni();

public:
    // IPC interface - MUST match iconfig.hpp exactly
    Result GetVersion(sf::Out<ams::mitm::ldn::RyuLdnVersion> version);
    Result GetLogging(sf::Out<u32> enabled);
    Result SetLogging(u32 enabled);
    Result GetEnabled(sf::Out<u32> enabled);
    Result SetEnabled(u32 enabled);
    Result GetPassphrase(sf::Out<ams::mitm::ldn::RyuLdnPassphrase> pass);
    Result SetPassphrase(const ams::mitm::ldn::RyuLdnPassphrase& pass);
    Result GetServerIP(sf::Out<ams::mitm::ldn::RyuLdnServerIP> ip);
    Result SetServerIP(const ams::mitm::ldn::RyuLdnServerIP& ip);
    Result GetServerPort(sf::Out<u16> port);
    Result SetServerPort(u16 port);
    Result GetLoggingLevel(sf::Out<u32> level);
    Result SetLoggingLevel(u32 level);

    // Runtime accessors for logging state
    static bool IsLoggingEnabled();
    static u32 GetLoggingLevelValue();

    // Internal accessors
    static bool IsEnabled() { return enabled; }
    static const char* GetServerIP() { return config.server_ip; }
    static u16 GetServerPort() { return config.server_port; }
    static const char* GetPassphrase() { return config.passphrase; }
    
    // Legacy accessors for ldn_icommunication compatibility
    static const char* getPassphrase() { return config.passphrase; }
    static bool getPassphraseIncluded() { return strlen(config.passphrase) > 0; }
    static u32 getPassphraseSize() { return strlen(config.passphrase); }

    static void SetPassphraseUpdateHandler(std::function<void(const char*, u32)> handler);
    
    // Initialize config from ini file
    static void Initialize();
};

static_assert(ams::mitm::ldn::IsILdnConfig<LdnConfig>);

} // namespace ams::mitm::ldn
