#include "components.hpp"
#include <cstring>
#include <cstdio>

/* ===== CONSTRUCTOR IMPLEMENTATIONS ===== */

EnabledToggleListItem::EnabledToggleListItem() : label("Enabled"), state(false) {}

LoggingToggleListItem::LoggingToggleListItem() : label("Logging"), state(false) {}

LoggingLevelListItem::LoggingLevelListItem() : label("Logging Level"), level(1) {}

std::string LoggingLevelListItem::getValue() const {
    return getLevelString(level);
}

const char* LoggingLevelListItem::getLevelString(uint32_t l) const {
    switch (l) {
        case 0: return "Off";
        case 1: return "Error";
        case 2: return "Warning";
        case 3: return "Info";
        case 4: return "Debug";
        default: return "Unknown";
    }
}

PassphraseToggleListItem::PassphraseToggleListItem() : label("Passphrase Included"), state(false) {}

PassphraseListItem::PassphraseListItem() : label("Passphrase") {
    std::memset(passphrase, 0, sizeof(passphrase));
    passphraseSize = 0;
}

std::string PassphraseListItem::getValue() const {
    if (passphraseSize == 0) return "";
    return std::string(passphrase, passphraseSize);
}

std::string PassphraseListItem::getDisplay() const {
    if (passphraseSize == 0) return "(empty)";
    return std::string(passphraseSize, '*');  // Show as asterisks for privacy
}

ServerAddressListItem::ServerAddressListItem() : label("Server Address"), port(30456) {
    std::memset(address, 0, sizeof(address));
    addressSize = 0;
    devModeEnabled = false;
}

std::string ServerAddressListItem::getDisplay() const {
    if (addressSize == 0) return "(default)";
    return std::string(address, addressSize);
}

ServerPortListItem::ServerPortListItem() : label("Server Port"), port(30456) {
    devModeEnabled = false;
}

std::string ServerPortListItem::getDisplay() const {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", port);
    return std::string(buf);
}

DevModeToggleListItem::DevModeToggleListItem() : label("Dev Mode"), state(false) {}
