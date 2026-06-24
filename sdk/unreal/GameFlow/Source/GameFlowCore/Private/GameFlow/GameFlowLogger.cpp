#include "GameFlow/GameFlowLogger.h"

DEFINE_LOG_CATEGORY(LogGameFlow);

void FGameFlowLog::Info(const FString& M)    { UE_LOG(LogGameFlow, Log,     TEXT("[gameflow] %s"), *M); }
void FGameFlowLog::Warn(const FString& M)    { UE_LOG(LogGameFlow, Warning, TEXT("[gameflow] %s"), *M); }
void FGameFlowLog::Error(const FString& M)   { UE_LOG(LogGameFlow, Error,   TEXT("[gameflow] %s"), *M); }
void FGameFlowLog::Verbose(const FString& M) { UE_LOG(LogGameFlow, Verbose, TEXT("[gameflow] %s"), *M); }
