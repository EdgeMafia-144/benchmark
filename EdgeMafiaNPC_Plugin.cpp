// =============================================================================
// FILE LAYOUT  (copy each section to its matching path inside your plugin dir)
//
//  EdgeMafiaPlugin.uplugin
//  Source/EdgeMafiaNPC/EdgeMafiaNPC.Build.cs
//  Source/EdgeMafiaNPC/Public/EdgeMafiaNPCTypes.h
//  Source/EdgeMafiaNPC/Public/EdgeMafiaNPCSubsystem.h
//  Source/EdgeMafiaNPC/Private/EdgeMafiaNPCModule.cpp
//  Source/EdgeMafiaNPC/Private/EdgeMafiaNPCSubsystem.cpp
//
// WHY TWO SAVE FILES?
//   mafia_agents_save.txt  – agent state + relations  (Python-parseable)
//   mafia_brains_save.txt  – NN weights               (loaded back by UE only)
//
// The Python parser splits the ENTIRE agents file on whitespace, then for each
// AGENT block scans backwards to find relCount where (remaining % 7 == 0).
// If brain floats appear after relations they produce false matches.
// Separating brains into their own file makes the backward scan unambiguous.
//
// AGENTS FILE TOKEN LAYOUT PER AGENT (positions in block[]):
//   [0]  "AGENT"
//   [1]  id          <- int
//   [2]  name        <- string  (no spaces – "P0", "P1" …)
//   [3]  alive       <- 0 / 1
//   [4]  role        <- 0=Mafia 1=Villager 2=Doctor 3=Detective
//   [5]  aggression  <- float
//   [6]  loyalty     <- float
//   [7]  paranoia    <- float
//   [8]  deceit      <- float
//   [9]  reward      <- float
//   [10] lastAction  <- int
//   [11] lastVoteTarget <- int
//   [12] attackedCount  <- int
//   [13] relCount       <- int   (Python backward scan lands here)
//   [14 … 14+relCount*7-1]  relations: other trust liking betrayal consistency knownMafia knownTown
// =============================================================================


// ─────────────────────────────────────────────────────────────────────────────
// EdgeMafiaPlugin.uplugin
// ─────────────────────────────────────────────────────────────────────────────
/*
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0",
    "FriendlyName": "EdgeMafia NPC",
    "Description": "Mafia game-theory NPC subsystem with Q-learning agents.",
    "Category": "Gameplay/AI",
    "CreatedBy": "EdgeMafia",
    "CanContainContent": false,
    "IsBetaVersion": false,
    "IsExperimentalVersion": false,
    "Installed": false,
    "Modules": [
        {
            "Name": "EdgeMafiaNPC",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        }
    ]
}
*/


// ─────────────────────────────────────────────────────────────────────────────
// Source/EdgeMafiaNPC/EdgeMafiaNPC.Build.cs
// ─────────────────────────────────────────────────────────────────────────────
/*
using UnrealBuildTool;

public class EdgeMafiaNPC : ModuleRules
{
    public EdgeMafiaNPC(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects"
        });

        // C++17 for std::filesystem used inside the simulation
        CppStandard = CppStandardVersion.Cpp17;
    }
}
*/


// ─────────────────────────────────────────────────────────────────────────────
// Source/EdgeMafiaNPC/Public/EdgeMafiaNPCTypes.h
// ─────────────────────────────────────────────────────────────────────────────

/*
#pragma once

#include "CoreMinimal.h"
#include "EdgeMafiaNPCTypes.generated.h"

UENUM(BlueprintType)
enum class EEdgeMafiaRole : uint8
{
    Mafia      UMETA(DisplayName = "Mafia"),
    Villager   UMETA(DisplayName = "Villager"),
    Doctor     UMETA(DisplayName = "Doctor"),
    Detective  UMETA(DisplayName = "Detective"),
};

USTRUCT(BlueprintType)
struct EDGEMAFIANPC_API FEdgeMafiaRelation
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Relation")
    int32 OtherAgentID = -1;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Relation")
    float Trust = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Relation")
    float Liking = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Relation")
    int32 BetrayalCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Relation")
    int32 ConsistencyCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Relation")
    bool bKnownMafia = false;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Relation")
    bool bKnownTown = false;
};

USTRUCT(BlueprintType)
struct EDGEMAFIANPC_API FEdgeMafiaAgent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    int32 AgentID = 0;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    int32 Role = 1;          // raw int matches save file; cast to EEdgeMafiaRole as needed

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    bool bAlive = true;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    float Aggression = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    float Loyalty = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    float Paranoia = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    float Deceit = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    float TotalReward = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    int32 LastAction = -1;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    int32 LastVoteTarget = -1;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    int32 AttackedCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia|Agent")
    TArray<FEdgeMafiaRelation> Relations;
};
*/


// ─────────────────────────────────────────────────────────────────────────────
// Source/EdgeMafiaNPC/Public/EdgeMafiaNPCSubsystem.h
// ─────────────────────────────────────────────────────────────────────────────

