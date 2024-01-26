#pragma once

#include "CoreMinimal.h"
#include "RHGameEngine.h"

struct FPCom_TelemetryPrimaryStats
{
    // stats that are sampled per frame
    struct
    {
        // The most recent frame (NOTE - measured in milliseconds!)
        float FrameTime;
        float GameThreadTime;
        float RenderThreadTime;
        float RHIThreadTime;
        float GPUTime;
        float DeltaTime;


        // accumulators
        int32 TickCount;
        int32 DelayedTickCount; // ticks that were behind

        // high water marks
        float MaxFrameTime;
        float MaxDeltaTime;
    } PerFrameStats;

    // stats that are sampled per second
    struct
    {
        // hardware
        float MemoryWS;
        float MemoryVB;
        float CPUProcess;
        float CPUMachine;

        // gamestate
        int32 PlayerControllerCount;
        int32 AIControllerCount;
        int32 PawnCount;
    } PerSecondStats;
};

struct FPCom_TelemetryNetworkStats
{
    // stats that are sampled per frame
    /*
    struct
    {
        // The most recent frame (NOTE - measured in milliseconds!)
        float FrameTime;
    } PerFrameStats;
    */

    // stats that are sampled per second
    struct
    {
        // network
        int32 ConnectionCount;
        float Ping;
        
        int32 PacketsIn;
        int32 PacketsOut;
        int32 PacketsTotal;

        int32 PacketsLostIn;
        int32 PacketsLostOut;
        int32 PacketsLostTotal;
        float PacketLoss;
    } PerSecondStats;
};


DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTelemetrySampledNative, const FPCom_TelemetryPrimaryStats&, const FPCom_TelemetryNetworkStats&);

class FPCom_RuntimeTelemetryCollector : public TSharedFromThis<FPCom_RuntimeTelemetryCollector>
{
public:
    FPCom_RuntimeTelemetryCollector();
    virtual ~FPCom_RuntimeTelemetryCollector();

    virtual bool Init(URHGameEngine* pEngine, UWorld* pWorld, const FString& InTelemtryId);
    virtual void UpdateWorld(UWorld* pWorld);
    virtual void Tick(float DeltaSeconds);

    FOnTelemetrySampledNative& OnTelemetrySampledNative() { return OnTelemetrySampledNativeDel; }

    const FString& GetTelemetryId() const { return TelemetryId; }
    const UWorld* GetWorld() const { return ParentWorld.Get(); }

protected:
    TWeakObjectPtr<URHGameEngine> ParentEngine;
    TWeakObjectPtr<UWorld> ParentWorld;
    FString TelemetryId;

    // File to log to
    class FArchive* StatsFileCSV;

	bool bHasWrittenCSVHeader;

    // Delegate that fires on the per second stats being collected
    FOnTelemetrySampledNative OnTelemetrySampledNativeDel;

    struct
    {
        bool bEnabled;
        bool bWriteToFile;

        int32 FileWriteFrequency;

        ELogVerbosity::Type LogVerbosity;
        FName LogCategory;
    } LogFeatures;

    // Simple counter used to determine when seconds roll over
    int32 CurrentSecondCounter;

    // initializes LogFeatures from ELogFlagsFromCore
    virtual void InitFeaturesFromConfig();

    // clears time tracking - MUST be called before stats are ticked first time
    void ResetTimeTracking();

    // clears tracked stats - MUST be called before stats are ticked first time
    virtual void ResetStats();

    virtual void CollectPerFrameStats(float DeltaSeconds);
    virtual void CollectPerSecondStats();

    virtual FString GetDetailedStatsToLog(const FDateTime& Time, bool bWriteHeader);

private:
    
    int32 LastDBWrite;
    int32 LastFileWrite;

    double TimeTracker;

    FPCom_TelemetryPrimaryStats PrimaryStats;
    FPCom_TelemetryNetworkStats NetworkStats;

    void ResetPrimaryStats();
    void ResetNetworkStats();

    void WriteDBStats();
    void WriteFileStats();
};
