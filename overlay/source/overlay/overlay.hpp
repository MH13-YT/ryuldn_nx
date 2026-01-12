#pragma once

#include "../ui/components.hpp"
#include "../ui/modals.hpp"
#include <cstdint>
#include <memory>

class RyuLDNOverlay {
private:
    bool initialized = false;
    std::unique_ptr<EnabledToggleListItem> enabledItem;
    std::unique_ptr<LoggingToggleListItem> loggingItem;
    std::unique_ptr<LoggingLevelListItem> loggingLevelItem;
    std::unique_ptr<PassphraseToggleListItem> passphraseToggleItem;
    std::unique_ptr<PassphraseListItem> passphraseItem;
    std::unique_ptr<ServerAddressListItem> serverAddressItem;
    std::unique_ptr<ServerPortListItem> serverPortItem;
    std::unique_ptr<DevModeToggleListItem> devModeItem;

    int selectedIndex = 0;
    int scrollOffset = 0;

public:
    RyuLDNOverlay();

    void render();
    void update();
    void handleInput(uint64_t keysDown, uint64_t keysHeld);

private:
    void ensureInitialized();

private:
    void handleSelection();
    void drawListItem(int x, int y, const std::string& label,
                     const std::string& value, bool selected);
};

// Global modal instances
extern PassphraseInputModal g_passphraseModal;
extern ServerAddressInputModal g_serverModal;
