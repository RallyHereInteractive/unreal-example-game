
#include "RallyHereStart.h"
#include "GameFramework/RHRuntimeTelemetryCollector.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/OutputDeviceFile.h"
#include "EngineUtils.h"
#include "Net/PerfCountersHelpers.h"

FPCom_RuntimeTelemetryCollector::FPCom_RuntimeTelemetryCollector()
    : StatsFileCSV(nullptr)
	, bHasWrittenCSVHeader(false)
{
}

FPCom_RuntimeTelemetryCollector::~FPCom_RuntimeTelemetryCollector()
{
    if (StatsFileCSV != nullptr)
    {
        delete StatsFileCSV;
		StatsFileCSV = nullptr;
    }
}

bool FPCom_RuntimeTelemetryCollector::Init(URHGameEngine* pEngine, UWorld* pWorld, const FString& InTelemetryId)
{
    ParentEngine = pEngine;
    TelemetryId = InTelemetryId;
    UpdateWorld(pWorld);

    FMemory::Memzero(LogFeatures);

    InitFeaturesFromConfig();

    // create log file
    if (LogFeatures.bWriteToFile)
    {
		// Create the file name		
		FString FileName = FPaths::ProjectLogDir() / FString::Printf(TEXT("Stats_%s.csv"), *TelemetryId);

		// save file
		StatsFileCSV = IFileManager::Get().CreateFileWriter(*FileName, FILEWRITE_AllowRead);
    }

#if USE_SERVER_PERF_COUNTERS
    if (LogFeatures.bEnabled)
    {
        // create performance counters for use in network tracking
        IPerfCountersModule::Get().CreatePerformanceCounters();
    }
#endif

    return LogFeatures.bEnabled;
}

void FPCom_RuntimeTelemetryCollector::UpdateWorld(UWorld* pWorld)
{
    ParentWorld = pWorld;

    ResetTimeTracking();
    ResetStats();
}

void FPCom_RuntimeTelemetryCollector::InitFeaturesFromConfig()
{
    uint32 LogFlags = 0;
    FMemory::Memzero(LogFeatures);

	if (IsRunningDedicatedServer())
	{
		LogFeatures.bEnabled = true;
		LogFeatures.bWriteToFile = true;
		LogFeatures.FileWriteFrequency = 1.0f;
	}

    LogFeatures.LogCategory = FName(TEXT("RuntimeTelemetry"));

    if (LogFeatures.bEnabled)
    {
        LogFeatures.LogVerbosity = ELogVerbosity::Log;
    }
    else
    {
        LogFeatures.bWriteToFile = false;
        LogFeatures.LogVerbosity = ELogVerbosity::NoLogging;
    }
}

void FPCom_RuntimeTelemetryCollector::ResetTimeTracking()
{
    TimeTracker = 0.f;
    CurrentSecondCounter = 0.f;
    LastDBWrite = LastFileWrite = -1;
}

void FPCom_RuntimeTelemetryCollector::ResetStats()
{
    ResetPrimaryStats();
    ResetNetworkStats();
}

void FPCom_RuntimeTelemetryCollector::ResetPrimaryStats()
{
    // simple memzero for basic stats
    FMemory::Memzero(PrimaryStats);

    // any specific reset logic here
}

void FPCom_RuntimeTelemetryCollector::ResetNetworkStats()
{
    // simple memzero for basic stats
    FMemory::Memzero(NetworkStats);

    // any specific reset logic here
}

// ticked just after main engine tick!
void FPCom_RuntimeTelemetryCollector::Tick(float DeltaSeconds)
{
    // if logging was disabled, exit
    if (!LogFeatures.bEnabled)
    {
        return;
    }

    TimeTracker += DeltaSeconds;

    // collect any stats that need to be tracked per frame (ex: longest frametime)
    CollectPerFrameStats(DeltaSeconds);

    const int32 NewSecondCounter = FMath::FloorToInt(TimeTracker);
    const bool bIsNewSecond = NewSecondCounter != CurrentSecondCounter;
    CurrentSecondCounter = NewSecondCounter;

    if (bIsNewSecond)
    {
        // collect any stats that need to be tracked once per second (ex: pawn count)
        CollectPerSecondStats();
    }

    const bool bWriteFile = LogFeatures.bWriteToFile && LogFeatures.FileWriteFrequency > 0 && StatsFileCSV != nullptr && (CurrentSecondCounter - LastFileWrite) >= LogFeatures.FileWriteFrequency;

    if (bWriteFile)
    {
        WriteFileStats();
        LastFileWrite = CurrentSecondCounter;
    }

    // if we hit a threshold, reset the stats
    if (bIsNewSecond)
    {
        OnTelemetrySampledNativeDel.Broadcast(PrimaryStats, NetworkStats);
        ResetStats();
    }
}