/*
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EdgeMafiaNPCTypes.h"
#include "EdgeMafiaNPCSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimulationComplete, bool, bMafiaWon);

UCLASS()
class EDGEMAFIANPC_API UEdgeMafiaNPCSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Blueprint API ──────────────────────────────────────────────────────────

    // Run a full mafia simulation with NumAgents players.
    // Populates Agents array and fires OnSimulationComplete when done.
    UFUNCTION(BlueprintCallable, Category = "EdgeMafia")
    void RunSimulation(int32 NumAgents = 20);

    // Access a specific agent by its ID after a simulation run.
    UFUNCTION(BlueprintCallable, Category = "EdgeMafia")
    const FEdgeMafiaAgent& GetAgent(int32 AgentID) const;

    // Total agents in the last completed run.
    UFUNCTION(BlueprintPure, Category = "EdgeMafia")
    int32 GetAgentCount() const { return Agents.Num(); }

    // Fires when RunSimulation finishes. Payload: true = Mafia won.
    UPROPERTY(BlueprintAssignable, Category = "EdgeMafia")
    FOnSimulationComplete OnSimulationComplete;

    // Read-only snapshot of all agents after the last run.
    UPROPERTY(BlueprintReadOnly, Category = "EdgeMafia")
    TArray<FEdgeMafiaAgent> Agents;

private:
    // Core simulation entry point (fills Agents)
    void RunMafiaSimulation(int32 n);

    // Resolved save directory (under ProjectSavedDir)
    FString GetSaveDirectory() const;
};
*/


// ─────────────────────────────────────────────────────────────────────────────
// Source/EdgeMafiaNPC/Private/EdgeMafiaNPCModule.cpp
// ─────────────────────────────────────────────────────────────────────────────

/*
#include "Modules/ModuleManager.h"

class FEdgeMafiaNPCModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FEdgeMafiaNPCModule, EdgeMafiaNPC)
*/


// ─────────────────────────────────────────────────────────────────────────────
// Source/EdgeMafiaNPC/Private/EdgeMafiaNPCSubsystem.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "EdgeMafiaNPCSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"

