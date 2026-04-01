// EdgeMafiaNPCSubsystem.cpp

#include "EdgeMafiaNPCSubsystem.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <unordered_map>
#include <functional>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <filesystem>

void UEdgeMafiaNPCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

const FEdgeMafiaAgent& UEdgeMafiaNPCSubsystem::GetAgent(int32 AgentID) const
{
    check(Agents.IsValidIndex(AgentID));
    return Agents[AgentID];
}

void UEdgeMafiaNPCSubsystem::RunSimulation(int32 NumAgents)
{
    Agents.Empty();
    Agents.SetNum(NumAgents);
    RunMafiaSimulation(NumAgents);
}

// ================= INTERNAL SIMULATION IMPLEMENTATION =================

namespace
{
    enum Role {
        ROLE_MAFIA = 0,
        ROLE_VILLAGER = 1,
        ROLE_DOCTOR = 2,
        ROLE_DETECTIVE = 3
    };

    static std::mt19937 rng((unsigned)std::time(nullptr));

    int randInt(int maxVal) {
        if (maxVal <= 0) return 0;
        std::uniform_int_distribution<int> dist(0, maxVal - 1);
        return dist(rng);
    }

    float randFloat() {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(rng);
    }

    float clampf(float x, float lo, float hi) {
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }

    FString roleToString(Role r) {
        switch (r) {
        case ROLE_MAFIA: return TEXT("Mafia");
        case ROLE_VILLAGER: return TEXT("Villager");
        case ROLE_DOCTOR: return TEXT("Doctor");
        case ROLE_DETECTIVE: return TEXT("Detective");
        default: return TEXT("Unknown");
        }
    }

    int weightedChoice(const std::vector<int>& candidates,
        const std::vector<float>& scores) {
        if (candidates.empty()) return -1;

        if (scores.size() != candidates.size()) {
            return candidates[randInt((int)candidates.size())];
        }

        float sum = 0.0f;
        for (float s : scores) {
            if (s > 0.0f) sum += s;
        }

        if (sum <= 0.0f || !std::isfinite(sum)) {
            return candidates[randInt((int)candidates.size())];
        }

        float r = randFloat() * sum;
        for (size_t i = 0; i < candidates.size(); ++i) {
            float w = scores[i] > 0.0f ? scores[i] : 0.0f;
            r -= w;
            if (r <= 0.0f) return candidates[i];
        }
        return candidates.back();
    }

    class SimpleNN {
    private:
        std::vector<std::vector<float>> weights1;
        std::vector<std::vector<float>> weights2;
        std::vector<float> bias1, bias2;
        int inputSize, hiddenSize, outputSize;

        float relu(float x) { return std::max(0.0f, x); }
        float reluDeriv(float x) { return x > 0 ? 1.0f : 0.0f; }

    public:
        SimpleNN() : inputSize(10), hiddenSize(32), outputSize(8) {}

        SimpleNN(int in, int hidden, int out)
            : inputSize(in), hiddenSize(hidden), outputSize(out) {

            weights1.resize(hidden, std::vector<float>(in, 0.0f));
            weights2.resize(out, std::vector<float>(hidden, 0.0f));
            bias1.resize(hidden, 0.0f);
            bias2.resize(out, 0.0f);

            float scale1 = std::sqrt(2.0f / (in + hidden));
            float scale2 = std::sqrt(2.0f / (hidden + out));

            for (auto& row : weights1)
                for (float& w : row)
                    w = (randFloat() - 0.5f) * 2 * scale1;

            for (auto& row : weights2)
                for (float& w : row)
                    w = (randFloat() - 0.5f) * 2 * scale2;
        }

        std::vector<float> forward(const std::vector<float>& input) {
            std::vector<float> hidden(hiddenSize, 0.0f);
            for (int h = 0; h < hiddenSize; ++h) {
                float sum = bias1[h];
                for (int i = 0; i < (int)input.size() && i < inputSize; ++i)
                    sum += weights1[h][i] * input[i];
                hidden[h] = relu(sum);
            }

            std::vector<float> output(outputSize, 0.0f);
            for (int o = 0; o < outputSize; ++o) {
                float sum = bias2[o];
                for (int h = 0; h < hiddenSize; ++h)
                    sum += weights2[o][h] * hidden[h];
                output[o] = sum;
            }
            return output;
        }

