#include "modals.hpp"
#include "../core/state.hpp"
#include "../ryuldn_ipc.hpp"
#include <algorithm>

/* ===== PASSPHRASE INPUT MODAL ===== */

PassphraseInputModal::PassphraseInputModal(std::function<void(const std::string&)> callback)
    : onConfirm(callback) {}

void PassphraseInputModal::open(const std::string& initialValue) {
    originalInput = initialValue;
    currentInput = initialValue;
    isOpen = true;
}

void PassphraseInputModal::appendChar(char c) {
    if (currentInput.length() < 64) {
        currentInput += c;
    }
}

void PassphraseInputModal::backspace() {
    if (!currentInput.empty()) {
        currentInput.pop_back();
    }
}

void PassphraseInputModal::clear() {
    currentInput.clear();
}

void PassphraseInputModal::confirm() {
    if (onConfirm) onConfirm(currentInput);
    isOpen = false;
}

void PassphraseInputModal::cancel() {
    currentInput = originalInput;
    isOpen = false;
}

const std::string& PassphraseInputModal::getInput() const {
    return currentInput;
}

const std::string& PassphraseInputModal::getDisplay() const {
    static std::string display;
    display = std::string(currentInput.length(), '*');
    return display;
}

/* ===== SERVER ADDRESS INPUT MODAL ===== */

ServerAddressInputModal::ServerAddressInputModal(std::function<void(const std::string&, uint32_t)> callback)
    : onConfirm(callback) {}

void ServerAddressInputModal::open(const std::string& addr, uint32_t p) {
    originalAddress = addr;
    address = addr;
    originalPort = p;
    port = p;
    isOpen = true;
    isEditingPort = false;
}

void ServerAddressInputModal::appendAddressChar(char c) {
    if (!isEditingPort && address.length() < 255) {
        address += c;
    }
}

void ServerAddressInputModal::backspaceAddress() {
    if (!isEditingPort && !address.empty()) {
        address.pop_back();
    }
}

void ServerAddressInputModal::appendPortDigit(char digit) {
    if (isEditingPort) {
        std::string portStr = std::to_string(port);
        if (portStr.length() < 5) {
            portStr += digit;
            port = std::stoul(portStr);
            if (port > 65535) port = 65535;
        }
    }
}

void ServerAddressInputModal::backspacePort() {
    if (isEditingPort && port > 0) {
        port /= 10;
    }
}

void ServerAddressInputModal::toggleField() {
    isEditingPort = !isEditingPort;
}

void ServerAddressInputModal::confirm() {
    if (onConfirm) onConfirm(address, port);
    isOpen = false;
}

void ServerAddressInputModal::cancel() {
    address = originalAddress;
    port = originalPort;
    isOpen = false;
}

const std::string& ServerAddressInputModal::getAddress() const {
    return address;
}

uint32_t ServerAddressInputModal::getPort() const {
    return port;
}

const std::string& ServerAddressInputModal::getFieldDisplay() const {
    static std::string display;
    if (isEditingPort) {
        display = std::to_string(port);
    } else {
        display = address;
    }
    return display;
}
