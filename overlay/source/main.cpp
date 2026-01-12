#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "ryuldn_ipc.hpp"
#include "custom_keyboard.hpp"
#include <string>
#include <random>
#include <cctype>

enum class State {
    Uninit,
    Error,
    Loaded,
};

Service g_ldnSrv;
RyuLdnConfigService g_configSrv;
State g_state;
char g_version[32];
RyuLdnStatus g_status = {};  // Connection status (static info)

// Logging level descriptions
constexpr const char *LOG_LEVEL_NAMES[] = {
    "", "Error [1]", "Warning [2]", "Info [3]", "Debug [4]", "Trace [5]"
};

// Generate valid passphrase in format: Ryujinx-[0-9a-f]{8}
static std::string GenerateValidPassphrase() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    const char hex[] = "0123456789abcdef";
    std::string result = "Ryujinx-";
    for (int i = 0; i < 8; i++) {
        result += hex[dis(gen)];
    }
    return result;
}

// Validate passphrase format (empty or Ryujinx-[0-9a-f]{8})
static bool IsValidPassphrase(const std::string& pass) {
    if (pass.empty()) return true;
    if (pass.length() != 16) return false;
    if (pass.substr(0, 8) != "Ryujinx-") return false;
    
    for (size_t i = 8; i < 16; i++) {
        char c = pass[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

class EnabledToggleListItem : public tsl::elm::ToggleListItem {
public:
    EnabledToggleListItem() : ToggleListItem("Enabled", false) {
        u32 enabled;
        Result rc;

        rc = ryuldnGetEnabled(&g_configSrv, &enabled);
        if (R_FAILED(rc)) {
            g_state = State::Error;
        }

        this->setState(enabled);

        this->setStateChangedListener([](bool enabled) {
            Result rc = ryuldnSetEnabled(&g_configSrv, enabled);
            if (R_FAILED(rc)) {
                g_state = State::Error;
            }
        });
    }
};

// Forward declarations
class LoggingLevelGui;

class CustomServerToggleListItem : public tsl::elm::ToggleListItem {
public:
    CustomServerToggleListItem() : ToggleListItem("Custom Server", false) {
        u32 enabled;
        Result rc;

        rc = ryuldnGetEnabled(&g_configSrv, &enabled);
        if (R_FAILED(rc)) {
            g_state = State::Error;
        }

        this->setState(enabled);

        this->setStateChangedListener([](bool enabled) {
            Result rc = ryuldnSetEnabled(&g_configSrv, enabled);
            if (R_FAILED(rc)) {
                g_state = State::Error;
            }
        });
    }
};

class ServerIPListItem : public tsl::elm::ListItem {
public:
    ServerIPListItem() : ListItem("Server IP", ">") {
        updateValue();
        
        this->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                char server_ip[256] = {0};
                ryuldnGetServerIP(&g_configSrv, server_ip);
                
                tsl::changeTo<CustomKeyboardGui>(server_ip, 255, "Server IP",
                    [this](const std::string& new_ip) {
                        // Sauvegarder directement
                        ryuldnSetServerIP(&g_configSrv, new_ip.c_str());
                        updateValue();
                    });
                return true;
            }
            return false;
        });
    }
    
    void updateValue() {
        char server_ip[256] = {0};
        ryuldnGetServerIP(&g_configSrv, server_ip);
        this->setValue(strlen(server_ip) > 0 ? server_ip : "[none]");
    }
};

