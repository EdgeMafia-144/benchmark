#ifndef CONTROLLERS_LINUX_H
#define CONTROLLERS_LINUX_H

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstring>

#include "evdev_controller.h"
#include "input_linux.h"

// ============================================================================
// CONTROLLERS MODULE for Linux - Supports Xbox One/8BitDo + Keyboard
// INVENTED BY: ASTON WALKER (ported to Linux with evdev)
// ============================================================================

// Button masks matching XInput for compatibility
#define XINPUT_GAMEPAD_DPAD_UP        0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN      0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT      0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT     0x0008
#define XINPUT_GAMEPAD_START          0x0010
#define XINPUT_GAMEPAD_BACK           0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB     0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB    0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A              0x1000
#define XINPUT_GAMEPAD_B              0x2000
#define XINPUT_GAMEPAD_X              0x4000
#define XINPUT_GAMEPAD_Y              0x8000

// ============================================================================
// ControlledPlayer - ONE PLAYER can control MULTIPLE agents
// ============================================================================
struct ControlledPlayer {
    std::vector<int>  agentIndices;          // all agents this player controls
    std::vector<bool> agentControlOn;        // PER-AGENT ON/OFF flag
    bool controlOn;                          // legacy compatibility
    int  xInputSlot;                         // Xbox slot (0-3) or -1 for keyboard
    int  currentAgentCursor;                 // which agent the cursor is on

    ControlledPlayer() : controlOn(true), xInputSlot(-1), currentAgentCursor(0) {}

    int agentIndex() const {
        if (agentIndices.empty()) return -1;
        if (currentAgentCursor >= (int)agentIndices.size()) return -1;
        return agentIndices[currentAgentCursor];
    }

    bool currentAgentIsOn() const {
        if (agentControlOn.empty() || currentAgentCursor >= (int)agentControlOn.size()) 
            return false;
        return agentControlOn[currentAgentCursor];
    }

    void setCurrentAgentOn(bool on) {
        if (currentAgentCursor < (int)agentControlOn.size())
            agentControlOn[currentAgentCursor] = on;
    }

    void setAgentControl(int agentPos, bool on) {
        if (agentPos >= 0 && agentPos < (int)agentControlOn.size()) {
            agentControlOn[agentPos] = on;
        }
    }

    bool getAgentControl(int agentPos) const {
        if (agentPos >= 0 && agentPos < (int)agentControlOn.size()) {
            return agentControlOn[agentPos];
        }
        return false;
    }

    bool anyAgentControlOn() const {
        for (bool b : agentControlOn) if (b) return true;
        return false;
    }
};

// ============================================================================
// ControllerManager - Manages Xbox/8BitDo controllers + keyboard
// ============================================================================
class ControllerManager {
private:
    EvdevControllerManager evdev;
    int lastControllerCount;
    bool keyboardEnabled;

public:
    ControllerManager() : lastControllerCount(0), keyboardEnabled(true) {
        evdev.connectControllers();
        lastControllerCount = evdev.getConnectedCount();
        
        std::cout << "[CONTROLLER] Found " << lastControllerCount << " gamepad(s)\n";
        for (int i = 0; i < lastControllerCount; ++i) {
            std::cout << "  Slot " << i << ": " << evdev.getControllerName(i) << "\n";
        }
        if (lastControllerCount == 0) {
            std::cout << "  No controllers detected. Using keyboard only.\n";
        }
    }
    
    void poll() {
        evdev.poll();
        int currentCount = evdev.getConnectedCount();
        if (currentCount != lastControllerCount) {
            std::cout << "[CONTROLLER] Controller count changed: " 
                      << lastControllerCount << " -> " << currentCount << "\n";
            lastControllerCount = currentCount;
        }
    }
    
    bool isConnected(int slot) const {
        return evdev.isConnected(slot);
    }
    
    int connectedCount() const {
        return evdev.getConnectedCount();
    }
    
    std::vector<int> connectedSlots() const {
        return evdev.getConnectedSlots();
    }
    
    uint16_t getButtons(int slot) const {
        return evdev.getButtons(slot);
    }
    
