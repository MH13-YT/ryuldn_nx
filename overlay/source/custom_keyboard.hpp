#pragma once
#include <tesla.hpp>
#include <string>
#include <vector>
#include <random>

// Custom keyboard GUI for text input
class CustomKeyboardGui : public tsl::Gui {
private:
    std::string m_text;
    std::string m_header;
    size_t m_maxLen;
    std::function<void(const std::string&)> m_callback;
    
    // Keyboard layout - numérique avec point pour les IPs
    static constexpr const char* KEYBOARD_ROWS[] = {
        "123",
        "456",
        "789",
        "0."
    };
    
    static constexpr int ROWS = 4;
    
    int m_cursorX = 0;
    int m_cursorY = 0;
    
    tsl::elm::CustomDrawer* m_drawer = nullptr;

public:
    CustomKeyboardGui(const std::string& initial, size_t maxLen, const std::string& header, 
                      std::function<void(const std::string&)> callback)
        : m_text(initial), m_header(header), m_maxLen(maxLen), m_callback(callback) {}

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame(m_header.c_str(), "Custom Keyboard");
        
        m_drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
            // Draw current text
            renderer->drawString(m_text.c_str(), false, x + 20, y + 80, 20, 0xFFFF);
            
            // Draw cursor indicator
            std::string cursor = "_";
            renderer->drawString(cursor.c_str(), false, x + 20 + (m_text.length() * 12), y + 80, 20, 0xFFFF);
            
            // Draw keyboard
            int startY = y + 120;
            for (int row = 0; row < ROWS; row++) {
                const char* rowStr = KEYBOARD_ROWS[row];
                int len = strlen(rowStr);
                
                for (int col = 0; col < len; col++) {
                    char ch[2] = {rowStr[col], '\0'};
                    
                    // Highlight selected character
                    u32 color = (row == m_cursorY && col == m_cursorX) ? 0xFF00 : 0xFFFF;
                    
                    renderer->drawString(ch, false, x + 40 + (col * 35), startY + (row * 40), 24, color);
                }
            }
            
            // Draw controls help
            renderer->drawString("A: Select  X: Backspace  +: Done  B: Cancel", false, x + 20, y + 320, 15, 0xAAAA);
        });
        
        frame->setContent(m_drawer);
        return frame;
    }

    virtual void update() override {
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, 
                            HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) {
        if (keysDown & HidNpadButton_Left) {
            if (m_cursorX > 0) m_cursorX--;
            return true;
        }
        if (keysDown & HidNpadButton_Right) {
            int maxX = strlen(KEYBOARD_ROWS[m_cursorY]) - 1;
            if (m_cursorX < maxX) m_cursorX++;
            return true;
        }
        if (keysDown & HidNpadButton_Up) {
            if (m_cursorY > 0) {
                m_cursorY--;
                // Adjust X if out of bounds
                int maxX = strlen(KEYBOARD_ROWS[m_cursorY]) - 1;
                if (m_cursorX > maxX) m_cursorX = maxX;
            }
            return true;
        }
        if (keysDown & HidNpadButton_Down) {
            if (m_cursorY < ROWS - 1) {
                m_cursorY++;
                // Adjust X if out of bounds
                int maxX = strlen(KEYBOARD_ROWS[m_cursorY]) - 1;
                if (m_cursorX > maxX) m_cursorX = maxX;
            }
            return true;
        }
        if (keysDown & HidNpadButton_A) {
            // Add character
            if (m_text.length() < m_maxLen) {
                char ch = KEYBOARD_ROWS[m_cursorY][m_cursorX];
                m_text += ch;
            }
            return true;
        }
        if (keysDown & HidNpadButton_X) {
            // Backspace
            if (!m_text.empty()) {
                m_text.pop_back();
            }
            return true;
        }
        if (keysDown & HidNpadButton_Plus) {
            // Done
            if (m_callback) {
                m_callback(m_text);
            }
            tsl::goBack();
            return true;
        }
        if (keysDown & HidNpadButton_B) {
            // Cancel
            tsl::goBack();
            return true;
        }
        
        return false;
    }
};

// Passphrase keyboard GUI (hex chars only + actions)
class PassphraseKeyboardGui : public tsl::Gui {
private:
    std::string m_text;
    std::string m_header;
    size_t m_maxLen;
    std::function<void(const std::string&)> m_callback;
    
    // Keyboard layout - hex chars in 4x4 grid
    static constexpr const char* KEYBOARD_ROWS[] = {
        "0123",
        "4567",
        "89ab",
        "cdef"
    };
    
    static constexpr int ROWS = 4;
    
    int m_cursorX = 0;
    int m_cursorY = 0;
    int m_selectedAction = -1; // -1 = keyboard, 0 = Randomize, 1 = Clear, 2 = Cancel
    
    tsl::elm::CustomDrawer* m_drawer = nullptr;
public:
    PassphraseKeyboardGui(const std::string& initial, size_t maxLen, const std::string& header, 
                          std::function<void(const std::string&)> callback)

        : m_text(initial), m_header(header), m_maxLen(maxLen), m_callback(callback) {}
    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame(m_header.c_str(), "Passphrase");
        
                    // Draw current text with Ryujinx- prefix
        m_drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
            std::string display = "Ryujinx-" + m_text;
            renderer->drawString(display.c_str(), false, x + 20, y + 80, 20, 0xFFFF);
            
