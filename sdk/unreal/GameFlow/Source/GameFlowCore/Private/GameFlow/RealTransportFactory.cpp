#include "GameFlow/RealTransportFactory.h"
#include "GameFlow/SidecarTransport.h"
#include "GameFlow/GameFlowOptions.h"

TUniquePtr<IGfTransport> FRealTransportFactory::Create(
    const FString& BaseUrl,
    const FGameFlowOptions& Options,
    IGameFlowLogger& Logger)
{
    return MakeUnique<FSidecarTransport>(BaseUrl, Options.RequestTimeoutMs, Logger);
}
