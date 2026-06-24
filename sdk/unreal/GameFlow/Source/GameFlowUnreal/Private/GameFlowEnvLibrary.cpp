#include "GameFlowEnvLibrary.h"
#include "GameFlow/GameFlowEnv.h"

// ---------------------------------------------------------------------------
// Helpers (local to this TU)
// ---------------------------------------------------------------------------

namespace
{
    /** Singleton real-env reader; constructed once, used by all library statics. */
    static FGameFlowEnvReader& RealReader()
    {
        static FGameFlowEnvReader Instance;
        return Instance;
    }

    /** Normalize a user-supplied port name: trim, upper, spaces→underscores. */
    static FString NormalizeName(FString Name)
    {
        return Name.TrimStartAndEnd().ToUpper().Replace(TEXT(" "), TEXT("_"));
    }
}

// ---------------------------------------------------------------------------
// UGameFlowEnvLibrary implementation
// ---------------------------------------------------------------------------

bool UGameFlowEnvLibrary::DefaultPort(int32& OutPort)
{
    return RealReader().PortVar(TEXT("GAMEFLOW_DEFAULT_PORT"), OutPort);
}

bool UGameFlowEnvLibrary::Port(FString Name, int32& OutPort)
{
    const FString Key = FString::Printf(TEXT("GAMEFLOW_%s_PORT"), *NormalizeName(MoveTemp(Name)));
    return RealReader().PortVar(Key, OutPort);
}

bool UGameFlowEnvLibrary::TlsDefaultPort(int32& OutPort)
{
    return RealReader().PortVar(TEXT("GAMEFLOW_TLS_DEFAULT_PORT"), OutPort);
}

bool UGameFlowEnvLibrary::TlsPort(FString Name, int32& OutPort)
{
    const FString Key = FString::Printf(TEXT("GAMEFLOW_TLS_%s_PORT"), *NormalizeName(MoveTemp(Name)));
    return RealReader().PortVar(Key, OutPort);
}

FString UGameFlowEnvLibrary::Region()
{
    FString Out;
    RealReader().Get(TEXT("GAMEFLOW_REGION"), Out);
    return Out;
}

FString UGameFlowEnvLibrary::BuildId()
{
    FString Out;
    RealReader().Get(TEXT("GAMEFLOW_BUILD_ID"), Out);
    return Out;
}
