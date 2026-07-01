#ifndef EVDEV_CONTROLLER_H
#define EVDEV_CONTROLLER_H

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

// ============================================================================
// Xbox One / 8BitDo Controller Support via evdev
// Supports up to 4 Xbox One controllers (wired or Bluetooth)
// Also supports 8BitDo USB/Bluetooth gamepads
// ============================================================================

// NOTE: BTN_A, BTN_B, BTN_X, BTN_Y, etc. are already defined in linux/input-event-codes.h
// We will use the kernel's definitions and map them to XInput values in code.

// Xbox/8BitDo specific button codes that may not be in standard headers
#ifndef BTN_MODE
#define BTN_MODE      0x13c   // Guide/Home button
#endif

#ifndef BTN_SELECT
#define BTN_SELECT    0x136   // Some controllers use SELECT instead of BACK
#endif

// Axis mapping - using standard kernel definitions
// ABS_X (0x00), ABS_Y (0x01), ABS_RX (0x03), ABS_RY (0x04) are standard
// ABS_Z (0x02) and ABS_RZ (0x05) are standard triggers on some controllers
// ABS_HAT0X (0x10) and ABS_HAT0Y (0x11) are standard D-pad

struct ControllerState {
    bool connected = false;
    std::string devicePath;
    std::string name;
    int fd = -1;
    
    // XInput-compatible state
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t leftStickX = 0;
    int16_t leftStickY = 0;
    int16_t rightStickX = 0;
    int16_t rightStickY = 0;
    
    // Raw axis values for calibration
    int16_t raw_leftStickX = 0;
    int16_t raw_leftStickY = 0;
    int16_t raw_rightStickX = 0;
    int16_t raw_rightStickY = 0;
    int32_t raw_leftTrigger = 0;
    int32_t raw_rightTrigger = 0;
    
    // Calibration centers
    int16_t center_leftX = 0;
    int16_t center_leftY = 0;
    int16_t center_rightX = 0;
    int16_t center_rightY = 0;
    
    ControllerState() {
        center_leftX = 0;
        center_leftY = 0;
        center_rightX = 0;
        center_rightY = 0;
    }
};

class EvdevControllerManager {
private:
    static constexpr int MAX_CONTROLLERS = 4;
    std::vector<ControllerState> controllers;
    int activeControllerCount = 0;
    
    // Map kernel event codes to XInput button bits
    uint16_t mapButtonToXInput(uint16_t evCode, int value) {
        // Using kernel's standard button codes
        switch (evCode) {
            case BTN_SOUTH:     // A button on standard gamepads
                return 0x1000;  // XINPUT_GAMEPAD_A
            case BTN_EAST:      // B button
                return 0x2000;  // XINPUT_GAMEPAD_B
            case BTN_NORTH:     // X button
                return 0x4000;  // XINPUT_GAMEPAD_X
            case BTN_WEST:      // Y button
                return 0x8000;  // XINPUT_GAMEPAD_Y
            case BTN_TL:        // Left bumper (shoulder)
                return 0x0100;  // XINPUT_GAMEPAD_LEFT_SHOULDER
            case BTN_TR:        // Right bumper (shoulder)
                return 0x0200;  // XINPUT_GAMEPAD_RIGHT_SHOULDER
            case BTN_SELECT:    // Select/Back button
            case BTN_BACK:
                return 0x0020;  // XINPUT_GAMEPAD_BACK
            case BTN_START:
                return 0x0010;  // XINPUT_GAMEPAD_START
            case BTN_THUMBL:    // Left thumbstick click
                return 0x0040;  // XINPUT_GAMEPAD_LEFT_THUMB
            case BTN_THUMBR:    // Right thumbstick click
                return 0x0080;  // XINPUT_GAMEPAD_RIGHT_THUMB
            case BTN_MODE:      // Guide/Home button
                return 0x0000;  // No direct XInput mapping, but used for pause
            default:
                return 0x0000;  // Unknown button
        }
    }
    
    // Convert raw axis value (-32768 to 32767) to normalized XInput style
    int16_t normalizeStickAxis(int32_t value, int16_t center) {
        int32_t centered = value - center;
        // Clamp to -32768..32767 range
        if (centered < -32768) centered = -32768;
        if (centered > 32767) centered = 32767;
        return static_cast<int16_t>(centered);
    }
    
