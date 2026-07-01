#define _GNU_SOURCE   // MUST be first – enables shm_unlink, etc.
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <random>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <ctime>
#include <omp.h>
#include <iomanip>
#include <map>
#include <set>
#include <queue>
#include <sstream>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <cstring>

// Linux replacements for Windows
#include "nn_cpu.h"
#include "input_linux.h"
#include "controllers_linux.h"

// Shared memory header (must be in the same directory or in include path)
#include "shared_mafia_state.h"

// ============================================================================
// CONSTANTS - All defined in nn_cpu.h:
// STATE_SIZE=14, HIDDEN_SIZE=64, ACTION_SIZE=8, MEM_SIZE=4,
// MEM_CTX_EXTRA=5, SOCIAL_IN=8, SOCIAL_OUT=2
// DO NOT REDEFINE HERE - they cause redefinition errors
// ============================================================================

namespace fs = std::filesystem;

// ============================================================================
// HUD MODULE - Linux ANSI version
// ============================================================================
#define VERBOSE 1

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"

class NullStream : public std::ostream {
    struct NullBuffer : public std::streambuf {
        int overflow(int c) override { return c; }
    } nullBuffer;
public:
    NullStream() : std::ostream(&nullBuffer) {}
};

static NullStream nullStream;
static bool hudEnabled = false;

#define SIM_LOG (hudEnabled ? nullStream : std::cout)

void setColor(const char* color) {
    if (hudEnabled) std::cout << color;
}

void printColoredBar(float val, int width) {
    if (!hudEnabled) return;
    int half = width / 2;
    int pos = (int)(val * half);
    pos = std::max(-half, std::min(half, pos));
    for (int i = -half; i <= half; ++i) {
        if (i == 0) {
            setColor(COLOR_WHITE);
            std::cout << '|';
        } else if (i < 0) {
            if (i <= pos) {
                setColor(COLOR_RED);
                std::cout << '#';
            } else {
                setColor(COLOR_WHITE);
                std::cout << '.';
            }
        } else {
            if (i <= pos) {
                setColor(COLOR_GREEN);
                std::cout << '#';
            } else {
                setColor(COLOR_WHITE);
                std::cout << '.';
            }
        }
    }
    setColor(COLOR_WHITE);
}

void printValueWithColor(float val) {
    if (!hudEnabled) {
        std::cout << std::showpos << std::fixed << std::setprecision(3) << val << std::noshowpos;
        return;
    }
    if (val > 0) {
        setColor(COLOR_GREEN);
        std::cout << std::showpos << std::fixed << std::setprecision(3) << val;
    } else if (val < 0) {
        setColor(COLOR_RED);
        std::cout << std::showpos << std::fixed << std::setprecision(3) << val;
    } else {
        setColor(COLOR_YELLOW);
        std::cout << std::showpos << std::fixed << std::setprecision(3) << val;
    }
    setColor(COLOR_WHITE);
    std::cout << std::noshowpos;
}

// ============================================================================
// RELATIONSHIP STRUCT
// ============================================================================
struct Relationship {
    float trust = 0.0f;
    float liking = 0.0f;
    int   betrayal = 0;
    int   consistency = 0;
    bool  knownMafia = false;
    bool  knownTown = false;
};

struct Step {
    std::vector<float> state;
    int action;
    float reward;
};

// ============================================================================
// ENUMS AND CONSTANTS
// ============================================================================
enum Role {
    ROLE_MAFIA = 0,
    ROLE_VILLAGER = 1,
    ROLE_DOCTOR = 2,
    ROLE_DETECTIVE = 3
};

// Global neural networks (declared extern in nn_cpu.h, defined here)
SimpleNN_CPU globalBrain(STATE_SIZE, HIDDEN_SIZE, ACTION_SIZE);
SocialNN_CPU socialBrain;

static std::mt19937 rng((unsigned)std::time(nullptr));

float randFloat() {
    std::uniform_real_distribution<float> d(0.0f, 1.0f);
    return d(rng);
}

int randInt(int maxVal) {
    if (maxVal <= 0) return 0;
    std::uniform_int_distribution<int> d(0, maxVal - 1);
    return d(rng);
}

float clampf(float x, float lo, float hi) {
    return std::max(lo, std::min(hi, x));
}

std::string roleToString(Role r) {
    switch (r) {
    case ROLE_MAFIA: return "Mafia";
    case ROLE_VILLAGER: return "Villager";
    case ROLE_DOCTOR: return "Doctor";
    case ROLE_DETECTIVE: return "Detective";
    }
    return "Unknown";
}

// ============================================================================
// AGENT EMOTIONAL STATE OUTPUT
// ============================================================================
void speakAgentEmotionalState(int agentIdx, const std::vector<std::string>& names,
    const std::vector<int>& roles, const std::vector<float>& aggression,
    const std::vector<float>& loyalty, const std::vector<float>& paranoia,
    const std::vector<float>& deceit, const std::vector<int>& alive) {

    if (agentIdx < 0 || agentIdx >= (int)names.size() || !alive[agentIdx]) {
        std::cout << "[EMOTION] Agent " << agentIdx << " is dead." << std::endl;
        return;
    }

    std::string roleStr = roleToString((Role)roles[agentIdx]);

    std::string emotion;

    if (aggression[agentIdx] > 0.7f) emotion = "FURIOUS";
    else if (aggression[agentIdx] > 0.4f) emotion = "ANGRY";
    else if (aggression[agentIdx] > 0.0f) emotion = "IRRITATED";
    else if (aggression[agentIdx] < -0.3f) emotion = "TIMID";

    if (paranoia[agentIdx] > 0.7f) emotion = (emotion.empty() ? "PARANOID" : emotion + " + PARANOID");
    else if (paranoia[agentIdx] > 0.4f) emotion = (emotion.empty() ? "SUSPICIOUS" : emotion + " + SUSPICIOUS");

    if (loyalty[agentIdx] > 0.7f) emotion = (emotion.empty() ? "DEVOTED" : emotion + " + DEVOTED");
    else if (loyalty[agentIdx] < -0.3f) emotion = (emotion.empty() ? "TREACHEROUS" : emotion + " + TREACHEROUS");

    if (deceit[agentIdx] > 0.7f) emotion = (emotion.empty() ? "DECEPTIVE" : emotion + " + DECEPTIVE");
    else if (deceit[agentIdx] > 0.4f) emotion = (emotion.empty() ? "SLY" : emotion + " + SLY");

    if (emotion.empty()) emotion = "CALM";

    std::string dialogue;
    if (roles[agentIdx] == ROLE_MAFIA) {
        if (aggression[agentIdx] > 0.5f) dialogue = "I will destroy anyone who crosses me.";
        else if (deceit[agentIdx] > 0.5f) dialogue = "They have no idea what I'm planning.";
        else dialogue = "I will survive. No matter what.";
    }
    else if (roles[agentIdx] == ROLE_DETECTIVE) {
        if (paranoia[agentIdx] > 0.5f) dialogue = "Someone is lying. I can feel it.";
        else dialogue = "The truth will come out.";
    }
    else if (roles[agentIdx] == ROLE_DOCTOR) {
        if (loyalty[agentIdx] > 0.5f) dialogue = "I will save as many as I can.";
        else dialogue = "I do what I must.";
    }
    else {
        if (loyalty[agentIdx] > 0.5f) dialogue = "I trust my friends.";
        else if (paranoia[agentIdx] > 0.5f) dialogue = "I trust no one.";
        else dialogue = "Just let me live.";
    }

    std::cout << "\n═══════════════════════════════════════════════════════════" << std::endl;
    std::cout << "🎙️  [" << roleStr << "] " << names[agentIdx] << " (Agent " << agentIdx << ")" << std::endl;
    std::cout << "═══════════════════════════════════════════════════════════" << std::endl;
    std::cout << "💭 EMOTION: " << emotion << std::endl;
    std::cout << "📊 TRAITS: A=" << std::fixed << std::setprecision(2) << aggression[agentIdx]
        << " L=" << loyalty[agentIdx] << " P=" << paranoia[agentIdx]
        << " D=" << deceit[agentIdx] << std::endl;
    std::cout << "💬 \"" << dialogue << "\"" << std::endl;
    std::cout << "═══════════════════════════════════════════════════════════\n" << std::endl;
}

// ============================================================================
// GAME MODULE 003 - LLM Layer 2
// ============================================================================
class LLMLayer2 {
private:
    fs::path saveDir;
    int lastSnapshotCycle = -1;

    std::string formatTraitForLLM(float value, const std::string& name) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3) << value;
        std::string level;
        if (value > 0.5f) level = "HIGH";
        else if (value > 0.0f) level = "MODERATE";
        else if (value > -0.5f) level = "LOW";
        else level = "VERY_LOW";
        ss << " (" << level << ")";
        return ss.str();
    }

    std::string getPersonalityDescription(float aggression, float loyalty, float paranoia, float deceit) {
        std::stringstream desc;
        if (aggression > 0.6f) desc << "aggressive ";
        else if (aggression > 0.3f) desc << "assertive ";
        else desc << "passive ";
        if (loyalty > 0.6f) desc << "loyal ";
        else if (loyalty > 0.3f) desc << "cooperative ";
        else desc << "untrustworthy ";
        if (paranoia > 0.6f) desc << "suspicious ";
        else if (paranoia > 0.3f) desc << "cautious ";
        else desc << "trusting ";
        if (deceit > 0.6f) desc << "deceptive";
        else if (deceit > 0.3f) desc << "secretive";
        else desc << "honest";
        return desc.str();
    }

