#include "GameFlow/GameFlowError.h"

FGameFlowError FGameFlowError::Make(EGameFlowErrorCode C, const FString& M, int64 Cap)
{
    FGameFlowError E; E.Code = C; E.Message = M; E.Capacity = Cap; return E;
}

FString FGameFlowError::CodeString() const
{
    switch (Code) {
        case EGameFlowErrorCode::SidecarUnavailable:     return TEXT("SIDECAR_UNAVAILABLE");
        case EGameFlowErrorCode::PlayerAlreadyConnected: return TEXT("PLAYER_ALREADY_CONNECTED");
        case EGameFlowErrorCode::ServerFull:             return TEXT("SERVER_FULL");
        case EGameFlowErrorCode::PlayerTrackingDisabled: return TEXT("PLAYER_TRACKING_DISABLED");
        case EGameFlowErrorCode::NotConnected:           return TEXT("NOT_CONNECTED");
        case EGameFlowErrorCode::RequestFailed:          return TEXT("REQUEST_FAILED");
        default:                                         return FString();
    }
}

FGameFlowError FGameFlowError::Ok()
{
    return FGameFlowError{};
}
