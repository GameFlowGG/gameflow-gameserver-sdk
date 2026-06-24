#if WITH_DEV_AUTOMATION_TESTS

#include "Fixture.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ---------------------------------------------------------------------------
// serve.mjs lives at <repo>/tools/conformance/serve.mjs. Builds and tests run
// on the same machine, so the compile-time __FILE__ path is valid at runtime.
// We can't count a fixed number of directories up because the plugin is reached
// through a symlink (HostProject/Plugins/GameFlow -> ../../GameFlow) whose depth
// differs from the canonical source tree, so instead walk up from this file
// until tools/conformance/serve.mjs is found.
// ---------------------------------------------------------------------------
FString Fixture::ServeScriptPath()
{
    FString Dir = FPaths::GetPath(FString(ANSI_TO_TCHAR(__FILE__))); // .../Private/Tests
    for (int32 i = 0; i < 32 && !Dir.IsEmpty(); ++i)
    {
        const FString Candidate = FPaths::Combine(Dir, TEXT("tools"), TEXT("conformance"), TEXT("serve.mjs"));
        if (FPaths::FileExists(Candidate))
        {
            return Candidate;
        }
        const FString Parent = FPaths::GetPath(Dir);
        if (Parent == Dir)
        {
            break; // reached filesystem root
        }
        Dir = Parent;
    }
    // Fall back to the canonical relative path; the constructor reports failure
    // clearly (Port stays 0) if it doesn't exist.
    return FString();
}

bool Fixture::NodeAvailable()
{
    // Spawn `node --version`, capture stdout, require major >= 23 (or 23.6+).
    void* Read = nullptr;
    void* Write = nullptr;
    if (!FPlatformProcess::CreatePipe(Read, Write))
    {
        return false;
    }

    FProcHandle Handle = FPlatformProcess::CreateProc(
        TEXT("node"), TEXT("--version"),
        /*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
        nullptr, 0, nullptr, Write, nullptr);

    if (!Handle.IsValid())
    {
        FPlatformProcess::ClosePipe(Read, Write);
        return false;
    }

    FString Output;
    const double Start = FPlatformTime::Seconds();
    while (FPlatformProcess::IsProcRunning(Handle) && (FPlatformTime::Seconds() - Start) < 5.0)
    {
        Output += FPlatformProcess::ReadPipe(Read);
        FPlatformProcess::Sleep(0.01f);
    }
    Output += FPlatformProcess::ReadPipe(Read);

    FPlatformProcess::TerminateProc(Handle, /*KillTree*/ true);
    FPlatformProcess::CloseProc(Handle);
    FPlatformProcess::ClosePipe(Read, Write);

    // Output looks like "v25.1.0". Strip the leading 'v' and parse major.minor.
    Output.TrimStartAndEndInline();
    if (!Output.StartsWith(TEXT("v")))
    {
        return false;
    }
    Output.RemoveAt(0); // drop 'v'

    TArray<FString> Parts;
    Output.ParseIntoArray(Parts, TEXT("."), /*CullEmpty*/ true);
    if (Parts.Num() < 2)
    {
        return false;
    }
    const int32 Major = FCString::Atoi(*Parts[0]);
    const int32 Minor = FCString::Atoi(*Parts[1]);
    return (Major > 23) || (Major == 23 && Minor >= 6);
}

Fixture::Fixture(const FString& Args)
{
    if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
    {
        return;
    }

    const FString Script = ServeScriptPath();
    const FString FullArgs = FString::Printf(TEXT("\"%s\" %s"), *Script, *Args);

    ProcHandle = FPlatformProcess::CreateProc(
        TEXT("node"), *FullArgs,
        /*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
        nullptr, 0, nullptr, /*PipeWriteChild*/ WritePipe, nullptr);

    if (!ProcHandle.IsValid())
    {
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        ReadPipe = WritePipe = nullptr;
        return;
    }

    // Accumulate stdout until both PORT= and CONTROL_PORT= lines are present.
    FString Accumulated;
    const double Start = FPlatformTime::Seconds();
    while (FPlatformTime::Seconds() - Start < 10.0)
    {
        Accumulated += FPlatformProcess::ReadPipe(ReadPipe);

        TArray<FString> Lines;
        Accumulated.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
        for (const FString& Line : Lines)
        {
            if (Port == 0 && Line.StartsWith(TEXT("PORT=")))
            {
                Port = FCString::Atoi(*Line.RightChop(5));
            }
            else if (ControlPort == 0 && Line.StartsWith(TEXT("CONTROL_PORT=")))
            {
                ControlPort = FCString::Atoi(*Line.RightChop(13));
            }
        }

        if (Port != 0 && ControlPort != 0)
        {
            break;
        }
        FPlatformProcess::Sleep(0.01f);
    }
}

Fixture::~Fixture()
{
    if (ProcHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(ProcHandle, /*KillTree*/ true);
        FPlatformProcess::CloseProc(ProcHandle);
    }
    if (ReadPipe || WritePipe)
    {
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        ReadPipe = WritePipe = nullptr;
    }
}

void Fixture::ControlPost(const FString& Path)
{
    const FString Url = FString::Printf(TEXT("http://127.0.0.1:%d%s"), ControlPort, *Path);

    bool bDone = false;
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(TEXT("POST"));
    Req->SetTimeout(3.0f);
    Req->OnProcessRequestComplete().BindLambda(
        [&bDone](FHttpRequestPtr, FHttpResponsePtr, bool) { bDone = true; });
    Req->ProcessRequest();

    GfPumpUntil([&bDone] { return bDone; }, 3.0);
}

int32 Fixture::CountRequests(const FString& Path)
{
    const FString Url = FString::Printf(TEXT("http://127.0.0.1:%d/requests"), ControlPort);

    bool bDone = false;
    FString Body;
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(TEXT("GET"));
    Req->SetTimeout(3.0f);
    Req->OnProcessRequestComplete().BindLambda(
        [&bDone, &Body](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
        {
            if (bConnected && Resp.IsValid())
            {
                Body = Resp->GetContentAsString();
            }
            bDone = true;
        });
    Req->ProcessRequest();

    GfPumpUntil([&bDone] { return bDone; }, 3.0);

    // /requests is a JSON array of {method, path, body}; count path matches.
    int32 Count = 0;
    TArray<TSharedPtr<FJsonValue>> Entries;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (FJsonSerializer::Deserialize(Reader, Entries))
    {
        for (const TSharedPtr<FJsonValue>& Entry : Entries)
        {
            const TSharedPtr<FJsonObject>* Obj;
            if (Entry.IsValid() && Entry->TryGetObject(Obj) && (*Obj).IsValid())
            {
                FString EntryPath;
                if ((*Obj)->TryGetStringField(TEXT("path"), EntryPath) && EntryPath == Path)
                {
                    ++Count;
                }
            }
        }
    }
    return Count;
}

#endif // WITH_DEV_AUTOMATION_TESTS
