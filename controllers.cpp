// ============================================================================
// CONTROLLERS MODULE - Multi-Controller XInput Manager
// INVENTED BY: ASTON WALKER
// ============================================================================

#include "controllers.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// XInput button masks
// ============================================================================
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
// ControllerManager Implementation
// ============================================================================

ControllerManager::ControllerManager() {
    for (int i = 0; i < CTRL_MAX; ++i) {
        m_slots[i].connected = false;
    }
}

void ControllerManager::poll() {
    for (int i = 0; i < CTRL_MAX; ++i) {
        XINPUT_STATE state;
        DWORD result = XInputGetState(i, &state);

        if (result == ERROR_SUCCESS) {
            m_slots[i].connected = true;
            m_slots[i].buttons = state.Gamepad.wButtons;
            m_slots[i].leftTrigger = state.Gamepad.bLeftTrigger;
            m_slots[i].rightTrigger = state.Gamepad.bRightTrigger;
            m_slots[i].leftStickX = state.Gamepad.sThumbLX;
            m_slots[i].leftStickY = state.Gamepad.sThumbLY;
            m_slots[i].rightStickX = state.Gamepad.sThumbRX;
            m_slots[i].rightStickY = state.Gamepad.sThumbRY;
        }
        else {
            m_slots[i].connected = false;
            m_slots[i].buttons = 0;
            m_slots[i].leftTrigger = 0;
            m_slots[i].rightTrigger = 0;
            m_slots[i].leftStickX = 0;
            m_slots[i].leftStickY = 0;
            m_slots[i].rightStickX = 0;
            m_slots[i].rightStickY = 0;
        }
    }
}

bool ControllerManager::isConnected(int slot) const {
    if (slot < 0 || slot >= CTRL_MAX) return false;
    return m_slots[slot].connected;
}

const RawControllerState& ControllerManager::slotState(int slot) const {
    static RawControllerState empty;
    if (slot < 0 || slot >= CTRL_MAX) return empty;
    return m_slots[slot];
}

int ControllerManager::connectedCount() const {
    int count = 0;
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected) count++;
    }
    return count;
}

std::vector<int> ControllerManager::connectedSlots() const {
    std::vector<int> slots;
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected) slots.push_back(i);
    }
    return slots;
}

bool ControllerManager::anyButton(WORD mask) const {
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected && (m_slots[i].buttons & mask)) return true;
    }
    return false;
}

bool ControllerManager::anyRightTrigger(BYTE threshold) const {
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected && m_slots[i].rightTrigger >= threshold) return true;
    }
    return false;
}

bool ControllerManager::anyLeftTrigger(BYTE threshold) const {
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected && m_slots[i].leftTrigger >= threshold) return true;
    }
    return false;
}

bool ControllerManager::leftTriggerPressed(BYTE threshold) const {
    return anyLeftTrigger(threshold);
}

bool ControllerManager::rightTriggerPressed(BYTE threshold) const {
    return anyRightTrigger(threshold);
}

bool ControllerManager::isPauseCombo(bool press6, bool press7) const {
    bool xboxX = anyButton(XINPUT_GAMEPAD_X);
    return (press6 || press7 || xboxX);
}

bool ControllerManager::isTextMafiaTrigger(bool pressT) const {
    return pressT;
}

// ============================================================================
// Trait Nudging Helpers
// ============================================================================

bool ControllerManager::doIncAggression(bool pressW, bool shiftHeld) const {
    bool stickUp = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected && m_slots[i].leftStickY > 8000) {
            stickUp = true;
            break;
        }
    }
    return (pressW && !shiftHeld) || stickUp;
}

bool ControllerManager::doIncLoyalty(bool pressA, bool shiftHeld) const {
    bool stickLeft = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected && m_slots[i].leftStickX < -8000) {
            stickLeft = true;
            break;
        }
    }
    return (pressA && !shiftHeld) || stickLeft;
}

bool ControllerManager::doIncParanoia(bool pressS, bool shiftHeld) const {
    bool stickDown = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected && m_slots[i].leftStickY < -8000) {
            stickDown = true;
            break;
        }
    }
    return (pressS && !shiftHeld) || stickDown;
}

bool ControllerManager::doIncDeceit(bool pressD, bool shiftHeld) const {
    bool stickRight = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        if (m_slots[i].connected && m_slots[i].leftStickX > 8000) {
            stickRight = true;
            break;
        }
    }
    return (pressD && !shiftHeld) || stickRight;
}
//start here
bool ControllerManager::doDecAggression(bool pressW, bool shiftHeld) const {
    bool stickUpClick = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        // FIX: Check stick is UP (>8000) AND left thumbstick clicked
        if (m_slots[i].connected &&
            (m_slots[i].buttons & XINPUT_GAMEPAD_LEFT_THUMB) &&
            m_slots[i].leftStickY > 8000) {
            stickUpClick = true;
            break;
        }
    }
    return (pressW && shiftHeld) || stickUpClick;
}