    // Convert raw trigger value (0-1023) to Xbox style (0-255)
    uint8_t normalizeTrigger(int32_t value, int32_t maxVal) {
        if (maxVal <= 0) return 0;
        return static_cast<uint8_t>((value * 255) / maxVal);
    }
    
    // Detect if a device is a gamepad
    bool isGamepad(const std::string& devicePath) {
        int fd = open(devicePath.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) return false;
        
        unsigned int evbit[EV_MAX/8 + 1];
        unsigned int keybit[KEY_MAX/8 + 1];
        unsigned int absbit[ABS_MAX/8 + 1];
        
        memset(evbit, 0, sizeof(evbit));
        memset(keybit, 0, sizeof(keybit));
        memset(absbit, 0, sizeof(absbit));
        
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) >= 0 &&
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) >= 0 &&
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) >= 0) {
            
            bool hasGamepadButton = (keybit[BTN_GAMEPAD/8] & (1 << (BTN_GAMEPAD%8))) != 0;
            bool hasJoystickButton = (keybit[BTN_JOYSTICK/8] & (1 << (BTN_JOYSTICK%8))) != 0;
            bool hasAbsX = (absbit[ABS_X/8] & (1 << (ABS_X%8))) != 0;
            
            if (hasGamepadButton || hasJoystickButton || hasAbsX) {
                close(fd);
                return true;
            }
        }
        
        close(fd);
        return false;
    }
    
    // Get device name
    std::string getDeviceName(int fd) {
        char name[256] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
            return std::string(name);
        }
        return "Unknown Gamepad";
    }
    
    // Calibrate axis centers
    void calibrateController(ControllerState& ctrl) {
        const int SAMPLES = 60;
        int64_t sumLX = 0, sumLY = 0, sumRX = 0, sumRY = 0;
        int samplesLX = 0, samplesLY = 0, samplesRX = 0, samplesRY = 0;
        
        // Read axis info from device first
        struct input_absinfo absinfo;
        if (ioctl(ctrl.fd, EVIOCGABS(ABS_X), &absinfo) >= 0) {
            ctrl.center_leftX = (absinfo.maximum + absinfo.minimum) / 2;
        }
        if (ioctl(ctrl.fd, EVIOCGABS(ABS_Y), &absinfo) >= 0) {
            ctrl.center_leftY = (absinfo.maximum + absinfo.minimum) / 2;
        }
        if (ioctl(ctrl.fd, EVIOCGABS(ABS_RX), &absinfo) >= 0) {
            ctrl.center_rightX = (absinfo.maximum + absinfo.minimum) / 2;
        }
        if (ioctl(ctrl.fd, EVIOCGABS(ABS_RY), &absinfo) >= 0) {
            ctrl.center_rightY = (absinfo.maximum + absinfo.minimum) / 2;
        }
        
        // Sample actual values for fine-tuning
        for (int i = 0; i < SAMPLES; ++i) {
            struct input_event ev;
            ssize_t n = read(ctrl.fd, &ev, sizeof(ev));
            if (n == sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    switch (ev.code) {
                        case ABS_X: sumLX += ev.value; samplesLX++; break;
                        case ABS_Y: sumLY += ev.value; samplesLY++; break;
                        case ABS_RX: sumRX += ev.value; samplesRX++; break;
                        case ABS_RY: sumRY += ev.value; samplesRY++; break;
                    }
                }
            }
            usleep(10000); // 10ms
        }
        
        if (samplesLX > 0) ctrl.center_leftX = (ctrl.center_leftX + sumLX / samplesLX) / 2;
        if (samplesLY > 0) ctrl.center_leftY = (ctrl.center_leftY + sumLY / samplesLY) / 2;
        if (samplesRX > 0) ctrl.center_rightX = (ctrl.center_rightX + sumRX / samplesRX) / 2;
        if (samplesRY > 0) ctrl.center_rightY = (ctrl.center_rightY + sumRY / samplesRY) / 2;
        
        // Flush remaining events
        char buf[256];
        while (read(ctrl.fd, buf, sizeof(buf)) > 0);
    }
    
