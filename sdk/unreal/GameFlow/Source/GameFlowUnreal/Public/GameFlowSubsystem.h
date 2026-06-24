#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFlow/GameFlowClient.h"
#include "GameFlow/GameFlowDelegates.h"
#include "GameFlow/GameFlowOptions.h"
#include "GameFlow/GameFlowTypes.h"
#include "GameFlowSubsystem.generated.h"

// ---------------------------------------------------------------------------
// Dynamic multicast delegate declarations for Blueprint event binding
// ---------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGfOnServerInfo, FServerInfo, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGfOnPayloadChanged, const FString&, Payload, bool, bPresent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGfOnHealthDegraded);

/**
 * UGameFlowSubsystem — thin Blueprint/UObject wrapper over FGameFlowClient.
 *
 * Add to your GameInstance to expose GameFlow lifecycle, player tracking,
 * and server-info watch callbacks to Blueprint graphs. From C++ you can also
 * call Start/Ready/Shutdown/GetPayload/GetInfo directly with result delegates.
 *
 * Lifecycle: Initialize() constructs the client (but does NOT Start it).
 * Call Start() from your game logic when you are ready to connect.
 */
UCLASS()
class GAMEFLOWUNREAL_API UGameFlowSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    // Lifecycle — C++ delegate form (cannot be Blueprint-exposed: delegate types
    // take non-USTRUCT/non-UCLASS params that UHT cannot represent).
    // -----------------------------------------------------------------------

    /** Connect to the runtime (or local stub). Must be called before any other operation. */
    void Start(FGfVoidResult OnDone);

    /** Mark the server ready; starts the automatic health heartbeat in sidecar mode. */
    void Ready(FGfVoidResult OnDone);

    /** End the match. */
    void Shutdown(FGfVoidResult OnDone);

    /** Fetch the opaque launch payload. */
    void GetPayload(FGfPayloadResult OnDone);

    /** Fetch the full server-info snapshot. */
    void GetInfo(FGfInfoResult OnDone);

    /** Add a connected player session. */
    void ConnectPlayer(const FString& SessionId, FGfVoidResult OnDone);

    /** Remove a connected player session. */
    void DisconnectPlayer(const FString& SessionId, FGfRemoveResult OnDone);

    // -----------------------------------------------------------------------
    // Synchronous reads — Blueprint-pure, return cached values
    // -----------------------------------------------------------------------

    /** Current number of connected players. Returns 0 before Start(). */
    UFUNCTION(BlueprintPure, Category = "GameFlow")
    int32 PlayerCount() const;

    /** Session IDs of all connected players. Returns empty before Start(). */
    UFUNCTION(BlueprintPure, Category = "GameFlow")
    TArray<FString> PlayerSessionIds() const;

    /** Max player capacity reported by the runtime. Returns 0 before Start(). */
    UFUNCTION(BlueprintPure, Category = "GameFlow")
    int64 PlayerCapacity() const;

    /** Whether the runtime has player tracking enabled. Returns false before Start(). */
    UFUNCTION(BlueprintPure, Category = "GameFlow")
    bool PlayersTracked() const;

    /** The resolved operating mode after Start(). Returns Local before Start(). */
    UFUNCTION(BlueprintPure, Category = "GameFlow")
    EGameFlowMode GetMode() const;

    // -----------------------------------------------------------------------
    // Blueprint events — assign in Blueprint or via AddDynamic in C++
    // -----------------------------------------------------------------------

    /** Fires whenever a new server-info frame arrives from the watch stream. */
    UPROPERTY(BlueprintAssignable, Category = "GameFlow")
    FGfOnServerInfo OnServerInfo;

    /** Fires whenever the launch payload changes. */
    UPROPERTY(BlueprintAssignable, Category = "GameFlow")
    FGfOnPayloadChanged OnPayloadChanged;

    /** Fires when repeated health pings fail and the connection is considered degraded. */
    UPROPERTY(BlueprintAssignable, Category = "GameFlow")
    FGfOnHealthDegraded OnHealthDegraded;

    // -----------------------------------------------------------------------
    // USubsystem overrides
    // -----------------------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    TUniquePtr<FGameFlowClient> Client;
};