bool ControllerManager::doDecLoyalty(bool pressA, bool shiftHeld) const {
    bool stickLeftClick = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        // FIX: Check stick is LEFT (< -8000) AND left thumbstick clicked
        if (m_slots[i].connected &&
            (m_slots[i].buttons & XINPUT_GAMEPAD_LEFT_THUMB) &&
            m_slots[i].leftStickX < -8000) {
            stickLeftClick = true;
            break;
        }
    }
    return (pressA && shiftHeld) || stickLeftClick;
}

bool ControllerManager::doDecParanoia(bool pressS, bool shiftHeld) const {
    bool stickDownClick = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        // FIX: Check stick is DOWN (< -8000) AND left thumbstick clicked
        if (m_slots[i].connected &&
            (m_slots[i].buttons & XINPUT_GAMEPAD_LEFT_THUMB) &&
            m_slots[i].leftStickY < -8000) {
            stickDownClick = true;
            break;
        }
    }
    return (pressS && shiftHeld) || stickDownClick;
}

bool ControllerManager::doDecDeceit(bool pressD, bool shiftHeld) const {
    bool stickRightClick = false;
    for (int i = 0; i < CTRL_MAX; ++i) {
        // FIX: Check stick is RIGHT (>8000) AND left thumbstick clicked
        if (m_slots[i].connected &&
            (m_slots[i].buttons & XINPUT_GAMEPAD_LEFT_THUMB) &&
            m_slots[i].leftStickX > 8000) {
            stickRightClick = true;
            break;
        }
    }
    return (pressD && shiftHeld) || stickRightClick;
}

//finish here

// ============================================================================
// Menu Navigation
// ============================================================================

bool ControllerManager::menuY() const {
    return anyButton(XINPUT_GAMEPAD_Y);
}

bool ControllerManager::menuA() const {
    return anyButton(XINPUT_GAMEPAD_A);
}

bool ControllerManager::menuX() const {
    return anyButton(XINPUT_GAMEPAD_X);
}

bool ControllerManager::menuDLeft() const {
    return anyButton(XINPUT_GAMEPAD_DPAD_LEFT);
}

bool ControllerManager::menuDRight() const {
    return anyButton(XINPUT_GAMEPAD_DPAD_RIGHT);
}

// ============================================================================
// setupControllers - Initialize players and their controlled agents
// ============================================================================

std::vector<ControlledPlayer> setupControllers(
    int n,
    const std::vector<std::string>& names,
    ControllerManager& cm)
{
    std::vector<ControlledPlayer> players;

    std::cout << "\n[MODULE 001] Controller Detection\n";
    std::cout << "  XInput supports slots 0-3 (up to 4 simultaneous controllers)\n";

    cm.poll();
    std::vector<int> connected = cm.connectedSlots();

    if (connected.empty()) {
        std::cout << "  No Xbox controllers detected. Keyboard input only.\n";
    }
    else {
        std::cout << "  Detected " << connected.size() << " controller(s): ";
        for (int s : connected) std::cout << "Slot " << s << " ";
        std::cout << "\n";
    }

    int humanCount = 0;
    std::cout << "\n[MODULE 001] How many human players? (0 to disable, max 4): ";
    std::cin >> humanCount;
    humanCount = std::min(std::max(0, humanCount), 4);

    for (int i = 0; i < humanCount; ++i) {
        ControlledPlayer cp;

        if (i < (int)connected.size()) {
            cp.xInputSlot = connected[i];
            std::cout << "\n  Player " << (i + 1) << " | Slot " << cp.xInputSlot << " (Controller detected)\n";
        }
        else {
            cp.xInputSlot = -1;
            std::cout << "\n  Player " << (i + 1) << " | No pad available - keyboard input only\n";
        }

        int agentCount = 0;
        std::cout << "    How many agents does Player " << (i + 1) << " control? (1 to " << n << "): ";
        std::cin >> agentCount;
        agentCount = std::min(std::max(1, agentCount), n);

        cp.agentIndices.clear();
        cp.agentControlOn.clear();  // PER-AGENT CONTROL INIT

        for (int j = 0; j < agentCount; ++j) {
            int idx;
            std::cout << "    Agent " << (j + 1) << " index for Player " << (i + 1) << " (0 to " << (n - 1) << "): ";
            std::cin >> idx;
            if (idx >= 0 && idx < n) {
                cp.agentIndices.push_back(idx);
                cp.agentControlOn.push_back(true);  // ← PER-AGENT CONTROL ENABLED
                std::cout << "    -> Agent " << idx << " (" << names[idx] << ") | ON | alive\n";
            }
            else {
                std::cout << "    Invalid index, skipping.\n";
            }
        }

        cp.controlOn = true;  // legacy
        cp.currentAgentCursor = 0;
        players.push_back(cp);
    }

    std::cout << "\n[MODULE 001] " << players.size() << " human player(s) configured.\n";
    for (size_t i = 0; i < players.size(); ++i) {
        std::cout << "  Player " << (i + 1) << " | Slot " << players[i].xInputSlot << " | Agents: ";
        for (size_t j = 0; j < players[i].agentIndices.size(); ++j) {
            std::cout << players[i].agentIndices[j] << "(" << names[players[i].agentIndices[j]] << ")";
            if (players[i].getAgentControl(j)) std::cout << "[ON] ";
            else std::cout << "[OFF] ";
        }
        std::cout << "\n";
    }

    return players;
}