public:
    LLMLayer2(const fs::path& dir) : saveDir(dir) {}

    void writeAgentSnapshot(
        int cycle,
        const std::vector<std::string>& names,
        const std::vector<int>& roles,
        const std::vector<float>& aggression,
        const std::vector<float>& loyalty,
        const std::vector<float>& paranoia,
        const std::vector<float>& deceit,
        const std::vector<float>& totalReward,
        const std::vector<int>& alive,
        const std::vector<std::unordered_map<int, Relationship>>& relations,
        int n,
        const std::vector<std::string>& agentHouses = {}
    ) {
        if (cycle == lastSnapshotCycle) return;
        lastSnapshotCycle = cycle;

        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);

        std::stringstream filename;
        filename << "llm_snapshot_cycle_" << cycle << ".txt";
        fs::path outPath = saveDir / filename.str();

        std::ofstream file(outPath.string());
        if (!file.is_open()) {
            std::cout << "[LLM_LAYER2] WARNING: Could not open " << outPath << std::endl;
            return;
        }

        file << "╔══════════════════════════════════════════════════════════════════════════════════════╗\n";
        file << "║                                                                                      ║\n";
        file << "║     L L M   L A Y E R   2   -   A I   A G E N T   S P E E C H   D O C U M E N T A R Y   ║\n";
        file << "║                                                                                      ║\n";
        file << "║     INVENTED BY: ASTON WALKER                                                        ║\n";
        file << "║     MECHANIC: Pause-to-Speech Documentary - Global First                             ║\n";
        file << "║                                                                                      ║\n";
        file << "╚══════════════════════════════════════════════════════════════════════════════════════╝\n";
        file << "\n";
        file << "SNAPSHOT_TIMESTAMP: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
        file << "GAME_CYCLE: " << cycle << "\n";
        file << "ALIVE_AGENTS_COUNT: " << std::count(alive.begin(), alive.end(), 1) << "\n";
        file << "TOTAL_AGENTS: " << n << "\n\n";
        file << "═══════════════════════════════════════════════════════════════════════════════════════\n";
        file << "                              L I V I N G   A I   A G E N T S                           \n";
        file << "═══════════════════════════════════════════════════════════════════════════════════════\n\n";

        std::vector<int> livingAgents;
        for (int i = 0; i < n; ++i) {
            if (alive[i]) livingAgents.push_back(i);
        }

        for (int idx : livingAgents) {
            std::string roleStr;
            std::string roleColor;
            switch (roles[idx]) {
            case ROLE_MAFIA: roleStr = "MAFIA"; roleColor = "DARK_RED"; break;
            case ROLE_VILLAGER: roleStr = "VILLAGER"; roleColor = "GREEN"; break;
            case ROLE_DOCTOR: roleStr = "DOCTOR"; roleColor = "BLUE"; break;
            case ROLE_DETECTIVE: roleStr = "DETECTIVE"; roleColor = "GOLD"; break;
            default: roleStr = "UNKNOWN"; roleColor = "GRAY";
            }

            file << "┌─────────────────────────────────────────────────────────────────────────────────┐\n";
            file << "│ AGENT ID: " << std::setw(4) << idx << "  |  NAME: " << std::left << std::setw(20) << names[idx] << "  |  ROLE: " << roleStr << " (" << roleColor << ") │\n";
            file << "├─────────────────────────────────────────────────────────────────────────────────┤\n";
            file << "│ TRAITS:                                                                          │\n";
            file << "│   • AGGRESSION: " << std::setw(10) << formatTraitForLLM(aggression[idx], "Aggression") << "  [Range: -1.0 to 1.0]                              │\n";
            file << "│   • LOYALTY:    " << std::setw(10) << formatTraitForLLM(loyalty[idx], "Loyalty") << "  [Range: -1.0 to 1.0]                              │\n";
            file << "│   • PARANOIA:   " << std::setw(10) << formatTraitForLLM(paranoia[idx], "Paranoia") << "  [Range: -1.0 to 1.0]                              │\n";
            file << "│   • DECEIT:     " << std::setw(10) << formatTraitForLLM(deceit[idx], "Deceit") << "  [Range: -1.0 to 1.0]                              │\n";
            file << "│                                                                                   │\n";

            std::string personality = getPersonalityDescription(aggression[idx], loyalty[idx], paranoia[idx], deceit[idx]);
            file << "│ PERSONALITY PROFILE: " << std::left << std::setw(58) << personality << "│\n";
            file << "│                                                                                   │\n";
            file << "│ PERFORMANCE:                                                                      │\n";
            file << "│   • TOTAL REWARD: " << std::fixed << std::setprecision(4) << std::setw(10) << totalReward[idx] << "  [Cumulative score]                             │\n";
            file << "│                                                                                   │\n";
            file << "│ TRUST RELATIONSHIPS (Top 5 by trust score):                                       │\n";

            std::vector<std::pair<float, int>> trustScores;
            for (const auto& kv : relations[idx]) {
                if (kv.first != idx && alive[kv.first]) {
                    trustScores.push_back({ kv.second.trust, kv.first });
                }
            }
            std::sort(trustScores.begin(), trustScores.end(),
                [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                    return a.first > b.first;
                });

            int relCount = 0;
            for (const auto& ts : trustScores) {
                if (relCount >= 5) break;
                std::string trustLevel;
                if (ts.first > 0.5f) trustLevel = "TRUSTED";
                else if (ts.first > 0.0f) trustLevel = "NEUTRAL_POSITIVE";
                else if (ts.first > -0.5f) trustLevel = "NEUTRAL_NEGATIVE";
                else trustLevel = "DISTRUSTED";

                file << "│     -> " << std::left << std::setw(15) << names[ts.second]
                    << " | Trust: " << std::setw(8) << std::fixed << std::setprecision(3) << ts.first
                    << " | " << trustLevel << std::setw(30) << " " << "│\n";
                relCount++;
            }
            if (relCount == 0) {
                file << "│     (No established relationships)                                                │\n";
            }
            file << "│                                                                                   │\n";

            if (!agentHouses.empty() && idx < (int)agentHouses.size() && !agentHouses[idx].empty() && agentHouses[idx] != "Unaffiliated") {
                file << "│ AFFILIATION: House: " << std::left << std::setw(20) << agentHouses[idx] << "                                      │\n";
                file << "│                                                                                   │\n";
            }

            file << "├─────────────────────────────────────────────────────────────────────────────────┤\n";
            file << "│ 🎙️  ELEVENLABS TTS PROMPT - VOICE FOR AGENT " << names[idx] << "                                          │\n";
            file << "├─────────────────────────────────────────────────────────────────────────────────┤\n";
            file << "│ VOICE_PROFILE:                                                                   │\n";

            std::string suggestedVoice;
            if (roles[idx] == ROLE_MAFIA) suggestedVoice = "deep, menacing, slow tempo, low pitch";
            else if (roles[idx] == ROLE_DETECTIVE) suggestedVoice = "authoritative, clear, medium tempo, confident";
            else if (roles[idx] == ROLE_DOCTOR) suggestedVoice = "calm, soothing, warm, caring";
            else suggestedVoice = "neutral, natural, conversational";

            if (aggression[idx] > 0.6f) suggestedVoice += ", harsh, aggressive tone";
            if (paranoia[idx] > 0.6f) suggestedVoice += ", nervous, shaky, fast tempo";
            if (deceit[idx] > 0.6f) suggestedVoice += ", sly, cunning, whispery";
            if (loyalty[idx] > 0.6f) suggestedVoice += ", sincere, trustworthy";

            file << "│   SUGGESTED_VOICE: " << std::left << std::setw(62) << suggestedVoice << "│\n";
            file << "│                                                                                   │\n";
            file << "│ PROMPT_TEXT:                                                                      │\n";
            file << "│   \"I am " << names[idx] << ". My role is " << roleStr << ". ";

            std::string voiceLine;
            if (roles[idx] == ROLE_MAFIA) {
                voiceLine = "Trust no one. The town will burn before I reveal my hand. ";
                if (deceit[idx] > 0.5f) voiceLine += "I've already fooled them all. ";
                if (aggression[idx] > 0.5f) voiceLine += "Anyone who crosses me dies. ";
            }
            else if (roles[idx] == ROLE_DETECTIVE) {
                voiceLine = "I will find the truth. The mafia cannot hide forever. ";
                if (paranoia[idx] > 0.5f) voiceLine += "But I'm watching everyone. Even my allies. ";
            }
            else if (roles[idx] == ROLE_DOCTOR) {
                voiceLine = "I protect the innocent. But I can't save everyone. ";
                if (loyalty[idx] > 0.5f) voiceLine += "My loyalty is to the town, always. ";
            }
            else {
                voiceLine = "I'm just trying to survive. ";
                if (loyalty[idx] > 0.5f) voiceLine += "I stand with my friends. ";
                if (paranoia[idx] > 0.5f) voiceLine += "But I don't know who to trust anymore. ";
            }

            file << voiceLine;
            file << " My aggression level is " << std::fixed << std::setprecision(2) << aggression[idx];
            file << ", loyalty " << loyalty[idx];
            file << ", paranoia " << paranoia[idx];
            file << ", deceit " << deceit[idx];
            file << ". I have " << relCount << " close relationships.\"";
            file << std::setw(68 - (int)voiceLine.length() - 50) << " " << "│\n";

            file << "│                                                                                   │\n";
            file << "│ API_CALL_EXAMPLE:                                                                 │\n";
            file << "│   curl -X POST \"https://api.elevenlabs.io/v1/text-to-speech/VOICE_ID\" \\          │\n";
            file << "│        -H \"xi-api-key: YOUR_API_KEY\" \\                                            │\n";
            file << "│        -H \"Content-Type: application/json\" \\                                     │\n";
            file << "│        -d '{\"text\": \"PROMPT_TEXT_ABOVE\", \"voice_settings\": {\"stability\": 0.5,   │\n";
            file << "│                \"similarity_boost\": 0.75}}'                                       │\n";
            file << "└─────────────────────────────────────────────────────────────────────────────────┘\n\n";
        }

        int mafiaAlive = 0, townAlive = 0;
        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            if (roles[i] == ROLE_MAFIA) mafiaAlive++;
            else townAlive++;
        }

        file << "\n═══════════════════════════════════════════════════════════════════════════════════════\n";
        file << "                              G A M E   S T A T E   S U M M A R Y                       \n";
        file << "═══════════════════════════════════════════════════════════════════════════════════════\n\n";
        file << "MAFIA ALIVE: " << mafiaAlive << "\n";
        file << "TOWN ALIVE: " << townAlive << "\n";
        file << "GAME STATUS: ";
        if (mafiaAlive >= townAlive && mafiaAlive > 0) file << "MAFIA WINNING";
        else if (townAlive > mafiaAlive * 2) file << "TOWN DOMINANT";
        else file << "BALANCED";
        file << "\n\n";
        file << "═══════════════════════════════════════════════════════════════════════════════════════\n";
        file << "  This snapshot was generated by LLM Layer 2 - Aston Walker's pause-to-speech mechanic  \n";
        file << "  Copy any PROMPT_TEXT to ElevenLabs API to hear AI agents speak their thoughts         \n";
        file << "═══════════════════════════════════════════════════════════════════════════════════════\n";

        file.close();

        std::cout << "[LLM_LAYER2] Aston Walker - Snapshot saved: " << filename.str()
            << " (" << livingAgents.size() << " living agents ready for ElevenLabs TTS)\n";
    }

    void writeAgentSnapshotCSV(
        int cycle,
        const std::vector<std::string>& names,
        const std::vector<int>& roles,
        const std::vector<float>& aggression,
        const std::vector<float>& loyalty,
        const std::vector<float>& paranoia,
        const std::vector<float>& deceit,
        const std::vector<float>& totalReward,
        const std::vector<int>& alive,
        const std::vector<std::unordered_map<int, Relationship>>& relations,
        int n,
        const std::vector<std::string>& agentHouses = {}
    ) {
        std::stringstream filename;
        filename << "agent_snapshot_cycle_" << cycle << ".csv";
        fs::path outPath = saveDir / filename.str();

        std::ofstream file(outPath.string());
        if (!file.is_open()) {
            std::cout << "[LLM_LAYER2] WARNING: Could not open " << outPath << std::endl;
            return;
        }

        file << "AgentID,Name,Role,Alive,Aggression,Loyalty,Paranoia,Deceit,TotalReward,HouseAffiliation,TopTrustRelationships,PersonalitySummary,TTSSuggestedVoice\n";

        for (int idx = 0; idx < n; ++idx) {
            std::vector<std::pair<float, int>> trustScores;
            for (const auto& kv : relations[idx]) {
                if (kv.first != idx && alive[kv.first]) {
                    trustScores.push_back({ kv.second.trust, kv.first });
                }
            }
            std::sort(trustScores.begin(), trustScores.end(),
                [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                    return a.first > b.first;
                });

            std::string trustStr;
            for (int t = 0; t < std::min(3, (int)trustScores.size()); ++t) {
                if (t > 0) trustStr += "; ";
                trustStr += names[trustScores[t].second] + ":" + std::to_string(trustScores[t].first);
            }

            std::string roleStr;
            switch (roles[idx]) {
            case ROLE_MAFIA: roleStr = "MAFIA"; break;
            case ROLE_VILLAGER: roleStr = "VILLAGER"; break;
            case ROLE_DOCTOR: roleStr = "DOCTOR"; break;
            case ROLE_DETECTIVE: roleStr = "DETECTIVE"; break;
            default: roleStr = "UNKNOWN";
            }

            std::string house = (idx < (int)agentHouses.size() && !agentHouses[idx].empty()) ? agentHouses[idx] : "Unaffiliated";

            std::stringstream personalitySS;
            if (aggression[idx] > 0.6f) personalitySS << "aggressive ";
            else if (aggression[idx] > 0.3f) personalitySS << "assertive ";
            else personalitySS << "passive ";
            if (loyalty[idx] > 0.6f) personalitySS << "loyal ";
            else if (loyalty[idx] > 0.3f) personalitySS << "cooperative ";
            else personalitySS << "untrustworthy ";
            if (paranoia[idx] > 0.6f) personalitySS << "suspicious ";
            else if (paranoia[idx] > 0.3f) personalitySS << "cautious ";
            else personalitySS << "trusting ";
            if (deceit[idx] > 0.6f) personalitySS << "deceptive";
            else if (deceit[idx] > 0.3f) personalitySS << "secretive";
            else personalitySS << "honest";

            std::stringstream voiceSS;
            if (roles[idx] == ROLE_MAFIA) voiceSS << "deep, menacing, slow tempo, low pitch";
            else if (roles[idx] == ROLE_DETECTIVE) voiceSS << "authoritative, clear, medium tempo, confident";
            else if (roles[idx] == ROLE_DOCTOR) voiceSS << "calm, soothing, warm, caring";
            else voiceSS << "neutral, natural, conversational";

            if (aggression[idx] > 0.6f) voiceSS << ", harsh, aggressive tone";
            if (paranoia[idx] > 0.6f) voiceSS << ", nervous, shaky, fast tempo";
            if (deceit[idx] > 0.6f) voiceSS << ", sly, cunning, whispery";
            if (loyalty[idx] > 0.6f) voiceSS << ", sincere, trustworthy";

            file << idx << ","
                << names[idx] << ","
                << roleStr << ","
                << (alive[idx] ? "YES" : "NO") << ","
                << std::fixed << std::setprecision(4) << aggression[idx] << ","
                << loyalty[idx] << ","
                << paranoia[idx] << ","
                << deceit[idx] << ","
                << totalReward[idx] << ","
                << house << ","
                << "\"" << trustStr << "\","
                << "\"" << personalitySS.str() << "\","
                << "\"" << voiceSS.str() << "\"\n";
        }

        file.close();
        std::cout << "[LLM_LAYER2] CSV Snapshot saved: " << filename.str() << " (" << n << " agents)\n";
    }
};

// ============================================================================
// GAME MODULE 002 - City and Houses System
// ============================================================================
struct TrustGroup {
    std::string houseName;
    std::string cityBuilding;
    std::vector<int> memberIds;
    int formationCycle;
    int lastUpdateCycle;
    bool isActive;
    int buildingCapacity;
    int currentOccupancy;

    TrustGroup() : houseName(""), cityBuilding(""), formationCycle(0),
        lastUpdateCycle(0), isActive(false), buildingCapacity(1500), currentOccupancy(0) {
    }
};

const std::vector<std::string> HOUSE_NAMES_PRIMARY = {
    "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel",
    "India", "Juliett", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa",
    "Quebec", "Romeo", "Sierra", "Tango", "Uniform", "Victor", "Whiskey",
    "Xray", "Yankee", "Zulu"
};

const std::vector<std::string> CITY_BUILDINGS = {
    "Alpha_CITY1500", "Bravo_CITY1500", "Charlie_CITY1500", "Delta_CITY1500",
    "Echo_CITY1500", "Foxtrot_CITY1500", "Golf_CITY1500", "Hotel_CITY1500",
    "India_CITY1500", "Juliett_CITY1500", "Kilo_CITY1500", "Lima_CITY1500",
    "Mike_CITY1500", "November_CITY1500", "Oscar_CITY1500", "Papa_CITY1500",
    "Quebec_CITY1500", "Romeo_CITY1500", "Sierra_CITY1500", "Tango_CITY1500",
    "Uniform_CITY1500", "Victor_CITY1500", "Whiskey_CITY1500", "Xray_CITY1500",
    "Yankee_CITY1500", "Zulu_CITY1500"
};

class CityHousingSystem {
private:
    std::vector<TrustGroup> activeGroups;
    std::unordered_map<int, std::string> agentToHouse;
    std::unordered_map<int, std::string> agentToBuilding;
    std::unordered_map<std::string, int> houseUsageCount;
    std::vector<std::string> usedHouseNames;

public:
    CityHousingSystem() {}

    std::vector<std::string> getAllAgentHouses(int n) const {
        std::vector<std::string> result(n, "");
        for (int i = 0; i < n; ++i) {
            result[i] = getAgentHouse(i);
        }
        return result;
    }