// STL
#include <random>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <unordered_map>
#include <functional>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void UEdgeMafiaNPCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UEdgeMafiaNPCSubsystem::Deinitialize()
{
    Super::Deinitialize();
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

FString UEdgeMafiaNPCSubsystem::GetSaveDirectory() const
{
    // Saves land in  <Project>/Saved/EdgeMafia/
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EdgeMafia"));
}

// =============================================================================
//  INTERNAL SIMULATION  (anonymous namespace keeps symbols file-local)
// =============================================================================

namespace
{
    // ── Role constants ────────────────────────────────────────────────────────
    enum Role { ROLE_MAFIA = 0, ROLE_VILLAGER = 1, ROLE_DOCTOR = 2, ROLE_DETECTIVE = 3 };

    // ── Global RNG ────────────────────────────────────────────────────────────
    static std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));

    inline int randInt(int maxVal)
    {
        if (maxVal <= 0) return 0;
        std::uniform_int_distribution<int> d(0, maxVal - 1);
        return d(rng);
    }

    inline float randFloat()
    {
        std::uniform_real_distribution<float> d(0.f, 1.f);
        return d(rng);
    }

    inline float clampf(float x, float lo, float hi)
    {
        return x < lo ? lo : (x > hi ? hi : x);
    }

    // ── Weighted choice ───────────────────────────────────────────────────────
    int weightedChoice(const std::vector<int>& cands, const std::vector<float>& scores)
    {
        if (cands.empty()) return -1;
        if (scores.size() != cands.size())
            return cands[randInt(static_cast<int>(cands.size()))];

        float sum = 0.f;
        for (float s : scores) if (s > 0.f) sum += s;

        if (sum <= 0.f || !std::isfinite(sum))
            return cands[randInt(static_cast<int>(cands.size()))];

        float r = randFloat() * sum;
        for (size_t i = 0; i < cands.size(); ++i)
        {
            float w = scores[i] > 0.f ? scores[i] : 0.f;
            r -= w;
            if (r <= 0.f) return cands[i];
        }
        return cands.back();
    }

    // ── Shallow two-layer neural network (Q-value approximator) ──────────────
    class SimpleNN
    {
    public:
        // Default-constructed nets are valid but untrained
        SimpleNN() : inputSize(10), hiddenSize(32), outputSize(8) {}

        SimpleNN(int in, int hidden, int out)
            : inputSize(in), hiddenSize(hidden), outputSize(out)
        {
            weights1.assign(hidden, std::vector<float>(in, 0.f));
            weights2.assign(out,    std::vector<float>(hidden, 0.f));
            bias1.assign(hidden, 0.f);
            bias2.assign(out,    0.f);

            float s1 = std::sqrt(2.f / (in + hidden));
            float s2 = std::sqrt(2.f / (hidden + out));

            for (auto& row : weights1) for (float& w : row) w = (randFloat() - .5f) * 2.f * s1;
            for (auto& row : weights2) for (float& w : row) w = (randFloat() - .5f) * 2.f * s2;
        }

        // Forward pass → Q-value vector
        std::vector<float> forward(const std::vector<float>& input) const
        {
            std::vector<float> h(hiddenSize, 0.f);
            for (int hh = 0; hh < hiddenSize; ++hh)
            {
                float s = bias1[hh];
                for (int i = 0; i < static_cast<int>(input.size()) && i < inputSize; ++i)
                    s += weights1[hh][i] * input[i];
                h[hh] = std::max(0.f, s);            // ReLU
            }

            std::vector<float> out(outputSize, 0.f);
            for (int o = 0; o < outputSize; ++o)
            {
                float s = bias2[o];
                for (int hh = 0; hh < hiddenSize; ++hh)
                    s += weights2[o][hh] * h[hh];
                out[o] = s;
            }
            return out;
        }

        // Single-step Q-learning update
        void train(const std::vector<float>& input, int action, float target, float lr = 0.01f)
        {
            if (action < 0 || action >= outputSize) return;

            // Forward with cache
            std::vector<float> preH(hiddenSize, 0.f);
            std::vector<float> hid(hiddenSize, 0.f);

            for (int hh = 0; hh < hiddenSize; ++hh)
            {
                float s = bias1[hh];
                for (int i = 0; i < static_cast<int>(input.size()) && i < inputSize; ++i)
                    s += weights1[hh][i] * input[i];
                preH[hh] = s;
                hid[hh]  = std::max(0.f, s);
            }

            std::vector<float> qOut(outputSize, 0.f);
            for (int o = 0; o < outputSize; ++o)
            {
                float s = bias2[o];
                for (int hh = 0; hh < hiddenSize; ++hh)
                    s += weights2[o][hh] * hid[hh];
                qOut[o] = s;
            }

            float error = target - qOut[action];
            if (!std::isfinite(error)) return;

            // Output layer
            for (int hh = 0; hh < hiddenSize; ++hh) weights2[action][hh] += lr * error * hid[hh];
            bias2[action] += lr * error;

            // Hidden layer
            for (int hh = 0; hh < hiddenSize; ++hh)
            {
                float grad = error * weights2[action][hh] * (preH[hh] > 0.f ? 1.f : 0.f);
                if (!std::isfinite(grad)) continue;
                for (int i = 0; i < static_cast<int>(input.size()) && i < inputSize; ++i)
                    weights1[hh][i] += lr * grad * input[i];
                bias1[hh] += lr * grad;
            }
        }

        // ε-greedy action selection
        int chooseAction(const std::vector<float>& input,
                         const std::vector<int>& actions,
                         float epsilon = 0.1f) const
        {
            if (actions.empty()) return -1;

            // Filter to valid action indices
            std::vector<int> valid;
            valid.reserve(actions.size());
            for (int a : actions)
                if (a >= 0 && a < outputSize) valid.push_back(a);
            if (valid.empty()) return -1;

            if (randFloat() < epsilon)
                return valid[randInt(static_cast<int>(valid.size()))];

            std::vector<float> q = forward(input);
            float best = -1e9f;
            int   bestA = valid[0];
            for (int a : valid)
            {
                if (a < static_cast<int>(q.size()) && q[a] > best && std::isfinite(q[a]))
                {
                    best  = q[a];
                    bestA = a;
                }
            }
            return bestA;
        }

        // ── Serialisation ─────────────────────────────────────────────────────
        void save(std::ostream& os) const
        {
            os << inputSize << ' ' << hiddenSize << ' ' << outputSize << '\n';

            os << weights1.size() << ' '
               << (weights1.empty() ? 0 : weights1[0].size()) << '\n';
            for (const auto& row : weights1) { for (float v : row) os << v << ' '; os << '\n'; }

            os << weights2.size() << ' '
               << (weights2.empty() ? 0 : weights2[0].size()) << '\n';
            for (const auto& row : weights2) { for (float v : row) os << v << ' '; os << '\n'; }

            os << bias1.size() << '\n';
            for (float v : bias1) os << v << ' ';
            os << '\n';

            os << bias2.size() << '\n';
            for (float v : bias2) os << v << ' ';
            os << '\n';
        }

        bool load(std::istream& is)
        {
            int in, hid, out;
            if (!(is >> in >> hid >> out)) return false;
            inputSize  = in;
            hiddenSize = hid;
            outputSize = out;

            size_t r, c;

            if (!(is >> r >> c)) return false;
            weights1.assign(r, std::vector<float>(c, 0.f));
            for (size_t i = 0; i < r; ++i)
                for (size_t j = 0; j < c; ++j)
                    if (!(is >> weights1[i][j])) return false;

            if (!(is >> r >> c)) return false;
            weights2.assign(r, std::vector<float>(c, 0.f));
            for (size_t i = 0; i < r; ++i)
                for (size_t j = 0; j < c; ++j)
                    if (!(is >> weights2[i][j])) return false;

            if (!(is >> r)) return false;
            bias1.assign(r, 0.f);
            for (size_t i = 0; i < r; ++i) if (!(is >> bias1[i])) return false;

            if (!(is >> r)) return false;
            bias2.assign(r, 0.f);
            for (size_t i = 0; i < r; ++i) if (!(is >> bias2[i])) return false;

            return true;
        }

    private:
        int inputSize, hiddenSize, outputSize;
        std::vector<std::vector<float>> weights1, weights2;
        std::vector<float> bias1, bias2;
    };

    // ── Relationship record ───────────────────────────────────────────────────
    struct Relationship
    {
        float trust       = 0.f;
        float liking      = 0.f;
        int   betrayal    = 0;
        int   consistency = 0;
        bool  knownMafia  = false;
        bool  knownTown   = false;
    };

    const int MAX_CONNECTIONS  = 100;
    const int INIT_CONNECTIONS = 10;
    const int STATE_SIZE       = 10;
    const int HIDDEN_SIZE      = 32;
    const int ACTION_SIZE      = 8;

} // anonymous namespace


// =============================================================================
//  RunMafiaSimulation  – main entry point
// =============================================================================

