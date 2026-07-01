#pragma once
// ============================================================================
// CONTROLLERS MODULE - Multi-Controller XInput Manager
// INVENTED BY: ASTON WALKER
// ============================================================================

#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

#include <vector>
#include <string>
#include <array>
#include <iostream>

static constexpr int CTRL_MAX = XUSER_MAX_COUNT; // 4

// ============================================================================
// ControlledPlayer - ONE PLAYER can control MULTIPLE agents
// ============================================================================
struct ControlledPlayer {
    std::vector<int>  agentIndices;          // all agents this player controls
    std::vector<bool> agentControlOn;        // PER-AGENT ON/OFF flag (parallel to agentIndices)
    bool controlOn;                          // DEPRECATED - kept for compatibility
    int  xInputSlot;                         // Xbox slot (0-3) or -1 for keyboard
    int  currentAgentCursor;                 // which agent the cursor is on in the menu

    ControlledPlayer() : controlOn(true), xInputSlot(-1), currentAgentCursor(0) {}

    // Returns the index of the currently cursor-selected agent
    int agentIndex() const {
        if (agentIndices.empty()) return -1;
        return agentIndices[currentAgentCursor];
    }

    // Returns whether the cursor-selected agent is ON
    bool currentAgentIsOn() const {
        if (agentControlOn.empty() || currentAgentCursor >= (int)agentControlOn.size()) return false;
        return agentControlOn[currentAgentCursor];
    }

    // Toggle the cursor-selected agent ON or OFF
    void setCurrentAgentOn(bool on) {
        if (currentAgentCursor < (int)agentControlOn.size())
            agentControlOn[currentAgentCursor] = on;
    }

    // PER-AGENT control methods (THE FIX)
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
// RawControllerState - XInput state snapshot
// ============================================================================
struct RawControllerState {
    bool  connected = false;
    WORD  buttons = 0;
    BYTE  leftTrigger = 0;
    BYTE  rightTrigger = 0;
    SHORT leftStickX = 0;
    SHORT leftStickY = 0;
    SHORT rightStickX = 0;
    SHORT rightStickY = 0;
};

// ============================================================================
// ControllerManager - Manages up to 4 Xbox controllers
// ============================================================================
class ControllerManager {
public:
    ControllerManager();
    void poll();

    bool isConnected(int slot) const;
    const RawControllerState& slotState(int slot) const;
    int connectedCount() const;
    std::vector<int> connectedSlots() const;

    // Trigger access (2026 XInput Standards)
    bool leftTriggerPressed(BYTE threshold = 128) const;
    bool rightTriggerPressed(BYTE threshold = 128) const;

    // Combined keyboard + controller helpers
    bool isPauseCombo(bool press6, bool press7) const;
    bool isTextMafiaTrigger(bool pressT) const;

    // Trait nudging
    bool doIncAggression(bool pressW, bool shiftHeld) const;
    bool doIncLoyalty(bool pressA, bool shiftHeld) const;
    bool doIncParanoia(bool pressS, bool shiftHeld) const;
    bool doIncDeceit(bool pressD, bool shiftHeld) const;

    bool doDecAggression(bool pressW, bool shiftHeld) const;
    bool doDecLoyalty(bool pressA, bool shiftHeld) const;
    bool doDecParanoia(bool pressS, bool shiftHeld) const;
    bool doDecDeceit(bool pressD, bool shiftHeld) const;

    // Menu navigation
    bool menuY() const;
    bool menuA() const;
    bool menuX() const;
    bool menuDLeft() const;
    bool menuDRight() const;

private:
    std::array<RawControllerState, CTRL_MAX> m_slots;
    bool anyButton(WORD mask) const;
    bool anyRightTrigger(BYTE threshold = 128) const;
    bool anyLeftTrigger(BYTE threshold = 128) const;
};

// ============================================================================
// Setup controllers at game start
// ============================================================================
std::vector<ControlledPlayer> setupControllers(
    int n,
    const std::vector<std::string>& names,
    ControllerManager& cm);