class ServerPortListItem : public tsl::elm::ListItem {
public:
    ServerPortListItem() : ListItem("Server Port", ">") {
        updateValue();
        
        this->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                u16 port = 11452;
                ryuldnGetServerPort(&g_configSrv, &port);
                
                tsl::changeTo<CustomKeyboardGui>(std::to_string(port), 5, "Server Port",
                    [this](const std::string& new_port_str) {
                        if (!new_port_str.empty()) {
                            // Parser le port manuellement
                            bool valid = true;
                            unsigned long port_val = 0;
                            for (char c : new_port_str) {
                                if (c < '0' || c > '9') {
                                    valid = false;
                                    break;
                                }
                                port_val = port_val * 10 + (c - '0');
                                if (port_val > 65535) {
                                    valid = false;
                                    break;
                                }
                            }
                            if (valid && port_val > 0 && port_val <= 65535) {
                                ryuldnSetServerPort(&g_configSrv, static_cast<u16>(port_val));
                                updateValue();
                            }
                        }
                    });
                return true;
            }
            return false;
        });
    }
    
    void updateValue() {
        u16 port = 11452;
        ryuldnGetServerPort(&g_configSrv, &port);
        this->setValue(std::to_string(port));
    }
};

class PassphraseListItem : public tsl::elm::ListItem {
public:
    PassphraseListItem() : ListItem("Passphrase", ">") {
        char passphrase[17] = {0};
        ryuldnGetPassphrase(&g_configSrv, passphrase);
        this->setValue(strlen(passphrase) > 0 ? passphrase : "[empty]");
        
        this->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                char current_passphrase[17] = {0};
                ryuldnGetPassphrase(&g_configSrv, current_passphrase);
                
                // Extraire la partie hex après "Ryujinx-"
                std::string hex_part = "";
                if (strlen(current_passphrase) == 16 && strncmp(current_passphrase, "Ryujinx-", 8) == 0) {
                    hex_part = std::string(current_passphrase + 8);
                }
                
                tsl::changeTo<PassphraseKeyboardGui>(hex_part, 8, "Passphrase (8 hex chars)",
                    [](const std::string& new_passphrase) {
                        // Valide le format (vide ou Ryujinx-[0-9a-f]{8})
                        if (IsValidPassphrase(new_passphrase)) {
                            ryuldnSetPassphrase(&g_configSrv, new_passphrase.c_str());
                        }
                    });
                return true;
            }
            return false;
        });
    }
};

class LoggingToggleListItem : public tsl::elm::ToggleListItem {
public:
    LoggingToggleListItem() : ToggleListItem("Logging", false) {
        u32 enabled;
        Result rc;

        rc = ryuldnGetLogging(&g_configSrv, &enabled);
        if (R_FAILED(rc)) {
            g_state = State::Error;
        }

        this->setState(enabled);

        this->setStateChangedListener([](bool enabled) {
            Result rc = ryuldnSetLogging(&g_configSrv, enabled);
            if (R_FAILED(rc)) {
                g_state = State::Error;
            }
        });
    }
};

class DebugModeGui : public tsl::Gui {
private:
    ServerIPListItem* m_ipItem = nullptr;
    ServerPortListItem* m_portItem = nullptr;
    tsl::elm::ListItem* m_levelItem = nullptr;

public:
    DebugModeGui() { }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Debug Mode", g_version);

        auto list = new tsl::elm::List();

        // Custom Server Mode toggle
        list->addItem(new CustomServerToggleListItem());

        // Server IP
        m_ipItem = new ServerIPListItem();
        list->addItem(m_ipItem);

        // Server Port
        m_portItem = new ServerPortListItem();
        list->addItem(m_portItem);

        // Logging toggle
        list->addItem(new LoggingToggleListItem());

        // Logging Level with submenu
        u32 level = 1;
        ryuldnGetLoggingLevel(&g_configSrv, &level);
        m_levelItem = new tsl::elm::ListItem("Log Details");
        m_levelItem->setValue(LOG_LEVEL_NAMES[level]);
        m_levelItem->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<LoggingLevelGui>();
                return true;
            }
            return false;
        });
        list->addItem(m_levelItem);

        frame->setContent(list);
        
        return frame;
    }

    virtual void update() override {
        // Refresh Server IP
        if (m_ipItem) {
            m_ipItem->updateValue();
        }
        
        // Refresh Server Port
        if (m_portItem) {
            m_portItem->updateValue();
        }

        // Refresh Logging Level
        if (m_levelItem) {
            u32 level = 1;
            ryuldnGetLoggingLevel(&g_configSrv, &level);
            m_levelItem->setValue(LOG_LEVEL_NAMES[level]);
        }
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) {
        return false;
    }
};

