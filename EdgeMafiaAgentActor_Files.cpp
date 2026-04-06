// =============================================================================
// ADD THESE TWO FILES to your plugin – no existing code is changed.
//
//  Source/EdgeMafiaNPC/Public/EdgeMafiaAgentActor.h      ← section 1
//  Source/EdgeMafiaNPC/Private/EdgeMafiaAgentActor.cpp   ← section 2
//
// Each AEdgeMafiaAgentActor owns:
//   • UStaticMeshComponent  – sphere body (mesh + material assigned in a
//                              Blueprint child class, works without one too)
//   • UTextRenderComponent  – floating identity tag   "P3 | Detective"
//   • UTextRenderComponent  – floating status tag     "ALIVE  R:+2.0"
//   • Always-on debug drawing (DrawDebugSphere / DrawDebugString) so the
//     demo looks great in PIE with zero content assets.
//
// Blueprint-Implementable Events let designers layer on particles, sounds,
// UI widgets, or animations without touching C++.
// =============================================================================


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 ── Source/EdgeMafiaNPC/Public/EdgeMafiaAgentActor.h
// ─────────────────────────────────────────────────────────────────────────────

/*
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EdgeMafiaNPCTypes.h"
#include "EdgeMafiaAgentActor.generated.h"

// ── Role colour palette (static helpers, used by both actor and subsystem) ──

USTRUCT(BlueprintType)
struct EDGEMAFIANPC_API FEdgeMafiaRoleColours
{
    GENERATED_BODY()

    static FLinearColor ForRole(int32 Role, bool bAlive)
    {
        if (!bAlive) return FLinearColor(0.18f, 0.18f, 0.18f);   // grey = dead
        switch (Role)
        {
        case 0:  return FLinearColor(0.85f, 0.05f, 0.05f);       // Mafia     – red
        case 1:  return FLinearColor(0.20f, 0.55f, 1.00f);       // Villager  – blue
        case 2:  return FLinearColor(0.10f, 0.85f, 0.25f);       // Doctor    – green
        case 3:  return FLinearColor(1.00f, 0.78f, 0.05f);       // Detective – gold
        default: return FLinearColor::White;
        }
    }

    static FString RoleName(int32 Role)
    {
        switch (Role)
        {
        case 0: return TEXT("Mafia");
        case 1: return TEXT("Villager");
        case 2: return TEXT("Doctor");
        case 3: return TEXT("Detective");
        default: return TEXT("Unknown");
        }
    }
};


// ── The Actor ────────────────────────────────────────────────────────────────

UCLASS(BlueprintType, Blueprintable, meta=(ShortTooltip="Represents one EdgeMafia NPC agent in the world."))
class EDGEMAFIANPC_API AEdgeMafiaAgentActor : public AActor
{
    GENERATED_BODY()

public:
    AEdgeMafiaAgentActor();

    // ── Visual components ──────────────────────────────────────────────────

    // Assign your own mesh + material in a Blueprint child class.
    // Without a mesh the actor still draws debug spheres + text.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EdgeMafia|Components")
    UStaticMeshComponent* AgentMesh;

    // Floating label: "P3 | Detective"
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EdgeMafia|Components")
    UTextRenderComponent* IdentityLabel;

    // Status line below identity: "ALIVE   R: +2.0"
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EdgeMafia|Components")
    UTextRenderComponent* StatusLabel;

    // ── Agent data mirror (Blueprint-readable) ─────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category="EdgeMafia|Agent")
    int32 AgentID = -1;

    // Full snapshot populated after the simulation finishes.
    UPROPERTY(BlueprintReadOnly, Category="EdgeMafia|Agent")
    FEdgeMafiaAgent AgentSnapshot;

    // ── Visual configuration ───────────────────────────────────────────────

    // Height of the floating identity label above the actor origin.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Visuals")
    float LabelHeight = 120.f;

    // Height of the status label (sits below the identity label).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Visuals")
    float StatusLabelHeight = 80.f;

    // Scale of the sphere drawn via DrawDebugSphere when no mesh is assigned.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Visuals")
    float DebugSphereRadius = 40.f;

    // How long (seconds) the debug sphere persists each frame (≤0 = one frame).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Visuals")
    float DebugDrawDuration = 0.f;

    // When true, always draw a debug sphere even if a mesh is present.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Visuals")
    bool bAlwaysDrawDebugSphere = false;

    // Name of the Vector Parameter in the mesh's Material Instance that
    // controls the base colour.  Common names: "BaseColor", "Color", "Tint".
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EdgeMafia|Visuals")
    FName ColourParameterName = FName("BaseColor");

    // ── C++ callable API ───────────────────────────────────────────────────

    // Called by the subsystem immediately after spawning.
    // Sets AgentID and fires OnAgentSpawned so BP can play an intro effect.
    UFUNCTION(BlueprintCallable, Category="EdgeMafia")
    void InitialiseActor(int32 InAgentID);

    // Called by the subsystem after RunMafiaSimulation() completes.
    // Mirrors the final FEdgeMafiaAgent into this actor and refreshes visuals.
    UFUNCTION(BlueprintCallable, Category="EdgeMafia")
    void SyncWithAgentData(const FEdgeMafiaAgent& Data);

    // Force a visual refresh from AgentSnapshot (safe to call any time).
    UFUNCTION(BlueprintCallable, Category="EdgeMafia")
    void RefreshVisuals();

    // Returns the role colour for any role/alive combination.
    UFUNCTION(BlueprintPure, Category="EdgeMafia")
    static FLinearColor GetRoleColour(int32 Role, bool bAlive)
    { return FEdgeMafiaRoleColours::ForRole(Role, bAlive); }

    UFUNCTION(BlueprintPure, Category="EdgeMafia")
    static FString GetRoleName(int32 Role)
    { return FEdgeMafiaRoleColours::RoleName(Role); }

    // ── Blueprint-Implementable Events ─────────────────────────────────────
    // Implement these in a Blueprint child class for particles, sounds, UI, etc.

    // Fires once when the actor is first linked to an agent ID.
    UFUNCTION(BlueprintImplementableEvent, Category="EdgeMafia|Events")
    void OnAgentSpawned(int32 InAgentID);

    // Fires when SyncWithAgentData() determines the agent is dead (alive→dead).
    UFUNCTION(BlueprintImplementableEvent, Category="EdgeMafia|Events")
    void OnAgentEliminated();

    // Fires every time SyncWithAgentData() is called – useful for result panels.
    UFUNCTION(BlueprintImplementableEvent, Category="EdgeMafia|Events")
    void OnSimulationDataReceived(const FEdgeMafiaAgent& Data);

    // Fired by the subsystem's DrawTrustNetwork() for each high-trust edge.
    // Implement to spawn a particle ribbon, Niagara beam, etc.
    UFUNCTION(BlueprintImplementableEvent, Category="EdgeMafia|Events")
    void OnTrustLinkDrawn(AEdgeMafiaAgentActor* Other, float TrustValue);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    // Cached dynamic material (null if no mesh / no valid material set)
    UPROPERTY()
    UMaterialInstanceDynamic* DynMaterial = nullptr;

    bool bWasAliveLastSync = true;

    void ApplyColour(FLinearColor Colour);
    void UpdateLabels();
};
*/


// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 ── Source/EdgeMafiaNPC/Private/EdgeMafiaAgentActor.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "EdgeMafiaAgentActor.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"

// ─────────────────────────────────────────────────────────────────────────────
AEdgeMafiaAgentActor::AEdgeMafiaAgentActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // Root
    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    // Mesh (will be invisible until a mesh asset is assigned in a BP child)
    AgentMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AgentMesh"));
    AgentMesh->SetupAttachment(SceneRoot);
    AgentMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AgentMesh->SetRelativeScale3D(FVector(0.6f));   // 60 cm sphere by default

    // Identity label  – floats above the actor
    IdentityLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("IdentityLabel"));
    IdentityLabel->SetupAttachment(SceneRoot);
    IdentityLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight));
    IdentityLabel->SetHorizontalAlignment(EHTA_Center);
    IdentityLabel->SetWorldSize(28.f);
    IdentityLabel->SetTextRenderColor(FColor::White);
    IdentityLabel->SetText(FText::FromString(TEXT("? | ?")));

    // Status label – slightly lower
    StatusLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusLabel"));
    StatusLabel->SetupAttachment(SceneRoot);
    StatusLabel->SetRelativeLocation(FVector(0.f, 0.f, StatusLabelHeight));
    StatusLabel->SetHorizontalAlignment(EHTA_Center);
    StatusLabel->SetWorldSize(22.f);
    StatusLabel->SetTextRenderColor(FColor(180, 255, 180));
    StatusLabel->SetText(FText::FromString(TEXT("PENDING")));
}

