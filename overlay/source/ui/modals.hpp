#pragma once

#include <string>
#include <functional>

/* ===== PASSPHRASE INPUT MODAL ===== */
class PassphraseInputModal {
private:
    std::string currentInput;
    std::string originalInput;
    bool isOpen = false;
    std::function<void(const std::string&)> onConfirm;

public:
    PassphraseInputModal(std::function<void(const std::string&)> callback);

    void open(const std::string& initialValue = "");
    bool isVisible() const { return isOpen; }

    void appendChar(char c);
    void backspace();
    void clear();

    void confirm();
    void cancel();

    const std::string& getInput() const;
    const std::string& getDisplay() const;
};

/* ===== SERVER ADDRESS INPUT MODAL ===== */
class ServerAddressInputModal {
private:
    std::string address;
    std::string originalAddress;
    uint32_t port = 0;
    uint32_t originalPort = 0;
    bool isOpen = false;
    bool isEditingPort = false;
    std::function<void(const std::string&, uint32_t)> onConfirm;

public:
    ServerAddressInputModal(std::function<void(const std::string&, uint32_t)> callback);

    void open(const std::string& addr = "", uint32_t p = 0);
    bool isVisible() const { return isOpen; }
    bool isEditingPortField() const { return isEditingPort; }

    void appendAddressChar(char c);
    void backspaceAddress();
    void appendPortDigit(char digit);
    void backspacePort();
    void toggleField();

    void confirm();
    void cancel();

    const std::string& getAddress() const;
    uint32_t getPort() const;
    const std::string& getFieldDisplay() const;
};
