#ifndef SHARED_MAFIA_STATE_H
#define SHARED_MAFIA_STATE_H

#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_AGENTS 100
#define SHARED_MEM_NAME "/edgemafia_state"

// Do NOT define ROLE_MAFIA etc. here – they are defined in main.cpp enum.

struct AgentSharedState {
    char name[32];
    int  role;        // 0=Mafia, 1=Villager, 2=Doctor, 3=Detective
    int  alive;
    float aggression;
    float loyalty;
    float paranoia;
    float deceit;
    float totalReward;
};

struct MafiaSharedState {
    uint64_t version;
    int cycle;
    int numAgents;
    AgentSharedState agents[MAX_AGENTS];
    int mafiaAlive;
    int townAlive;
    int gameOver;      // 0=running, 1=mafia win, 2=town win
    char winnerMsg[64];
};

inline MafiaSharedState* openSharedMafiaState(bool create) {
    int fd = shm_open(SHARED_MEM_NAME, O_RDWR | (create ? O_CREAT : 0), 0666);
    if (fd == -1) return nullptr;
    if (create && ftruncate(fd, sizeof(MafiaSharedState)) == -1) {
        close(fd);
        return nullptr;
    }
    void* addr = mmap(nullptr, sizeof(MafiaSharedState), PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    close(fd);
    return (MafiaSharedState*)addr;
}

inline void closeSharedMafiaState(MafiaSharedState* state) {
    if (state) munmap(state, sizeof(MafiaSharedState));
}

#endif