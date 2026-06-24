#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFlowSubsystem.h"
#include "GameFlowServerBootstrap.generated.h"

/**
 * AGameFlowServerBootstrap — minimal GameMode that wires up the GameFlow
 * lifecycle on a dedicated server.
 *
 * Drop this into your project as-is or adapt it to your own GameMode subclass.
 * The heavy lifting lives in UGameFlowSubsystem; this class is the thin glue
 * that calls the right SDK methods at the right points in the server lifecycle.
 *
 * Usage:
 *   1. Add the GameFlow plugin to your project (see sdk/unreal/GameFlow/README.md).
 *   2. Set this class (or your subclass) as the GameMode for your dedicated-server map.
 *   3. Build and run on GameFlow — the subsystem connects automatically (sidecar mode).
 *      Off-platform the SDK falls back to local mode with no configuration required.
 */
UCLASS()
class AGameFlowServerBootstrap : public AGameModeBase
{
    GENERATED_BODY()

public:
    // -------------------------------------------------------------------------
    // GameMode overrides
    // -------------------------------------------------------------------------

    /** Called when the map is loaded and the game starts. Connects and marks ready. */
    virtual void BeginPlay() override;

    /** Called when a player logs in — registers the session with GameFlow. */
    virtual void PostLogin(APlayerController* NewPlayer) override;

    /** Called when a player logs out — removes the session from GameFlow. */
    virtual void Logout(AController* Exiting) override;

private:
    /** Helper: returns the subsystem or logs a warning and returns null. */
    UGameFlowSubsystem* GetGameFlow() const;

    /** Bound to UGameFlowSubsystem::OnHealthDegraded in BeginPlay. */
    UFUNCTION()
    void OnHealthDegradedEvent();
};
