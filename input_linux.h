#ifndef INPUT_LINUX_H
#define INPUT_LINUX_H

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <iostream>

// ============================================================================
// Linux Keyboard Input - Non-blocking key detection
// ============================================================================

class KeyboardInput {
private:
    struct termios orig_termios;
    bool initialized;
    int saved_flags;

    void setNonblocking() {
        struct termios newt;
        tcgetattr(STDIN_FILENO, &orig_termios);
        newt = orig_termios;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        
        saved_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, saved_flags | O_NONBLOCK);
    }

public:
    KeyboardInput() : initialized(false), saved_flags(0) {}
    
    ~KeyboardInput() {
        restore();
    }
    
    void init() {
        if (!initialized) {
            setNonblocking();
            initialized = true;
        }
    }
    
    void restore() {
        if (initialized) {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
            fcntl(STDIN_FILENO, F_SETFL, saved_flags);
            initialized = false;
        }
    }
    
    bool keyPressed(char key) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == key) return true;
        }
        return false;
    }
    
    bool keyPressed(int keyCode) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == keyCode) return true;
        }
        return false;
    }
    
    bool anyKeyPressed() {
        char c;
        return read(STDIN_FILENO, &c, 1) == 1;
    }
    
    char getKey() {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            return c;
        }
        return 0;
    }
    
    void flush() {
        char c;
        while (read(STDIN_FILENO, &c, 1) == 1);
    }
};

// Global instance
static KeyboardInput kb;

#endif // INPUT_LINUX_H
