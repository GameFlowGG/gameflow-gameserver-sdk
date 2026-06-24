#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Containers/Ticker.h"

class FAutomationTestBase;

// ---------------------------------------------------------------------------
// GfPumpUntil — drive the engine forward until a predicate is satisfied.
//
// HTTP completions (and the fixture's control responses) only fire while the
// HTTP manager and core ticker tick. Automation tests run outside the normal
// game loop, so latent results never arrive unless we pump manually. Returns
// the final predicate value so callers can assert it timed out vs. completed.
// ---------------------------------------------------------------------------
inline bool GfPumpUntil(TFunction<bool()> Predicate, double TimeoutSec)
{
    const double Start = FPlatformTime::Seconds();
    while (!Predicate() && (FPlatformTime::Seconds() - Start) < TimeoutSec)
    {
        FHttpModule::Get().GetHttpManager().Tick(0.01f);
        FTSTicker::GetCoreTicker().Tick(0.01f);
        FPlatformProcess::Sleep(0.005f);
    }
    return Predicate();
}

// Test-facing overload — accepts the automation test (unused, kept for parity
// with the Unity/Go harnesses and to read naturally at call sites).
inline bool GfPumpUntil(FAutomationTestBase& /*Test*/, TFunction<bool()> Predicate, double TimeoutSec)
{
    return GfPumpUntil(MoveTemp(Predicate), TimeoutSec);
}

// ---------------------------------------------------------------------------
// Fixture — spawns the Node conformance server (tools/conformance/serve.mjs)
// and exposes its sidecar port plus a small control surface for assertions.
//
// Mirrors the Unity/Go harness: the constructor blocks until both PORT and
// CONTROL_PORT lines have been read from the child's stdout; the destructor
// terminates the process tree and closes the pipe.
// ---------------------------------------------------------------------------
class Fixture
{
public:
    /** True when a Node >= 23.6 runtime is on PATH (fixture-backed tests skip otherwise). */
    static bool NodeAvailable();

    /** Spawn `node serve.mjs <Args>` and block until the ports are known. */
    explicit Fixture(const FString& Args);

    ~Fixture();

    /** Sidecar HTTP port; 0 when the fixture failed to start. */
    int32 Port = 0;

    /** Control HTTP port; 0 when the fixture failed to start. */
    int32 ControlPort = 0;

    /** True when both ports were parsed and the child is running. */
    bool IsValid() const { return Port != 0 && ControlPort != 0 && ProcHandle.IsValid(); }

    /** Fire a control POST (e.g. /push-update) and block until it completes. */
    void ControlPost(const FString& Path);

    /** Count recorded sidecar requests whose `path` equals Path. */
    int32 CountRequests(const FString& Path);

private:
    FProcHandle ProcHandle;
    void*       ReadPipe  = nullptr;
    void*       WritePipe = nullptr;

    /** Absolute path to tools/conformance/serve.mjs, derived from this file. */
    static FString ServeScriptPath();
};

#endif // WITH_DEV_AUTOMATION_TESTS