        void train(const std::vector<float>& input, int action, float target, float lr = 0.01f) {
            if (action < 0 || action >= outputSize) return;

            std::vector<float> preHidden(hiddenSize, 0.0f);
            std::vector<float> hidden(hiddenSize, 0.0f);

            for (int h = 0; h < hiddenSize; ++h) {
                float sum = bias1[h];
                for (int i = 0; i < (int)input.size() && i < inputSize; ++i)
                    sum += weights1[h][i] * input[i];
                preHidden[h] = sum;
                hidden[h] = relu(sum);
            }

            std::vector<float> output(outputSize, 0.0f);
            for (int o = 0; o < outputSize; ++o) {
                float sum = bias2[o];
                for (int h = 0; h < hiddenSize; ++h)
                    sum += weights2[o][h] * hidden[h];
                output[o] = sum;
            }

            float error = target - output[action];

            if (!std::isfinite(error)) {
                return;
            }

            for (int h = 0; h < hiddenSize; ++h)
                weights2[action][h] += lr * error * hidden[h];
            bias2[action] += lr * error;

            for (int h = 0; h < hiddenSize; ++h) {
                float grad = error * weights2[action][h] * reluDeriv(preHidden[h]);
                if (!std::isfinite(grad)) continue;
                for (int i = 0; i < (int)input.size() && i < inputSize; ++i)
                    weights1[h][i] += lr * grad * input[i];
                bias1[h] += lr * grad;
            }
        }

        int chooseAction(const std::vector<float>& input, const std::vector<int>& actions, float epsilon = 0.1f) {
            if (actions.empty()) return -1;

            std::vector<int> validActions;
            validActions.reserve(actions.size());
            for (int a : actions) {
                if (a >= 0 && a < outputSize) {
                    validActions.push_back(a);
                }
            }
            if (validActions.empty()) return -1;

            if (randFloat() < epsilon) {
                return validActions[randInt((int)validActions.size())];
            }

            std::vector<float> qValues = forward(input);
            if (qValues.empty()) {
                return validActions[randInt((int)validActions.size())];
            }

            float best = -1e9f;
            int bestAction = validActions[0];

            for (int a : validActions) {
                if (a >= 0 && a < (int)qValues.size() && qValues[a] > best && std::isfinite(qValues[a])) {
                    best = qValues[a];
                    bestAction = a;
                }
            }
            return bestAction;
        }

        void save(std::ostream& os) const {
            os << inputSize << ' ' << hiddenSize << ' ' << outputSize << '\n';

            os << weights1.size() << ' ' << (weights1.empty() ? 0 : weights1[0].size()) << '\n';
            for (const auto& row : weights1) {
                for (float v : row) os << v << ' ';
                os << '\n';
            }

            os << weights2.size() << ' ' << (weights2.empty() ? 0 : weights2[0].size()) << '\n';
            for (const auto& row : weights2) {
                for (float v : row) os << v << ' ';
                os << '\n';
            }

            os << bias1.size() << '\n';
            for (float v : bias1) os << v << ' ';
            os << '\n';

            os << bias2.size() << '\n';
            for (float v : bias2) os << v << ' ';
            os << '\n';
        }

        bool load(std::istream& is) {
            int in, hid, out;
            if (!(is >> in >> hid >> out)) return false;

            inputSize = in;
            hiddenSize = hid;
            outputSize = out;

            size_t r, c;

            if (!(is >> r >> c)) return false;
            weights1.assign(r, std::vector<float>(c, 0.0f));
            for (size_t i = 0; i < r; ++i) {
                for (size_t j = 0; j < c; ++j) {
                    if (!(is >> weights1[i][j])) return false;
                }
            }

            if (!(is >> r >> c)) return false;
            weights2.assign(r, std::vector<float>(c, 0.0f));
            for (size_t i = 0; i < r; ++i) {
                for (size_t j = 0; j < c; ++j) {
                    if (!(is >> weights2[i][j])) return false;
                }
            }

            if (!(is >> r)) return false;
            bias1.assign(r, 0.0f);
            for (size_t i = 0; i < r; ++i) {
                if (!(is >> bias1[i])) return false;
            }

            if (!(is >> r)) return false;
            bias2.assign(r, 0.0f);
            for (size_t i = 0; i < r; ++i) {
                if (!(is >> bias2[i])) return false;
            }

            return true;
        }
    };

