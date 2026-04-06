// =============================================================================
// EdgeMafiaNPC  ──  Actor Integration Patch
// =============================================================================
// This file shows ONLY the lines you must ADD to the existing subsystem files.
// Every line marked  ← ADD  is new.  Nothing from the previous file is removed.
//
// Files patched:
//   Source/EdgeMafiaNPC/Public/EdgeMafiaNPCSubsystem.h
//   Source/EdgeMafiaNPC/Private/EdgeMafiaNPCSubsystem.cpp
//
// Also update Build.cs as shown in the small section at the bottom.
// =============================================================================


// ─────────────────────────────────────────────────────────────────────────────
// PATCH A ── EdgeMafiaNPCSubsystem.h
//
// Locate the existing class body and insert the marked blocks.
// ─────────────────────────────────────────────────────────────────────────────

/*
// ← ADD this include near the top, after existing includes
#include "EdgeMafiaAgentActor.h"

UCLASS()
class EDGEMAFIANPC_API UEdgeMafiaNPCSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ... (all existing declarations stay exactly as they are) ...

    // ═══════════════════════════════════════════════════════════════════════
    // ← ADD everything below this line
    // ═══════════════════════════════════════════════════════════════════════

    // ── Spawned actor array ────────────────────────────────────────────────

    // One actor per agent – index matches AgentID exactly.
    UPROPERTY(BlueprintReadOnly, Category="EdgeMafia|Actors")
    TArray<AEdgeMafiaAgentActor*> SpawnedActors;

    // ── Spawn configuration (set these before calling RunSimulation) ───────

    // Class to spawn.  Assign a Blueprint child of AEdgeMafiaAgentActor here
    // to get custom meshes, particles, sounds, etc.  Defaults to the base class.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Actors")
    TSubclassOf<AEdgeMafiaAgentActor> AgentActorClass;

    // World-space centre of the spawn circle.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Actors")
    FVector SpawnOrigin = FVector::ZeroVector;

    // Radius of the circle agents are placed on (scales automatically with n).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Actors")
    float SpawnRadius = 900.f;

    // Fixed Z offset from SpawnOrigin for all agents.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Actors")
    float SpawnHeight = 0.f;

    // ── Trust-network drawing ──────────────────────────────────────────────

    // If true, trust network debug lines are drawn automatically after each sim.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Actors")
    bool bDrawTrustNetworkOnComplete = true;

    // Minimum trust value for an edge to appear in the drawn network.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Actors")
    float TrustNetworkMinThreshold = 0.5f;

    // How many seconds the trust-network lines persist in the viewport.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Actors")
    float TrustNetworkDrawDuration = 20.f;

    // ── Blueprint-callable actor management ────────────────────────────────

    // Spawn one actor per agent in a circle around SpawnOrigin.
    // Called automatically by RunSimulation; expose for manual use if needed.
    UFUNCTION(BlueprintCallable, Category="EdgeMafia|Actors")
    void SpawnAgentActors(int32 NumAgents);

    // Push final simulation data into every spawned actor and refresh visuals.
    // Called automatically by RunSimulation after the sim finishes.
    UFUNCTION(BlueprintCallable, Category="EdgeMafia|Actors")
    void SyncActorsToSimulation();

    // Destroy all previously spawned actors and empty SpawnedActors.
    UFUNCTION(BlueprintCallable, Category="EdgeMafia|Actors")
    void ClearSpawnedActors();

    // Draw coloured debug lines between agents whose mutual trust ≥ threshold.
    // Red lines = one side is Mafia; white/green lines = both town.
    // Also fires OnTrustLinkDrawn on each actor so BP can add Niagara beams.
    UFUNCTION(BlueprintCallable, Category="EdgeMafia|Actors")
    void DrawTrustNetwork(float MinTrust = 0.5f, float Duration = 20.f);

    // ── Blueprint-callable convenience getters ─────────────────────────────

    // Returns the spawned actor for a given AgentID, or nullptr if not found.
    UFUNCTION(BlueprintPure, Category="EdgeMafia|Actors")
    AEdgeMafiaAgentActor* GetActorForAgent(int32 AgentID) const;
};
*/


// ─────────────────────────────────────────────────────────────────────────────
// PATCH B ── EdgeMafiaNPCSubsystem.cpp
// ─────────────────────────────────────────────────────────────────────────────
//
// 1. Add one include near the top (after existing includes):
//
//    #include "EdgeMafiaAgentActor.h"      ← ADD
//    #include "DrawDebugHelpers.h"         ← ADD  (trust network lines)
//    #include "Kismet/GameplayStatics.h"   ← ADD  (GetAllActorsOfClass guard)
//    #include "Engine/World.h"             ← ADD
//
// 2. Modify RunSimulation() – insert the two lines marked ← ADD:
//
//    void UEdgeMafiaNPCSubsystem::RunSimulation(int32 NumAgents)
//    {
//        ClearSpawnedActors();                    // ← ADD  (destroy previous run)
//        SpawnAgentActors(NumAgents);             // ← ADD  (spawn before sim)
//        Agents.Empty();
//        Agents.SetNum(NumAgents);
//        RunMafiaSimulation(NumAgents);
//        SyncActorsToSimulation();                // ← ADD  (push results to actors)
//        if (bDrawTrustNetworkOnComplete)         // ← ADD
//            DrawTrustNetwork(TrustNetworkMinThreshold, TrustNetworkDrawDuration);
//    }
//
// 3. Append the new method bodies below at the END of the .cpp file.
// ─────────────────────────────────────────────────────────────────────────────

