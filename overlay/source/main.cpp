/**
 * @file main.cpp
 * @brief RyuLDN Tesla Overlay
 * @copyright 2025
 */

#include <tesla.hpp>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../../common/ryuldn_ipc.h"

// Service handle for ryuldn:cfg
static Service g_ryuldnCfgSrv;
static bool g_serviceInitialized = false;

/**
 * @brief Initialize ryuldn:cfg service
 */
Result ryuldnCfgInitialize() {
    if (g_serviceInitialized)
        return 0;

    Result rc = smGetService(&g_ryuldnCfgSrv, RYULDN_CONFIG_SERVICE_NAME);
    if (R_SUCCEEDED(rc))
        g_serviceInitialized = true;

    return rc;
}

/**
 * @brief Finalize ryuldn:cfg service
 */
void ryuldnCfgExit() {
    if (g_serviceInitialized) {
        serviceClose(&g_ryuldnCfgSrv);
        g_serviceInitialized = false;
    }
}

/**
 * @brief Get current status from sysmodule
 */
Result ryuldnCfgGetStatus(RyuLdnStatus* out) {
    if (!g_serviceInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatchOut(&g_ryuldnCfgSrv, RyuLdnConfigCmd_GetStatus, *out);
}

/**
 * @brief Get current configuration
 */
Result ryuldnCfgGetConfig(RyuLdnConfig* out) {
    if (!g_serviceInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatchOut(&g_ryuldnCfgSrv, RyuLdnConfigCmd_GetConfig, *out);
}

/**
 * @brief Set configuration
 */
Result ryuldnCfgSetConfig(const RyuLdnConfig* config) {
    if (!g_serviceInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatchIn(&g_ryuldnCfgSrv, RyuLdnConfigCmd_SetConfig, *config);
}

/**
 * @brief Enable/disable RyuLDN
 */
Result ryuldnCfgSetEnabled(bool enabled) {
    if (!g_serviceInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    const u8 val = enabled ? 1 : 0;
    return serviceDispatchIn(&g_ryuldnCfgSrv, RyuLdnConfigCmd_SetEnabled, val);
}

/**
 * @brief Reconnect to server
 */
Result ryuldnCfgReconnect() {
    if (!g_serviceInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatch(&g_ryuldnCfgSrv, RyuLdnConfigCmd_Reconnect);
}

/**
 * @brief Force disconnect
 */
Result ryuldnCfgForceDisconnect() {
    if (!g_serviceInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatch(&g_ryuldnCfgSrv, RyuLdnConfigCmd_ForceDisconnect);
}

//-----------------------------------------------------------------------------
// UI Elements
//-----------------------------------------------------------------------------

/**
 * @brief Status display element
 */
class StatusElement : public tsl::elm::Element {
private:
    RyuLdnStatus status;
    bool error;

public:
    StatusElement() : error(false) {
        std::memset(&status, 0, sizeof(status));
        update();
    }

    virtual void draw(tsl::gfx::Renderer* renderer) override {
        renderer->drawString("RyuLDN Status", false, 30, 80, 20, tsl::Color(0xFFFF));

        if (error) {
            renderer->drawString("Service Error", false, 30, 120, 16, tsl::Color(0xF00F));
            renderer->drawString("Sysmodule not running?", false, 30, 145, 14, tsl::Color(0xAAA));
            return;
        }

        // State (ajout Disconnecting)
        const char* state_str = "Unknown";
        switch (status.state) {
            case RyuLdnState_None: state_str = "Disabled"; break;
            case RyuLdnState_Initialized: state_str = "Ready"; break;
            case RyuLdnState_Scanning: state_str = "Scanning..."; break;
            case RyuLdnState_HostCreating: state_str = "Creating Room..."; break;
            case RyuLdnState_HostActive: state_str = "Hosting"; break;
            case RyuLdnState_ClientConnecting: state_str = "Joining..."; break;
            case RyuLdnState_ClientConnected: state_str = "Connected"; break;
            case RyuLdnState_Error: state_str = "Error"; break;
            case RyuLdnState_Disconnecting: state_str = "Disconnecting..."; break;  // Ajout
            default: state_str = "Unknown"; break;
        }

        // ... reste du draw() inchangé ...

        // Network stats (reste identique)
    }

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(parentX, parentY, parentWidth, 400);  // Hauteur fixe pour status
    }

    virtual tsl::elm::Element* requestFocus(tsl::elm::Element* old, tsl::FocusDirection direction) override {
        return nullptr;
    }

    void update() {
        Result rc = ryuldnCfgGetStatus(&status);
        error = R_FAILED(rc);
        this->invalidate();  // Force redraw
    }
};


//-----------------------------------------------------------------------------
// Configuration GUI
//-----------------------------------------------------------------------------

/**
 * @brief Server configuration screen
 */
class ServerConfigGui : public tsl::Gui {
private:
    RyuLdnConfig config;

public:
    ServerConfigGui() {
        ryuldnCfgGetConfig(&config);
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("RyuLDN", "Server Configuration");
        auto list = new tsl::elm::List();

        // Server IP
        auto* serverItem = new tsl::elm::ListItem("Server IP");
        serverItem->setValue(config.server_ip);
        list->addItem(serverItem);

        // Server Port
        char portBuf[16];
        snprintf(portBuf, sizeof(portBuf), "%u", config.server_port);
        auto* portItem = new tsl::elm::ListItem("Server Port");
        portItem->setValue(portBuf);
        list->addItem(portItem);

        // Note
        list->addItem(new tsl::elm::CategoryHeader("Note: Restart required for changes"));

        frame->setContent(list);
        return frame;
    }
};

/**
 * @brief Passphrase configuration screen
 */
class PassphraseGui : public tsl::Gui {
private:
    RyuLdnConfig config;

public:
    PassphraseGui() {
        ryuldnCfgGetConfig(&config);
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("RyuLDN", "Passphrase");
        auto list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Current Passphrase"));

        const char* display = config.passphrase[0] ? "********" : "(Public - No Password)";
        auto* passphraseItem = new tsl::elm::ListItem("Passphrase");
        passphraseItem->setValue(display);
        list->addItem(passphraseItem);

        list->addItem(new tsl::elm::CategoryHeader("Info"));
        list->addItem(new tsl::elm::ListItem("Format: Ryujinx-XXXXXXXX"));
        list->addItem(new tsl::elm::ListItem("X = hex digit (0-9, A-F)"));
        list->addItem(new tsl::elm::ListItem("Leave empty for public rooms"));

        list->addItem(new tsl::elm::CategoryHeader("Security"));
        list->addItem(new tsl::elm::ListItem("Cannot change while game running"));

        frame->setContent(list);
        return frame;
    }
};

/**
 * @brief Main menu GUI
 */
class MainGui : public tsl::Gui {
private:
    RyuLdnConfig config;
    RyuLdnStatus status;

public:
    MainGui() {
        ryuldnCfgGetConfig(&config);
        ryuldnCfgGetStatus(&status);
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("RyuLDN", "v1.0.1");
        auto list = new tsl::elm::List();

        // Status display
        auto* statusElement = new StatusElement();
        list->addItem(statusElement);

        list->addItem(new tsl::elm::CategoryHeader("Actions"));

        // Enable/Disable toggle
        auto* toggleItem = new tsl::elm::ToggleListItem("RyuLDN Enabled", config.enabled);
        toggleItem->setStateChangedListener([](bool state) {
            ryuldnCfgSetEnabled(state);
        });
        list->addItem(toggleItem);

        // Reconnect button
        auto* reconnectItem = new tsl::elm::ListItem("Reconnect to Server");
        reconnectItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                ryuldnCfgReconnect();
                return true;
            }
            return false;
        });
        list->addItem(reconnectItem);

        // Force disconnect button
        auto* disconnectItem = new tsl::elm::ListItem("Force Disconnect");
        disconnectItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                ryuldnCfgForceDisconnect();
                return true;
            }
            return false;
        });
        list->addItem(disconnectItem);

        list->addItem(new tsl::elm::CategoryHeader("Configuration"));

        // Server config
        auto* serverItem = new tsl::elm::ListItem("Server Settings");
        serverItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<ServerConfigGui>();
                return true;
            }
            return false;
        });
        list->addItem(serverItem);

        // Passphrase config
        auto* passphraseItem = new tsl::elm::ListItem("Passphrase");
        passphraseItem->setClickListener([](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<PassphraseGui>();
                return true;
            }
            return false;
        });
        list->addItem(passphraseItem);

        // Username
        auto* usernameItem = new tsl::elm::ListItem("Username");
        usernameItem->setValue(config.username[0] ? config.username : "(default)");
        list->addItem(usernameItem);

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {
        // Refresh status every second
        static u64 lastUpdate = 0;
        u64 now = armGetSystemTick();
        if (armTicksToNs(now - lastUpdate) >= 1000000000ULL) {
            ryuldnCfgGetStatus(&status);
            lastUpdate = now;
        }
    }
};

/**
 * @brief Tesla Overlay class
 */
class RyuLdnOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        Result rc = ryuldnCfgInitialize();
        if (R_FAILED(rc)) {
            // Service not available
        }
    }

    virtual void exitServices() override {
        ryuldnCfgExit();
    }

    virtual void onShow() override {}
    virtual void onHide() override {}

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

/**
 * @brief Entry point
 */
int main(int argc, char* argv[]) {
    return tsl::loop<RyuLdnOverlay>(argc, argv);
}
