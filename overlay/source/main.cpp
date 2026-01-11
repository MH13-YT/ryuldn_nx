#define TESLA_INIT_IMPL // If you have more than one file using the tesla header, only define this in the main one
#include <tesla.hpp>    // The Tesla Header
#include "ldn.h"

enum class State {
    Uninit,
    Error,
    Loaded,
};

Service g_ldnSrv;
LdnMitmConfigService g_ldnConfig;
State g_state;
char g_version[32];

// --- LoggingLevelSelector (popup) ---

class LoggingLevelSelector : public tsl::Gui {
private:
    tsl::elm::List* m_list;
    std::function<void(u32)> m_onSelected;

public:
    LoggingLevelSelector(u32 currentLevel, std::function<void(u32)> onSelected)
        : m_onSelected(onSelected) {
        m_list = new tsl::elm::List();

        const char* levels[] = {"ERR", "WARN", "INFO", "DBG", "TRC"};
        for (int i = 0; i < 5; i++) {
            auto item = new tsl::elm::ListItem(levels[i]);
            item->setClickListener([this, i](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    m_onSelected(i + 1);  // 1=ERR, 2=WARN, etc.
                    tsl::goBack();         // ferme ce Gui
                    return true;
                }
                return false;
            });
            m_list->addItem(item);
        }
    }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Logging Level", "Select level");
        frame->setContent(m_list);
        return frame;
    }
};

class EnabledToggleListItem : public tsl::elm::ToggleListItem {
public:
    EnabledToggleListItem() : ToggleListItem("Enabled", false) {
        u32 enabled;
        Result rc;

        rc = ldnMitmGetEnabled(&g_ldnConfig, &enabled);
        if (R_FAILED(rc)) {
            g_state = State::Error;
        }

        this->setState(enabled);

        this->setStateChangedListener([](bool enabled) {
            Result rc = ldnMitmSetEnabled(&g_ldnConfig, enabled);
            if (R_FAILED(rc)) {
                g_state = State::Error;
            }
        });
    }
};

class LoggingToggleListItem : public tsl::elm::ToggleListItem {
public:
    LoggingToggleListItem() : ToggleListItem("Logging", false) {
        u32 enabled;
        Result rc;

        rc = ldnMitmGetLoggingEnabled(&g_ldnConfig, &enabled);
        if (R_FAILED(rc)) {
            g_state = State::Error;
        }

        this->setState(enabled);

        this->setStateChangedListener([](bool enabled) {
            Result rc = ldnMitmSetLoggingEnabled(&g_ldnConfig, enabled);
            if (R_FAILED(rc)) {
                g_state = State::Error;
            }
        });
    }
};

class LoggingLevelListItem : public tsl::elm::ListItem {
    u32 level = 1;

    const char* getLevelString(u32 l) {
        static const char* strs[] = {"ERR", "WARN", "INFO", "DBG", "TRC"};
        return strs[(l-1)%5];
    }

public:
    LoggingLevelListItem() : ListItem("Logging Level") {
        Result rc = ldnMitmGetLoggingLevel(&g_ldnConfig, &level);
        if (R_FAILED(rc)) {
            g_state = State::Error;
            level = 1;
        }
        this->setValue(getLevelString(level));

        // Ouvre un sélecteur au clic
        this->setClickListener([this](u64 keys) -> bool {
            if (!(keys & HidNpadButton_A))
                return false;

            tsl::changeTo<LoggingLevelSelector>(level, [this](u32 new_level) {
                Result rc = ldnMitmSetLoggingLevel(&g_ldnConfig, new_level);
                if (R_SUCCEEDED(rc)) {
                    level = new_level;
                    this->setValue(getLevelString(level));
                }
                // Sinon, on garde l’ancien niveau
            });
            return true;
        });
    }
};

class MainGui : public tsl::Gui {
public:
    MainGui() { }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("RyuLDN_NX Manager", g_version);

        auto list = new tsl::elm::List();

        if (g_state == State::Error) {
            list->addItem(new tsl::elm::ListItem("RyuLDN_NX is not loaded."));
        } else if (g_state == State::Uninit) {
            list->addItem(new tsl::elm::ListItem("wrong state"));
        } else {
            list->addItem(new EnabledToggleListItem());
            list->addItem(new LoggingToggleListItem());
            list->addItem(new LoggingLevelListItem());
        }

        frame->setContent(list);

        return frame;
    }

    virtual void update() override {
        // Si tu veux rafraîchir des valeurs périodiquement, fais-le ici
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld,
                             const HidTouchState &touchPos,
                             HidAnalogStickState joyStickPosLeft,
                             HidAnalogStickState joyStickPosRight) {
        return false;
    }
};

class Overlay : public tsl::Overlay {
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

            rc = ldnMitmGetConfigFromService(&g_ldnSrv, &g_ldnConfig);
            if (R_FAILED(rc)) {
                g_state = State::Error;
                return;
            }

            rc = ldnMitmGetVersion(&g_ldnConfig, g_version);
            if (R_FAILED(rc)) {
                strcpy(g_version, "Error");
            }

            g_state = State::Loaded;
        });
    }

    virtual void exitServices() override {
        serviceClose(&g_ldnConfig.s);
        serviceClose(&g_ldnSrv);
    }

    virtual void onShow() override {}
    virtual void onHide() override {}

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<Overlay>(argc, argv);
}
