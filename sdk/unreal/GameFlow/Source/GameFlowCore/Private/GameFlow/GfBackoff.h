#pragma once
#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/** Internal exponential-backoff helper. Not public API. */
struct FGfBackoff
{
    FGfBackoff(const FRandomStream& InRng, int32 InBaseMs = 250, int32 InCapMs = 4000);

    /** Return next delay in milliseconds, then double the internal counter (capped). */
    int32 NextDelayMs();

    /** Reset the internal counter back to BaseMs. */
    void Reset();

private:
    FRandomStream Rng;
    int32 BaseMs;
    int32 CapMs;
    int32 Current;
};