// ─────────────────────────────────────────────────────────────────────────────
void AEdgeMafiaAgentActor::BeginPlay()
{
    Super::BeginPlay();

    // Attempt to build a dynamic material from whatever the mesh currently has.
    // Users can assign a Material with a "BaseColor" (or custom) vector param
    // in their Blueprint child class; if nothing is assigned this is a no-op.
    if (AgentMesh && AgentMesh->GetMaterial(0))
    {
        DynMaterial = AgentMesh->CreateAndSetMaterialInstanceDynamic(0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AEdgeMafiaAgentActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Always-on debug sphere – visible in PIE with no content assets at all.
    if (bAlwaysDrawDebugSphere || !AgentMesh || !AgentMesh->GetStaticMesh())
    {
        bool   bAlive  = AgentSnapshot.bAlive;
        int32  role    = AgentSnapshot.Role;
        FLinearColor lc = FEdgeMafiaRoleColours::ForRole(role, bAlive);
        FColor c = lc.ToFColor(/*bSRGB=*/true);

        DrawDebugSphere(
            GetWorld(),
            GetActorLocation(),
            DebugSphereRadius,
            /*Segments=*/12,
            c,
            /*bPersistentLines=*/false,
            DebugDrawDuration,
            /*DepthPriority=*/0,
            /*Thickness=*/1.5f
        );
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AEdgeMafiaAgentActor::InitialiseActor(int32 InAgentID)
{
    AgentID = InAgentID;

    // Set a preliminary label so the actor is identifiable in the viewport
    // immediately after spawning – before simulation results are in.
    IdentityLabel->SetText(
        FText::FromString(FString::Printf(TEXT("P%d | --"), AgentID))
    );
    StatusLabel->SetText(FText::FromString(TEXT("SIM RUNNING…")));
    StatusLabel->SetTextRenderColor(FColor(200, 200, 80));

    OnAgentSpawned(AgentID);   // BP event – play spawn VFX / sound here
}

// ─────────────────────────────────────────────────────────────────────────────
void AEdgeMafiaAgentActor::SyncWithAgentData(const FEdgeMafiaAgent& Data)
{
    bool bWasAlive = AgentSnapshot.bAlive;
    AgentSnapshot = Data;

    RefreshVisuals();

    // Fire elimination event only on the alive→dead transition.
    if (bWasAlive && !Data.bAlive)
        OnAgentEliminated();

    OnSimulationDataReceived(Data);   // always fire (BP result panel hook)
}

// ─────────────────────────────────────────────────────────────────────────────
void AEdgeMafiaAgentActor::RefreshVisuals()
{
    const FEdgeMafiaAgent& D = AgentSnapshot;

    // ── Colour ────────────────────────────────────────────────────────────
    FLinearColor roleColour = FEdgeMafiaRoleColours::ForRole(D.Role, D.bAlive);
    ApplyColour(roleColour);

    // ── Identity label ─────────────────────────────────────────────────────
    //   "P3 | Detective"
    FString idLine = FString::Printf(TEXT("%s | %s"),
        *D.Name,
        *FEdgeMafiaRoleColours::RoleName(D.Role));
    IdentityLabel->SetText(FText::FromString(idLine));
    IdentityLabel->SetTextRenderColor(roleColour.ToFColor(true));

    // ── Status label ───────────────────────────────────────────────────────
    //   "ALIVE   R: +1.0"  or  "ELIMINATED"
    FString statusLine;
    FColor  statusColour;
    if (D.bAlive)
    {
        statusLine   = FString::Printf(TEXT("ALIVE     R: %+.1f"), D.TotalReward);
        statusColour = FColor(120, 255, 120);
    }
    else
    {
        statusLine   = FString::Printf(TEXT("ELIMINATED  R: %+.1f"), D.TotalReward);
        statusColour = FColor(255, 80, 80);
    }
    StatusLabel->SetText(FText::FromString(statusLine));
    StatusLabel->SetTextRenderColor(statusColour);

    // ── Mesh scale: shrink dead agents to 40% to signal elimination visually
    if (AgentMesh)
        AgentMesh->SetRelativeScale3D(D.bAlive ? FVector(0.6f) : FVector(0.24f));

    UpdateLabels();
}

// ─────────────────────────────────────────────────────────────────────────────
void AEdgeMafiaAgentActor::ApplyColour(FLinearColor Colour)
{
    // Dynamic material path (requires a material with a vector parameter)
    if (DynMaterial && ColourParameterName != NAME_None)
    {
        DynMaterial->SetVectorParameterValue(ColourParameterName, Colour);
        return;
    }

    // If no dynamic material, try to create one now (material may have been
    // assigned after BeginPlay by a Blueprint constructor override).
    if (AgentMesh && AgentMesh->GetMaterial(0) && !DynMaterial)
    {
        DynMaterial = AgentMesh->CreateAndSetMaterialInstanceDynamic(0);
        if (DynMaterial && ColourParameterName != NAME_None)
            DynMaterial->SetVectorParameterValue(ColourParameterName, Colour);
    }
    // No mesh / no material → colour is shown only via debug sphere in Tick().
}

// ─────────────────────────────────────────────────────────────────────────────
void AEdgeMafiaAgentActor::UpdateLabels()
{
    // Keep labels facing up (fixed Z, no roll) so they always read correctly
    // regardless of camera angle.
    IdentityLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight));
    StatusLabel->SetRelativeLocation(FVector(0.f, 0.f, StatusLabelHeight));
}
