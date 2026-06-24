#include "GameFlow/GfBackoff.h"

FGfBackoff::FGfBackoff(const FRandomStream& InRng, int32 InBaseMs, int32 InCapMs)
    : Rng(InRng), BaseMs(InBaseMs), CapMs(InCapMs), Current(InBaseMs)
{
}

int32 FGfBackoff::NextDelayMs()
{
    const int32 Raw = FMath::Min(Current, CapMs);
    const double Jitter = 1.0 + (Rng.FRand() * 0.4 - 0.2);
    const int32 Delay = (int32)(Raw * Jitter);
    Current = FMath::Min(Current * 2, CapMs);
    return Delay;
}

void FGfBackoff::Reset()
{
    Current = BaseMs;
}