    std::vector<std::vector<int>> detectTrustGroups(
        int n,
        const std::vector<std::unordered_map<int, Relationship>>& relations,
        const std::vector<int>& alive,
        float trustThreshold = 0.6f,
        int minGroupSize = 5,
        int maxGroupSize = 20
    ) {
        std::vector<std::vector<int>> groups;
        std::vector<bool> visited(n, false);
        std::vector<int> startIndices;
        for (int i = 0; i < n; ++i) {
            if (alive[i]) startIndices.push_back(i);
        }
        std::shuffle(startIndices.begin(), startIndices.end(), rng);

        for (int start : startIndices) {
            if (!alive[start] || visited[start]) continue;
            std::queue<int> q;
            std::vector<int> component;
            q.push(start);
            visited[start] = true;

            while (!q.empty()) {
                int curr = q.front();
                q.pop();
                component.push_back(curr);
                std::vector<int> neighbors;
                for (int neighbor = 0; neighbor < n; ++neighbor) {
                    if (!alive[neighbor] || visited[neighbor]) continue;
                    if (curr == neighbor) continue;
                    auto it1 = relations[curr].find(neighbor);
                    auto it2 = relations[neighbor].find(curr);
                    float trust1 = (it1 != relations[curr].end()) ? it1->second.trust : 0.0f;
                    float trust2 = (it2 != relations[neighbor].end()) ? it2->second.trust : 0.0f;
                    if (trust1 > trustThreshold && trust2 > trustThreshold) {
                        neighbors.push_back(neighbor);
                    }
                }
                std::shuffle(neighbors.begin(), neighbors.end(), rng);
                for (int neighbor : neighbors) {
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        q.push(neighbor);
                    }
                }
            }

            if ((int)component.size() >= minGroupSize) {
                std::vector<int> clique;
                for (size_t i = 0; i < component.size() && (int)clique.size() < maxGroupSize; ++i) {
                    bool allTrust = true;
                    for (int existing : clique) {
                        auto it1 = relations[component[i]].find(existing);
                        auto it2 = relations[existing].find(component[i]);
                        float t1 = (it1 != relations[component[i]].end()) ? it1->second.trust : 0.0f;
                        float t2 = (it2 != relations[existing].end()) ? it2->second.trust : 0.0f;
                        if (t1 <= trustThreshold || t2 <= trustThreshold) {
                            allTrust = false;
                            break;
                        }
                    }
                    if (allTrust || clique.empty()) {
                        clique.push_back(component[i]);
                    }
                }
                if ((int)clique.size() >= minGroupSize) {
                    groups.push_back(clique);
                }
            }
        }
        return groups;
    }

    std::string allocateHouseName() {
        for (const auto& name : HOUSE_NAMES_PRIMARY) {
            if (std::find(usedHouseNames.begin(), usedHouseNames.end(), name) == usedHouseNames.end()) {
                usedHouseNames.push_back(name);
                houseUsageCount[name] = 1;
                return name;
            }
        }
        for (const auto& name : HOUSE_NAMES_PRIMARY) {
            int count = houseUsageCount[name];
            std::string suffix = std::to_string(count);
            while (suffix.length() < 3) suffix = "0" + suffix;
            std::string suffixedName = name + "_" + suffix;
            houseUsageCount[name]++;
            return suffixedName;
        }
        static int foxtrotCounter = 1;
        return "Foxtrot_Tango_Zulu_" + std::to_string(foxtrotCounter++);
    }

    std::string allocateCityBuilding(int groupSize) {
        if (groupSize < 20) return "";
        static int buildingIndex = 0;
        std::string building = CITY_BUILDINGS[buildingIndex % CITY_BUILDINGS.size()];
        buildingIndex++;
        return building;
    }

    void updateGroups(
        int cycle,
        int n,
        const std::vector<std::unordered_map<int, Relationship>>& relations,
        const std::vector<int>& alive,
        const std::vector<std::string>& names,
        const std::vector<int>& roles
    ) {
        cleanupDeadAgents(alive);
        std::vector<std::vector<int>> newGroups = detectTrustGroups(n, relations, alive, 0.65f, 5, 20);
        std::unordered_map<int, bool> agentAssigned;

        for (auto& groupMembers : newGroups) {
            if (groupMembers.size() < 5) continue;
            TrustGroup newGroup;
            newGroup.memberIds = groupMembers;
            newGroup.formationCycle = cycle;
            newGroup.lastUpdateCycle = cycle;
            newGroup.isActive = true;
            newGroup.currentOccupancy = groupMembers.size();
            newGroup.houseName = allocateHouseName();
            if (groupMembers.size() >= 20) {
                newGroup.cityBuilding = allocateCityBuilding(groupMembers.size());
            }
            for (int agentId : groupMembers) {
                agentToHouse[agentId] = newGroup.houseName;
                if (!newGroup.cityBuilding.empty()) {
                    agentToBuilding[agentId] = newGroup.cityBuilding;
                }
                agentAssigned[agentId] = true;
            }
            activeGroups.push_back(newGroup);
            std::cout << "[CITY_MODULE] Cycle " << cycle << ": House '" << newGroup.houseName
                << "' formed with " << groupMembers.size() << " members";
            if (!newGroup.cityBuilding.empty()) {
                std::cout << " | Allocated building: " << newGroup.cityBuilding;
            }
            std::cout << std::endl;
        }

        activeGroups.erase(
            std::remove_if(activeGroups.begin(), activeGroups.end(),
                [&](const TrustGroup& g) {
                    int aliveCount = 0;
                    for (int id : g.memberIds) {
                        if (id < n && alive[id]) aliveCount++;
                    }
                    if (aliveCount < 5) {
                        std::cout << "[CITY_MODULE] House '" << g.houseName
                            << "' disbanded (fell below 5 members)" << std::endl;
                        for (int id : g.memberIds) {
                            agentToHouse.erase(id);
                            agentToBuilding.erase(id);
                        }
                        return true;
                    }
                    return false;
                }),
            activeGroups.end()
        );
    }

    void cleanupDeadAgents(const std::vector<int>& alive) {
        for (auto& group : activeGroups) {
            group.memberIds.erase(
                std::remove_if(group.memberIds.begin(), group.memberIds.end(),
                    [&](int id) { return id >= (int)alive.size() || !alive[id]; }),
                group.memberIds.end()
            );
            group.currentOccupancy = group.memberIds.size();
            group.isActive = (group.memberIds.size() >= 5);
            for (int id : group.memberIds) {
                if (id >= (int)alive.size() || !alive[id]) {
                    agentToHouse.erase(id);
                    agentToBuilding.erase(id);
                }
            }
        }
    }

    void saveGroupHistoryCSV(int cycle, int n, const std::vector<int>& alive,
        const std::vector<std::string>& names,
        const std::vector<int>& roles,
        const std::vector<std::unordered_map<int, Relationship>>& relations,
        const fs::path& saveDir) {
        if (cycle % 50 != 0 && cycle != 0) return;

        std::string filename = "group_allegiance_cycle_" + std::to_string(cycle) + ".csv";
        fs::path outPath = saveDir / filename;
        std::ofstream file(outPath.string());
        if (!file.is_open()) {
            std::cout << "[CITY_MODULE] WARNING: could not open " << outPath << std::endl;
            return;
        }

        file << "Cycle,AgentID,AgentName,Role,HouseAffiliation,CityBuilding,AverageTrustToGroup,IsAlive\n";
        for (int i = 0; i < n; ++i) {
            std::string house = (agentToHouse.find(i) != agentToHouse.end()) ? agentToHouse[i] : "None";
            std::string building = (agentToBuilding.find(i) != agentToBuilding.end()) ? agentToBuilding[i] : "";
            float avgGroupTrust = 0.0f;
            int trustCount = 0;
            for (const auto& group : activeGroups) {
                if (std::find(group.memberIds.begin(), group.memberIds.end(), i) != group.memberIds.end()) {
                    for (int member : group.memberIds) {
                        if (member != i) {
                            auto it = relations[i].find(member);
                            if (it != relations[i].end()) {
                                avgGroupTrust += it->second.trust;
                                trustCount++;
                            }
                        }
                    }
                    break;
                }
            }
            if (trustCount > 0) avgGroupTrust /= trustCount;
            std::string roleStr;
            switch (roles[i]) {
            case 0: roleStr = "Mafia"; break;
            case 1: roleStr = "Villager"; break;
            case 2: roleStr = "Doctor"; break;
            case 3: roleStr = "Detective"; break;
            default: roleStr = "Unknown";
            }
            file << cycle << "," << i << "," << names[i] << "," << roleStr << ","
                << house << "," << building << "," << std::fixed << std::setprecision(3) << avgGroupTrust << ","
                << (alive[i] ? "Alive" : "Dead") << "\n";
        }
        file.close();
        std::cout << "[CITY_MODULE] Saved group allegiance snapshot to " << filename
            << " (Cycle " << cycle << ")" << std::endl;

        std::string summaryFile = "group_summary_cycle_" + std::to_string(cycle) + ".txt";
        fs::path summaryPath = saveDir / summaryFile;
        std::ofstream summary(summaryPath.string());

        if (summary.is_open()) {
            summary << "=== CITY AND HOUSES GROUP SUMMARY - CYCLE " << cycle << " ===\n\n";
            summary << "Active Groups: " << activeGroups.size() << "\n\n";

            for (size_t g = 0; g < activeGroups.size(); ++g) {
                const auto& group = activeGroups[g];
                summary << "Group " << (g + 1) << ": " << group.houseName << "\n";
                summary << "  Members: " << group.memberIds.size() << "/" << group.buildingCapacity << "\n";
                summary << "  City Building: " << (group.cityBuilding.empty() ? "None (needs 20+ members)" : group.cityBuilding) << "\n";
                summary << "  Formed: Cycle " << group.formationCycle << "\n";
                summary << "  Active: " << (group.isActive ? "Yes" : "No") << "\n";
                summary << "  Member IDs: ";
                for (int id : group.memberIds) summary << id << " ";
                summary << "\n\n";
            }
            summary.close();
            std::cout << "[CITY_MODULE] Saved group summary to " << summaryFile << std::endl;
        }
    }

    std::string getAgentHouse(int agentId) const {
        auto it = agentToHouse.find(agentId);
        return (it != agentToHouse.end()) ? it->second : "Unaffiliated";
    }

    std::string getAgentBuilding(int agentId) const {
        auto it = agentToBuilding.find(agentId);
        return (it != agentToBuilding.end()) ? it->second : "";
    }

    void printGroupStats(int cycle) const {
        std::cout << "\n=== CITY MODULE STATS (Cycle " << cycle << ") ===\n";
        std::cout << "Active Houses: " << activeGroups.size() << "\n";
        int totalHoused = 0;
        for (const auto& group : activeGroups) {
            totalHoused += group.memberIds.size();
            std::cout << "  - " << group.houseName << ": " << group.memberIds.size() << " members";
            if (!group.cityBuilding.empty()) {
                std::cout << " [BUILDING: " << group.cityBuilding << "]";
            }
            std::cout << "\n";
        }
        std::cout << "Total Agents in Houses: " << totalHoused << "\n";
        std::cout << "=====================================\n\n";
    }
};

// ============================================================================
// Helper Functions
// ============================================================================
int weightedChoice(const std::vector<int>& c, const std::vector<float>& w) {
    if (c.empty()) return -1;
    if (c.size() != w.size()) return c[randInt((int)c.size())];
    float sum = 0.0f;
    for (float v : w) if (v > 0.0f) sum += v;
    if (sum <= 0.0f || !std::isfinite(sum)) return c[randInt((int)c.size())];
    float r = randFloat() * sum;
    for (size_t i = 0; i < c.size(); ++i) {
        float v = (w[i] > 0.0f ? w[i] : 0.0f);
        r -= v;
        if (r <= 0.0f) return c[i];
    }
    return c.back();
}

void saveTrustMatrixSnapshot(int cycle, int n,
    const std::vector<std::unordered_map<int, Relationship>>& relations,
    const fs::path& saveDir) {
    if (n > 10000) return;
    if (cycle != 0 && cycle % 100 != 0) return;

    std::string filename = "trust_matrix_cycle_" + std::to_string(cycle) + ".csv";
    fs::path outPath = saveDir / filename;
    std::ofstream file(outPath.string());
    if (!file.is_open()) {
        SIM_LOG << "[TRUST_SNAPSHOT] WARNING: could not open " << outPath << "\n";
        return;
    }

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            auto it = relations[i].find(j);
            float t = (it != relations[i].end()) ? it->second.trust : 0.0f;
            file << t;
            if (j < n - 1) file << ",";
        }
        file << "\n";
    }
    file.close();
    SIM_LOG << "[TRUST_SNAPSHOT] Saved " << outPath.filename() << "\n";
}