#include "EdgeMafiaAgentActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

// ─────────────────────────────────────────────────────────────────────────────
void UEdgeMafiaNPCSubsystem::SpawnAgentActors(int32 NumAgents)
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[EdgeMafia] SpawnAgentActors: no valid World."));
        return;
    }

    // Resolve which class to spawn – fall back to the base class if none set.
    TSubclassOf<AEdgeMafiaAgentActor> ClassToSpawn =
        AgentActorClass ? AgentActorClass
                        : AEdgeMafiaAgentActor::StaticClass();

    // Auto-scale radius so agents don't overlap: min 500 UU, grows with count.
    const float EffectiveRadius = FMath::Max(SpawnRadius, 80.f * NumAgents);

    SpawnedActors.SetNum(NumAgents);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.bNoFail = true;

    for (int32 i = 0; i < NumAgents; ++i)
    {
        // Place agents evenly around a circle.
        const float Angle = (2.f * PI * i) / NumAgents;
        const FVector Location(
            SpawnOrigin.X + EffectiveRadius * FMath::Cos(Angle),
            SpawnOrigin.Y + EffectiveRadius * FMath::Sin(Angle),
            SpawnOrigin.Z + SpawnHeight
        );

        // Face the agent toward the circle centre so labels read forwards.
        const FRotator Rotation =
            (SpawnOrigin - Location).GetSafeNormal2D().Rotation();

        AEdgeMafiaAgentActor* Actor =
            World->SpawnActor<AEdgeMafiaAgentActor>(ClassToSpawn, Location, Rotation, SpawnParams);

        if (Actor)
        {
            // Agent ID == Actor index == array position – the invariant the
            // question requires.  This is set here and never changes.
            Actor->InitialiseActor(i);
            SpawnedActors[i] = Actor;

            UE_LOG(LogTemp, Verbose,
                TEXT("[EdgeMafia] Spawned agent actor %d at (%.0f, %.0f, %.0f)"),
                i, Location.X, Location.Y, Location.Z);
        }
        else
        {
            SpawnedActors[i] = nullptr;
            UE_LOG(LogTemp, Error,
                TEXT("[EdgeMafia] Failed to spawn actor for agent %d."), i);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[EdgeMafia] Spawned %d agent actors in a circle (r=%.0f)."),
        NumAgents, EffectiveRadius);
}

// ─────────────────────────────────────────────────────────────────────────────
void UEdgeMafiaNPCSubsystem::SyncActorsToSimulation()
{
    // Agents array is already populated by RunMafiaSimulation().
    // Walk every spawned actor and push its matching FEdgeMafiaAgent into it.

    for (int32 i = 0; i < SpawnedActors.Num(); ++i)
    {
        AEdgeMafiaAgentActor* Actor = SpawnedActors[i];
        if (!IsValid(Actor)) continue;

        if (!Agents.IsValidIndex(i))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[EdgeMafia] SyncActors: Agents[%d] out of range."), i);
            continue;
        }

        // AgentID in actor must equal array index – assert in debug builds.
        ensure(Actor->AgentID == i);

        // Push the full simulation result snapshot.
        Actor->SyncWithAgentData(Agents[i]);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[EdgeMafia] Synced %d actors with simulation results."),
        SpawnedActors.Num());
}

// ─────────────────────────────────────────────────────────────────────────────
void UEdgeMafiaNPCSubsystem::ClearSpawnedActors()
{
    for (AEdgeMafiaAgentActor* Actor : SpawnedActors)
    {
        if (IsValid(Actor))
            Actor->Destroy();
    }
    SpawnedActors.Empty();
}

