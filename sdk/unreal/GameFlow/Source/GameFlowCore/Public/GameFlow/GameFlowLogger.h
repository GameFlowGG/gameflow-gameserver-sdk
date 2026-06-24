#pragma once

#include "CoreMinimal.h"

GAMEFLOWCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogGameFlow, Log, All);

/** Injectable logger interface — implement to redirect GameFlow output. */
class GAMEFLOWCORE_API IGameFlowLogger
{
public:
    virtual ~IGameFlowLogger() = default;
    virtual void Info(const FString& M) = 0;
    virtual void Warn(const FString& M) = 0;
    virtual void Error(const FString& M) = 0;
    virtual void Verbose(const FString& M) = 0;
};

/** Default logger — forwards to UE_LOG(LogGameFlow, ...) with a [gameflow] prefix. */
class GAMEFLOWCORE_API FGameFlowLog : public IGameFlowLogger
{
public:
    void Info(const FString& M) override;
    void Warn(const FString& M) override;
    void Error(const FString& M) override;
    void Verbose(const FString& M) override;
};

/** Null logger — discards all messages. Useful for silencing output in tests or production. */
class GAMEFLOWCORE_API FNullGameFlowLogger : public IGameFlowLogger
{
public:
    void Info(const FString& M) override    {}
    void Warn(const FString& M) override    {}
    void Error(const FString& M) override   {}
    void Verbose(const FString& M) override {}
};