            // Draw cursor indicator
            std::string cursor = "_";
            renderer->drawString(cursor.c_str(), false, x + 20 + (display.length() * 12), y + 80, 20, 0xFFFF);
            
            // Draw keyboard (4x4 grid for hex chars)
            if (m_selectedAction == -1) {
                for (int row = 0; row < ROWS; row++) {
                    const char* keys = KEYBOARD_ROWS[row];
                    int keyCount = strlen(keys);
                    
                    for (int col = 0; col < keyCount; col++) {
                        int drawX = x + 50 + (col * 70);
                        int drawY = y + 130 + (row * 50);
                        
                        // Highlight selected key
                        tsl::Color color = (row == m_cursorY && col == m_cursorX) ? tsl::Color(0xF, 0xF, 0x0, 0xF) : tsl::Color(0xF, 0xF, 0xF, 0xF);
                        
                        char key[2] = {keys[col], '\0'};
                        renderer->drawString(key, false, drawX, drawY, 28, color);
                    }
                }
            }
            
            // Draw action buttons
            int buttonY = y + 350;
            const char* buttons[] = {"[Randomize]", "[Clear]", "[Cancel]"};
            for (int i = 0; i < 3; i++) {
                tsl::Color color = (m_selectedAction == i) ? tsl::Color(0xF, 0xF, 0x0, 0xF) : tsl::Color(0xA, 0xA, 0xA, 0xF);
                renderer->drawString(buttons[i], false, x + 30 + (i * 150), buttonY, 18, color);
            }
            
            // Draw controls help
            renderer->drawString("D-Pad: Navigate  A: Select  X: Backspace  Y: Actions", false, x + 20, y + 390, 14, 0x8888);
        

        });
        frame->setContent(m_drawer);
        return frame;
    }
    virtual void update() override {}

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        // Toggle between keyboard and action buttons with Y
        if (keysDown & HidNpadButton_Y) {
            if (m_selectedAction == -1) {
                m_selectedAction = 0; // Go to actions
            } else {
                m_selectedAction = -1; // Back to keyboard
                m_cursorX = 0;
                m_cursorY = 0;
            }
            return true;
        }
        
        if (m_selectedAction == -1) {
            // Keyboard navigation
            if (keysDown & HidNpadButton_Down) {
                m_cursorY = (m_cursorY + 1) % ROWS;
                const char* row = KEYBOARD_ROWS[m_cursorY];
                if (m_cursorX >= (int)strlen(row)) {
                    m_cursorX = strlen(row) - 1;
                }
                return true;
            }
            if (keysDown & HidNpadButton_Up) {
                m_cursorY = (m_cursorY - 1 + ROWS) % ROWS;
                const char* row = KEYBOARD_ROWS[m_cursorY];
                if (m_cursorX >= (int)strlen(row)) {
                    m_cursorX = strlen(row) - 1;
                }
                return true;
            }
            if (keysDown & HidNpadButton_Right) {
                const char* row = KEYBOARD_ROWS[m_cursorY];
                m_cursorX = (m_cursorX + 1) % strlen(row);
                return true;
            }
            if (keysDown & HidNpadButton_Left) {
                const char* row = KEYBOARD_ROWS[m_cursorY];
                m_cursorX = (m_cursorX - 1 + strlen(row)) % strlen(row);
                return true;
            }
            if (keysDown & HidNpadButton_A) {
                // Add character (limit to maxLen hex digits)
                if (m_text.length() < m_maxLen) {
                    const char* row = KEYBOARD_ROWS[m_cursorY];
                    char ch = row[m_cursorX];
                    m_text += ch;
                    // Auto-validate on 8th char
                    if (m_text.length() == 8 && m_callback) {
                        m_callback(std::string("Ryujinx-") + m_text);
                        tsl::goBack();
                        return true;
                    }
                }
                return true;
            }
        } else {
            // Action buttons navigation
            if (keysDown & HidNpadButton_Right) {
                m_selectedAction = (m_selectedAction + 1) % 3;
                return true;
            }
            if (keysDown & HidNpadButton_Left) {
                m_selectedAction = (m_selectedAction - 1 + 3) % 3;
                return true;
            }
            if (keysDown & HidNpadButton_A) {
                switch (m_selectedAction) {
                    case 0: { // Randomize and apply immediately
                        static const char hex[] = "0123456789abcdef";
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<> dis(0, 15);
                        m_text.clear();
                        for (int i = 0; i < 8; i++) {
                            m_text += hex[dis(gen)];
                        }
                        if (m_callback) {
                            m_callback(std::string("Ryujinx-") + m_text);
                        }
                        tsl::goBack();
                        return true;
                    }
                    case 1: // Clear and apply immediately (empty passphrase)
                        if (m_callback) {
                            m_callback("");
                        }
                        tsl::goBack();
                        return true;
                    case 2: // Cancel
                        tsl::goBack();
                        return true;
                }
                return true;
            }
        }
        
        if (keysDown & HidNpadButton_X) {
            // Backspace
            if (!m_text.empty()) {
                m_text.pop_back();
            }
            return true;
        }
        if (keysDown & HidNpadButton_B) {
            // Cancel
            tsl::goBack();
            return true;
        }
        
        return false;
    }
};
