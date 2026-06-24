#include "GameFlow/GfJson.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Policies/CondensedJsonPrintPolicy.h"

TSharedPtr<FJsonObject> FGfJson::ParseObject(const FString& Text)
{
    TSharedPtr<FJsonObject> Obj;
    const auto Reader = TJsonReaderFactory<TCHAR>::Create(Text);
    return (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid()) ? Obj : nullptr;
}

int64 FGfJson::AsInt64(const TSharedPtr<FJsonValue>& V)
{
    if (!V.IsValid()) return 0;
    if (V->Type == EJson::String) { return FCString::Atoi64(*V->AsString()); }
    if (V->Type == EJson::Number) { return (int64)V->AsNumber(); }
    return 0;
}

FString FGfJson::ObjEmpty()
{
    return TEXT("{}");
}

FString FGfJson::ObjValue(const FString& SessionId)
{
    // Use the JSON writer so escaping is correct.
    FString Out;
    const auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    W->WriteObjectStart();
    W->WriteValue(TEXT("value"), SessionId);
    W->WriteObjectEnd();
    W->Close();
    return Out;
}