#define TO_MS(a) (1000*(a))

void FPCom_RuntimeTelemetryCollector::CollectPerFrameStats(float DeltaSeconds)
{
    // if we have access to stat unit data, prefer it
    if (ParentWorld.IsValid() && ParentWorld->GetGameViewport() != nullptr && ParentWorld->GetGameViewport()->GetStatUnitData())
    {
        FStatUnitData* pData = ParentWorld->GetGameViewport()->GetStatUnitData();
        PrimaryStats.PerFrameStats.FrameTime = pData->FrameTime;
        PrimaryStats.PerFrameStats.GameThreadTime = pData->GameThreadTime;
        PrimaryStats.PerFrameStats.RenderThreadTime = pData->RenderThreadTime;
        PrimaryStats.PerFrameStats.RHIThreadTime = pData->RHITTime;
        //PrimaryStats.PerFrameStats.GPUTime = pData->GPUFrameTime;
    }
    else
    {
        PrimaryStats.PerFrameStats.FrameTime = TO_MS(FApp::GetDeltaTime() - FApp::GetIdleTime());
        PrimaryStats.PerFrameStats.GameThreadTime = PrimaryStats.PerFrameStats.FrameTime;
    }

    PrimaryStats.PerFrameStats.DeltaTime = TO_MS(FApp::GetDeltaTime());

    // accumulators
    ++PrimaryStats.PerFrameStats.TickCount;

    if (ParentEngine.IsValid())
    {
        float MaxTickRate = ParentEngine->GetMaxTickRate(DeltaSeconds, false);
        if (MaxTickRate > 0 && DeltaSeconds > (1.1f / MaxTickRate))    // note 1.1 on purpose, 10% overage
        {
            ++PrimaryStats.PerFrameStats.DelayedTickCount;
        }
    }

    // high water marks
    PrimaryStats.PerFrameStats.MaxFrameTime = FMath::Max(PrimaryStats.PerFrameStats.FrameTime, PrimaryStats.PerFrameStats.MaxFrameTime);
    PrimaryStats.PerFrameStats.MaxDeltaTime = FMath::Max(PrimaryStats.PerFrameStats.DeltaTime, PrimaryStats.PerFrameStats.MaxDeltaTime);
}

void FPCom_RuntimeTelemetryCollector::CollectPerSecondStats()
{
    // network
    NetworkStats.PerSecondStats.ConnectionCount = PerfCountersGet(TEXT("NumConnections"), 0);

    NetworkStats.PerSecondStats.Ping = PerfCountersGet(TEXT("AvgPing"), 0.f);

    NetworkStats.PerSecondStats.PacketsIn = PerfCountersGet(TEXT("InPackets"), 0);
    NetworkStats.PerSecondStats.PacketsOut = PerfCountersGet(TEXT("OutPackets"), 0);
    NetworkStats.PerSecondStats.PacketsTotal = NetworkStats.PerSecondStats.PacketsIn + NetworkStats.PerSecondStats.PacketsOut;

    NetworkStats.PerSecondStats.PacketsLostIn = PerfCountersGet(TEXT("InPacketsLost"), 0);
    NetworkStats.PerSecondStats.PacketsLostOut = PerfCountersGet(TEXT("OutPacketsLost"), 0);
    NetworkStats.PerSecondStats.PacketsLostTotal = NetworkStats.PerSecondStats.PacketsLostIn + NetworkStats.PerSecondStats.PacketsLostOut;

    NetworkStats.PerSecondStats.PacketLoss = ((float)NetworkStats.PerSecondStats.PacketsLostTotal) / FMath::Max<float>(1.0f, (float)NetworkStats.PerSecondStats.PacketsTotal);

    // hardware
    // TODO - move UE3 utility to core, then call here
    PrimaryStats.PerSecondStats.MemoryWS = FPlatformMemory::GetStats().UsedPhysical >> 20;
    PrimaryStats.PerSecondStats.MemoryVB = FPlatformMemory::GetStats().UsedVirtual >> 20;
    PrimaryStats.PerSecondStats.CPUProcess = FPlatformTime::GetCPUTime().CPUTimePctRelative;

    // gamestate
    if (ParentWorld.IsValid())
    {
        // samples for per-second stats should not be carried over in current implementation, but ensure we are counting from zero as the implementation may change
        PrimaryStats.PerSecondStats.AIControllerCount = 0;
        PrimaryStats.PerSecondStats.PlayerControllerCount = 0;
        PrimaryStats.PerSecondStats.PawnCount = 0;

        for (auto It = GetWorld()->GetControllerIterator(); It; ++It)
        {
            const AController* Controller = It->Get();
            if (Controller != nullptr)
            {
                if (Controller->IsPlayerController())
                {
                    ++PrimaryStats.PerSecondStats.PlayerControllerCount;
                }
                else
                {
                    ++PrimaryStats.PerSecondStats.AIControllerCount;
                }
            }
        }

        for (APawn* Pawn : TActorRange<APawn>(ParentWorld.Get())) // For backwards compat GetNumPawns needs to remain const, but TActorRange can't use a const UWorld.
        {
            ++PrimaryStats.PerSecondStats.PawnCount;
        }

    }
}