// ─────────────────────────────────────────────────────────────────────────────
void UEdgeMafiaNPCSubsystem::DrawTrustNetwork(float MinTrust, float Duration)
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World) return;

    const int32 n = Agents.Num();

    // We need agent locations – pull from spawned actors.
    // Build a fast lookup: AgentID → world location.
    TMap<int32, FVector> Locations;
    for (AEdgeMafiaAgentActor* A : SpawnedActors)
    {
        if (IsValid(A))
            Locations.Add(A->AgentID, A->GetActorLocation());
    }

    int32 EdgesDrawn = 0;

    for (const FEdgeMafiaAgent& Agent : Agents)
    {
        const FVector* FromPos = Locations.Find(Agent.AgentID);
        if (!FromPos) continue;

        for (const FEdgeMafiaRelation& Rel : Agent.Relations)
        {
            if (Rel.Trust < MinTrust) continue;

            const FVector* ToPos = Locations.Find(Rel.OtherAgentID);
            if (!ToPos) continue;

            // Colour encodes edge type:
            //   Both town   → blue-white gradient by trust strength
            //   One is Mafia → warm orange / red
            //   Mutual high trust (both sides ≥ threshold) → brighter line
            bool bAgentMafia = (Agent.Role == 0);
            bool bOtherMafia = Agents.IsValidIndex(Rel.OtherAgentID)
                                && (Agents[Rel.OtherAgentID].Role == 0);

            FLinearColor EdgeColour;
            float        Thickness;

            if (bAgentMafia || bOtherMafia)
            {
                // At least one Mafia member – orange/red to show hidden threat
                EdgeColour = FLinearColor::LerpUsingHSV(
                    FLinearColor(1.f, 0.4f, 0.f),   // orange
                    FLinearColor(0.9f, 0.f, 0.f),   // red
                    FMath::Clamp((Rel.Trust - MinTrust) / (1.f - MinTrust), 0.f, 1.f));
                Thickness = 2.5f;
            }
            else
            {
                // Town–town edge: white→cyan by trust strength
                EdgeColour = FLinearColor::LerpUsingHSV(
                    FLinearColor(0.6f, 0.8f, 1.f),  // pale blue
                    FLinearColor(0.f, 1.f, 0.9f),   // bright cyan
                    FMath::Clamp((Rel.Trust - MinTrust) / (1.f - MinTrust), 0.f, 1.f));
                Thickness = 1.2f;
            }

            // Lift lines slightly so they don't clip through the ground
            const FVector Lift(0.f, 0.f, 20.f);

            DrawDebugLine(
                World,
                *FromPos + Lift,
                *ToPos   + Lift,
                EdgeColour.ToFColor(true),
                /*bPersistentLines=*/Duration > 0.f,
                Duration,
                /*DepthPriority=*/1,
                Thickness
            );

            // Notify both actors so Blueprint can add a Niagara beam, etc.
            if (SpawnedActors.IsValidIndex(Agent.AgentID) &&
                IsValid(SpawnedActors[Agent.AgentID]) &&
                SpawnedActors.IsValidIndex(Rel.OtherAgentID) &&
                IsValid(SpawnedActors[Rel.OtherAgentID]))
            {
                SpawnedActors[Agent.AgentID]->OnTrustLinkDrawn(
                    SpawnedActors[Rel.OtherAgentID], Rel.Trust);
            }

            ++EdgesDrawn;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[EdgeMafia] Trust network drawn: %d edges (threshold ≥ %.2f, duration %.0fs)."),
        EdgesDrawn, MinTrust, Duration);
}

// ─────────────────────────────────────────────────────────────────────────────
AEdgeMafiaAgentActor* UEdgeMafiaNPCSubsystem::GetActorForAgent(int32 AgentID) const
{
    if (SpawnedActors.IsValidIndex(AgentID))
        return SpawnedActors[AgentID];
    return nullptr;
}


// =============================================================================
// PATCH C ── EdgeMafiaNPC.Build.cs  (add two module names only)
// =============================================================================
//
// In the existing PublicDependencyModuleNames list, add:
//
//    "RenderCore",          ← required by UTextRenderComponent
//    "RHI"                  ← required by dynamic material colour writes
//
// The full list becomes:
//
//    PublicDependencyModuleNames.AddRange(new string[]
//    {
//        "Core",
//        "CoreUObject",
//        "Engine",
//        "RenderCore",      ← ADD
//        "RHI"              ← ADD
//    });
//
// =============================================================================


// =============================================================================
// USAGE SUMMARY
// =============================================================================
//
// In Blueprints (or C++ BeginPlay):
//
//   // 1. Get the subsystem
//   UEdgeMafiaNPCSubsystem* MafiaSys =
//       GetGameInstance()->GetSubsystem<UEdgeMafiaNPCSubsystem>();
//
//   // 2. (Optional) point to a BP child class for custom visuals
//   MafiaSys->AgentActorClass = ABP_MafiaAgent::StaticClass();
//
//   // 3. (Optional) customise placement
//   MafiaSys->SpawnOrigin = FVector(0, 0, 100);
//   MafiaSys->SpawnRadius = 1200.f;
//
//   // 4. Run – actors spawn, sim runs, actors sync, trust network draws
//   MafiaSys->RunSimulation(20);
//
//   // 5. Every spawned actor's AgentID == its index in SpawnedActors[]:
//   //      SpawnedActors[3]->AgentID == 3   ✓ (guaranteed by SpawnAgentActors)
//
//   // 6. Retrieve a specific actor at any time
//   AEdgeMafiaAgentActor* P7 = MafiaSys->GetActorForAgent(7);
//
// Blueprint child class events to implement for a polished MegaGrant demo:
//   • OnAgentSpawned(AgentID)       → play spawn particle + sound
//   • OnAgentEliminated()           → death ragdoll / dissolve material
//   • OnSimulationDataReceived(Data)→ update UI widget above agent head
//   • OnTrustLinkDrawn(Other, Trust)→ spawn Niagara beam between agents
//
// =============================================================================
