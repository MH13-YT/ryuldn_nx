#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include "ryuldnnx_config.hpp"
#include "debug.hpp"

namespace ams::mitm::ldn {

// Config file paths
constexpr char kIniPath[] = "sdmc:/config/ryuldn_nx/config.ini";
constexpr char kIniDir[] = "sdmc:/config/ryuldn_nx";

// Helper to trim whitespace
static void Trim(std::string& str) {
    str.erase(0, str.find_first_not_of(" \t\r\n"));
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

// Static members
RyuLdnConfig LdnConfig::config = {
    .enabled = true,
    .server_ip = {0},
    .server_port = 30456,
    .passphrase = {0},
    ._reserved = {0}  // Unused (was username)
};

std::atomic_bool LdnConfig::enabled = true;
std::atomic_bool LdnConfig::logging_enabled = false;  // Default logging disabled
std::atomic_uint32_t LdnConfig::logging_level = 1;    // Default level 1
std::function<void(const char*, u32)> LdnConfig::PassphraseUpdateHandler{};

// Load config from ini file
void LdnConfig::LoadConfigFromIni() {
    // Ensure directories exist
    ams::fs::DirectoryEntryType et{};
    if (R_FAILED(ams::fs::GetEntryType(&et, "sdmc:/config"))) {
        ams::fs::CreateDirectory("sdmc:/config");
    }
    if (R_FAILED(ams::fs::GetEntryType(&et, kIniDir))) {
        ams::fs::CreateDirectory(kIniDir);
    }

    // Read ini file if it exists
    ams::fs::FileHandle fh{};
    if (R_FAILED(ams::fs::OpenFile(&fh, kIniPath, ams::fs::OpenMode_Read))) {
        return; // No file, use defaults
    }

    s64 fsize = 0;
    if (R_FAILED(ams::fs::GetFileSize(&fsize, fh)) || fsize <= 0) {
        ams::fs::CloseFile(fh);
        return;
    }

    std::string content;
    content.resize(static_cast<size_t>(fsize));
    size_t read_sz = 0;
    (void)ams::fs::ReadFile(&read_sz, fh, 0, content.data(), content.size(), ams::fs::ReadOption::None);
    ams::fs::CloseFile(fh);

    // Parse ini file - custom_host, custom_port, logging_enabled, logging_level
    std::string custom_host{};
    int custom_port = 30456;
    bool log_enabled = false;
    int log_level = 3;  // INFO par défaut

    std::string entry;
    entry.reserve(256);
    std::size_t start = 0;
    while (start < content.size()) {
        std::size_t nl = content.find('\n', start);
        if (nl == std::string::npos) nl = content.size();
        entry.assign(content.data() + start, nl - start);

        // Strip comments and trim
        const std::size_t comment = entry.find_first_of("#;");
        if (comment != std::string::npos) {
            entry.erase(comment);
        }
        Trim(entry);

        if (!entry.empty()) {
            const std::size_t eq = entry.find('=');
            if (eq != std::string::npos) {
                std::string key = entry.substr(0, eq);
                std::string value = entry.substr(eq + 1);
                Trim(key);
                Trim(value);
                std::transform(key.begin(), key.end(), key.begin(), 
                               [](unsigned char c) { return std::tolower(c); });

                if (key == "custom_host") {
                    custom_host = value;
                } else if (key == "custom_port") {
                    int port = std::atoi(value.c_str());
                    if (port > 0 && port <= 65535) {
                        custom_port = port;
                    }
                } else if (key == "logging_enabled") {
                    log_enabled = (value == "true" || value == "1");
                } else if (key == "logging_level") {
                    int lvl = std::atoi(value.c_str());
                    if (lvl >= 1 && lvl <= 5) {
                        log_level = lvl;
                    }
                }
            }
        }
        start = nl + 1;
    }

    // Apply values if host is not empty
    if (!custom_host.empty()) {
        std::strncpy(config.server_ip, custom_host.c_str(), sizeof(config.server_ip) - 1);
        config.server_ip[sizeof(config.server_ip) - 1] = '\0';
        config.server_port = static_cast<u16>(custom_port);
    }
    
    // Apply logging settings
    logging_enabled = log_enabled;
    logging_level = log_level;
    ams::log::gLogLevel.store(log_level, std::memory_order_relaxed);
}

// Save config to ini file
void LdnConfig::SaveConfigToIni() {
    // Ensure directories exist
    ams::fs::DirectoryEntryType et{};
    if (R_FAILED(ams::fs::GetEntryType(&et, "sdmc:/config"))) {
        ams::fs::CreateDirectory("sdmc:/config");
    }
    if (R_FAILED(ams::fs::GetEntryType(&et, kIniDir))) {
        ams::fs::CreateDirectory(kIniDir);
    }

    // Build ini content - IP, port, and logging settings
    std::string content;
    content += "custom_host = ";
    content += (strlen(config.server_ip) > 0) ? config.server_ip : "0.0.0.0";
    content += "\n";
    content += "custom_port = ";
    content += std::to_string((config.server_port > 0) ? config.server_port : 30456);
    content += "\n";
    content += "logging_enabled = ";
    content += logging_enabled ? "true" : "false";
    content += "\n";
    content += "logging_level = ";
    content += std::to_string(logging_level.load());
    content += "\n";

    // Write to file
    ams::fs::DeleteFile(kIniPath); // Delete old file
    if (R_SUCCEEDED(ams::fs::CreateFile(kIniPath, content.size()))) {
        ams::fs::FileHandle fh{};
        if (R_SUCCEEDED(ams::fs::OpenFile(&fh, kIniPath, ams::fs::OpenMode_Write))) {
            (void)ams::fs::WriteFile(fh, 0, content.data(), content.size(), ams::fs::WriteOption::Flush);
            ams::fs::CloseFile(fh);
        }
    }
}

// Initialize - called once at startup
void LdnConfig::Initialize() {
    LoadConfigFromIni();
}

// Get version string (cmd 65001)
Result LdnConfig::GetVersion(sf::Out<RyuLdnVersion> version) {
    constexpr const char* VERSION_STRING = "v1.0.0";
    std::strcpy(version.GetPointer()->raw, VERSION_STRING);
    R_SUCCEED();
}

// Get logging enabled state (cmd 65002)
Result LdnConfig::GetLogging(sf::Out<u32> enabled) {
    enabled.SetValue(logging_enabled ? 1u : 0u);
    R_SUCCEED();
}

// Set logging enabled state (cmd 65003)
Result LdnConfig::SetLogging(u32 enabled) {
    logging_enabled = (enabled != 0);
    SaveConfigToIni(); // Save to file
    R_SUCCEED();
}

// Get enabled state (cmd 65004)
Result LdnConfig::GetEnabled(sf::Out<u32> enabled) {
    enabled.SetValue(LdnConfig::enabled);
    R_SUCCEED();
}

// Set enabled state (cmd 65005)
Result LdnConfig::SetEnabled(u32 enabled) {
    LdnConfig::enabled = enabled;
    R_SUCCEED();
}

// Get passphrase (cmd 65006)
Result LdnConfig::GetPassphrase(sf::Out<RyuLdnPassphrase> pass) {
    std::strcpy(pass.GetPointer()->raw, config.passphrase);
    R_SUCCEED();
}

// Set passphrase (cmd 65007)
Result LdnConfig::SetPassphrase(const RyuLdnPassphrase& pass) {
    std::strcpy(config.passphrase, pass.raw);
    if (PassphraseUpdateHandler) {
        PassphraseUpdateHandler(pass.raw, std::strlen(pass.raw));
    }
    R_SUCCEED();
}

// Get server IP (cmd 65008)
Result LdnConfig::GetServerIP(sf::Out<RyuLdnServerIP> ip) {
    std::strcpy(ip.GetPointer()->raw, config.server_ip);
    R_SUCCEED();
}

// Set server IP (cmd 65009)
Result LdnConfig::SetServerIP(const RyuLdnServerIP& ip) {
    std::strcpy(config.server_ip, ip.raw);
    SaveConfigToIni(); // Save to file
    R_SUCCEED();
}

// Get server port (cmd 65010)
Result LdnConfig::GetServerPort(sf::Out<u16> port) {
    port.SetValue(config.server_port);
    R_SUCCEED();
}

// Set server port (cmd 65011)
Result LdnConfig::SetServerPort(u16 port) {
    config.server_port = port;
    SaveConfigToIni(); // Save to file
    R_SUCCEED();
}

// Get logging level (cmd 65012)
Result LdnConfig::GetLoggingLevel(sf::Out<u32> level) {
    level.SetValue(logging_level);
    R_SUCCEED();
}

// Set logging level (cmd 65013)
Result LdnConfig::SetLoggingLevel(u32 level) {
    if (level >= 1 && level <= 5) {
        logging_level = level;
        // Apply to actual logging system
        ams::log::gLogLevel.store(level, std::memory_order_relaxed);
        SaveConfigToIni(); // Save to file
    }
    R_SUCCEED();
}

void LdnConfig::SetPassphraseUpdateHandler(std::function<void(const char*, u32)> handler) {
    PassphraseUpdateHandler = std::move(handler);
}

// Runtime accessors
bool LdnConfig::IsLoggingEnabled() {
    return logging_enabled.load();
}

u32 LdnConfig::GetLoggingLevelValue() {
    return logging_level.load();
}

} // namespace ams::mitm::ldn