    uint8_t getLeftTrigger(int slot) const {
        return evdev.getLeftTrigger(slot);
    }
    
    uint8_t getRightTrigger(int slot) const {
        return evdev.getRightTrigger(slot);
    }
    
    int16_t getLeftStickX(int slot) const {
        return evdev.getLeftStickX(slot);
    }
    
    int16_t getLeftStickY(int slot) const {
        return evdev.getLeftStickY(slot);
    }
    
    // Combined controller + keyboard button checks
    bool anyButton(uint16_t mask) const {
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if ((evdev.getButtons(i) & mask) != 0) return true;
        }
        return false;
    }
    
    bool anyRightTrigger(uint8_t threshold = 128) const {
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if (evdev.getRightTrigger(i) >= threshold) return true;
        }
        return false;
    }
    
    bool anyLeftTrigger(uint8_t threshold = 128) const {
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if (evdev.getLeftTrigger(i) >= threshold) return true;
        }
        return false;
    }
    
    bool leftTriggerPressed(uint8_t threshold = 128) const {
        return anyLeftTrigger(threshold);
    }
    
    bool rightTriggerPressed(uint8_t threshold = 128) const {
        return anyRightTrigger(threshold);
    }
    
    // Menu navigation - Controller buttons
    bool menuY() const {
        return anyButton(XINPUT_GAMEPAD_Y);
    }
    
    bool menuA() const {
        return anyButton(XINPUT_GAMEPAD_A);
    }
    
    bool menuX() const {
        return anyButton(XINPUT_GAMEPAD_X);
    }
    
    bool menuDLeft() const {
        return anyButton(XINPUT_GAMEPAD_DPAD_LEFT);
    }
    
    bool menuDRight() const {
        return anyButton(XINPUT_GAMEPAD_DPAD_RIGHT);
    }
    
    bool menuUp() const {
        return anyButton(XINPUT_GAMEPAD_DPAD_UP);
    }
    
    bool menuDown() const {
        return anyButton(XINPUT_GAMEPAD_DPAD_DOWN);
    }
    
    // Trait nudging - controller support
    bool doIncAggression(bool pressW, bool shiftHeld) const {
        bool stickUp = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if (evdev.getLeftStickY(i) > 8000) {
                stickUp = true;
                break;
            }
        }
        return (pressW && !shiftHeld) || stickUp;
    }
    
    bool doIncLoyalty(bool pressA, bool shiftHeld) const {
        bool stickLeft = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if (evdev.getLeftStickX(i) < -8000) {
                stickLeft = true;
                break;
            }
        }
        return (pressA && !shiftHeld) || stickLeft;
    }
    
    bool doIncParanoia(bool pressS, bool shiftHeld) const {
        bool stickDown = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if (evdev.getLeftStickY(i) < -8000) {
                stickDown = true;
                break;
            }
        }
        return (pressS && !shiftHeld) || stickDown;
    }
    
    bool doIncDeceit(bool pressD, bool shiftHeld) const {
        bool stickRight = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if (evdev.getLeftStickX(i) > 8000) {
                stickRight = true;
                break;
            }
        }
        return (pressD && !shiftHeld) || stickRight;
    }
    
    bool doDecAggression(bool pressW, bool shiftHeld) const {
        bool stickUpClick = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if ((evdev.getButtons(i) & XINPUT_GAMEPAD_LEFT_THUMB) &&
                evdev.getLeftStickY(i) > 8000) {
                stickUpClick = true;
                break;
            }
        }
        return (pressW && shiftHeld) || stickUpClick;
    }
    
    bool doDecLoyalty(bool pressA, bool shiftHeld) const {
        bool stickLeftClick = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if ((evdev.getButtons(i) & XINPUT_GAMEPAD_LEFT_THUMB) &&
                evdev.getLeftStickX(i) < -8000) {
                stickLeftClick = true;
                break;
            }
        }
        return (pressA && shiftHeld) || stickLeftClick;
    }
    
    bool doDecParanoia(bool pressS, bool shiftHeld) const {
        bool stickDownClick = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if ((evdev.getButtons(i) & XINPUT_GAMEPAD_LEFT_THUMB) &&
                evdev.getLeftStickY(i) < -8000) {
                stickDownClick = true;
                break;
            }
        }
        return (pressS && shiftHeld) || stickDownClick;
    }
    
    bool doDecDeceit(bool pressD, bool shiftHeld) const {
        bool stickRightClick = false;
        for (int i = 0; i < evdev.getConnectedCount(); ++i) {
            if ((evdev.getButtons(i) & XINPUT_GAMEPAD_LEFT_THUMB) &&
                evdev.getLeftStickX(i) > 8000) {
                stickRightClick = true;
                break;
            }
        }
        return (pressD && shiftHeld) || stickRightClick;
    }
};