class LoggingLevelGui : public tsl::Gui {
public:
    LoggingLevelGui() { }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Select Logging Level", g_version);
        auto list = new tsl::elm::List();

        // 5 logging levels
        for (int i = 1; i <= 5; i++) {
            auto item = new tsl::elm::ListItem(LOG_LEVEL_NAMES[i]);
            int level_val = i; // Capture by value
            
            item->setClickListener([level_val](u64 keys) {
                if (keys & HidNpadButton_A) {
                    Result rc = ryuldnSetLoggingLevel(&g_configSrv, level_val);
                    if (R_SUCCEEDED(rc)) {
                        tsl::goBack();
                    }
                    return true;
                }
                return false;
            });
            
            list->addItem(item);
        }

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) {
        if (keysDown & HidNpadButton_B) {
            tsl::goBack();
            return true;
        }
        return false;
    }
};

class MainGui : public tsl::Gui {
private:
    tsl::elm::ListItem* m_passphraseItem = nullptr;

public:
    MainGui() { }

    virtual tsl::elm::Element* createUI() override {
        // Get connection status (static info only)
        ryuldnGetStatus(&g_configSrv, &g_status);
        
        // Build subtitle with static connection info
        char subtitle[128];
        if (g_status.server_connected) {
            snprintf(subtitle, sizeof(subtitle), "%s | Server: Connected | Node: %d",
                     g_version, g_status.node_id);
        } else {
            snprintf(subtitle, sizeof(subtitle), "%s | Server: Disconnected",
                     g_version);
        }
        
        auto frame = new tsl::elm::OverlayFrame("RyuLDN NX", subtitle);

        auto list = new tsl::elm::List();

        if (g_state == State::Error) {
            list->addItem(new tsl::elm::ListItem("RyuLDN is not loaded."));
        } else if (g_state == State::Uninit) {
            list->addItem(new tsl::elm::ListItem("wrong state"));
        } else {
            list->addItem(new EnabledToggleListItem());
            
            // Passphrase display
            m_passphraseItem = new PassphraseListItem();
            list->addItem(m_passphraseItem);
            
            // Debug Mode button
            auto debug_item = new tsl::elm::ListItem("Debug Mode >");
            debug_item->setClickListener([](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<DebugModeGui>();
                    return true;
                }
                return false;
            });
            list->addItem(debug_item);
        }

        frame->setContent(list);
        
        return frame;
    }

    virtual void update() override {
        // Refresh Passphrase
        if (m_passphraseItem) {
            char passphrase[17] = {0};
            ryuldnGetPassphrase(&g_configSrv, passphrase);
            m_passphraseItem->setValue(strlen(passphrase) > 0 ? passphrase : "[none]");
        }
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) {
        return false;
    }
};

class RyuLdnOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        g_state = State::Uninit;
        tsl::hlp::doWithSmSession([&] {
            Result rc;

            rc = smGetService(&g_ldnSrv, "ldn:u");
            if (R_FAILED(rc)) {
                g_state = State::Error;
                return;
            }

            rc = ryuldnGetConfigFromService(&g_ldnSrv, &g_configSrv);
            if (R_FAILED(rc)) {
                g_state = State::Error;
                return;
            }

            rc = ryuldnGetVersion(&g_configSrv, g_version);
            if (R_FAILED(rc)) {
                g_state = State::Error;
                return;
            }

            g_state = State::Loaded;
        });
    }

    virtual void exitServices() override {
        serviceClose(&g_configSrv.s);
        serviceClose(&g_ldnSrv);
    }

    virtual void onShow() override {}
    virtual void onHide() override {}

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<RyuLdnOverlay>(argc, argv);
}