void UEdgeMafiaNPCSubsystem::RunMafiaSimulation(int32 n)
{
    if (n < 6)
    {
        UE_LOG(LogTemp, Warning, TEXT("[EdgeMafia] Need at least 6 agents, got %d."), n);
        return;
    }

    // ── Working arrays ────────────────────────────────────────────────────────
    std::vector<std::string>   names(n);
    std::vector<int>           alive(n, 1);
    std::vector<int>           roles(n, ROLE_VILLAGER);
    std::vector<float>         aggression(n), loyalty(n), paranoia(n), deceit(n);
    std::vector<float>         totalReward(n, 0.f);
    std::vector<int>           lastVoteTarget(n, -1);
    std::vector<int>           attackedCount(n, 0);
    std::vector<int>           lastAction(n, -1);
    std::vector<std::vector<float>> lastState(n);
    std::vector<std::vector<float>> probMafia(n, std::vector<float>(n, 0.f));
    std::vector<int>           signalType(n, -1);
    std::vector<int>           signalTarget(n, -1);
    std::vector<std::unordered_map<int, Relationship>> relations(n);

    // Allocate brains
    std::vector<SimpleNN> brains;
    brains.reserve(n);
    for (int i = 0; i < n; ++i)
        brains.emplace_back(STATE_SIZE, HIDDEN_SIZE, ACTION_SIZE);

    // Default names
    for (int i = 0; i < n; ++i)
        names[i] = "P" + std::to_string(i);

    // ── Resolve save paths (UE ProjectSavedDir) ───────────────────────────────
    const FString SaveDir    = GetSaveDirectory();
    const FString AgentPath  = FPaths::Combine(SaveDir, TEXT("mafia_agents_save.txt"));
    const FString BrainPath  = FPaths::Combine(SaveDir, TEXT("mafia_brains_save.txt"));

    // ── Load previous state ───────────────────────────────────────────────────
    // Agents file: state + relations
    // Brains file: NN weights (separate so Python parser never sees brain tokens)
    {
        std::string agentPathStr(TCHAR_TO_UTF8(*AgentPath));
        std::ifstream afs(agentPathStr);
        if (afs.is_open())
        {
            std::string magic;
            afs >> magic;
            if (magic != "MAFIA_SIM_SAVE_V2")
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[EdgeMafia] Agent save magic mismatch. Starting fresh."));
            }
            else
            {
                int savedN = 0;
                afs >> savedN;
                int loadCount = std::min(savedN, n);

                for (int i = 0; i < savedN; ++i)
                {
                    std::string agentLabel;
                    int idx = 0;
                    afs >> agentLabel >> idx;       // "AGENT" <id>

                    // Name (rest of line)
                    afs >> std::ws;
                    std::string savedName;
                    std::getline(afs, savedName);

                    int aliveI, roleI;
                    float aggrI, loyI, parI, decI, rewardI;
                    int   actI, voteI, atkI;

                    afs >> aliveI >> roleI;
                    afs >> aggrI >> loyI >> parI >> decI;
                    afs >> rewardI;
                    afs >> actI >> voteI >> atkI;

                    // Relations at the END of each agent block in the agents file
                    int relCount = 0;
                    afs >> relCount;

                    std::unordered_map<int, Relationship> loadedRels;
                    for (int r = 0; r < relCount; ++r)
                    {
                        int   other, betrayal, consistency, kMafia, kTown;
                        float trust, liking;
                        afs >> other >> trust >> liking >> betrayal
                            >> consistency >> kMafia >> kTown;

                        Relationship rel;
                        rel.trust       = trust;
                        rel.liking      = liking;
                        rel.betrayal    = betrayal;
                        rel.consistency = consistency;
                        rel.knownMafia  = (kMafia != 0);
                        rel.knownTown   = (kTown  != 0);
                        loadedRels[other] = rel;
                    }

                    if (i < n)
                    {
                        if (!savedName.empty()) names[i] = savedName;
                        alive[i]          = aliveI;
                        roles[i]          = roleI;
                        aggression[i]     = aggrI;
                        loyalty[i]        = loyI;
                        paranoia[i]       = parI;
                        deceit[i]         = decI;
                        totalReward[i]    = rewardI;
                        lastAction[i]     = actI;
                        lastVoteTarget[i] = voteI;
                        attackedCount[i]  = atkI;
                        relations[i]      = std::move(loadedRels);
                    }
                }

                if (savedN < n)
                    UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Loaded %d agents; %d new agents start fresh."), savedN, n - savedN);
                else if (savedN > n)
                    UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Loaded %d agents; %d extra discarded."), n, savedN - n);
                else
                    UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Loaded all %d agents."), n);
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] No agent save found. Starting fresh."));
        }

        // Load brains from separate file (brain data stays out of Python file)
        std::string brainPathStr(TCHAR_TO_UTF8(*BrainPath));
        std::ifstream bfs(brainPathStr);
        if (bfs.is_open())
        {
            std::string magic;
            bfs >> magic;
            if (magic == "MAFIA_BRAINS_V1")
            {
                int savedN = 0;
                bfs >> savedN;
                for (int i = 0; i < savedN; ++i)
                {
                    std::string label;
                    int idx = 0;
                    bfs >> label >> idx;   // "BRAIN" <id>

                    SimpleNN tmp;
                    if (i < n)
                    {
                        if (!brains[i].load(bfs))
                            UE_LOG(LogTemp, Warning, TEXT("[EdgeMafia] Failed to load brain %d."), i);
                    }
                    else
                    {
                        tmp.load(bfs);   // consume and discard
                    }
                }
                UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Brain file loaded."));
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] No brain save found. Brains start fresh."));
        }
    }

    // ── Assign roles ──────────────────────────────────────────────────────────
    int mafiaCount    = std::max(1, n / 4);
    int doctorCount   = (n >= 7) ? 1 : 0;
    int detectiveCount = (n >= 7) ? 1 : 0;

    std::vector<int> rolePool;
    rolePool.reserve(n);
    for (int i = 0; i < mafiaCount;    ++i) rolePool.push_back(ROLE_MAFIA);
    for (int i = 0; i < doctorCount;   ++i) rolePool.push_back(ROLE_DOCTOR);
    for (int i = 0; i < detectiveCount;++i) rolePool.push_back(ROLE_DETECTIVE);
    while (static_cast<int>(rolePool.size()) < n) rolePool.push_back(ROLE_VILLAGER);
    std::shuffle(rolePool.begin(), rolePool.end(), rng);
    for (int i = 0; i < n; ++i) roles[i] = rolePool[i];

    // ── Initialise probMafia ──────────────────────────────────────────────────
    float baseProb = static_cast<float>(mafiaCount) / std::max(1, n - 1);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            probMafia[i][j] = (i == j) ? 0.f : baseProb;

    // ── Personality traits (role-biased) ──────────────────────────────────────
    //   Row index: 0=Mafia 1=Villager 2=Detective 3=Doctor
    const float baseAgg[4] = {0.7f, 0.2f, 0.3f, 0.0f};
    const float spanAgg[4] = {0.3f, 0.4f, 0.3f, 0.2f};
    const float baseLoy[4] = {0.4f, 0.4f, 0.6f, 0.7f};
    const float spanLoy[4] = {0.3f, 0.4f, 0.3f, 0.3f};
    const float basePar[4] = {0.4f, 0.3f, 0.7f, 0.5f};
    const float spanPar[4] = {0.4f, 0.4f, 0.3f, 0.3f};
    const float baseDec[4] = {0.7f, 0.1f, 0.3f, 0.2f};
    const float spanDec[4] = {0.3f, 0.3f, 0.3f, 0.3f};

    for (int i = 0; i < n; ++i)
    {
        int k = (roles[i] == ROLE_MAFIA     ? 0 :
                 roles[i] == ROLE_VILLAGER  ? 1 :
                 roles[i] == ROLE_DETECTIVE ? 2 : 3);

        aggression[i] = baseAgg[k] + spanAgg[k] * randFloat();
        loyalty[i]    = baseLoy[k] + spanLoy[k] * randFloat();
        paranoia[i]   = basePar[k] + spanPar[k] * randFloat();
        deceit[i]     = baseDec[k] + spanDec[k] * randFloat();
    }

    // ── Relation helpers ──────────────────────────────────────────────────────

    auto pruneConnections = [&](int a)
    {
        auto& m = relations[a];
        while (static_cast<int>(m.size()) > MAX_CONNECTIONS)
        {
            int   worstId    = -1;
            float worstTrust =  1e9f;
            for (auto& kv : m)
                if (kv.second.trust < worstTrust) { worstTrust = kv.second.trust; worstId = kv.first; }
            if (worstId != -1) m.erase(worstId); else break;
        }
    };

    auto ensureRel = [&](int a, int b) -> Relationship&
    {
        auto& m  = relations[a];
        auto  it = m.find(b);
        if (it == m.end())
        {
            auto res = m.emplace(b, Relationship{});
            pruneConnections(a);
            return res.first->second;
        }
        return it->second;
    };

    auto getTrust = [&](int a, int b) -> float
    {
        auto it = relations[a].find(b);
        return it != relations[a].end() ? it->second.trust : 0.f;
    };

    auto getLiking = [&](int a, int b) -> float
    {
        auto it = relations[a].find(b);
        return it != relations[a].end() ? it->second.liking : 0.f;
    };

    auto getBetrayal = [&](int a, int b) -> int
    {
        auto it = relations[a].find(b);
        return it != relations[a].end() ? it->second.betrayal : 0;
    };

    auto getConsistency = [&](int a, int b) -> int
    {
        auto it = relations[a].find(b);
        return it != relations[a].end() ? it->second.consistency : 0;
    };

    auto knowsMafia = [&](int a, int b) -> bool
    {
        auto it = relations[a].find(b);
        return it != relations[a].end() && it->second.knownMafia;
    };

    auto knowsTown = [&](int a, int b) -> bool
    {
        auto it = relations[a].find(b);
        return it != relations[a].end() && it->second.knownTown;
    };

    auto isAlly = [&](int a, int b) -> bool
    {
        return getTrust(a, b) > 0.5f && getLiking(a, b) > 0.3f;
    };

    // ── Seed initial connections ───────────────────────────────────────────────
    for (int i = 0; i < n; ++i)
    {
        int conns = std::min(INIT_CONNECTIONS, n - 1);
        for (int c = 0; c < conns; ++c)
        {
            int j = randInt(n);
            if (j == i) continue;
            Relationship& rel = ensureRel(i, j);
            float base = (randFloat() - 0.5f) * 0.4f;
            rel.trust  = base;
            rel.liking = base;
        }
    }

    // Mafia members trust each other from the start
    for (int i = 0; i < n; ++i)
    {
        if (roles[i] != ROLE_MAFIA) continue;
        for (int j = 0; j < n; ++j)
        {
            if (i == j || roles[j] != ROLE_MAFIA) continue;
            Relationship& rel = ensureRel(i, j);
            rel.trust  = clampf(rel.trust  + 0.5f, -1.f, 1.f);
            rel.liking = clampf(rel.liking + 0.3f, -1.f, 1.f);
        }
    }

    // ── Win conditions ────────────────────────────────────────────────────────

    auto mafiaWin = [&]() -> bool
    {
        int m = 0, t = 0;
        for (int i = 0; i < n; ++i)
            if (alive[i]) (roles[i] == ROLE_MAFIA ? m : t)++;
        return m > 0 && m >= t;
    };

    auto townWin = [&]() -> bool
    {
        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_MAFIA) return false;
        return true;
    };

    // ── Relation decay ────────────────────────────────────────────────────────

    auto decayRelations = [&]()
    {
        for (int i = 0; i < n; ++i)
        {
            float d = 0.01f * (0.5f + paranoia[i]);
            for (auto& kv : relations[i])
            {
                float& t = kv.second.trust;
                t -= d * (t > 0.f ? 1.f : 0.2f);
                t  = clampf(t, -1.f, 1.f);
            }
        }
    };

    // ── Night-phase target selectors ──────────────────────────────────────────

    auto chooseMafiaTarget = [&](int self) -> int
    {
        std::vector<int>   cand;
        std::vector<float> score;
        float A = aggression[self], D = deceit[self];

        for (int j = 0; j < n; ++j)
        {
            if (!alive[j] || j == self) continue;
            float susp   = -(getTrust(self,j) + getLiking(self,j)) * (0.5f + A);
            float betray = (roles[j] == ROLE_MAFIA && getTrust(self,j) < -0.4f) ? 0.5f*D : 0.f;
            float s      = susp + betray;
            if (s > 0.f && std::isfinite(s)) { cand.push_back(j); score.push_back(std::max(0.01f, s)); }
        }
        return weightedChoice(cand, score);
    };

    auto chooseDoctorSave = [&](int self) -> int
    {
        std::vector<int>   cand;
        std::vector<float> score;
        float L = loyalty[self];

        int detId = -1;
        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_DETECTIVE) detId = i;

        for (int j = 0; j < n; ++j)
        {
            if (!alive[j]) continue;
            float s = 0.1f
                    + (isAlly(self,j)            ? 0.6f*L    : 0.f)
                    + 0.2f * (getLiking(self,j)  + 1.f)*0.5f
                    + 0.3f * attackedCount[j]
                    + (j == detId               ? 1.f        : 0.f);
            if (!std::isfinite(s)) s = 0.1f;
            cand.push_back(j);
            score.push_back(std::max(0.01f, s));
        }
        return weightedChoice(cand, score);
    };

    auto chooseDetectiveCheck = [&](int self) -> int
    {
        std::vector<int>   cand;
        std::vector<float> score;
        float P = paranoia[self];

        for (int j = 0; j < n; ++j)
        {
            if (!alive[j] || j == self) continue;
            if (knowsMafia(self,j) || knowsTown(self,j)) continue;
            float s = -getTrust(self,j) * (0.5f + P);
            if (s <= 0.f || !std::isfinite(s)) s = 0.05f;
            cand.push_back(j);
            score.push_back(std::max(0.01f, s));
        }

        if (cand.empty())
        {
            for (int j = 0; j < n; ++j)
            {
                if (!alive[j] || j == self) continue;
                float s = -getTrust(self,j) * (0.5f + P);
                if (s <= 0.f || !std::isfinite(s)) s = 0.05f;
                cand.push_back(j);
                score.push_back(std::max(0.01f, s));
            }
        }
        return weightedChoice(cand, score);
    };

    // ── Day-phase lynch target (uses RL brain) ────────────────────────────────

    auto chooseLynchTarget = [&](int self) -> int
    {
        std::vector<int> cand;
        for (int j = 0; j < n; ++j)
            if (alive[j] && j != self) cand.push_back(j);

        if (cand.empty()) { lastState[self].clear(); lastAction[self] = -1; return -1; }

        float P = paranoia[self], L = loyalty[self], D = deceit[self];

        // Heuristic to pick a primary target (anchors the NN state)
        int   primaryTarget = cand[0];
        float bestH         = -1e9f;
        for (int j : cand)
        {
            float t      = getTrust(self, j);
            float base   = -t * (0.7f + P);
            float ally   = isAlly(self, j) ? 1.f : 0.f;
            float allyF  = 1.f - 0.7f * L * ally;
            float betray = (ally > 0.f && t < 0.f) ? 0.3f*D : 0.f;
            float s      = base * allyF + betray + 2.f * clampf(probMafia[self][j], 0.f, 1.f);
            if (!std::isfinite(s)) s = 0.05f;
            if (s > bestH) { bestH = s; primaryTarget = j; }
        }

        float avgBelief = 0.f;
        for (int j : cand) avgBelief += clampf(probMafia[self][j], 0.f, 1.f);
        if (!cand.empty()) avgBelief /= static_cast<float>(cand.size());

        std::vector<float> state(STATE_SIZE, 0.f);
        state[0] = (getTrust(self, primaryTarget)  + 1.f) / 2.f;
        state[1] = (getBetrayal(self, primaryTarget) > 0) ? 1.f : 0.f;
        state[2] = (getConsistency(self, primaryTarget) > 0) ? 1.f : 0.f;
        state[3] = clampf(probMafia[self][primaryTarget], 0.f, 1.f);
        state[4] = (roles[self] == ROLE_DETECTIVE && knowsMafia(self, primaryTarget)) ? 1.f : 0.f;
        state[5] = (P + 1.f) / 2.f;
        state[6] = (L + 1.f) / 2.f;
        state[7] = (D + 1.f) / 2.f;
        state[8] = avgBelief;
        state[9] = (roles[self] == ROLE_MAFIA) ? 1.f : 0.f;

        lastState[self] = state;

        // Map up to ACTION_SIZE-1 candidates into action slots (slot 0 = abstain)
        const int MAX_TARGETS = ACTION_SIZE - 1;
        std::vector<int> idxMap;
        idxMap.reserve(MAX_TARGETS);
        for (int k = 0; k < static_cast<int>(cand.size()) && static_cast<int>(idxMap.size()) < MAX_TARGETS; ++k)
            idxMap.push_back(cand[k]);

        std::vector<int> actions;
        actions.push_back(0);
        for (int a = 1; a <= static_cast<int>(idxMap.size()); ++a) actions.push_back(a);

        int action = brains[self].chooseAction(state, actions, 0.1f);
        lastAction[self] = action;

        if (action == 0) return -1;
        int chosen = action - 1;
        if (chosen < 0 || chosen >= static_cast<int>(idxMap.size())) return -1;
        return idxMap[chosen];
    };

    // ── Post-lynch trust update ────────────────────────────────────────────────

    auto updateTrustAfterLynch = [&](int lyncher, int target, bool wasMafia)
    {
        float delta = wasMafia ? 0.15f : -0.15f;
        for (int i = 0; i < n; ++i)
        {
            if (!alive[i] || i == lyncher) continue;
            Relationship& rel = ensureRel(i, lyncher);
            rel.trust = clampf(rel.trust + delta, -1.f, 1.f);
        }
        if (roles[lyncher] == ROLE_MAFIA && roles[target] == ROLE_MAFIA)
        {
            Relationship& rel = ensureRel(lyncher, target);
            rel.trust = clampf(rel.trust - 0.4f, -1.f, 1.f);
        }
    };

    // ── Night phase ───────────────────────────────────────────────────────────

    auto nightPhase = [&](int /*cycle*/)
    {
        int mafiaKill    = -1;
        int doctorSave   = -1;
        int detectiveChk = -1;
        int detectiveId  = -1;

        for (int i = 0; i < n; ++i)
            if (alive[i] && roles[i] == ROLE_MAFIA) { mafiaKill = chooseMafiaTarget(i); break; }

        for (int i = 0; i < n; ++i)
        {
            if (!alive[i]) continue;
            if (roles[i] == ROLE_DOCTOR)    doctorSave   = chooseDoctorSave(i);
            if (roles[i] == ROLE_DETECTIVE) { detectiveChk = chooseDetectiveCheck(i); detectiveId = i; }
        }

        if (mafiaKill != -1 && mafiaKill != doctorSave)
        {
            alive[mafiaKill] = 0;
            attackedCount[mafiaKill]++;
        }

        if (detectiveChk != -1 && detectiveId != -1)
        {
            Relationship& rel = ensureRel(detectiveId, detectiveChk);
            if (roles[detectiveChk] == ROLE_MAFIA)
            {
                rel.knownMafia              = true;
                paranoia[detectiveId]       = std::min(1.f, paranoia[detectiveId] + 0.1f);
                rel.trust                   = clampf(rel.trust - 0.7f, -1.f, 1.f);
                probMafia[detectiveId][detectiveChk] = 0.95f;
            }
            else
            {
                rel.knownTown = true;
                rel.trust     = clampf(rel.trust + 0.4f, -1.f, 1.f);
                probMafia[detectiveId][detectiveChk] = 0.01f;
            }
        }
        decayRelations();
    };

    // ── Day phase ─────────────────────────────────────────────────────────────

    auto dayPhase = [&](int /*cycle*/)
    {
        std::vector<int> votes(n, -1);
        std::vector<int> voteCount(n, 0);

        for (int i = 0; i < n; ++i)
            if (alive[i]) votes[i] = chooseLynchTarget(i);

        for (int i = 0; i < n; ++i) { signalType[i] = -1; signalTarget[i] = -1; }

        // Town agents avoid voting against known allies
        for (int i = 0; i < n; ++i)
        {
            if (!alive[i] || roles[i] == ROLE_MAFIA) continue;
            if (votes[i] != -1 && isAlly(i, votes[i]) && !knowsMafia(i, votes[i]))
                votes[i] = -1;

            if (randFloat() < 0.6f)
            {
                int   bestAlly  = -1;
                float bestTrust = -1.f;
                for (int j = 0; j < n; ++j)
                {
                    if (!alive[j] || j == i) continue;
                    float t = getTrust(i, j);
                    if (t > bestTrust) { bestTrust = t; bestAlly = j; }
                }
                if (bestAlly != -1 && votes[bestAlly] != -1)
                {
                    signalType[i]   = 1;
                    signalTarget[i] = votes[bestAlly];
                }
            }
        }

        for (int i = 0; i < n; ++i)
        {
            if (!alive[i]) continue;
            int v = votes[i];
            if (v >= 0 && v < n) { voteCount[v]++; lastVoteTarget[i] = v; }
        }

        // Plurality lynch
        int lynched   = -1;
        int bestVotes = 0;
        for (int i = 0; i < n; ++i)
        {
            if (!alive[i]) continue;
            if (voteCount[i] > bestVotes) { bestVotes = voteCount[i]; lynched = i; }
        }

        if (lynched != -1)
        {
            bool wasMafia = (roles[lynched] == ROLE_MAFIA);
            alive[lynched] = 0;

            // RL reward + weight update
            for (int i = 0; i < n; ++i)
            {
                if (!alive[i] || lastAction[i] < 0 || lastState[i].empty()) continue;

                float reward = (wasMafia && lastVoteTarget[i] == lynched) ?  1.f :
                               (!wasMafia && lastVoteTarget[i] == lynched) ? -1.f : 0.f;
                totalReward[i] += reward;

                std::vector<float> q = brains[i].forward(lastState[i]);
                float oldQ = (lastAction[i] < static_cast<int>(q.size())) ? q[lastAction[i]] : 0.f;
                brains[i].train(lastState[i], lastAction[i], oldQ + 0.1f*(reward - oldQ), 0.01f);
            }

            for (int i = 0; i < n; ++i)
                if (alive[i] && lastVoteTarget[i] == lynched)
                    updateTrustAfterLynch(i, lynched, wasMafia);
        }
    };

    // ── Main game loop ────────────────────────────────────────────────────────

    for (int cycle = 1; cycle <= 50; ++cycle)
    {
        nightPhase(cycle);
        if (mafiaWin() || townWin()) break;
        dayPhase(cycle);
        if (mafiaWin() || townWin()) break;
    }

    const bool bMafiaWon = mafiaWin();
    const bool bTownWon  = townWin();

    if      (bMafiaWon) UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Mafia win!"));
    else if (bTownWon)  UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Town win!"));
    else                UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Game ended without a clear winner."));

    // =========================================================================
    //  SAVE  – two files
    //
    //  mafia_agents_save.txt:
    //    Layout per agent (all tokens, relations LAST):
    //      AGENT {id}
    //      {name}
    //      {alive} {role}
    //      {aggression} {loyalty} {paranoia} {deceit}
    //      {totalReward}
    //      {lastAction} {lastVoteTarget} {attackedCount}
    //      {relCount}
    //      {other} {trust} {liking} {betrayal} {consistency} {knownMafia} {knownTown}
    //      ...
    //
    //  mafia_brains_save.txt:
    //    NN weights only; never seen by the Python parser.
    // =========================================================================

    // Ensure directory exists via UE's platform API
    IFileManager::Get().MakeDirectory(*SaveDir, /*bTree=*/true);

    // ── Write agents file ─────────────────────────────────────────────────────
    {
        std::string agentPathStr(TCHAR_TO_UTF8(*AgentPath));
        std::ofstream ofs(agentPathStr);

        if (!ofs.is_open())
        {
            UE_LOG(LogTemp, Error, TEXT("[EdgeMafia] Could not open agents save file: %s"), *AgentPath);
        }
        else
        {
            ofs << "MAFIA_SIM_SAVE_V2\n";
            ofs << n << "\n";

            for (int i = 0; i < n; ++i)
            {
                // ── Fixed-position tokens (block[0..12]) ───────────────────────
                ofs << "AGENT " << i << "\n";            // block[0,1]
                ofs << names[i] << "\n";                  // block[2]
                ofs << alive[i] << " " << roles[i] << "\n";          // block[3,4]
                ofs << aggression[i] << " " << loyalty[i] << " "
                    << paranoia[i]   << " " << deceit[i]  << "\n";   // block[5-8]
                ofs << totalReward[i] << "\n";            // block[9]
                ofs << lastAction[i]     << " "
                    << lastVoteTarget[i] << " "
                    << attackedCount[i]  << "\n";         // block[10,11,12]

                // ── Relations LAST so Python backward scan is unambiguous ───────
                //    block[13] = relCount
                //    block[14 + r*7 .. 14 + r*7 + 6] per relation
                const auto& relMap = relations[i];
                ofs << relMap.size() << "\n";
                for (const auto& kv : relMap)
                {
                    const Relationship& r = kv.second;
                    ofs << kv.first           << " "
                        << r.trust            << " "
                        << r.liking           << " "
                        << r.betrayal         << " "
                        << r.consistency      << " "
                        << (r.knownMafia ? 1 : 0) << " "
                        << (r.knownTown  ? 1 : 0) << "\n";
                }
            }

            UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Agents saved to %s"), *AgentPath);
        }
    }

    // ── Write brains file ─────────────────────────────────────────────────────
    {
        std::string brainPathStr(TCHAR_TO_UTF8(*BrainPath));
        std::ofstream bfs(brainPathStr);

        if (!bfs.is_open())
        {
            UE_LOG(LogTemp, Error, TEXT("[EdgeMafia] Could not open brains save file: %s"), *BrainPath);
        }
        else
        {
            bfs << "MAFIA_BRAINS_V1\n";
            bfs << n << "\n";
            for (int i = 0; i < n; ++i)
            {
                bfs << "BRAIN " << i << "\n";
                brains[i].save(bfs);
            }
            UE_LOG(LogTemp, Log, TEXT("[EdgeMafia] Brains saved to %s"), *BrainPath);
        }
    }

    // ── Populate public Agents array for Blueprint / game code ────────────────
    for (int32 i = 0; i < n; ++i)
    {
        FEdgeMafiaAgent& Out = Agents[i];
        Out.AgentID        = i;
        Out.Name           = FString(names[i].c_str());
        Out.Role           = roles[i];
        Out.bAlive         = (alive[i] != 0);
        Out.Aggression     = aggression[i];
        Out.Loyalty        = loyalty[i];
        Out.Paranoia       = paranoia[i];
        Out.Deceit         = deceit[i];
        Out.TotalReward    = totalReward[i];
        Out.LastAction     = lastAction[i];
        Out.LastVoteTarget = lastVoteTarget[i];
        Out.AttackedCount  = attackedCount[i];

        Out.Relations.Empty();
        for (const auto& kv : relations[i])
        {
            FEdgeMafiaRelation rel;
            rel.OtherAgentID    = kv.first;
            rel.Trust           = kv.second.trust;
            rel.Liking          = kv.second.liking;
            rel.BetrayalCount   = kv.second.betrayal;
            rel.ConsistencyCount= kv.second.consistency;
            rel.bKnownMafia     = kv.second.knownMafia;
            rel.bKnownTown      = kv.second.knownTown;
            Out.Relations.Add(rel);
        }
    }

    // Fire Blueprint delegate
    OnSimulationComplete.Broadcast(bMafiaWon);
}
