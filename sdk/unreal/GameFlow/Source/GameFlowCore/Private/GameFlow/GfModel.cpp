#include "GameFlow/GfModel.h"
#include "GameFlow/GfJson.h"

// ---------------------------------------------------------------------------
// File-local helpers — keep the mapper methods readable
// ---------------------------------------------------------------------------

static TSharedPtr<FJsonObject> Sub(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
{
    const TSharedPtr<FJsonObject>* P;
    return (O.IsValid() && O->TryGetObjectField(K, P)) ? *P : nullptr;
}

static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
{
    FString V;
    return (O.IsValid() && O->TryGetStringField(K, V)) ? V : FString();
}

static TMap<FString, FString> StrMap(const TSharedPtr<FJsonObject>& O)
{
    TMap<FString, FString> M;
    if (O.IsValid())
    {
        for (const auto& KV : O->Values)
        {
            FString S;
            // KV.Key may be FString or FSharedString depending on UE version; FString() converts both
            if (KV.Value.IsValid() && KV.Value->TryGetString(S))
                M.Add(FString(KV.Key), S);
        }
    }
    return M;
}

// ---------------------------------------------------------------------------
// FGfModel
// ---------------------------------------------------------------------------

FPlayerList FGfModel::ParseList(const TSharedPtr<FJsonObject>& Root)
{
    FPlayerList L;
    if (!Root.IsValid()) return L;
    L.bTrackingEnabled = true;
    L.Capacity = FGfJson::AsInt64(Root->TryGetField(TEXT("capacity")));
    const TArray<TSharedPtr<FJsonValue>>* Vals;
    if (Root->TryGetArrayField(TEXT("values"), Vals))
        for (const auto& V : *Vals)
        {
            FString S;
            if (V->TryGetString(S)) L.SessionIds.Add(S);
        }
    return L;
}

FServerInfo FGfModel::ParseGameServer(const TSharedPtr<FJsonObject>& Root)
{
    FServerInfo I;
    if (!Root.IsValid()) return I;

    // Tolerate proto snake_case (real runtime) and camelCase (local/fixtures)
    TSharedPtr<FJsonObject> Meta = Sub(Root, TEXT("object_meta"));
    if (!Meta.IsValid()) Meta = Sub(Root, TEXT("objectMeta"));

    const TSharedPtr<FJsonObject> Status = Sub(Root, TEXT("status"));
    const TSharedPtr<FJsonObject> Ann    = Sub(Meta, TEXT("annotations"));

    I.Name        = Str(Meta,   TEXT("name"));
    I.State       = Str(Status, TEXT("state"));
    I.Address     = Str(Status, TEXT("address"));
    I.Labels      = StrMap(Sub(Meta, TEXT("labels")));
    I.Annotations = StrMap(Ann);
    I.Region      = I.Annotations.FindRef(TEXT("GAMEFLOW_REGION"));
    I.BuildId     = I.Annotations.FindRef(TEXT("GAMEFLOW_BUILD_ID"));

    // Parse ports array
    const TArray<TSharedPtr<FJsonValue>>* Ports;
    if (Status.IsValid() && Status->TryGetArrayField(TEXT("ports"), Ports))
    {
        for (const auto& P : *Ports)
        {
            const TSharedPtr<FJsonObject>& PO = P->AsObject();
            if (PO.IsValid())
            {
                FServerPort SP;
                SP.Name = Str(PO, TEXT("name"));
                SP.Port = (int32)FGfJson::AsInt64(PO->TryGetField(TEXT("port")));
                I.Ports.Add(SP);
            }
        }
    }

    // Parse players list; if the block is absent, leave Players default (untracked)
    const TSharedPtr<FJsonObject> Players = Sub(Sub(Status, TEXT("lists")), TEXT("players"));
    if (Players.IsValid()) I.Players = ParseList(Players);

    return I;
}

bool FGfModel::PayloadOf(const FServerInfo& Info, FString& Out)
{
    if (const FString* P = Info.Annotations.Find(PayloadAnnotation))
    {
        Out = *P;
        return true;
    }
    return false;
}