public:
    EvdevControllerManager() {
        controllers.reserve(MAX_CONTROLLERS);
        for (int i = 0; i < MAX_CONTROLLERS; ++i) {
            controllers.push_back(ControllerState());
        }
    }
    
    ~EvdevControllerManager() {
        for (auto& ctrl : controllers) {
            if (ctrl.fd >= 0) {
                close(ctrl.fd);
            }
        }
    }
    
    // Scan and connect to gamepads
    int connectControllers() {
        activeControllerCount = 0;
        
        DIR* dir = opendir("/dev/input");
        if (!dir) {
            std::cerr << "[EVDEV] Could not open /dev/input\n";
            return 0;
        }
        
        std::vector<std::string> evdevDevices;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "event", 5) == 0) {
                std::string path = "/dev/input/" + std::string(entry->d_name);
                if (isGamepad(path)) {
                    evdevDevices.push_back(path);
                }
            }
        }
        closedir(dir);
        
        // Also check joydev devices
        dir = opendir("/dev/input");
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "js", 2) == 0) {
                    std::string path = "/dev/input/" + std::string(entry->d_name);
                    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
                    if (fd >= 0) {
                        close(fd);
                        evdevDevices.push_back(path);
                    }
                }
            }
            closedir(dir);
        }
        
        // Remove duplicates and connect
        std::sort(evdevDevices.begin(), evdevDevices.end());
        evdevDevices.erase(std::unique(evdevDevices.begin(), evdevDevices.end()), evdevDevices.end());
        
        for (const auto& path : evdevDevices) {
            if (activeControllerCount >= MAX_CONTROLLERS) break;
            
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            
            // Get device name
            std::string name = getDeviceName(fd);
            bool isXbox = (name.find("Xbox") != std::string::npos) ||
                          (name.find("X-Box") != std::string::npos) ||
                          (name.find("Microsoft") != std::string::npos);
            bool is8BitDo = (name.find("8BitDo") != std::string::npos) ||
                            (name.find("8BITDO") != std::string::npos);
            
            if (isXbox || is8BitDo || isGamepad(path)) {
                controllers[activeControllerCount].connected = true;
                controllers[activeControllerCount].devicePath = path;
                controllers[activeControllerCount].name = name;
                controllers[activeControllerCount].fd = fd;
                
                // Get axis info for calibration
                struct input_absinfo absinfo;
                if (ioctl(fd, EVIOCGABS(ABS_X), &absinfo) >= 0) {
                    controllers[activeControllerCount].center_leftX = (absinfo.maximum + absinfo.minimum) / 2;
                }
                if (ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) >= 0) {
                    controllers[activeControllerCount].center_leftY = (absinfo.maximum + absinfo.minimum) / 2;
                }
                if (ioctl(fd, EVIOCGABS(ABS_RX), &absinfo) >= 0) {
                    controllers[activeControllerCount].center_rightX = (absinfo.maximum + absinfo.minimum) / 2;
                }
                if (ioctl(fd, EVIOCGABS(ABS_RY), &absinfo) >= 0) {
                    controllers[activeControllerCount].center_rightY = (absinfo.maximum + absinfo.minimum) / 2;
                }
                
                std::cout << "[EVDEV] Connected: " << name << " at " << path 
                          << " (Slot " << activeControllerCount << ")\n";
                activeControllerCount++;
            } else {
                close(fd);
            }
        }
        
        return activeControllerCount;
    }
    
    // Poll all connected controllers
    void poll() {
        for (int i = 0; i < activeControllerCount; ++i) {
            if (!controllers[i].connected || controllers[i].fd < 0) continue;
            
            struct input_event ev;
            ssize_t n;
            
            while ((n = read(controllers[i].fd, &ev, sizeof(ev))) == sizeof(ev)) {
                if (ev.type == EV_KEY) {
                    // Map kernel button codes to XInput bits
                    uint16_t xinputBit = mapButtonToXInput(ev.code, ev.value);
                    if (xinputBit != 0) {
                        if (ev.value) {
                            controllers[i].buttons |= xinputBit;
                        } else {
                            controllers[i].buttons &= ~xinputBit;
                        }
                    }
                } else if (ev.type == EV_ABS) {
                    // Handle analog axes and triggers
                    switch (ev.code) {
                        case ABS_X:
                            controllers[i].raw_leftStickX = ev.value;
                            controllers[i].leftStickX = normalizeStickAxis(ev.value, controllers[i].center_leftX);
                            break;
                        case ABS_Y:
                            controllers[i].raw_leftStickY = ev.value;
                            controllers[i].leftStickY = -normalizeStickAxis(ev.value, controllers[i].center_leftY);
                            break;
                        case ABS_RX:
                            controllers[i].raw_rightStickX = ev.value;
                            controllers[i].rightStickX = normalizeStickAxis(ev.value, controllers[i].center_rightX);
                            break;
                        case ABS_RY:
                            controllers[i].raw_rightStickY = ev.value;
                            controllers[i].rightStickY = -normalizeStickAxis(ev.value, controllers[i].center_rightY);
                            break;
                        case ABS_Z:     // Left trigger on many controllers
                            controllers[i].raw_leftTrigger = ev.value;
                            // Get axis range
                            struct input_absinfo absinfo;
                            if (ioctl(controllers[i].fd, EVIOCGABS(ABS_Z), &absinfo) >= 0) {
                                controllers[i].leftTrigger = normalizeTrigger(ev.value, absinfo.maximum);
                            } else {
                                controllers[i].leftTrigger = normalizeTrigger(ev.value, 255);
                            }
                            break;
                        case ABS_RZ:    // Right trigger on many controllers
                            controllers[i].raw_rightTrigger = ev.value;
                            if (ioctl(controllers[i].fd, EVIOCGABS(ABS_RZ), &absinfo) >= 0) {
                                controllers[i].rightTrigger = normalizeTrigger(ev.value, absinfo.maximum);
                            } else {
                                controllers[i].rightTrigger = normalizeTrigger(ev.value, 255);
                            }
                            break;
                        case ABS_BRAKE:
                            if (controllers[i].raw_leftTrigger == 0) {
                                controllers[i].leftTrigger = normalizeTrigger(ev.value, 255);
                            }
                            break;
                        case ABS_GAS:
                            if (controllers[i].raw_rightTrigger == 0) {
                                controllers[i].rightTrigger = normalizeTrigger(ev.value, 255);
                            }
                            break;
                        case ABS_HAT0X: // D-pad X
                            if (ev.value < 0) {
                                controllers[i].buttons |= 0x0004; // Left
                                controllers[i].buttons &= ~0x0008; // Right off
                            } else if (ev.value > 0) {
                                controllers[i].buttons |= 0x0008; // Right
                                controllers[i].buttons &= ~0x0004; // Left off
                            } else {
                                controllers[i].buttons &= ~0x0004;
                                controllers[i].buttons &= ~0x0008;
                            }
                            break;
                        case ABS_HAT0Y: // D-pad Y
                            if (ev.value < 0) {
                                controllers[i].buttons |= 0x0002; // Down
                                controllers[i].buttons &= ~0x0001; // Up off
                            } else if (ev.value > 0) {
                                controllers[i].buttons |= 0x0001; // Up
                                controllers[i].buttons &= ~0x0002; // Down off
                            } else {
                                controllers[i].buttons &= ~0x0002;
                                controllers[i].buttons &= ~0x0001;
                            }
                            break;
                    }
                }
            }
        }
    }
    
    bool isConnected(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return false;
        return controllers[slot].connected;
    }
    
    const ControllerState& getControllerState(int slot) const {
        static ControllerState empty;
        if (slot < 0 || slot >= activeControllerCount) return empty;
        return controllers[slot];
    }
    
    int getButtonState(int slot, uint16_t mask) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return (controllers[slot].buttons & mask) ? 1 : 0;
    }
    
    uint16_t getButtons(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return controllers[slot].buttons;
    }
    
    uint8_t getLeftTrigger(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return controllers[slot].leftTrigger;
    }
    
    uint8_t getRightTrigger(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return controllers[slot].rightTrigger;
    }
    
    int16_t getLeftStickX(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return controllers[slot].leftStickX;
    }
    
    int16_t getLeftStickY(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return controllers[slot].leftStickY;
    }
    
    int16_t getRightStickX(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return controllers[slot].rightStickX;
    }
    
    int16_t getRightStickY(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return 0;
        return controllers[slot].rightStickY;
    }
    
    int getConnectedCount() const {
        return activeControllerCount;
    }
    
    std::vector<int> getConnectedSlots() const {
        std::vector<int> slots;
        for (int i = 0; i < activeControllerCount; ++i) {
            if (controllers[i].connected) slots.push_back(i);
        }
        return slots;
    }
    
    std::string getControllerName(int slot) const {
        if (slot < 0 || slot >= activeControllerCount) return "";
        return controllers[slot].name;
    }
};

#endif // EVDEV_CONTROLLER_H
