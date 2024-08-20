#pragma once

#include "Engine/GameEngine.h"
#include "RHGameEngine.generated.h"

UCLASS(config=Engine)
class RALLYHERESTART_API URHGameEngine : public UGameEngine
{
    GENERATED_UCLASS_BODY()

public:
    virtual bool LoadMap(FWorldContext &WorldContext, FURL URL, class UPendingNetGame *Pending, FString &Error);

    // UEngine interface
    virtual void Init(class IEngineLoop* InEngineLoop) override;
    virtual void Tick(float DeltaSeconds, bool bIdleMode) override;

    // Gets Telemetry collector so client can access recorded stats
    const TSharedPtr<class FPCom_RuntimeTelemetryCollector>& GetTelemetryCollector() { return RuntimeTelemetryCollector; }

protected:
    virtual void OnPostLoadMapWithWorld(UWorld* pWorld);

private:
    //bool ShouldCollectRuntimeTelemetry() { return (IsRunningDedicatedServer() || IsRunningGame()); }
	bool ShouldCollectRuntimeTelemetry() { return false; }
    virtual void CreateRuntimeTelemetryCollector(class UWorld* pWorld);

    // stats collector with hooks to send stats to DB/file for use by operations
    TSharedPtr<class FPCom_RuntimeTelemetryCollector> RuntimeTelemetryCollector;
};
