#include "RallyHereStart.h"
#include "GameFramework/RHGameEngine.h"
#include "GameFramework/RHRuntimeTelemetryCollector.h"
#include "UnrealEngine.h"

#include "RH_GameInstanceSessionSubsystem.h"

URHGameEngine::URHGameEngine(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void URHGameEngine::Init(class IEngineLoop* InEngineLoop)
{
    Super::Init(InEngineLoop);

    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &URHGameEngine::OnPostLoadMapWithWorld);
}

void URHGameEngine::OnPostLoadMapWithWorld(UWorld* pLoadedWorld)
{
    if (pLoadedWorld != nullptr)
    {
        CreateRuntimeTelemetryCollector(pLoadedWorld);
    }
}

void URHGameEngine::CreateRuntimeTelemetryCollector(UWorld* pWorld)
{
	FString SessionId = pWorld->URL.GetOption(RH_SESSION_PARAMETER_NAME, TEXT(""));
	FString InstanceId = pWorld->URL.GetOption(RH_INSTANCE_PARAMETER_NAME, TEXT(""));
	FString TelemetryId = (SessionId.Len() > 0 || InstanceId.Len() > 0) ? SessionId + TEXT("__") + InstanceId : TEXT("");
    if (ShouldCollectRuntimeTelemetry() && TelemetryId.Len() > 0)
    {
        // if we do not have a collecctor, or the old one was for a different instance, make a new one
        if (!RuntimeTelemetryCollector.IsValid() || RuntimeTelemetryCollector->GetTelemetryId() != TelemetryId)
        {
            RuntimeTelemetryCollector = MakeShared<FPCom_RuntimeTelemetryCollector>();
            if (!RuntimeTelemetryCollector->Init(this, pWorld, TelemetryId))
            {
                RuntimeTelemetryCollector = nullptr;
            }
        }
        else if (RuntimeTelemetryCollector->GetWorld() != pWorld)
        {
            RuntimeTelemetryCollector->UpdateWorld(pWorld);
        }
    }
    else
    {
        // abandon old runtime stats collector (should delete if it needed)
        RuntimeTelemetryCollector = nullptr;
    }
}

bool URHGameEngine::LoadMap(FWorldContext &WorldContext, FURL URL, class UPendingNetGame *Pending, FString &Error)
{
    // Notify the back-end that we have started loading a map.
	FString TelemetryId = URL.GetOption(RH_SESSION_PARAMETER_NAME, TEXT(""));

    if (RuntimeTelemetryCollector.IsValid() && RuntimeTelemetryCollector->GetTelemetryId() != TelemetryId)
    {
        // if we are changing Telemetry IDs, abandon old runtime stats collector early, to prevent it potentially tracking map load
        RuntimeTelemetryCollector = nullptr;
    }

    bool bSuccess = Super::LoadMap(WorldContext, URL, Pending, Error);

    return bSuccess;
}


void URHGameEngine::Tick(float DeltaSeconds, bool bIdleMode)
{
    Super::Tick(DeltaSeconds, bIdleMode);

    if (RuntimeTelemetryCollector.IsValid())
    {
        RuntimeTelemetryCollector->Tick(DeltaSeconds);
    }
}