// ============================================================================
// setupControllers - Initialize players and their controlled agents
// ============================================================================
inline std::vector<ControlledPlayer> setupControllers(
    int n,
    const std::vector<std::string>& names,
    ControllerManager& cm) {
    
    std::vector<ControlledPlayer> players;
    
    std::cout << "\n[MODULE 001] Controller Detection (Linux - evdev)\n";
    std::cout << "  Supported: Xbox One (wired/BT), 8BitDo, standard gamepads\n";
    
    int controllerCount = cm.connectedCount();
    if (controllerCount > 0) {
        std::cout << "  Detected " << controllerCount << " gamepad(s):\n";
        for (int i = 0; i < controllerCount; ++i) {
            std::cout << "    Slot " << i << ": " << cm.getButtons(i) << " buttons\n";
        }
    } else {
        std::cout << "  No gamepads detected. Keyboard input only.\n";
        std::cout << "  (Try: sudo modprobe xpad or install xpadneo/xone driver)\n";
    }
    
    int humanCount = 0;
    std::cout << "\n[MODULE 001] How many human players? (0 to disable, max 4): ";
    std::cin >> humanCount;
    humanCount = std::min(std::max(0, humanCount), 4);
    
    for (int i = 0; i < humanCount; ++i) {
        ControlledPlayer cp;
        
        if (i < controllerCount) {
            cp.xInputSlot = i;
            std::cout << "\n  Player " << (i + 1) << " | Controller Slot " << cp.xInputSlot << "\n";
        } else {
            cp.xInputSlot = -1;
            std::cout << "\n  Player " << (i + 1) << " | Keyboard input\n";
        }
        
        int agentCount = 0;
        std::cout << "    How many agents does Player " << (i + 1) << " control? (1 to " << n << "): ";
        std::cin >> agentCount;
        agentCount = std::min(std::max(1, agentCount), n);
        
        cp.agentIndices.clear();
        cp.agentControlOn.clear();
        
        for (int j = 0; j < agentCount; ++j) {
            int idx;
            std::cout << "    Agent " << (j + 1) << " index for Player " << (i + 1) << " (0 to " << (n - 1) << "): ";
            std::cin >> idx;
            if (idx >= 0 && idx < n) {
                cp.agentIndices.push_back(idx);
                cp.agentControlOn.push_back(true);
                std::cout << "    -> Agent " << idx << " (" << names[idx] << ") | ON | alive\n";
            } else {
                std::cout << "    Invalid index, skipping.\n";
            }
        }
        
        cp.controlOn = true;
        cp.currentAgentCursor = 0;
        players.push_back(cp);
    }
    
    std::cout << "\n[MODULE 001] " << players.size() << " human player(s) configured.\n";
    for (size_t i = 0; i < players.size(); ++i) {
        std::cout << "  Player " << (i + 1) << " | ";
        if (players[i].xInputSlot >= 0) {
            std::cout << "Controller Slot " << players[i].xInputSlot;
        } else {
            std::cout << "Keyboard";
        }
        std::cout << " | Agents: ";
        for (size_t j = 0; j < players[i].agentIndices.size(); ++j) {
            std::cout << players[i].agentIndices[j] << "(" << names[players[i].agentIndices[j]] << ")";
            if (players[i].getAgentControl(j)) std::cout << "[ON] ";
            else std::cout << "[OFF] ";
        }
        std::cout << "\n";
    }
    
    return players;
}

#endif // CONTROLLERS_LINUX_H
