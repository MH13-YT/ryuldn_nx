#include "overlay.hpp"
#include "../core/state.hpp"
#include "../ryuldn_ipc.hpp"

/* ===== GLOBAL MODALS ===== */
// Note: Configuration is now done via SetConfig IPC command only

/* ===== OVERLAY CLASS ===== */

RyuLDNOverlay::RyuLDNOverlay() {}

void RyuLDNOverlay::render() {
    // TODO: Implement rendering using libultrahand
}

void RyuLDNOverlay::update() {
    ensureInitialized();
    if (g_state != State::Loaded) {
        return;
    }
}

void RyuLDNOverlay::handleInput(uint64_t keysDown, uint64_t keysHeld) {
    ensureInitialized();
    if (g_state != State::Loaded) {
        return; // service unavailable; ignore input to avoid IPC
    }

    if (keysDown & HidNpadButton_Up) {
        if (selectedIndex > 0) selectedIndex--;
    }
    if (keysDown & HidNpadButton_Down) {
        if (selectedIndex < 10) selectedIndex++;
    }

    if (keysDown & HidNpadButton_A) {
        handleSelection();
    }
}

void RyuLDNOverlay::handleSelection() {
    if (!initialized) {
        return;
    }

    switch (selectedIndex) {
        case 0:
            if (enabledItem) enabledItem->onStateChanged(!enabledItem->state);
            break;
        case 1:
            if (loggingItem) loggingItem->onStateChanged(!loggingItem->state);
            break;
        case 2:
            // TODO: Show logging level menu
            break;
        case 4:
            if (devModeItem) devModeItem->onStateChanged(!devModeItem->state);
            break;
        default:
            break;
    }
}

void RyuLDNOverlay::drawListItem(int x, int y, const std::string& label,
                                 const std::string& value, bool selected) {
    // TODO: Implement drawing with libultrahand
}

void RyuLDNOverlay::ensureInitialized() {
    if (initialized || g_state != State::Loaded) {
        return;
    }

    initialized = true;

    enabledItem = std::make_unique<EnabledToggleListItem>();
    loggingItem = std::make_unique<LoggingToggleListItem>();
    loggingLevelItem = std::make_unique<LoggingLevelListItem>();
    passphraseToggleItem = std::make_unique<PassphraseToggleListItem>();
    passphraseItem = std::make_unique<PassphraseListItem>();
    serverAddressItem = std::make_unique<ServerAddressListItem>();
    serverPortItem = std::make_unique<ServerPortListItem>();
    devModeItem = std::make_unique<DevModeToggleListItem>();
}