// ============================================================================
// HUD DRAW FUNCTION - Linux ANSI version
// ============================================================================
void drawHUD(const std::vector<int>& watchedAgents,
    const std::vector<std::string>& names,
    const std::vector<int>& roles,
    const std::vector<float>& aggression,
    const std::vector<float>& loyalty,
    const std::vector<float>& paranoia,
    const std::vector<float>& deceit,
    const std::vector<float>& totalReward,
    const std::vector<std::unordered_map<int, Relationship>>& relations,
    const std::vector<int>& alive,
    int cycle,
    int n) {

    if (!hudEnabled) return;

    // Clear screen using ANSI
    std::cout << "\033[2J\033[1;1H";
    
    setColor(COLOR_CYAN);
    std::cout << "╔════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║";
    setColor(COLOR_YELLOW);
    std::cout << "                                      E D G E M A F I A   -   L I V E   D A S H B O A R D                                      ";
    setColor(COLOR_CYAN);
    std::cout << "║\n";
    std::cout << "╠════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╣\n";

    int mafiaAlive = 0, townAlive = 0;
    for (int i = 0; i < n; ++i) {
        if (!alive[i]) continue;
        if (roles[i] == 0) mafiaAlive++;
        else townAlive++;
    }

    setColor(COLOR_CYAN);
    std::cout << "║";
    setColor(COLOR_WHITE);
    std::cout << "  📊 STATISTICS                                                                                                                          ";
    setColor(COLOR_CYAN);
    std::cout << "║\n";
    std::cout << "║  ┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐ ║\n";
    std::cout << "║  │";
    setColor(COLOR_RED);
    std::cout << "  MAFIA: " << std::setw(3) << mafiaAlive << " alive    ";
    setColor(COLOR_GREEN);
    std::cout << "│  TOWN: " << std::setw(3) << townAlive << " alive    ";
    setColor(COLOR_WHITE);
    std::cout << "│  TOTAL: " << std::setw(4) << n << "    │  ALIVE: " << std::setw(3) << (mafiaAlive + townAlive) << "    │  DEAD: " << std::setw(3) << (n - mafiaAlive - townAlive) << "    │  ";
    setColor(COLOR_CYAN);
    std::cout << "║\n";
    std::cout << "║  └──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘ ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╣\n";

    setColor(COLOR_CYAN);
    std::cout << "║";
    setColor(COLOR_YELLOW);
    std::cout << "  🏆 TOP 20 AGENTS BY REWARD                                                                                                            ";
    setColor(COLOR_CYAN);
    std::cout << "║\n";
    std::cout << "║  ┌─────┬────────────────────┬──────────────┬───────────────┬───────────────┬───────────────┬───────────────┬────────────────────┐ ║\n";
    std::cout << "║  │";
    setColor(COLOR_WHITE);
    std::cout << " RANK";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " NAME                ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " ROLE         ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " AGGRESSION    ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " LOYALTY       ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " PARANOIA      ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " DECEIT        ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " REWARD              ";
    setColor(COLOR_CYAN);
    std::cout << "│ ║\n";
    std::cout << "║  ├─────┼────────────────────┼──────────────┼───────────────┼───────────────┼───────────────┼───────────────┼────────────────────┤ ║\n";

    std::vector<std::pair<float, int>> rewardSorted;
    for (int i = 0; i < n; ++i) {
        rewardSorted.push_back({ totalReward[i], i });
    }
    std::sort(rewardSorted.begin(), rewardSorted.end(),
        [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
            return a.first > b.first;
        });

    int topCount = std::min(20, n);
    for (int rank = 0; rank < topCount; ++rank) {
        int idx = rewardSorted[rank].second;
        std::string roleStr;
        switch (roles[idx]) {
        case 0: roleStr = "Mafia"; break;
        case 1: roleStr = "Villager"; break;
        case 2: roleStr = "Doctor"; break;
        case 3: roleStr = "Detective"; break;
        default: roleStr = "Unknown"; break;
        }

        std::cout << "║  │ " << std::setw(3) << (rank + 1) << " │ "
            << std::setw(18) << names[idx].substr(0, 18) << " │ "
            << std::setw(12) << roleStr << " │ ";

        printValueWithColor(aggression[idx]);
        std::cout << " │ ";
        printValueWithColor(loyalty[idx]);
        std::cout << " │ ";
        printValueWithColor(paranoia[idx]);
        std::cout << " │ ";
        printValueWithColor(deceit[idx]);
        std::cout << " │ ";
        printValueWithColor(totalReward[idx]);
        std::cout << " │ ║\n";
    }

    for (int rank = topCount; rank < 20; ++rank) {
        std::cout << "║  │ " << std::setw(3) << (rank + 1) << " │ "
            << std::setw(18) << "---" << " │ "
            << std::setw(12) << "---" << " │ "
            << std::setw(13) << "---" << " │ "
            << std::setw(13) << "---" << " │ "
            << std::setw(13) << "---" << " │ "
            << std::setw(13) << "---" << " │ "
            << std::setw(18) << "---" << " │ ║\n";
    }

    setColor(COLOR_CYAN);
    std::cout << "║  └─────┴────────────────────┴──────────────┴───────────────┴───────────────┴───────────────┴───────────────┴────────────────────┘ ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╣\n";

    setColor(COLOR_CYAN);
    std::cout << "║";
    setColor(COLOR_YELLOW);
    std::cout << "  👁️  WATCHED AGENTS (Live Trait Visualization)                                                                                         ";
    setColor(COLOR_CYAN);
    std::cout << "║\n";
    std::cout << "║  ┌─────┬────────────────────┬──────────────┬─────────────────────────────────────────────────────────────────────────────────────┐ ║\n";
    std::cout << "║  │";
    setColor(COLOR_WHITE);
    std::cout << " ID  ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " NAME                ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " ROLE         ";
    setColor(COLOR_CYAN);
    std::cout << "│";
    setColor(COLOR_WHITE);
    std::cout << " TRAIT BARS                                                                           ";
    setColor(COLOR_CYAN);
    std::cout << "│ ║\n";
    std::cout << "║  ├─────┼────────────────────┼──────────────┼─────────────────────────────────────────────────────────────────────────────────────┤ ║\n";

    for (size_t wi = 0; wi < watchedAgents.size(); ++wi) {
        int idx = watchedAgents[wi];
        if (idx < 0 || idx >= (int)names.size()) continue;

        std::string roleStr;
        switch (roles[idx]) {
        case 0: roleStr = "Mafia"; break;
        case 1: roleStr = "Villager"; break;
        case 2: roleStr = "Doctor"; break;
        case 3: roleStr = "Detective"; break;
        default: roleStr = "Unknown"; break;
        }

        if (!alive[idx]) {
            setColor(COLOR_RED);
            std::cout << "║  │ " << std::setw(3) << idx << " │ "
                << std::setw(18) << names[idx].substr(0, 18) << " │ "
                << std::setw(12) << "☠️ DEAD ☠️" << " │ "
                << std::setw(85) << " " << " │ ║\n";
            setColor(COLOR_CYAN);
        } else {
            float avgTrust = 0.0f;
            int count = 0;
            for (const auto& kv : relations[idx]) {
                if (kv.first != idx && alive[kv.first]) {
                    avgTrust += kv.second.trust;
                    count++;
                }
            }
            if (count > 0) avgTrust /= count;

            setColor(COLOR_CYAN);
            std::cout << "║  │ ";
            setColor(COLOR_WHITE);
            std::cout << std::setw(3) << idx << " │ "
                << std::setw(18) << names[idx].substr(0, 18) << " │ "
                << std::setw(12);
            if (roles[idx] == 0) setColor(COLOR_RED);
            else if (roles[idx] == 3) setColor(COLOR_GREEN);
            else setColor(COLOR_WHITE);
            std::cout << roleStr;
            setColor(COLOR_CYAN);
            std::cout << " │ ";

            setColor(COLOR_WHITE);
            std::cout << "Agg: ";
            printColoredBar(aggression[idx], 13);
            std::cout << " ";
            printValueWithColor(aggression[idx]);
            std::cout << " │ ║\n";

            setColor(COLOR_CYAN);
            std::cout << "║  │     │                    │              │ ";
            setColor(COLOR_WHITE);
            std::cout << "Loy: ";
            printColoredBar(loyalty[idx], 13);
            std::cout << " ";
            printValueWithColor(loyalty[idx]);
            std::cout << " │ ║\n";

            setColor(COLOR_CYAN);
            std::cout << "║  │     │                    │              │ ";
            setColor(COLOR_WHITE);
            std::cout << "Par: ";
            printColoredBar(paranoia[idx], 13);
            std::cout << " ";
            printValueWithColor(paranoia[idx]);
            std::cout << " │ ║\n";

            setColor(COLOR_CYAN);
            std::cout << "║  │     │                    │              │ ";
            setColor(COLOR_WHITE);
            std::cout << "Dec: ";
            printColoredBar(deceit[idx], 13);
            std::cout << " ";
            printValueWithColor(deceit[idx]);
            std::cout << " │ ║\n";

            setColor(COLOR_CYAN);
            std::cout << "║  │     │                    │              │ ";
            setColor(COLOR_WHITE);
            std::cout << "Trs: ";
            printColoredBar(avgTrust, 13);
            std::cout << " ";
            printValueWithColor(avgTrust);
            std::cout << " │ ║\n";

            setColor(COLOR_CYAN);
            std::cout << "║  │     │                    │              │ ";
            setColor(COLOR_YELLOW);
            std::cout << "Reward: ";
            printValueWithColor(totalReward[idx]);
            std::cout << std::setw(68) << " " << " │ ║\n";
            setColor(COLOR_CYAN);
        }

        if (wi != watchedAgents.size() - 1) {
            std::cout << "║  ├─────┼────────────────────┼──────────────┼─────────────────────────────────────────────────────────────────────────────────────┤ ║\n";
        }
    }

    setColor(COLOR_CYAN);
    std::cout << "║  └─────┴────────────────────┴──────────────┴─────────────────────────────────────────────────────────────────────────────────────┘ ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n";
    setColor(COLOR_WHITE);
    std::cout << std::flush;
}