    int getTrustBucket(float trust) {
        if (trust < -0.3f) return 0;
        if (trust < 0.3f) return 1;
        return 2;
    }

    struct Relationship {
        float trust = 0.0f;
        float liking = 0.0f;
        int betrayal = 0;
        int consistency = 0;
        bool knownMafia = false;
        bool knownTown = false;
    };

    const int MAX_CONNECTIONS = 100;
    const int INIT_CONNECTIONS = 10;

    struct InternalAgentSnapshot
    {
        int32 AgentID = 0;
        int32 Role = ROLE_VILLAGER;
        bool bAlive = true;
        float Aggression = 0.0f;
        float Loyalty = 0.0f;
        float Paranoia = 0.0f;
        float Deceit = 0.0f;
    };
}

// This is the direct port of your main() logic into a function that fills Agents.
void UEdgeMafiaNPCSubsystem::RunMafiaSimulation(int32 n)
{
    if (n < 6)
    {
        UE_LOG(LogTemp, Warning, TEXT("Too few players (%d)."), n);
        return;
    }

    std::vector<FString> names(n);
    std::vector<int> alive(n, 1);
    std::vector<int> roles(n, ROLE_VILLAGER);

    std::vector<float> aggression(n);
    std::vector<float> loyalty(n);
    std::vector<float> paranoia(n);
    std::vector<float> deceit(n);

    std::vector<std::unordered_map<int, Relationship>> relations(n);

    const int STATE_SIZE = 10;
    const int HIDDEN_SIZE = 32;
    const int ACTION_SIZE = 8;

    std::vector<SimpleNN> brains;
    brains.reserve(n);
    for (int i = 0; i < n; ++i) {
        brains.emplace_back(STATE_SIZE, HIDDEN_SIZE, ACTION_SIZE);
    }
    std::vector<std::vector<float>> lastState(n);
    std::vector<int> lastAction(n, -1);

    std::vector<int> lastVoteTarget(n, -1);
    std::vector<int> attackedCount(n, 0);

    std::vector<std::vector<float>> probMafia(n, std::vector<float>(n, 0.0f));

    std::vector<int> signalType(n, -1);
    std::vector<int> signalTarget(n, -1);

    std::vector<float> totalReward(n, 0.0f);

    for (int i = 0; i < n; ++i) names[i] = FString::Printf(TEXT("P%d"), i);

    {
        int loadFlag = 1; // always try load in this port; you can expose as parameter later

        if (loadFlag == 1) {
            namespace fs = std::filesystem;
            fs::path saveDir = "/home/araivr/edgeMafiaNNqlearning/saves";
            fs::path savePath = saveDir / "mafia_agents_save.txt";

            std::ifstream ifs(savePath.string());
            if (ifs.is_open()) {
                std::string magic;
                ifs >> magic;
                if (magic != "MAFIA_SIM_SAVE_V2") {
                    UE_LOG(LogTemp, Warning, TEXT("Save file magic mismatch or old format. Starting fresh."));
                }
                else {
                    int savedN = 0;
                    ifs >> savedN;
                    UE_LOG(LogTemp, Log, TEXT("Save has %d agents. Current game has %d agents."), savedN, n);

                    int loadCount = std::min(savedN, (int)n);
                    for (int i = 0; i < savedN; ++i) {
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

                        size_t lastStateSize;
                        ifs >> lastStateSize;
                        for (size_t k = 0; k < lastStateSize; ++k) {
                            float tmp;
                            ifs >> tmp;
                        }

                        size_t probSize;
                        ifs >> probSize;
                        for (size_t k = 0; k < probSize; ++k) {
                            float tmp;
                            ifs >> tmp;
                        }

                        size_t relCount;
                        ifs >> relCount;
                        for (size_t r = 0; r < relCount; ++r) {
                            int other;
                            float trust, liking;
                            int betrayal, consistency;
                            bool knownMafia, knownTown;
                            ifs >> other >> trust >> liking >> betrayal >> consistency >> knownMafia >> knownTown;
                        }

                        if (i < n) {
                            if (!brains[i].load(ifs)) {
                                UE_LOG(LogTemp, Warning, TEXT("Warning: failed to load brain for agent %d"), i);
                            }
                            else {
                                if (!savedName.empty()) names[i] = FString(savedName.c_str());
                                alive[i] = alive_i;
                                roles[i] = role_i;
                                aggression[i] = aggr_i;
                                loyalty[i] = loy_i;
                                paranoia[i] = par_i;
                                deceit[i] = dec_i;
                                totalReward[i] = totalReward_i;
                                lastAction[i] = lastAction_i;
                                lastVoteTarget[i] = lastVote_i;
                                attackedCount[i] = attacked_i;
                            }
                        }
                        else {
                            SimpleNN tmp;
                            tmp.load(ifs);
                        }
                    }

                    if (savedN < n) {
                        UE_LOG(LogTemp, Log, TEXT("Loaded %d brains, %d new agents start fresh."), savedN, n - savedN);
                    }
                    else if (savedN > n) {
                        UE_LOG(LogTemp, Log, TEXT("Loaded %d brains, %d extra agents discarded."), n, savedN - n);
                    }
                    else {
                        UE_LOG(LogTemp, Log, TEXT("Loaded brains for all %d agents."), n);
                    }
                }
            }
            else {
                UE_LOG(LogTemp, Log, TEXT("No previous save found. Starting fresh."));
            }
        }
        else {
            UE_LOG(LogTemp, Log, TEXT("Starting fresh (no load)."));
        }
    }

    int mafiaCount = std::max(1, n / 4);
    int doctorCount = (n >= 7) ? 1 : 0;
    int detectiveCount = (n >= 7) ? 1 : 0;

    std::vector<int> rolePool;
    rolePool.reserve(n);
    for (int i = 0; i < mafiaCount; ++i) rolePool.push_back(ROLE_MAFIA);
    for (int i = 0; i < doctorCount; ++i) rolePool.push_back(ROLE_DOCTOR);
    for (int i = 0; i < detectiveCount; ++i) rolePool.push_back(ROLE_DETECTIVE);
    while ((int)rolePool.size() < n) rolePool.push_back(ROLE_VILLAGER);

    std::shuffle(rolePool.begin(), rolePool.end(), rng);
    for (int i = 0; i < n; ++i) roles[i] = rolePool[i];

    float baseProb = (float)mafiaCount / std::max(1, n - 1);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == j) probMafia[i][j] = 0.0f;
            else probMafia[i][j] = baseProb;
        }
    }

    for (int i = 0; i < n; ++i) {
        float r1 = randFloat();
        float r2 = randFloat();
        float r3 = randFloat();
        float r4 = randFloat();

        float baseAgg[4] = { 0.7f, 0.2f, 0.3f, 0.0f };
        float spanAgg[4] = { 0.3f, 0.4f, 0.3f, 0.2f };

        float baseLoy[4] = { 0.4f, 0.4f, 0.6f, 0.7f };
        float spanLoy[4] = { 0.3f, 0.4f, 0.3f, 0.3f };

        float basePar[4] = { 0.4f, 0.3f, 0.7f, 0.5f };
        float spanPar[4] = { 0.4f, 0.4f, 0.3f, 0.3f };

        float baseDec[4] = { 0.7f, 0.1f, 0.3f, 0.2f };
        float spanDec[4] = { 0.3f, 0.3f, 0.3f, 0.3f };

        int r = roles[i];
        int k = (r == ROLE_MAFIA ? 0 :
            r == ROLE_VILLAGER ? 1 :
            r == ROLE_DETECTIVE ? 2 : 3);

        aggression[i] = baseAgg[k] + spanAgg[k] * r1;
        loyalty[i] = baseLoy[k] + spanLoy[k] * r2;
        paranoia[i] = basePar[k] + spanPar[k] * r3;
        deceit[i] = baseDec[k] + spanDec[k] * r4;
    }

    auto pruneConnections = [&](int a) {
        auto& m = relations[a];
        if ((int)m.size() <= MAX_CONNECTIONS) return;
        while ((int)m.size() > MAX_CONNECTIONS) {
            int worstId = -1;
            float worstTrust = 1e9f;
            for (auto& kv : m) {
                if (kv.second.trust < worstTrust) {
                    worstTrust = kv.second.trust;
                    worstId = kv.first;
                }
            }
            if (worstId != -1) m.erase(worstId);
            else break;
        }
        };

    auto ensureRel = [&](int a, int b) -> Relationship& {
        auto& m = relations[a];
        auto it = m.find(b);
        if (it == m.end()) {
            Relationship r;
            auto res = m.emplace(b, r);
            pruneConnections(a);
            return res.first->second;
        }
        return it->second;
        };

    auto getTrust = [&](int a, int b) -> float {
        auto& m = relations[a];
        auto it = m.find(b);
        if (it == m.end()) return 0.0f;
        return it->second.trust;
        };

    auto getLiking = [&](int a, int b) -> float {
        auto& m = relations[a];
        auto it = m.find(b);
        if (it == m.end()) return 0.0f;
        return it->second.liking;
        };

    auto getBetrayal = [&](int a, int b) -> int {
        auto& m = relations[a];
        auto it = m.find(b);
        if (it == m.end()) return 0;
        return it->second.betrayal;
        };

    auto getConsistency = [&](int a, int b) -> int {
        auto& m = relations[a];
        auto it = m.find(b);
        if (it == m.end()) return 0;
        return it->second.consistency;
        };

    auto knowsMafia = [&](int a, int b) -> bool {
        auto& m = relations[a];
        auto it = m.find(b);
        if (it == m.end()) return false;
        return it->second.knownMafia;
        };

    auto knowsTown = [&](int a, int b) -> bool {
        auto& m = relations[a];
        auto it = m.find(b);
        if (it == m.end()) return false;
        return it->second.knownTown;
        };

    for (int i = 0; i < n; ++i) {
        int connections = std::min(INIT_CONNECTIONS, n - 1);
        for (int c = 0; c < connections; ++c) {
            int j = randInt(n);
            if (j == i) continue;
            Relationship& rel = ensureRel(i, j);
            float base = (randFloat() - 0.5f) * 0.4f;
            rel.trust = base;
            rel.liking = base;
        }
    }

    for (int i = 0; i < n; ++i) {
        if (roles[i] != ROLE_MAFIA) continue;
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            if (roles[j] == ROLE_MAFIA) {
                Relationship& rel = ensureRel(i, j);
                rel.trust += 0.5f;
                rel.liking += 0.3f;
                if (rel.trust > 1.0f) rel.trust = 1.0f;
                if (rel.trust < -1.0f) rel.trust = -1.0f;
                if (rel.liking > 1.0f) rel.liking = 1.0f;
                if (rel.liking < -1.0f) rel.liking = -1.0f;
            }
        }
    }

    auto isAlly = [&](int a, int b) {
        float t = getTrust(a, b);
        float l = getLiking(a, b);
        return t > 0.5f && l > 0.3f;
        };

    auto mafiaWin = [&]() {
        int m = 0, t = 0;
        for (int i = 0; i < n; ++i)
            if (alive[i])
                (roles[i] == ROLE_MAFIA ? m : t)++;
        return m > 0 && m >= t;
        };

    auto townWin = [&]() {
        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_MAFIA)
                return false;
        return true;
        };

    auto decayRelations = [&]() {
        for (int i = 0; i < n; ++i) {
            float d = 0.01f * (0.5f + paranoia[i]);
            for (auto& kv : relations[i]) {
                float& t = kv.second.trust;
                float s = (t > 0.0f ? 1.0f : 0.2f);
                t -= d * s;
                if (t > 1.0f) t = 1.0f;
                if (t < -1.0f) t = -1.0f;
            }
        }
        };

    auto chooseMafiaTarget = [&](int self) {
        std::vector<int> cand;
        std::vector<float> score;
        cand.reserve(n);
        score.reserve(n);

        float A = aggression[self];
        float D = deceit[self];

        for (int j = 0; j < n; ++j) {
            if (!alive[j] || j == self) continue;

            float t = getTrust(self, j);
            float l = getLiking(self, j);

            float susp = -(t + l) * (0.5f + A);
            float betray = (roles[j] == ROLE_MAFIA && t < -0.4f) ? (0.5f * D) : 0.0f;

            float s = susp + betray;
            if (s > 0.0f && std::isfinite(s)) {
                cand.push_back(j);
                score.push_back(std::max(0.01f, s));
            }
        }
        return weightedChoice(cand, score);
        };

    auto chooseDoctorSave = [&](int self) {
        std::vector<int> cand;
        std::vector<float> score;
        cand.reserve(n);
        score.reserve(n);

        float L = loyalty[self];

        int detectiveId = -1;
        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_DETECTIVE)
                detectiveId = i;

        for (int j = 0; j < n; ++j) {
            if (!alive[j]) continue;
            float base = 0.1f;
            float ally = isAlly(self, j) ? (0.6f * L) : 0.0f;
            float likeTerm = 0.2f * (getLiking(self, j) + 1.0f) * 0.5f;
            float attackedTerm = 0.3f * attackedCount[j];

            if (j == detectiveId) {
                base += 1.0f;
            }

            float s = base + ally + likeTerm + attackedTerm;
            if (!std::isfinite(s)) s = 0.1f;
            cand.push_back(j);
            score.push_back(std::max(0.01f, s));
        }
        return weightedChoice(cand, score);
        };

    auto chooseDetectiveCheck = [&](int self) {
        std::vector<int> cand;
        std::vector<float> score;
        cand.reserve(n);
        score.reserve(n);

        float P = paranoia[self];

        for (int j = 0; j < n; ++j) {
            if (!alive[j] || j == self) continue;
            if (knowsMafia(self, j) || knowsTown(self, j)) continue;
            float t = getTrust(self, j);
            float s = -t * (0.5f + P);
            if (s <= 0.0f) s = 0.05f;
            if (!std::isfinite(s)) s = 0.05f;
            cand.push_back(j);
            score.push_back(std::max(0.01f, s));
        }

        if (cand.empty()) {
            for (int j = 0; j < n; ++j) {
                if (!alive[j] || j == self) continue;
                float t = getTrust(self, j);
                float s = -t * (0.5f + P);
                if (s <= 0.0f) s = 0.05f;
                if (!std::isfinite(s)) s = 0.05f;
                cand.push_back(j);
                score.push_back(std::max(0.01f, s));
            }
        }

        return weightedChoice(cand, score);
        };

    auto chooseLynchTarget = [&](int self) {
        std::vector<int> cand;
        cand.reserve(n);

        for (int j = 0; j < n; ++j) {
            if (!alive[j] || j == self) continue;
            cand.push_back(j);
        }

        if (cand.empty()) {
            lastState[self].clear();
            lastAction[self] = -1;
            return -1;
        }

        float P = paranoia[self];
        float L = loyalty[self];
        float D = deceit[self];

        int primaryTarget = cand[0];
        float bestHeuristic = -1e9f;

        for (int j : cand) {
            float t = getTrust(self, j);
            float base = -t * (0.7f + P);
            float ally = isAlly(self, j) ? 1.0f : 0.0f;
            float allyFactor = (1.0f - 0.7f * L * ally);
            float betrayal = (ally && t < 0.0f) ? (0.3f * D) : 0.0f;
            float s = base * allyFactor + betrayal;

            float belief = clampf(probMafia[self][j], 0.0f, 1.0f);
            s += 2.0f * belief;

            if (!std::isfinite(s)) s = 0.05f;
            if (s > bestHeuristic) {
                bestHeuristic = s;
                primaryTarget = j;
            }
        }

        float tPrimary = getTrust(self, primaryTarget);
        int betrayalFlag = (getBetrayal(self, primaryTarget) > 0) ? 1 : 0;
        int consistencyFlag = (getConsistency(self, primaryTarget) > 0) ? 1 : 0;
        float beliefPrimary = clampf(probMafia[self][primaryTarget], 0.0f, 1.0f);
        int detectiveFlag = (roles[self] == ROLE_DETECTIVE && knowsMafia(self, primaryTarget)) ? 1 : 0;

        float avgBelief = 0.0f;
        int cntBelief = 0;
        for (int j : cand) {
            avgBelief += clampf(probMafia[self][j], 0.0f, 1.0f);
            cntBelief++;
        }
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

        lastState[self] = state;

        const int MAX_TARGETS = ACTION_SIZE - 1;
        std::vector<int> idxMap;
        idxMap.reserve(MAX_TARGETS);
        for (int k = 0; k < (int)cand.size() && (int)idxMap.size() < MAX_TARGETS; ++k) {
            idxMap.push_back(cand[k]);
        }

        std::vector<int> actions;
        actions.push_back(0);
        for (int a = 1; a <= (int)idxMap.size(); ++a) {
            actions.push_back(a);
        }

        float epsilon = 0.1f;
        int action = brains[self].chooseAction(state, actions, epsilon);
        lastAction[self] = action;

        if (action == 0) {
            return -1;
        }

        int chosenIndex = action - 1;
        if (chosenIndex < 0 || chosenIndex >= (int)idxMap.size()) {
            return -1;
        }

        int target = idxMap[chosenIndex];
        return target;
        };

    auto updateTrustAfterLynch = [&](int lyncher, int target, bool wasMafia) {
        float delta = wasMafia ? 0.15f : -0.15f;
        for (int i = 0; i < n; ++i) {
            if (!alive[i] || i == lyncher) continue;
            Relationship& rel = ensureRel(i, lyncher);
            float& t = rel.trust;
            t += delta;
            if (t > 1.0f) t = 1.0f;
            if (t < -1.0f) t = -1.0f;
        }
        if (roles[lyncher] == ROLE_MAFIA && roles[target] == ROLE_MAFIA) {
            Relationship& rel = ensureRel(lyncher, target);
            float& t = rel.trust;
            t -= 0.4f;
            if (t < -1.0f) t = -1.0f;
        }
        };

    auto nightPhase = [&](int cycle) {
        int mafiaKill = -1;
        int doctorSave = -1;
        int detectiveCheck = -1;
        int detectiveId = -1;

        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_MAFIA) {
                mafiaKill = chooseMafiaTarget(i);
                break;
            }

        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            if (roles[i] == ROLE_DOCTOR) doctorSave = chooseDoctorSave(i);
            if (roles[i] == ROLE_DETECTIVE) {
                detectiveCheck = chooseDetectiveCheck(i);
                detectiveId = i;
            }
        }

        if (mafiaKill != -1 && mafiaKill != doctorSave) {
            alive[mafiaKill] = 0;
            attackedCount[mafiaKill]++;
        }

        if (detectiveCheck != -1 && detectiveId != -1) {
            Relationship& rel = ensureRel(detectiveId, detectiveCheck);
            if (roles[detectiveCheck] == ROLE_MAFIA) {
                rel.knownMafia = true;
                paranoia[detectiveId] = std::min(1.0f, paranoia[detectiveId] + 0.1f);
                rel.trust -= 0.7f;
                if (rel.trust < -1.0f) rel.trust = -1.0f;
                probMafia[detectiveId][detectiveCheck] = 0.95f;
            }
            else {
                rel.knownTown = true;
                rel.trust += 0.4f;
                if (rel.trust > 1.0f) rel.trust = 1.0f;
                probMafia[detectiveId][detectiveCheck] = 0.01f;
            }
        }

        decayRelations();
        };

    auto dayPhase = [&](int cycle) {
        std::vector<int> votes(n, -1);
        std::vector<int> voteCount(n, 0);

        for (int i = 0; i < n; ++i)
            if (alive[i]) {
                int t = chooseLynchTarget(i);
                votes[i] = t;
            }

        for (int i = 0; i < n; ++i) {
            signalType[i] = -1;
            signalTarget[i] = -1;
        }

        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            if (roles[i] == ROLE_MAFIA) continue;

            if (votes[i] != -1 && isAlly(i, votes[i]) && !knowsMafia(i, votes[i])) {
                votes[i] = -1;
            }

            if (randFloat() < 0.6f) {
                int bestAlly = -1;
                float bestTrust = -1.0f;
                for (int j = 0; j < n; ++j) {
                    if (!alive[j] || j == i) continue;
                    float t = getTrust(i, j);
                    if (t > bestTrust) {
                        bestTrust = t;
                        bestAlly = j;
                    }
                }
                if (bestAlly != -1 && votes[bestAlly] != -1) {
                    signalType[i] = 1;
                    signalTarget[i] = votes[bestAlly];
                }
            }
        }

        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            int v = votes[i];
            if (v >= 0 && v < n) {
                voteCount[v]++;
                lastVoteTarget[i] = v;
            }
        }

        int lynched = -1;
        int bestVotes = 0;
        for (int i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            if (voteCount[i] > bestVotes) {
                bestVotes = voteCount[i];
                lynched = i;
            }
        }

        if (lynched != -1) {
            bool wasMafia = (roles[lynched] == ROLE_MAFIA);
            alive[lynched] = 0;

            for (int i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                if (lastAction[i] < 0 || lastState[i].empty()) continue;

                float reward = 0.0f;
                if (wasMafia && lastVoteTarget[i] == lynched) {
                    reward = 1.0f;
                }
                else if (!wasMafia && lastVoteTarget[i] == lynched) {
                    reward = -1.0f;
                }
                else {
                    reward = 0.0f;
                }

                totalReward[i] += reward;

                std::vector<float> qValues = brains[i].forward(lastState[i]);
                float oldQ = 0.0f;
                if (lastAction[i] >= 0 && lastAction[i] < (int)qValues.size()) {
                    oldQ = qValues[lastAction[i]];
                }
                float target = oldQ + 0.1f * (reward - oldQ);
                brains[i].train(lastState[i], lastAction[i], target, 0.01f);
            }

            for (int i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                if (lastVoteTarget[i] == lynched) {
                    updateTrustAfterLynch(i, lynched, wasMafia);
                }
            }
        }
        };

    int cycle = 1;
    while (true) {
        nightPhase(cycle);
        if (mafiaWin() || townWin()) break;
        dayPhase(cycle);
        if (mafiaWin() || townWin()) break;
        cycle++;
        if (cycle > 50) break;
    }

    bool mafiaWon = mafiaWin();
    bool townWon = townWin();

    if (mafiaWon) {
        UE_LOG(LogTemp, Log, TEXT("Mafia win!"));
    }
    else if (townWon) {
        UE_LOG(LogTemp, Log, TEXT("Town win!"));
    }
    else {
        UE_LOG(LogTemp, Log, TEXT("Game ended without clear winner."));
    }

    {
        namespace fs = std::filesystem;
        fs::path saveDir = "/home/araivr/edgeMafiaNNqlearning/saves";
        fs::create_directories(saveDir);
        fs::path savePath = saveDir / "mafia_agents_save.txt";

        std::ofstream ofs(savePath.string());
        if (ofs.is_open()) {
            ofs << "MAFIA_SIM_SAVE_V2\n";
            ofs << n << "\n";
            for (int i = 0; i < n; ++i) {
                ofs << "AGENT " << i << "\n";
                std::string nameStd(TCHAR_TO_UTF8(*names[i]));
                ofs << nameStd << "\n";
                ofs << alive[i] << " " << roles[i] << "\n";
                ofs << aggression[i] << " " << loyalty[i] << " " << paranoia[i] << " " << deceit[i] << "\n";
                ofs << totalReward[i] << "\n";
                ofs << lastAction[i] << " " << lastVoteTarget[i] << " " << attackedCount[i] << "\n";

                ofs << lastState[i].size() << "\n";
                for (float v : lastState[i]) ofs << v << " ";
                ofs << "\n";

                ofs << probMafia[i].size() << "\n";
                for (float v : probMafia[i]) ofs << v << " ";
                ofs << "\n";

                auto& m = relations[i];
                ofs << m.size() << "\n";
                for (auto& kv : m) {
                    ofs << kv.first << " "
                        << kv.second.trust << " "
                        << kv.second.liking << " "
                        << kv.second.betrayal << " "
                        << kv.second.consistency << " "
                        << kv.second.knownMafia << " "
                        << kv.second.knownTown << "\n";
                }

                brains[i].save(ofs);
            }
        }
    }

    for (int32 i = 0; i < n; ++i)
    {
        FEdgeMafiaAgent& Out = Agents[i];
        Out.AgentID = i;
        Out.Role = roles[i];
        Out.bAlive = (alive[i] != 0);
        Out.Aggression = aggression[i];
        Out.Loyalty = loyalty[i];
        Out.Paranoia = paranoia[i];
        Out.Deceit = deceit[i];
    }
}
