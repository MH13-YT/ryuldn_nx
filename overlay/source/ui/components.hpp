#pragma once

#include <string>
#include <functional>
#include <cstdint>

/* ===== UI COMPONENT CLASSES ===== */

class EnabledToggleListItem {
public:
    std::string label = "Enabled";
    bool state = false;
    std::function<void(bool)> onStateChanged;

    EnabledToggleListItem();
};

class LoggingToggleListItem {
public:
    std::string label = "Logging";
    bool state = false;
    std::function<void(bool)> onStateChanged;

    LoggingToggleListItem();
};

class LoggingLevelListItem {
public:
    std::string label = "Logging Level";
    uint32_t level = 1;
    std::function<void(uint32_t)> onChanged;

    LoggingLevelListItem();
    std::string getValue() const;

private:
    const char* getLevelString(uint32_t l) const;
};

class PassphraseToggleListItem {
public:
    std::string label = "Passphrase Included";
    bool state = false;
    std::function<void(bool)> onStateChanged;

    PassphraseToggleListItem();
};

class PassphraseListItem {
public:
    std::string label = "Passphrase";
    char passphrase[65] = {0};
    uint32_t passphraseSize = 0;

    PassphraseListItem();
    std::string getValue() const;
    std::string getDisplay() const;
};

class ServerAddressListItem {
public:
    std::string label = "Server Address";
    char address[256] = {0};
    uint32_t addressSize = 0;
    uint32_t port = 0;
    bool devModeEnabled = false;

    ServerAddressListItem();
    std::string getDisplay() const;
};

class ServerPortListItem {
public:
    std::string label = "Server Port";
    uint32_t port = 0;
    bool devModeEnabled = false;

    ServerPortListItem();
    std::string getDisplay() const;
};

class DevModeToggleListItem {
public:
    std::string label = "Dev Mode";
    bool state = false;
    std::function<void(bool)> onStateChanged;

    DevModeToggleListItem();
};