// ============================================================================
// MAIN FUNCTION - with shared memory integration
// ============================================================================
int main() {
    // ALL std::cin inputs happen BEFORE kb.init()
    int n;
    std::cout << "Number of AI players (>=6 recommended): ";
    std::cin >> n;
    if (n < 6) {
        std::cout << "Too few players.\n";
        return 0;
    }

    MemoryNN_CPU memBrain(n, STATE_SIZE);

    std::vector<std::string> names(n);
    std::vector<int> alive(n, 1);
    std::vector<int> roles(n, ROLE_VILLAGER);

    std::vector<float> aggression(n);
    std::vector<float> loyalty(n);
    std::vector<float> paranoia(n);
    std::vector<float> deceit(n);
    std::vector<float> totalReward(n, 0.0f);

    std::vector<std::unordered_map<int, Relationship>> relations(n);

    std::vector<float> trustDense(n * n, 0.0f);
    std::vector<float> likingDense(n * n, 0.0f);
    std::vector<int>   betrayalDense(n * n, 0);
    std::vector<int>   consistencyDense(n * n, 0);
    std::vector<float> probMafiaDense(n * n, 0.0f);
    std::vector<int>   aliveDense(n, 1);

    std::vector<std::vector<float>> lastState(n);
    std::vector<int> lastAction(n, -1);
    std::vector<int> lastVote(n, -1);
    std::vector<int> attacked(n, 0);

    std::vector<std::vector<float>> probMafia(n, std::vector<float>(n, 0.0f));
    std::vector<std::vector<Step>> episodes(n);

    std::vector<int> gpuBestTarget(n, -1);

    for (int i = 0; i < n; ++i)
        names[i] = "P" + std::to_string(i);

    fs::path saveDir = fs::current_path() / "EdgeMafiaTTS" / "saves";

    {
        std::error_code ec;
        fs::create_directories(saveDir, ec);
        if (ec) std::cout << "ERROR: create_directories failed: " << ec.message() << "\n";
    }

    CityHousingSystem cityHousing;
    std::cout << "\n[CITY MODULE 002] Initialized City and Houses tracking system.\n";
    std::cout << "  - Groups detected when 5+ agents have mutual trust > 0.6\n";
    std::cout << "  - Houses named Alpha through Zulu (with 001 suffix for reuse)\n";
    std::cout << "  - City buildings allocated when group reaches 20+ members\n";
    std::cout << "  - CSV snapshots saved every 50 cycles\n\n";

    LLMLayer2 llmModule(saveDir);
    std::cout << "\n[LLM_LAYER2] Aston Walker - Pause-to-Speech Documentary mechanic initialized.\n";
    std::cout << "  - When game pauses [6] or Xbox X button, agent snapshot saved\n";
    std::cout << "  - Use llm_snapshot_cycle_X.txt with ElevenLabs API to hear AI voices\n";
    std::cout << "  - CSV snapshots (agent_snapshot_cycle_X.csv) for data ingestion\n\n";

    // --- LOAD PREVIOUS SAVE ---
    {
        int loadFlag = 1;
        std::cout << "Load previous save? (1=yes, 0=no): ";
        std::cin >> loadFlag;

        if (loadFlag == 1) {
            fs::path savePath = saveDir / "mafia_agents_save.txt";
            std::ifstream ifs(savePath.string());
            if (ifs.is_open()) {
                std::string magic;
                ifs >> magic;
                if (magic.rfind("MAFIA_SIM_SAVE", 0) != 0) {
                    std::cout << "Save file magic mismatch or old format. Starting fresh.\n";
                }
                else {
                    int savedN = 0;
                    ifs >> savedN;
                    std::cout << "Save has " << savedN << " agents. Current game has " << n << " agents.\n";

                    for (int i = 0; i < savedN && i < n; ++i) {
                        std::string agentLabel;
                        int idx;
                        ifs >> agentLabel >> idx;
                        ifs >> std::ws;
                        std::string savedName;
                        std::getline(ifs, savedName);
                        int alive_i, role_i;
                        ifs >> alive_i >> role_i;
                        float aggr_i, loy_i, par_i, dec_i;
                        ifs >> aggr_i >> loy_i >> par_i >> dec_i;
                        float totalReward_i;
                        ifs >> totalReward_i;
                        int lastAction_i, lastVote_i, attacked_i;
                        ifs >> lastAction_i >> lastVote_i >> attacked_i;
                        size_t relCount;
                        ifs >> relCount;
                        std::unordered_map<int, Relationship> relMap;
                        relMap.reserve(relCount);
                        for (size_t r = 0; r < relCount; ++r) {
                            int other;
                            float trust, liking;
                            int betrayal, consistency;
                            bool knownMafia, knownTown;
                            ifs >> other >> trust >> liking >> betrayal >> consistency >> knownMafia >> knownTown;
                            Relationship rel;
                            rel.trust = trust; rel.liking = liking;
                            rel.betrayal = betrayal; rel.consistency = consistency;
                            rel.knownMafia = knownMafia; rel.knownTown = knownTown;
                            relMap[other] = rel;
                        }
                        if (!savedName.empty()) names[i] = savedName;
                        alive[i] = alive_i; roles[i] = role_i;
                        aggression[i] = aggr_i; loyalty[i] = loy_i;
                        paranoia[i] = par_i; deceit[i] = dec_i;
                        totalReward[i] = totalReward_i;
                        lastAction[i] = lastAction_i; lastVote[i] = lastVote_i;
                        attacked[i] = attacked_i;
                        relations[i] = std::move(relMap);
                    }

                    std::string marker;
                    if (ifs >> marker && marker == "GLOBAL_BRAIN") {
                        if (!globalBrain.load(ifs))
                            std::cout << "Warning: failed to load global brain.\n";
                        else
                            std::cout << "Loaded global brain from save.\n";
                    }
                    std::string marker2;
                    if (ifs >> marker2 && marker2 == "SOCIAL_BRAIN") {
                        if (socialBrain.load(ifs))
                            std::cout << "Loaded social brain.\n";
                        else
                            std::cout << "Warning: failed to load social brain.\n";
                    }
                    std::string marker3;
                    if (ifs >> marker3 && marker3 == "MEMORY_BRAIN") {
                        if (memBrain.load(ifs))
                            std::cout << "Loaded memory brain.\n";
                        else
                            std::cout << "Warning: failed to load memory brain.\n";
                    }

                    if (savedN < n)
                        std::cout << "Loaded " << savedN << " agents, " << (n - savedN) << " new agents start fresh.\n";
                    else if (savedN > n)
                        std::cout << "Loaded " << n << " agents, " << (savedN - n) << " extra agents discarded.\n";
                    else
                        std::cout << "Loaded agents for all " << n << " agents.\n";
                }
            }
            else {
                std::cout << "No previous save found. Starting fresh.\n";
            }
        }
        else {
            std::cout << "Starting fresh (no load).\n";
        }
    }

    // HUD Setup
    int enableHUD = 0;
    std::cout << "Enable live console dashboard? (1=yes, 0=no): ";
    std::cin >> enableHUD;
    hudEnabled = (enableHUD == 1);

    std::vector<int> watchedAgents;
    if (hudEnabled) {
        int watchCount = std::min(12, n);
        for (int i = 0; i < watchCount; ++i) watchedAgents.push_back(i);
        std::cout << "HUD enabled. Watching " << watchCount << " agents.\n";
    }

    // =========================================================================
    // MODULE 001 - Multi-Controller Setup (up to 4 Xbox controllers)
    // =========================================================================
    ControllerManager ctrlMgr;
    std::vector<ControlledPlayer> controlledPlayers = setupControllers(n, names, ctrlMgr);

    int playerAgentIndex = controlledPlayers.empty() ? -1
        : controlledPlayers[0].agentIndex();

    int menuPlayerCursor = 0;

    // =========================================================================
    // CRITICAL FIX: kb.init() called AFTER all std::cin inputs
    // This prevents input buffering issues that blocked the n input prompt
    // =========================================================================
    std::cout << "Initializing keyboard input system...\n";
    kb.init();  // NOW called after all std::cin operations are complete

    // Clear screen for HUD if enabled (after kb.init)
    if (hudEnabled) {
        std::cout << "\033[2J\033[1;1H";
        drawHUD(watchedAgents, names, roles, aggression, loyalty, paranoia, deceit, totalReward, relations, alive, 0, n);
    }

    // Role assignment
    int mafiaCount = std::max(1, n / 6);
    int doctorCount = (n >= 7 ? 2 : 1);
    int detectiveCount = (n >= 7 ? 2 : 1);

    std::vector<int> pool;
    pool.reserve(n);
    for (int i = 0; i < mafiaCount; ++i)   pool.push_back(ROLE_MAFIA);
    for (int i = 0; i < doctorCount; ++i)  pool.push_back(ROLE_DOCTOR);
    for (int i = 0; i < detectiveCount; ++i) pool.push_back(ROLE_DETECTIVE);
    while ((int)pool.size() < n) pool.push_back(ROLE_VILLAGER);
    std::shuffle(pool.begin(), pool.end(), rng);
    for (int i = 0; i < n; ++i)
        if (roles[i] == ROLE_VILLAGER) roles[i] = pool[i];

    float baseProb = float(mafiaCount) / std::max(1, n - 1);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            probMafia[i][j] = (i == j ? 0.0f : baseProb);

    for (int i = 0; i < n; ++i) {
        if (aggression[i] != 0.0f || loyalty[i] != 0.0f ||
            paranoia[i] != 0.0f || deceit[i] != 0.0f) continue;
        float r1 = randFloat(), r2 = randFloat(), r3 = randFloat(), r4 = randFloat();
        int k = (roles[i] == ROLE_MAFIA ? 0 : roles[i] == ROLE_VILLAGER ? 1 :
            roles[i] == ROLE_DETECTIVE ? 2 : 3);
        float baseAgg[4] = { 0.7f, 0.2f, 0.3f, 0.0f }; float spanAgg[4] = { 0.3f, 0.4f, 0.3f, 0.2f };
        float baseLoy[4] = { 0.4f, 0.4f, 0.6f, 0.7f }; float spanLoy[4] = { 0.3f, 0.4f, 0.3f, 0.3f };
        float basePar[4] = { 0.4f, 0.3f, 0.7f, 0.5f }; float spanPar[4] = { 0.4f, 0.4f, 0.3f, 0.3f };
        float baseDec[4] = { 0.7f, 0.1f, 0.3f, 0.2f }; float spanDec[4] = { 0.3f, 0.3f, 0.3f, 0.3f };
        aggression[i] = baseAgg[k] + spanAgg[k] * r1;
        loyalty[i] = baseLoy[k] + spanLoy[k] * r2;
        paranoia[i] = basePar[k] + spanPar[k] * r3;
        deceit[i] = baseDec[k] + spanDec[k] * r4;
    }

    // Randomize trust initialization
    for (int i = 0; i < n; ++i) {
        if (!relations[i].empty()) continue;
        std::vector<int> possibleConnections;
        for (int j = 0; j < n; ++j) {
            if (j != i) possibleConnections.push_back(j);
        }
        std::shuffle(possibleConnections.begin(), possibleConnections.end(), rng);
        int initConn = std::min(10, n - 1);
        for (int c = 0; c < initConn; ++c) {
            int j = possibleConnections[c];
            float base = (randFloat() - 0.5f) * 0.6f;
            relations[i][j].trust = base;
            relations[i][j].liking = base * (0.7f + randFloat() * 0.6f);
            relations[i][j].liking = clampf(relations[i][j].liking, -1.0f, 1.0f);
        }
    }

    for (int i = 0; i < n; ++i) {
        if (roles[i] != ROLE_MAFIA) continue;
        for (int j = 0; j < n; ++j) {
            if (i == j || roles[j] != ROLE_MAFIA) continue;
            relations[i][j].trust = clampf(relations[i][j].trust + 0.10f, -1.0f, 1.0f);
            relations[i][j].liking = clampf(relations[i][j].liking + 0.05f, -1.0f, 1.0f);
        }
    }

    auto syncDenseArrays = [&]() {
#pragma omp parallel for
        for (int a = 0; a < n; ++a) {
            aliveDense[a] = alive[a];
            for (int b = 0; b < n; ++b) {
                auto it = relations[a].find(b);
                if (it != relations[a].end()) {
                    trustDense[a * n + b] = it->second.trust;
                    likingDense[a * n + b] = it->second.liking;
                    betrayalDense[a * n + b] = it->second.betrayal;
                    consistencyDense[a * n + b] = it->second.consistency;
                }
                else {
                    trustDense[a * n + b] = 0.0f;
                    likingDense[a * n + b] = 0.0f;
                    betrayalDense[a * n + b] = 0;
                    consistencyDense[a * n + b] = 0;
                }
                probMafiaDense[a * n + b] = probMafia[a][b];
            }
        }
    };

    auto getTrust = [&](int a, int b) {
        auto it = relations[a].find(b);
        return (it == relations[a].end() ? 0.0f : it->second.trust);
    };
    auto getLiking = [&](int a, int b) {
        auto it = relations[a].find(b);
        return (it == relations[a].end() ? 0.0f : it->second.liking);
    };
    auto getBetrayal = [&](int a, int b) {
        auto it = relations[a].find(b);
        return (it == relations[a].end() ? 0 : it->second.betrayal);
    };
    auto getConsistency = [&](int a, int b) {
        auto it = relations[a].find(b);
        return (it == relations[a].end() ? 0 : it->second.consistency);
    };
    auto isAlly = [&](int a, int b) {
        return getTrust(a, b) > 0.5f && getLiking(a, b) > 0.3f;
    };
    auto knowsMafia = [&](int a, int b) {
        auto it = relations[a].find(b);
        return (it != relations[a].end() && it->second.knownMafia);
    };
    auto knowsTown = [&](int a, int b) {
        auto it = relations[a].find(b);
        return (it != relations[a].end() && it->second.knownTown);
    };
    auto ensureRel = [&](int a, int b) -> Relationship& {
        return relations[a][b];
    };

    auto decayRelations = [&]() {
        for (int i = 0; i < n; ++i) {
            float d = 0.0008f * (0.5f + paranoia[i]);
            for (auto& kv : relations[i]) {
                float& t = kv.second.trust;
                if (t > 0.0f) t = std::max(0.0f, t - d);
                else if (t < 0.0f) t = std::min(0.0f, t + d * 0.25f);
                t = clampf(t, -1.0f, 1.0f);
            }
        }
    };

    auto reinforceCliques = [&]() {
        const float thr = 0.75f, delta = 0.01f;
        const int maxStrong = 50;
        for (int c = 0; c < n; ++c) {
            if (!alive[c]) continue;
            std::vector<int> strong;
            for (int a = 0; a < n; ++a) {
                if (alive[a] && a != c && getTrust(a, c) > thr)
                    strong.push_back(a);
                if ((int)strong.size() >= maxStrong) break;
            }
            for (size_t i1 = 0; i1 < strong.size(); ++i1)
                for (size_t i2 = i1 + 1; i2 < strong.size(); ++i2) {
                    int a = strong[i1], b = strong[i2];
                    auto& ra = ensureRel(a, b); auto& rb = ensureRel(b, a);
                    ra.trust = clampf(ra.trust + delta, -1.0f, 1.0f);
                    rb.trust = clampf(rb.trust + delta, -1.0f, 1.0f);
                    ra.liking = clampf(ra.liking + delta * 0.5f, -1.0f, 1.0f);
                    rb.liking = clampf(rb.liking + delta * 0.5f, -1.0f, 1.0f);
                }
        }
    };

    auto mafiaWin = [&]() {
        int m = 0, t = 0;
#pragma omp parallel for reduction(+:m,t)
        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            if (roles[i] == ROLE_MAFIA) m++; else t++;
        }
        return m > 0 && m >= t;
    };

    auto townWin = [&]() {
        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_MAFIA) return false;
        return true;
    };

    auto chooseMafiaTarget = [&](int self) {
        std::vector<int> cand; std::vector<float> score;
        cand.reserve(n); score.reserve(n);
        float A = aggression[self], D = deceit[self];
        for (int j = 0; j < n; ++j) {
            if (!alive[j] || j == self) continue;
            float t = getTrust(self, j), l = getLiking(self, j);
            float susp = -(t + l) * (0.5f + A);
            float betray = (roles[j] == ROLE_MAFIA && t < -0.4f ? 0.5f * D : 0.0f);
            float s = susp + betray;
            if (!std::isfinite(s)) s = 0.0f;

            cand.push_back(j);

            if (roles[j] == ROLE_DETECTIVE) {
                score.push_back(5.0f);
            }
            else {
                score.push_back(std::max(0.01f, s + 0.5f));
            }
        }

        int t = weightedChoice(cand, score);
        if (t == -1 && !cand.empty()) t = cand[randInt((int)cand.size())];
        return t;
    };

    auto chooseDoctorSave = [&](int self) {
        std::vector<int> cand; std::vector<float> score;
        cand.reserve(n); score.reserve(n);
        float L = loyalty[self];
        int det = -1;
        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_DETECTIVE) det = i;
        for (int j = 0; j < n; ++j) {
            if (!alive[j]) continue;
            float base = 0.1f;
            if (j == det) base += 0.7f;
            float s = base + (isAlly(self, j) ? 0.6f * L : 0.0f)
                + 0.2f * (getLiking(self, j) + 1.0f) * 0.5f
                + 0.3f * attacked[j];
            if (!std::isfinite(s)) s = 0.1f;
            cand.push_back(j); score.push_back(std::max(0.01f, s));
        }
        return weightedChoice(cand, score);
    };

    auto chooseDetectiveCheck = [&](int self) {
        std::vector<int> cand; std::vector<float> score;
        cand.reserve(n); score.reserve(n);
        float P = paranoia[self];
        for (int j = 0; j < n; ++j) {
            if (!alive[j] || j == self || knowsMafia(self, j) || knowsTown(self, j)) continue;
            float s = -getTrust(self, j) * (0.5f + P);
            if (s <= 0.0f || !std::isfinite(s)) s = 0.05f;
            cand.push_back(j); score.push_back(std::max(0.01f, s));
        }
        if (cand.empty())
            for (int j = 0; j < n; ++j) {
                if (!alive[j] || j == self) continue;
                float s = -getTrust(self, j) * (0.5f + P);
                if (s <= 0.0f || !std::isfinite(s)) s = 0.05f;
                cand.push_back(j); score.push_back(std::max(0.01f, s));
            }
        return weightedChoice(cand, score);
    };

    auto chooseLynchTarget = [&](int self) {
        std::vector<int> cand;
        cand.reserve(n);
        for (int j = 0; j < n; ++j)
            if (alive[j] && j != self) cand.push_back(j);

        if (cand.empty()) { lastState[self].clear(); lastAction[self] = -1; return -1; }

        std::shuffle(cand.begin(), cand.end(), rng);

        float P = paranoia[self], L = loyalty[self], D = deceit[self];

        int primaryTarget = gpuBestTarget[self];

        if (primaryTarget < 0 || !alive[primaryTarget]) {
            float best = -1.0f;
            for (int j : cand)
                if (probMafia[self][j] > best) { best = probMafia[self][j]; primaryTarget = j; }
            if (best <= 0.0f) { lastAction[self] = 0; return -1; }
        }

        float tPrimary = getTrust(self, primaryTarget);
        int betrayalFlag = (getBetrayal(self, primaryTarget) > 0) ? 1 : 0;
        int consistencyFlag = (getConsistency(self, primaryTarget) > 0) ? 1 : 0;
        float beliefPrimary = clampf(probMafia[self][primaryTarget], 0.0f, 1.0f);
        int detectiveFlag = (roles[self] == ROLE_DETECTIVE && knowsMafia(self, primaryTarget)) ? 1 : 0;

        float avgBelief = 0.0f; int cntBelief = 0;
        for (int j : cand) { avgBelief += clampf(probMafia[self][j], 0.0f, 1.0f); cntBelief++; }
        if (cntBelief > 0) avgBelief /= (float)cntBelief;

        std::vector<float> state(STATE_SIZE, 0.0f);
        state[0] = (tPrimary + 1.0f) / 2.0f;
        state[1] = (float)betrayalFlag;
        state[2] = (float)consistencyFlag;
        state[3] = beliefPrimary;
        state[4] = (float)detectiveFlag;
        state[5] = (P + 1.0f) / 2.0f;
        state[6] = (L + 1.0f) / 2.0f;
        state[7] = (D + 1.0f) / 2.0f;
        state[8] = avgBelief;
        state[9] = (roles[self] == ROLE_MAFIA) ? 1.0f : 0.0f;

        {
            float mem[MEM_SIZE];
            memBrain.getMemory(self, mem);
            state[10] = mem[0];
            state[11] = mem[1];
            state[12] = mem[2];
            state[13] = mem[3];
        }

        lastState[self] = state;
        episodes[self].push_back({ state, -1, 0.0f });

        std::vector<int> idxMap;
        idxMap.reserve(ACTION_SIZE - 1);
        for (int k = 0; k < (int)cand.size() && (int)idxMap.size() < ACTION_SIZE - 1; ++k)
            idxMap.push_back(cand[k]);

        std::vector<int> actions;
        actions.push_back(0);
        for (int a = 1; a <= (int)idxMap.size(); ++a) actions.push_back(a);

        std::vector<float> qOut(ACTION_SIZE, 0.0f);
        globalBrain.forward(state.data(), qOut.data());

        int chosenAction;
        if (randFloat() < 0.1f) {
            chosenAction = actions[randInt((int)actions.size())];
        }
        else {
            float best = -1e9f; int bestA = actions[0];
            for (int a : actions)
                if (a >= 0 && a < (int)qOut.size() && std::isfinite(qOut[a]) && qOut[a] > best)
                {
                    best = qOut[a]; bestA = a;
                }
            chosenAction = bestA;
        }

        lastAction[self] = chosenAction;
        if (!episodes[self].empty()) episodes[self].back().action = chosenAction;
        if (chosenAction == 0) return -1;
        int idx = chosenAction - 1;
        if (idx < 0 || idx >= (int)idxMap.size()) return -1;
        return idxMap[idx];
    };

    auto updateTrustAfterLynch = [&](int lyncher, int target, bool wasMafia) {
        float delta = wasMafia ? 0.8f : -0.8f;
        for (int i = 0; i < n; ++i) {
            if (!alive[i] || i == lyncher) continue;
            Relationship& rel = ensureRel(i, lyncher);
            rel.trust = clampf(rel.trust + delta, -1.0f, 1.0f);
        }
        if (roles[lyncher] == ROLE_MAFIA && roles[target] == ROLE_MAFIA) {
            Relationship& rel = ensureRel(lyncher, target);
            rel.trust = clampf(rel.trust - 1.0f, -1.0f, 1.0f);
        }
    };

    auto nightPhase = [&](int cycle) {
        SIM_LOG << "\n=== NIGHT " << cycle << " ===\n";

        syncDenseArrays();

        int mafiaKill = -1, doctorSave = -1, detectiveCheck = -1, detectiveId = -1;

        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_MAFIA) { mafiaKill = chooseMafiaTarget(i); break; }

        if (mafiaKill == -1) {
            std::vector<int> aliveNonMafia;
            for (int i = 0; i < n; ++i)
                if (alive[i] && roles[i] != ROLE_MAFIA) aliveNonMafia.push_back(i);
            if (!aliveNonMafia.empty())
                mafiaKill = aliveNonMafia[randInt((int)aliveNonMafia.size())];
        }

        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            if (roles[i] == ROLE_DOCTOR) doctorSave = chooseDoctorSave(i);
            if (roles[i] == ROLE_DETECTIVE) { detectiveCheck = chooseDetectiveCheck(i); detectiveId = i; }
        }

        bool doctorEffective = (randFloat() < 0.7f);

        if (mafiaKill != -1 && (!doctorEffective || mafiaKill != doctorSave)) {
            alive[mafiaKill] = 0;
            attacked[mafiaKill]++;
            memBrain.resetAgent(mafiaKill);

            if (playerAgentIndex >= 0 && playerAgentIndex < n) {
                SIM_LOG << "[NIGHT ROLE CHECK] Player = "
                    << names[playerAgentIndex]
                    << " | ROLE = "
                    << roleToString((Role)roles[playerAgentIndex])
                    << "   <-- this is the role of the player\n";
            }

            SIM_LOG << names[mafiaKill] << " was killed at night. ("
                << roleToString((Role)roles[mafiaKill]) << ")\n";
            for (int i = 0; i < n; ++i) {
                if (!alive[i] || i == mafiaKill) continue;
                float like = getLiking(i, mafiaKill);
                if (like > 0.3f)
                    for (int j = 0; j < n; ++j) {
                        if (!alive[j] || j == i || roles[j] != ROLE_MAFIA) continue;
                        {
                            Relationship& rij = ensureRel(i, j);
                            float in8[SOCIAL_IN] = {
                                rij.trust,
                                rij.liking,
                                clampf(probMafia[i][j], 0.0f, 1.0f),
                                0.0f / 4.0f,
                                0.0f,
                                0.0f,
                                (aggression[i] + paranoia[i]) * 0.5f,
                                (loyalty[j] + deceit[j]) * 0.5f
                            };
                            float out2[SOCIAL_OUT];
                            socialBrain.forward(in8, out2);
                            rij.trust = clampf(rij.trust + out2[0], -1.0f, 1.0f);
                            rij.liking = clampf(rij.liking + out2[1], -1.0f, 1.0f);
                        }
                    }
            }
        }
        else {
            SIM_LOG << "No one died tonight.\n";
        }

        if (detectiveCheck != -1 && detectiveId != -1) {
            SIM_LOG << "Detective secretly checked "
                << names[detectiveCheck] << " - "
                << roleToString((Role)roles[detectiveCheck]) << "\n";

            Relationship& rel = ensureRel(detectiveId, detectiveCheck);
            if (roles[detectiveCheck] == ROLE_MAFIA) {
                rel.knownMafia = true;
                paranoia[detectiveId] = std::min(1.0f, paranoia[detectiveId] + 0.1f);
                rel.trust = clampf(rel.trust - 1.0f, -1.0f, 1.0f);
                probMafia[detectiveId][detectiveCheck] = 0.95f;

                for (int i = 0; i < n; ++i) {
                    if (!alive[i] || i == detectiveId || i == detectiveCheck) continue;
                    float w = clampf(getTrust(i, detectiveId), 0.0f, 1.0f);
                    if (w > 0.2f) {
                        probMafia[i][detectiveCheck] = clampf(
                            probMafia[i][detectiveCheck] + w * 0.9f, 0.0f, 0.99f);
                        ensureRel(i, detectiveCheck).trust =
                            clampf(ensureRel(i, detectiveCheck).trust - 0.3f * w, -1.0f, 1.0f);
                    }
                }
            }
            else {
                rel.knownTown = true;
                rel.trust = clampf(rel.trust + 1.0f, -1.0f, 1.0f);
                probMafia[detectiveId][detectiveCheck] = 0.01f;

                for (int i = 0; i < n; ++i) {
                    if (!alive[i] || i == detectiveId || i == detectiveCheck) continue;
                    float w = clampf(getTrust(i, detectiveId), 0.0f, 1.0f);
                    if (w > 0.2f)
                        probMafia[i][detectiveCheck] = clampf(
                            probMafia[i][detectiveCheck] - w * 0.3f, 0.01f, 1.0f);
                }
            }
        }

        decayRelations();
        reinforceCliques();

        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            float ctx[STATE_SIZE + MEM_CTX_EXTRA] = {};
            if ((int)lastState[i].size() == STATE_SIZE)
                std::memcpy(ctx, lastState[i].data(), STATE_SIZE * sizeof(float));
            ctx[STATE_SIZE + 0] = (lastAction[i] >= 0) ? (float)lastAction[i] / 8.0f : 0.0f;
            ctx[STATE_SIZE + 1] = (mafiaKill != -1) ? 1.0f : 0.0f;
            ctx[STATE_SIZE + 2] = 0.0f;
            ctx[STATE_SIZE + 3] = 0.0f;
            ctx[STATE_SIZE + 4] = (float)cycle / 100.0f;
            memBrain.step(i, ctx);
        }
    };

    auto dayPhase = [&](int cycle) {
        SIM_LOG << "\n=== DAY " << cycle << " ===\n";
        SIM_LOG << "Alive players:\n";
        for (int i = 0; i < n; ++i)
            if (alive[i])
                SIM_LOG << " - " << names[i] << " (" << roleToString((Role)roles[i]) << ")\n";

        syncDenseArrays();

        globalBrain.scoreLynchBatch(
            n, aliveDense.data(), paranoia.data(), loyalty.data(), deceit.data(),
            trustDense.data(), likingDense.data(), probMafiaDense.data(),
            gpuBestTarget.data());

        std::vector<int> votes(n, -1);
        for (int i = 0; i < n; ++i)
            if (alive[i]) votes[i] = chooseLynchTarget(i);

        for (int i = 0; i < n; ++i) {
            if (!alive[i] || votes[i] == -1) continue;
            int accused = votes[i];
            float cred = (roles[i] == ROLE_MAFIA)
                ? (1.0f - deceit[i]) * 0.3f
                : 0.15f;
            for (int obs = 0; obs < n; ++obs) {
                if (!alive[obs] || obs == i || obs == accused) continue;
                float w = clampf((getTrust(obs, i) + 1.0f) / 2.0f, 0.0f, 1.0f);
                probMafia[obs][accused] = clampf(
                    probMafia[obs][accused] + cred * w * 0.05f, 0.0f, 0.99f);
            }
        }

        SIM_LOG << "Signals (accusations):\n";
        for (int i = 0; i < n; ++i)
            if (alive[i] && votes[i] != -1)
                SIM_LOG << " - " << names[i] << " accuses " << names[votes[i]] << "\n";

        int aliveCount = 0;
        for (int i = 0; i < n; ++i) if (alive[i]) aliveCount++;

        std::vector<int> voteCount2(n, 0);
        for (int i = 0; i < n; ++i)
            if (alive[i] && votes[i] != -1) voteCount2[votes[i]]++;

        int lynchTarget = -1, maxVotes = 0;
        for (int i = 0; i < n; ++i)
            if (alive[i] && voteCount2[i] > maxVotes) { maxVotes = voteCount2[i]; lynchTarget = i; }
        int majority = 2;

        if (lynchTarget == -1 || maxVotes < majority) {
            SIM_LOG << "No consensus (threshold not reached). No one is lynched.\n";
            for (int i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                int v = votes[i];
                if (v != -1) {
                    Relationship& rel = ensureRel(i, v);
                    if (lastVote[i] == v) { rel.consistency++; rel.trust = clampf(rel.trust + 0.03f, -1.0f, 1.0f); }
                    lastVote[i] = v;
                }
            }
            reinforceCliques();

            for (int i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                float ctx[STATE_SIZE + MEM_CTX_EXTRA] = {};
                if ((int)lastState[i].size() == STATE_SIZE)
                    std::memcpy(ctx, lastState[i].data(), STATE_SIZE * sizeof(float));
                ctx[STATE_SIZE + 0] = (lastAction[i] >= 0) ? (float)lastAction[i] / 8.0f : 0.0f;
                ctx[STATE_SIZE + 1] = 0.0f;
                {
                    bool wasAccused = false;
                    for (int j = 0; j < n; ++j)
                        if (alive[j] && votes[j] == i) { wasAccused = true; break; }
                    ctx[STATE_SIZE + 2] = wasAccused ? 1.0f : 0.0f;
                }
                ctx[STATE_SIZE + 3] = 0.0f;
                ctx[STATE_SIZE + 4] = (float)cycle / 100.0f;
                memBrain.step(i, ctx);
            }
            return;
        }

        SIM_LOG << names[lynchTarget] << " is lynched by vote. ("
            << roleToString((Role)roles[lynchTarget]) << ")\n";
        bool wasMafia = (roles[lynchTarget] == ROLE_MAFIA);
        alive[lynchTarget] = 0;
        memBrain.resetAgent(lynchTarget);

#pragma omp parallel for
        for (int i = 0; i < n; ++i) probMafia[i][lynchTarget] = 0.0f;

        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            int v = votes[i];
            if (v == -1) continue;

            if (v == lynchTarget && isAlly(i, lynchTarget)) {
                Relationship& rel = ensureRel(i, lynchTarget);
                rel.betrayal++;
                rel.trust = clampf(rel.trust - 1.0f, -1.0f, 1.0f);
                rel.liking = clampf(rel.liking - 0.3f, -1.0f, 1.0f);
            }
            {
                Relationship& rel = ensureRel(i, v);
                if (lastVote[i] == v) { rel.consistency++; rel.trust = clampf(rel.trust + 0.05f, -1.0f, 1.0f); }
            }
            lastVote[i] = v;

            if (v == lynchTarget) {
                totalReward[i] += wasMafia ? 0.4f : -0.1f;
                updateTrustAfterLynch(i, lynchTarget, wasMafia);

                {
                    float tgt_dt = wasMafia ? -0.5f : 0.3f;
                    float tgt_dl = wasMafia ? -0.3f : 0.2f;
                    Relationship& rel = ensureRel(i, lynchTarget);
                    float in8[SOCIAL_IN] = {
                        rel.trust,
                        rel.liking,
                        clampf(probMafia[i][lynchTarget], 0.0f, 1.0f),
                        1.0f / 4.0f,
                        1.0f,
                        wasMafia ? 1.0f : 0.0f,
                        (aggression[i] + paranoia[i]) * 0.5f,
                        (loyalty[lynchTarget] + deceit[lynchTarget]) * 0.5f
                    };
                    socialBrain.train(in8, tgt_dt, tgt_dl, 0.001f);
                }
            }
        }

        reinforceCliques();

        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            float ctx[STATE_SIZE + MEM_CTX_EXTRA] = {};
            if ((int)lastState[i].size() == STATE_SIZE)
                std::memcpy(ctx, lastState[i].data(), STATE_SIZE * sizeof(float));
            ctx[STATE_SIZE + 0] = (lastAction[i] >= 0) ? (float)lastAction[i] / 8.0f : 0.0f;
            ctx[STATE_SIZE + 1] = 0.0f;
            {
                bool wasAccused = false;
                for (int j = 0; j < n; ++j)
                    if (alive[j] && votes[j] == i) { wasAccused = true; break; }
                ctx[STATE_SIZE + 2] = wasAccused ? 1.0f : 0.0f;
            }
            ctx[STATE_SIZE + 3] = (lastVote[i] == lynchTarget && wasMafia) ? 1.0f : 0.0f;
            ctx[STATE_SIZE + 4] = (float)cycle / 100.0f;
            memBrain.step(i, ctx);
        }
    };

    // Dead-player pruning helper
    auto pruneDeadControlledPlayers = [&]() {
        for (auto& cp : controlledPlayers) {
            auto it = cp.agentIndices.begin();
            size_t pos = 0;
            while (it != cp.agentIndices.end()) {
                int ai = *it;
                if (ai < 0 || ai >= n || !alive[ai]) {
                    if (ai >= 0 && ai < n) {
                        SIM_LOG << "[MODULE 001b] Agent " << ai
                            << " (" << names[ai] << ") is DEAD"
                            << " -- removed from player agent list.\n";
                    }
                    if (pos < cp.agentControlOn.size()) {
                        cp.agentControlOn.erase(cp.agentControlOn.begin() + pos);
                    }
                    it = cp.agentIndices.erase(it);
                }
                else {
                    ++it;
                    ++pos;
                }
            }
        }
        controlledPlayers.erase(
            std::remove_if(controlledPlayers.begin(), controlledPlayers.end(),
                [](const ControlledPlayer& cp) {
                    return cp.agentIndices.empty();
                }),
            controlledPlayers.end()
        );
        if (!controlledPlayers.empty() && menuPlayerCursor >= (int)controlledPlayers.size())
            menuPlayerCursor = (int)controlledPlayers.size() - 1;
        if (controlledPlayers.empty()) menuPlayerCursor = 0;
        playerAgentIndex = controlledPlayers.empty() ? -1 : controlledPlayers[0].agentIndex();
    };

    // =========================================================================
    // NUDGE MECHANIC
    // =========================================================================
    auto nudgeAgent = [&](int agentIdx, const std::string& trait, float delta) {
        if (agentIdx < 0 || agentIdx >= n) return false;
        if (!alive[agentIdx]) {
            SIM_LOG << "[NUDGE] Agent " << agentIdx << " (" << names[agentIdx] << ") is dead - cannot nudge\n";
            return false;
        }

        if (trait == "aggression") {
            float oldVal = aggression[agentIdx];
            aggression[agentIdx] = clampf(aggression[agentIdx] + delta, -1.0f, 1.0f);
            SIM_LOG << "[NUDGE] Agent " << agentIdx << " (" << names[agentIdx] << ") aggression: "
                << std::showpos << oldVal << " -> " << aggression[agentIdx] << std::noshowpos << "\n";
        }
        else if (trait == "loyalty") {
            float oldVal = loyalty[agentIdx];
            loyalty[agentIdx] = clampf(loyalty[agentIdx] + delta, -1.0f, 1.0f);
            SIM_LOG << "[NUDGE] Agent " << agentIdx << " (" << names[agentIdx] << ") loyalty: "
                << std::showpos << oldVal << " -> " << loyalty[agentIdx] << std::noshowpos << "\n";
        }
        else if (trait == "paranoia") {
            float oldVal = paranoia[agentIdx];
            paranoia[agentIdx] = clampf(paranoia[agentIdx] + delta, -1.0f, 1.0f);
            SIM_LOG << "[NUDGE] Agent " << agentIdx << " (" << names[agentIdx] << ") paranoia: "
                << std::showpos << oldVal << " -> " << paranoia[agentIdx] << std::noshowpos << "\n";
        }
        else if (trait == "deceit") {
            float oldVal = deceit[agentIdx];
            deceit[agentIdx] = clampf(deceit[agentIdx] + delta, -1.0f, 1.0f);
            SIM_LOG << "[NUDGE] Agent " << agentIdx << " (" << names[agentIdx] << ") deceit: "
                << std::showpos << oldVal << " -> " << deceit[agentIdx] << std::noshowpos << "\n";
        }
        return true;
    };

    auto nudgeAllControlled = [&](const std::string& trait, float delta) {
        if (controlledPlayers.empty()) {
            SIM_LOG << "[NUDGE] No controlled players to nudge\n";
            return;
        }
        for (auto& cp : controlledPlayers) {
            for (size_t agentPos = 0; agentPos < cp.agentIndices.size(); ++agentPos) {
                if (!cp.getAgentControl(agentPos)) continue;
                int ai = cp.agentIndices[agentPos];
                if (ai >= 0 && ai < n && alive[ai])
                    nudgeAgent(ai, trait, delta);
            }
        }
    };

    auto getNudgeStatus = [&]() -> std::string {
        if (controlledPlayers.empty()) return "No controlled agents";
        std::stringstream ss;
        ss << "Nudging: ";
        for (size_t i = 0; i < controlledPlayers.size() && i < 3; ++i) {
            for (size_t ap = 0; ap < controlledPlayers[i].agentIndices.size(); ++ap) {
                if (controlledPlayers[i].getAgentControl(ap)) {
                    int ai = controlledPlayers[i].agentIndices[ap];
                    if (ai >= 0 && ai < n && alive[ai])
                        ss << names[ai] << " ";
                }
            }
        }
        if (controlledPlayers.size() > 3) ss << "+" << (controlledPlayers.size() - 3) << " more";
        return ss.str();
    };

    int cycle = 1;
    const int maxCycles = 1000000;

    bool prevPauseKey = false;
    // Separate prev states for controller and keyboard (THIS IS THE FIX)
    bool prevCtrlY = false, prevCtrlA = false, prevCtrlX = false, prevCtrlDpadLeft = false, prevCtrlDpadRight = false;
    bool prevKbUp = false, prevKbDown = false, prevKbLeft = false, prevKbRight = false;
    bool prevKbEsc = false;
    bool prevTKey = false;
    bool prevLeftTrigger = false;
    bool prevRightTrigger = false;
    bool prevLKey = false;
    bool prevRKey = false;

    // =========================================================================
    // PAUSE MENU - FIXED with arrow keys support
    // =========================================================================
    auto pauseMenu = [&]() {
        std::vector<std::string> agentHouses = cityHousing.getAllAgentHouses(n);

        llmModule.writeAgentSnapshot(cycle, names, roles, aggression, loyalty,
            paranoia, deceit, totalReward, alive, relations, n, agentHouses);

        llmModule.writeAgentSnapshotCSV(cycle, names, roles, aggression, loyalty,
            paranoia, deceit, totalReward, alive, relations, n, agentHouses);

        SIM_LOG << "\n[MODULE 001a] *** GAME PAUSED -- MENU ***\n";
        SIM_LOG << "  [LLM LAYER 2] Agent snapshots written to saves/llm_snapshot_cycle_" << cycle << ".txt\n";
        SIM_LOG << "  [LLM LAYER 2] CSV snapshot written to saves/agent_snapshot_cycle_" << cycle << ".csv\n";
        SIM_LOG << "  UP/DOWN arrows = cycle through agents\n";
        SIM_LOG << "  LEFT arrow = set OFF | RIGHT arrow = set ON\n";
        SIM_LOG << "  X / ESC = exit menu and resume game\n";

        if (controlledPlayers.empty()) {
            SIM_LOG << "  (No controlled players to display)\n";
        }
        else {
            int ci = controlledPlayers[menuPlayerCursor].agentIndex();
            SIM_LOG << "  --> You are player " << (menuPlayerCursor + 1)
                << " | agent " << ci << " | " << names[ci]
                << " | role: " << roleToString((Role)roles[ci])
                << " | alive: " << (alive[ci] ? "YES" : "NO") << "\n";
        }

        // Flush any pending input
        kb.flush();

        int menuAgentCursor = 0;
        if (!controlledPlayers.empty()) {
            auto& cp = controlledPlayers[menuPlayerCursor];
            if (menuAgentCursor >= (int)cp.agentIndices.size())
                menuAgentCursor = 0;
        }

        if (!controlledPlayers.empty()) {
            auto& cp = controlledPlayers[menuPlayerCursor];
            SIM_LOG << "[MENU] Player " << (menuPlayerCursor + 1)
                << " controls " << cp.agentIndices.size() << " agent(s):\n";
            for (int k = 0; k < (int)cp.agentIndices.size(); ++k) {
                int ai = cp.agentIndices[k];
                SIM_LOG << (k == menuAgentCursor ? "  --> " : "      ")
                    << "[" << k << "] Agent " << ai << " | " << names[ai]
                    << " | " << roleToString((Role)roles[ai])
                    << " | " << (cp.getAgentControl(k) ? "ON" : "OFF")
                    << " | " << (alive[ai] ? "alive" : "DEAD") << "\n";
            }
        }

        bool inMenu = true;
        // Reset prev states on menu entry
        prevCtrlY = true; prevCtrlA = true; prevCtrlX = true; prevCtrlDpadLeft = true; prevCtrlDpadRight = true;
        prevKbUp = true; prevKbDown = true; prevKbLeft = true; prevKbRight = true; prevKbEsc = true;

        while (inMenu) {
            ctrlMgr.poll();
            kb.flush();

            // Controller inputs
            bool curY = ctrlMgr.menuY();
            bool curA = ctrlMgr.menuA();
            bool curX = ctrlMgr.menuX();
            bool curDpadLeft = ctrlMgr.menuDLeft();
            bool curDpadRight = ctrlMgr.menuDRight();
            
            // Keyboard inputs - WITH ARROW KEYS!
            bool arrowUp = kb.keyPressed(KEY_UP);
            bool arrowDown = kb.keyPressed(KEY_DOWN);
            bool arrowLeft = kb.keyPressed(KEY_LEFT);
            bool arrowRight = kb.keyPressed(KEY_RIGHT);
            bool kbEsc = kb.keyPressed(27); // ESC key
            
            // Also support WASD as fallback
            bool wasdUp = kb.keyPressed('w') || kb.keyPressed('W');
            bool wasdDown = kb.keyPressed('s') || kb.keyPressed('S');
            bool wasdLeft = kb.keyPressed('q') || kb.keyPressed('Q');
            bool wasdRight = kb.keyPressed('e') || kb.keyPressed('E');
            
            // Combine arrow keys and WASD
            bool kbUp = arrowUp || wasdUp;
            bool kbDown = arrowDown || wasdDown;
            bool kbLeft = arrowLeft || wasdLeft;
            bool kbRight = arrowRight || wasdRight;

            // Edge detection using SEPARATE prev states
            bool doForward = (curY && !prevCtrlY) || (kbUp && !prevKbUp);
            bool doBackward = (curA && !prevCtrlA) || (kbDown && !prevKbDown);
            bool doExit = (curX && !prevCtrlX) || (kbEsc && !prevKbEsc);
            bool doSetOff = (curDpadLeft && !prevCtrlDpadLeft) || (kbLeft && !prevKbLeft);
            bool doSetOn = (curDpadRight && !prevCtrlDpadRight) || (kbRight && !prevKbRight);

            // Update controller prev states
            prevCtrlY = curY;
            prevCtrlA = curA;
            prevCtrlX = curX;
            prevCtrlDpadLeft = curDpadLeft;
            prevCtrlDpadRight = curDpadRight;

            // Update keyboard prev states
            prevKbUp = kbUp;
            prevKbDown = kbDown;
            prevKbLeft = kbLeft;
            prevKbRight = kbRight;
            prevKbEsc = kbEsc;

            if (!controlledPlayers.empty()) {
                auto& cp = controlledPlayers[menuPlayerCursor];
                int agentCount = (int)cp.agentIndices.size();

                if (doForward && agentCount > 0) {
                    menuAgentCursor = (menuAgentCursor + 1) % agentCount;
                    int ai = cp.agentIndices[menuAgentCursor];
                    SIM_LOG << "[MENU] --> Agent [" << menuAgentCursor << "] "
                        << ai << " | " << names[ai]
                        << " | " << roleToString((Role)roles[ai])
                        << " | " << (cp.getAgentControl(menuAgentCursor) ? "ON" : "OFF")
                        << " | " << (alive[ai] ? "alive" : "DEAD") << "\n";
                }
                if (doBackward && agentCount > 0) {
                    menuAgentCursor = (menuAgentCursor - 1 + agentCount) % agentCount;
                    int ai = cp.agentIndices[menuAgentCursor];
                    SIM_LOG << "[MENU] --> Agent [" << menuAgentCursor << "] "
                        << ai << " | " << names[ai]
                        << " | " << roleToString((Role)roles[ai])
                        << " | " << (cp.getAgentControl(menuAgentCursor) ? "ON" : "OFF")
                        << " | " << (alive[ai] ? "alive" : "DEAD") << "\n";
                }
                if (doSetOff && agentCount > 0) {
                    cp.setAgentControl(menuAgentCursor, false);
                    SIM_LOG << "[MENU] Agent " << cp.agentIndices[menuAgentCursor]
                        << " (" << names[cp.agentIndices[menuAgentCursor]] << ") control set to OFF.\n";
                }
                if (doSetOn && agentCount > 0) {
                    cp.setAgentControl(menuAgentCursor, true);
                    SIM_LOG << "[MENU] Agent " << cp.agentIndices[menuAgentCursor]
                        << " (" << names[cp.agentIndices[menuAgentCursor]] << ") control set to ON.\n";
                }
            }

            if (doExit) {
                inMenu = false;
                SIM_LOG << "[MODULE 001a] Menu exited -- WASD restored. Game resumes.\n";
                prevPauseKey = true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        kb.flush();
    };

    auto pollPlayerInput = [&]() {
        pruneDeadControlledPlayers();
        if (controlledPlayers.empty()) return;

        ctrlMgr.poll();

        // Pause menu (R key or Right Trigger)
        bool pressR = kb.keyPressed('r') || kb.keyPressed('R');
        bool rightTrigger = ctrlMgr.rightTriggerPressed();

        bool pauseTrigger = pressR || (rightTrigger && !prevRightTrigger);

        if (pauseTrigger && !prevPauseKey) {
            prevPauseKey = true;
            pauseMenu();
            return;
        }
        if (!pauseTrigger) prevPauseKey = false;
        prevRightTrigger = rightTrigger;

        // L key or Left Trigger - print agent info
        bool pressL = kb.keyPressed('l') || kb.keyPressed('L');
        bool leftTrigger = ctrlMgr.leftTriggerPressed();

        bool leftTriggerActive = leftTrigger || pressL;

        if (leftTriggerActive && !prevLeftTrigger) {
            std::cout << "\n[LEFT TRIGGER / L KEY] *** ALL CONTROLLED AGENTS - PLAYER 1 ***\n";
            if (menuPlayerCursor >= 0 && menuPlayerCursor < (int)controlledPlayers.size()) {
                auto& cp = controlledPlayers[menuPlayerCursor];
                std::cout << "Player " << (menuPlayerCursor + 1)
                    << " | " << cp.agentIndices.size() << " agent(s)\n";
                std::cout << "═══════════════════════════════════════════════════════════\n";
                for (size_t agentPos = 0; agentPos < cp.agentIndices.size(); ++agentPos) {
                    int ai = cp.agentIndices[agentPos];
                    if (ai < 0 || ai >= n) continue;
                    std::cout << "\n";
                    std::cout << "  Agent " << ai << " | " << names[ai]
                        << " | " << roleToString((Role)roles[ai])
                        << " | " << (alive[ai] ? "ALIVE" : "DEAD")
                        << " | CONTROL: " << (cp.getAgentControl(agentPos) ? "ON" : "OFF") << "\n";
                    std::cout << "  TRAITS:"
                        << "  Aggression=" << std::fixed << std::setprecision(3) << aggression[ai]
                        << "  Loyalty=" << loyalty[ai]
                        << "  Paranoia=" << paranoia[ai]
                        << "  Deceit=" << deceit[ai] << "\n";
                    std::cout << "  Reward=" << totalReward[ai] << "\n";
                    std::vector<std::pair<float, int>> tr;
                    for (auto& kv : relations[ai])
                        if (kv.first != ai && alive[kv.first])
                            tr.push_back({ kv.second.trust, kv.first });
                    std::sort(tr.begin(), tr.end(), [](auto& a, auto& b) { return a.first > b.first; });
                    std::cout << "  Top trust: ";
                    int shown = 0;
                    for (auto& t : tr) {
                        if (shown >= 3) break;
                        std::cout << names[t.second] << "=" << std::fixed << std::setprecision(2) << t.first << "  ";
                        shown++;
                    }
                    if (shown == 0) std::cout << "(none)";
                    std::cout << "\n";
                    std::cout << "───────────────────────────────────────────────────────\n";
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4000));
        }
        prevLeftTrigger = leftTriggerActive;
        prevLKey = pressL;
        prevRKey = pressR;

        // T key or X button - speak agent
        bool tKey = kb.keyPressed('t') || kb.keyPressed('T');
        bool xButton = ctrlMgr.menuX();

        if ((tKey || xButton) && !prevTKey) {
            if (menuPlayerCursor >= 0 && menuPlayerCursor < (int)controlledPlayers.size()) {
                int agentIdx = controlledPlayers[menuPlayerCursor].agentIndex();
                if (agentIdx >= 0 && agentIdx < n && alive[agentIdx]) {
                    speakAgentEmotionalState(agentIdx, names, roles, aggression, loyalty, paranoia, deceit, alive);
                }
            }
        }
        prevTKey = tKey || xButton;

        bool shiftHeld = kb.keyPressed(16); // Shift key

        bool pressW = kb.keyPressed('w') || kb.keyPressed('W');
        bool pressA = kb.keyPressed('a') || kb.keyPressed('A');
        bool pressS = kb.keyPressed('s') || kb.keyPressed('S');
        bool pressD = kb.keyPressed('d') || kb.keyPressed('D');

        bool incAggression = ctrlMgr.doIncAggression(pressW, shiftHeld);
        bool incLoyalty = ctrlMgr.doIncLoyalty(pressA, shiftHeld);
        bool incParanoia = ctrlMgr.doIncParanoia(pressS, shiftHeld);
        bool incDeceit = ctrlMgr.doIncDeceit(pressD, shiftHeld);

        bool decAggression = ctrlMgr.doDecAggression(pressW, shiftHeld);
        bool decLoyalty = ctrlMgr.doDecLoyalty(pressA, shiftHeld);
        bool decParanoia = ctrlMgr.doDecParanoia(pressS, shiftHeld);
        bool decDeceit = ctrlMgr.doDecDeceit(pressD, shiftHeld);

        bool anyPressed = incAggression || incLoyalty || incParanoia || incDeceit
            || decAggression || decLoyalty || decParanoia || decDeceit;
        if (!anyPressed) return;

        for (auto& cp : controlledPlayers) {
            for (size_t agentPos = 0; agentPos < cp.agentIndices.size(); ++agentPos) {
                if (!cp.getAgentControl(agentPos)) continue;
                int i = cp.agentIndices[agentPos];
                if (i < 0 || i >= n) continue;
                if (!alive[i]) continue;

                if (incAggression) {
                    float oldVal = aggression[i];
                    aggression[i] = clampf(aggression[i] + 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") aggression: "
                        << std::showpos << oldVal << " -> " << aggression[i] << std::noshowpos << "\n";
                }
                if (incLoyalty) {
                    float oldVal = loyalty[i];
                    loyalty[i] = clampf(loyalty[i] + 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") loyalty: "
                        << std::showpos << oldVal << " -> " << loyalty[i] << std::noshowpos << "\n";
                }
                if (incParanoia) {
                    float oldVal = paranoia[i];
                    paranoia[i] = clampf(paranoia[i] + 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") paranoia: "
                        << std::showpos << oldVal << " -> " << paranoia[i] << std::noshowpos << "\n";
                }
                if (incDeceit) {
                    float oldVal = deceit[i];
                    deceit[i] = clampf(deceit[i] + 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") deceit: "
                        << std::showpos << oldVal << " -> " << deceit[i] << std::noshowpos << "\n";
                }

                if (decAggression) {
                    float oldVal = aggression[i];
                    aggression[i] = clampf(aggression[i] - 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") aggression: "
                        << std::showpos << oldVal << " -> " << aggression[i] << std::noshowpos << "\n";
                }
                if (decLoyalty) {
                    float oldVal = loyalty[i];
                    loyalty[i] = clampf(loyalty[i] - 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") loyalty: "
                        << std::showpos << oldVal << " -> " << loyalty[i] << std::noshowpos << "\n";
                }
                if (decParanoia) {
                    float oldVal = paranoia[i];
                    paranoia[i] = clampf(paranoia[i] - 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") paranoia: "
                        << std::showpos << oldVal << " -> " << paranoia[i] << std::noshowpos << "\n";
                }
                if (decDeceit) {
                    float oldVal = deceit[i];
                    deceit[i] = clampf(deceit[i] - 0.001f, -1.0f, 1.0f);
                    SIM_LOG << "[NUDGE] Agent " << i << " (" << names[i] << ") deceit: "
                        << std::showpos << oldVal << " -> " << deceit[i] << std::noshowpos << "\n";
                }
            }
        }
    };

    // ============================================================================
    // SHARED MEMORY INTEGRATION (Added here)
    // ============================================================================
    MafiaSharedState* sharedState = openSharedMafiaState(true);
    if (!sharedState) {
        std::cerr << "Failed to create shared memory. Continuing without sharing.\n";
    } else {
        std::cout << "Shared memory created at " << SHARED_MEM_NAME << "\n";
        sharedState->version = 0;
        sharedState->cycle = 0;
        sharedState->numAgents = n;
        sharedState->mafiaAlive = 0;
        sharedState->townAlive = 0;
        for (int i = 0; i < n && i < MAX_AGENTS; ++i) {
            strncpy(sharedState->agents[i].name, names[i].c_str(), 31);
            sharedState->agents[i].name[31] = 0;
            sharedState->agents[i].role = roles[i];
            sharedState->agents[i].alive = alive[i];
            sharedState->agents[i].aggression = aggression[i];
            sharedState->agents[i].loyalty = loyalty[i];
            sharedState->agents[i].paranoia = paranoia[i];
            sharedState->agents[i].deceit = deceit[i];
            sharedState->agents[i].totalReward = totalReward[i];
            if (alive[i]) {
                if (roles[i] == ROLE_MAFIA) sharedState->mafiaAlive++;
                else sharedState->townAlive++;
            }
        }
        sharedState->gameOver = 0;
        sharedState->winnerMsg[0] = 0;
        sharedState->version++;
    }

    auto writeSharedState = [&](int cycleNow, bool gameEnded = false) {
        if (!sharedState) return;
        sharedState->cycle = cycleNow;
        sharedState->numAgents = n;
        sharedState->mafiaAlive = 0;
        sharedState->townAlive = 0;
        for (int i = 0; i < n && i < MAX_AGENTS; ++i) {
            strncpy(sharedState->agents[i].name, names[i].c_str(), 31);
            sharedState->agents[i].name[31] = 0;
            sharedState->agents[i].role = roles[i];
            sharedState->agents[i].alive = alive[i];
            sharedState->agents[i].aggression = aggression[i];
            sharedState->agents[i].loyalty = loyalty[i];
            sharedState->agents[i].paranoia = paranoia[i];
            sharedState->agents[i].deceit = deceit[i];
            sharedState->agents[i].totalReward = totalReward[i];
            if (alive[i]) {
                if (roles[i] == ROLE_MAFIA) sharedState->mafiaAlive++;
                else sharedState->townAlive++;
            }
        }
        if (gameEnded) {
            sharedState->gameOver = (mafiaWin() ? 1 : townWin() ? 2 : 0);
            if (sharedState->gameOver) {
                strncpy(sharedState->winnerMsg,
                        mafiaWin() ? "MAFIA WIN!" : "TOWN WIN!",
                        63);
            } else {
                sharedState->winnerMsg[0] = 0;
            }
        } else {
            sharedState->gameOver = 0;
            sharedState->winnerMsg[0] = 0;
        }
        sharedState->version++;
    };

    // ============================================================================
    // END OF SHARED MEMORY ADDITION
    // ============================================================================

    saveTrustMatrixSnapshot(0, n, relations, saveDir);
    cityHousing.updateGroups(0, n, relations, alive, names, roles);
    cityHousing.saveGroupHistoryCSV(0, n, alive, names, roles, relations, saveDir);
    cityHousing.printGroupStats(0);

    while (!mafiaWin() && !townWin() && cycle <= maxCycles) {
        pollPlayerInput();

        if (hudEnabled) {
            watchedAgents.erase(
                std::remove_if(watchedAgents.begin(), watchedAgents.end(),
                    [&](int idx) { return idx < 0 || idx >= n || !alive[idx]; }),
                watchedAgents.end());

            drawHUD(watchedAgents, names, roles, aggression, loyalty, paranoia, deceit, totalReward, relations, alive, cycle, n);
        }

        nightPhase(cycle);
        if (mafiaWin() || townWin()) {
            writeSharedState(cycle, true);   // final state before break
            break;
        }
        dayPhase(cycle);

        writeSharedState(cycle, false);   // update shared memory after each full cycle

        saveTrustMatrixSnapshot(cycle, n, relations, saveDir);
        cityHousing.updateGroups(cycle, n, relations, alive, names, roles);
        cityHousing.saveGroupHistoryCSV(cycle, n, alive, names, roles, relations, saveDir);
        if (cycle % 100 == 0) {
            cityHousing.printGroupStats(cycle);
        }

        cycle++;
    }

    // Write final game over state if not already written
    if (mafiaWin() || townWin()) {
        writeSharedState(cycle - 1, true);
    } else {
        // max cycles reached, game ended without a win
        if (sharedState) {
            sharedState->gameOver = 0;
            strcpy(sharedState->winnerMsg, "MAX CYCLES REACHED");
            writeSharedState(cycle - 1, true);
        }
    }

    if (mafiaWin()) {
        SIM_LOG << "\nMafia win after " << cycle - 1 << " cycles.\n";
        for (int i = 0; i < n; ++i) {
            if (alive[i] && roles[i] == ROLE_MAFIA) totalReward[i] += 1.0f;
            else if (alive[i] && roles[i] != ROLE_MAFIA) totalReward[i] -= 0.5f;
        }
    }
    else if (townWin()) {
        SIM_LOG << "\nTown win after " << cycle - 1 << " cycles.\n";
        for (int i = 0; i < n; ++i) {
            if (alive[i] && roles[i] != ROLE_MAFIA) totalReward[i] += 1.0f;
            else if (alive[i] && roles[i] == ROLE_MAFIA) totalReward[i] -= 0.5f;
        }
    }
    else {
        SIM_LOG << "Max cycles reached.\n";
    }

    std::cout << "\n[CITY MODULE 002] FINAL GROUP STATISTICS:\n";
    cityHousing.printGroupStats(cycle - 1);

    cityHousing.saveGroupHistoryCSV(cycle - 1, n, alive, names, roles, relations, saveDir);

    for (int i = 0; i < n; ++i) {
        if (episodes[i].empty()) continue;

        float finalReward = (roles[i] == ROLE_MAFIA)
            ? (mafiaWin() ? 1.0f : -1.0f)
            : (townWin() ? 1.0f : -1.0f);
        for (auto& step : episodes[i])
            step.reward = finalReward;

        float gamma = 0.99f;
        float G = 0.0f;
        for (int t = (int)episodes[i].size() - 1; t >= 0; --t) {
            G = episodes[i][t].reward + gamma * G;
            if (episodes[i][t].action < 0) continue;
            if ((int)episodes[i][t].state.size() != STATE_SIZE) continue;
            globalBrain.train(
                episodes[i][t].state.data(),
                episodes[i][t].action,
                G,
                0.001f
            );
        }
    }

    {
        fs::path savePath = saveDir / "mafia_agents_save.txt";
        std::ofstream ofs(savePath.string());
        if (!ofs.is_open()) {
            std::cout << "Failed to open save file for writing.\n";
        }
        else {
            ofs << "MAFIA_SIM_SAVE_V2\n" << n << "\n";
            for (int i = 0; i < n; ++i) {
                ofs << "AGENT " << i << "\n" << names[i] << "\n";
                ofs << alive[i] << " " << roles[i] << "\n";
                ofs << aggression[i] << " " << loyalty[i] << " " << paranoia[i] << " " << deceit[i] << "\n";
                ofs << totalReward[i] << "\n";
                ofs << lastAction[i] << " " << lastVote[i] << " " << attacked[i] << "\n";
                ofs << relations[i].size() << "\n";
                for (auto& kv : relations[i]) {
                    Relationship& rel = kv.second;
                    ofs << kv.first << " " << rel.trust << " " << rel.liking << " "
                        << rel.betrayal << " " << rel.consistency << " "
                        << (rel.knownMafia ? 1 : 0) << " " << (rel.knownTown ? 1 : 0) << "\n";
                }
            }
            if (mafiaWin())
                ofs << "WINNER MAFIA\n";
            else if (townWin())
                ofs << "WINNER TOWN\n";
            else
                ofs << "WINNER DRAW\n";

            if (playerAgentIndex >= 0 && playerAgentIndex < n)
                ofs << "PLAYER_ROLE " << roleToString((Role)roles[playerAgentIndex]) << "\n";
            else
                ofs << "PLAYER_ROLE NONE\n";

            ofs << "GLOBAL_BRAIN\n";
            globalBrain.save(ofs);
            ofs << "SOCIAL_BRAIN\n";
            socialBrain.save(ofs);
            ofs << "MEMORY_BRAIN\n";
            memBrain.save(ofs);
        }
    }

    // Restore terminal settings
    kb.restore();

    // Clean up shared memory
    if (sharedState) {
        closeSharedMafiaState(sharedState);
        shm_unlink(SHARED_MEM_NAME);
    }

    std::cout << "\nGame saved. Goodbye!\n";

    return 0;
}