void FPCom_RuntimeTelemetryCollector::WriteFileStats()
{
	if (StatsFileCSV != nullptr)
	{
		auto Time = FDateTime::UtcNow();

		// write the header if needed
		if (!bHasWrittenCSVHeader)
		{
			bHasWrittenCSVHeader = true;
			FString logLine = LogStatsToString(Time, true);

			// write the line to the file
			auto Src = StringCast<ANSICHAR>(*logLine, logLine.Len());
			StatsFileCSV->Serialize((ANSICHAR*)Src.Get(), Src.Length() * sizeof(ANSICHAR));
		}

		// write the stats
		{
			FString logLine = LogStatsToString(Time, false);

			// write the line to the file
			auto Src = StringCast<ANSICHAR>(*logLine, logLine.Len());
			StatsFileCSV->Serialize((ANSICHAR*)Src.Get(), Src.Length() * sizeof(ANSICHAR));
		}
    }
}

FString FPCom_RuntimeTelemetryCollector::LogStatsToString(const FDateTime& Time, bool bHeader)
{
	FString Output;

	// sample info
	if (bHeader)
	{
		Output += TEXT("Timestamp, FrameNumber, ");
	}
	else
	{
		Output += FString::Printf(TEXT("%s, %d, "), *Time.ToIso8601(), GFrameNumber);
	}

	// performance stats
	if (bHeader)
	{
		Output += TEXT("TickCount, MaxFrameTime, MaxDeltaTime, CPUProcess, Memory_WS, ");
	}
	else
	{
		Output += FString::Printf(TEXT("%d, %0.2f, %0.2f, %0.2f,%0.f, "),
			PrimaryStats.PerFrameStats.TickCount,
			PrimaryStats.PerFrameStats.MaxFrameTime,
			PrimaryStats.PerFrameStats.MaxDeltaTime,
			PrimaryStats.PerSecondStats.CPUProcess,
			PrimaryStats.PerSecondStats.MemoryWS
		);
	}

	// network stats
	if (bHeader)
	{
		Output += TEXT("Connections, AveragePing, PacketsIn, PacketsOut, PacketsTotal, PacketsLostIn, PacketsLostOut, PacketsLostTotal, PacketLossPct, ");
	}
	else
	{
		Output += FString::Printf(TEXT("%d, %0.f, %d, %d, %d, %d, %d, %d, %0.2f, "),
			NetworkStats.PerSecondStats.ConnectionCount,
			NetworkStats.PerSecondStats.Ping,

			NetworkStats.PerSecondStats.PacketsIn,
			NetworkStats.PerSecondStats.PacketsOut,
			NetworkStats.PerSecondStats.PacketsTotal,

			NetworkStats.PerSecondStats.PacketsLostIn,
			NetworkStats.PerSecondStats.PacketsLostOut,
			NetworkStats.PerSecondStats.PacketsLostTotal,
			NetworkStats.PerSecondStats.PacketLoss * 100.f
		);
	}
    
	// gameplay stats
	if (bHeader)
	{
		Output += TEXT("PlayerControllers, AIControllers, Pawns, ");
	}
	else
	{
		Output += FString::Printf(TEXT("%d, %d, %d, "),
			PrimaryStats.PerSecondStats.PlayerControllerCount,
			PrimaryStats.PerSecondStats.AIControllerCount,
			PrimaryStats.PerSecondStats.PawnCount
		);
	}

	if (LogStatsToStringDel.IsBound())
	{
		Output += LogStatsToStringDel.Execute(Time, bHeader);
	}

	// end of line
	Output += LINE_TERMINATOR;

    return Output;
}
