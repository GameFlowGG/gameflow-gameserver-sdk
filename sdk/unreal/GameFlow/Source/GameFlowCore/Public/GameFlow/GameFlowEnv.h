#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CString.h"

/**
 * Injectable environment-variable reader used internally.
 *
 * The Provider function returns true + fills Out when the variable exists.
 * When Provider is null the real process environment is read via
 * FPlatformMisc::GetEnvironmentVariable.
 */
class GAMEFLOWCORE_API FGameFlowEnvReader
{
public:
    using FProvider = TFunction<bool(const FString&, FString&)>;

    explicit FGameFlowEnvReader(FProvider InProvider = nullptr)
        : Provider(InProvider ? MoveTemp(InProvider) : FProvider(
            [](const FString& Name, FString& Out) -> bool
            {
                Out = FPlatformMisc::GetEnvironmentVariable(*Name);
                return !Out.IsEmpty();
            }))
    {}

    /** Returns true when the variable exists and is non-empty; fills Out. */
    bool Get(const FString& Name, FString& Out) const
    {
        return Provider(Name, Out);
    }

    /**
     * Returns true when the variable exists and parses as a valid integer;
     * fills Out with the parsed value. Returns false when absent OR non-numeric.
     */
    bool PortVar(const FString& Name, int32& Out) const
    {
        FString Raw;
        if (!Get(Name, Raw))
        {
            return false;
        }
        if (!FCString::IsNumeric(*Raw))
        {
            return false;
        }
        Out = FCString::Atoi(*Raw);
        return true;
    }

private:
    FProvider Provider;
};
