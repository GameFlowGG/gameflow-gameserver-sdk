#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GfTransport.h"

/**
 * Default IGfTransportFactory used in Sidecar mode: builds the real
 * FSidecarTransport. The factory is a seam so tests can inject a fake transport
 * via FGameFlowOptions::TransportFactory; production leaves it null and the
 * client falls back to this.
 */
class FRealTransportFactory : public IGfTransportFactory
{
public:
    virtual TUniquePtr<IGfTransport> Create(
        const FString& BaseUrl,
        const FGameFlowOptions& Options,
        IGameFlowLogger& Logger) override;
};
