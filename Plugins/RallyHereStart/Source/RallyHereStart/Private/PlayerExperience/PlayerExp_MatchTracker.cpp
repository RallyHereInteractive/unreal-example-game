// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "EngineMinimal.h"
#include "PlayerExperience/PlayerExperienceGlobals.h"
#include "PlayerExperience/PlayerExp_MatchTracker.h"
#include "PlayerExperience/PlayerExp_MatchReportSender.h"

#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/App.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "PerfCountersModule.h"
#include "Misc/Build.h"
#include "Containers/Queue.h"
#include "Internationalization/Regex.h"
#include "Misc/EngineVersion.h"
#include "HAL/PlatformProcess.h"

#include "RH_GameInstanceSubsystem.h"
#include "RH_GameInstanceSessionSubsystem.h"

#include "GameFramework/RHGameModeBase.h"

//============================================================================
// THIS VERSION NUMBER MUST BE UPDATED WHEN THE JSON SCHEMA CHANGES.
// That means all name changes, deletions, or additions of fields of any sort.
const int32 UPlayerExp_MatchTracker::SchemaVersionNumber = 6;
//============================================================================

UPlayerExp_MatchTracker::UPlayerExp_MatchTracker(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
    : Super(ObjectInitializer)
{
    PlayerTrackerClass = UPlayerExp_PlayerTracker::StaticClass();
    OwningGameInstance = nullptr;
    bInitialized = false;
    LoadedPlayerTrackerClass = nullptr;
    bMatchInProgress = false;
    MatchStartTimeSeconds = -1.0f;
    MatchEndTime = MatchStartTimeSeconds;
    MatchStartTime = FDateTime::MinValue();
    bAutoStartMatch = true;
    bAutoSendReportOnMatchEnded = true;
    MatchIsFubar = false;
    FubarReason = TEXT("");
    FrameTimeStats = FPlayerExp_StatAccumulator();
    LastTime = -1.0;
    CachedMatchReport = nullptr;
    FrameTrackerPtr = nullptr;
}

void UPlayerExp_MatchTracker::BeginDestroy()
{
    FrameTrackerPtr.Reset();
    Super::BeginDestroy();
}

void UPlayerExp_MatchTracker::DoInit(UGameInstance* InOwningGameInstance)
{
    if(!bInitialized)
    {
        bInitialized = true;
        check(InOwningGameInstance != nullptr);
        OwningGameInstance = InOwningGameInstance;
        Init();
    }
}

void UPlayerExp_MatchTracker::DoCleanup()
{
    if (bInitialized)
    {
        bInitialized = false;
        Cleanup();
    }
}

void UPlayerExp_MatchTracker::Tick(float DeltaTime)
{
    double FrameTime = 0.0;

    if (IsRunningDedicatedServer())
    {
        FrameTime = (FApp::GetDeltaTime() - FApp::GetIdleTime()) * 1000.0;
    }
    else
    {
        FrameTime = UPlayerExperienceGlobals::CalcFrameTime(LastTime);
    }

    if (FrameTime > 0.0)
    {
        FrameTimeStats.AddSample(FrameTime);
    }
}

static void ForceIdentifiersToSnakeCase(TSharedPtr<FJsonObject>& InReportJsonObject)
{
    if (!InReportJsonObject.IsValid())
    {
        return;
    }

    TQueue< TSharedPtr<FJsonValue> > JsonValuesToProcess;

    JsonValuesToProcess.Enqueue(MakeShared<FJsonValueObject>(InReportJsonObject));

    TSharedPtr<FJsonValue> CurrentValue;
    while (JsonValuesToProcess.Dequeue(CurrentValue))
    {
        if (!CurrentValue.IsValid())
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* JsonObjectPtr = nullptr;
        const TArray< TSharedPtr<FJsonValue> >* JsonArrayPtr = nullptr;
        if (CurrentValue->TryGetObject(JsonObjectPtr))
        {
            if (JsonObjectPtr == nullptr || !JsonObjectPtr->IsValid())
            {
                continue;
            }

            FRegexPattern Finder(TEXT("(GPU|CPU|MB$|[A-Z]+)"));
            TMap<FString, TSharedPtr<FJsonValue>> OldValues = MoveTemp((*JsonObjectPtr)->Values);
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : OldValues)
            {
                FString ModifiedName = Pair.Key;
                bool FoundOne = false;
                do {
                    FRegexMatcher Matcher(Finder, *ModifiedName);
                    FoundOne = Matcher.FindNext();
                    if (FoundOne)
                    {
                        FString Substring = Matcher.GetCaptureGroup(0);
                        // add underscores before the groups after the first character
                        FString Replacement = (Matcher.GetCaptureGroupBeginning(0) > 0) ? TEXT("_") : TEXT("");
                        // to lower so next search does not find this sequence again
                        Replacement += Substring.ToLower();

                        // FString::Replace() does a global replace so we must implement our own one-time replace
                        ModifiedName = ModifiedName.Left(Matcher.GetCaptureGroupBeginning(0))
                            + Replacement
                            + ModifiedName.RightChop(Matcher.GetCaptureGroupEnding(0));
                    }
                } while (FoundOne);

                (*JsonObjectPtr)->SetField(ModifiedName, Pair.Value);
                JsonValuesToProcess.Enqueue(Pair.Value);
            }
        }
        else if (CurrentValue->TryGetArray(JsonArrayPtr))
        {
            if (JsonArrayPtr == nullptr)
            {
                continue;
            }

            for (const TSharedPtr<FJsonValue>& JsonArrayEntryValue : *JsonArrayPtr)
            {
                JsonValuesToProcess.Enqueue(JsonArrayEntryValue);
            }
        }
    }
}

TSharedPtr<FJsonObject> UPlayerExp_MatchTracker::CreateReport()
{
    TSharedPtr<FJsonObject> FullReportJson = MakeShared<FJsonObject>();

    //============================================================================
    // THIS VERSION NUMBER MUST BE UPDATED WHEN THE JSON SCHEMA CHANGES.
    // That means all name changes, deletions, or additions of fields of any sort.
    FullReportJson->SetField(TEXT("SchemaVersion"), MakeShared<FJsonValueNumber>(SchemaVersionNumber));

    const FEngineVersion& EngineVersion = FEngineVersion::Current();
    FString EngineVersionString = FString::Format(TEXT("{0}.{1}.{2}"), { EngineVersion.GetMajor(), EngineVersion.GetMinor(), EngineVersion.GetPatch() });
    FullReportJson->SetField(TEXT("EngineVersion"), MakeShared<FJsonValueString>(EngineVersionString));
    FullReportJson->SetField(TEXT("Branch"), MakeShared<FJsonValueString>(EngineVersion.GetBranch()));
    FullReportJson->SetField(TEXT("ChangeList"), MakeShared<FJsonValueNumber>(EngineVersion.GetChangelist()));

    if (TSharedPtr<FJsonValue> MatchInfoJson = CreateReport_MatchInfo())
    {
        FullReportJson->SetField(TEXT("MatchInfo"), MatchInfoJson);
    }

#if WITH_PERFCOUNTERS
    if (IPerfCounters* pPerfCounters = IPerfCountersModule::Get().GetPerformanceCounters())
    {
        TSharedPtr<FJsonObject> PerfCountersJsonObject = nullptr;
        FString PerfCountersJsonString = pPerfCounters->GetAllCountersAsJson();
        auto PerfCountersReader = TJsonReaderFactory<>::Create(PerfCountersJsonString);
        if (FJsonSerializer::Deserialize(PerfCountersReader, PerfCountersJsonObject) && PerfCountersJsonObject.IsValid())
        {
            FullReportJson->SetObjectField(TEXT("PerfCounters"), PerfCountersJsonObject);
        }
    }
#endif

    // RedShift has some issues with using camel case with identifier names. This forces all identifier names to snake case.
    ForceIdentifiersToSnakeCase(FullReportJson);

    return FullReportJson;
}

TSharedPtr<FJsonObject> UPlayerExp_MatchTracker::CacheMatchReport()
{
    return CachedMatchReport = CreateReport();
}

void UPlayerExp_MatchTracker::DoStartMatch()
{
	auto* Session = GetSession();

	// do not start a match if we are already in one, or if we don't have a session (ex: boot map)
    if (!bMatchInProgress && Session != nullptr)
    {
        bMatchInProgress = true;
        StartMatch();
    }
}

void UPlayerExp_MatchTracker::DoEndMatch(float MatchScoreDeviation)
{
    if (bMatchInProgress)
    {
        bMatchInProgress = false;
        EndMatch(MatchScoreDeviation);
    }
}

void UPlayerExp_MatchTracker::MarkMatchFubar(const FString& InFubarReason)
{
    MatchIsFubar = true;
    FubarReason = InFubarReason;
}

UPlayerExp_PlayerTracker* UPlayerExp_MatchTracker::GetPlayerTrackerForController(APlayerController* InPlayerController) const
{
    TWeakObjectPtr<UPlayerExp_PlayerTracker> const* PlayerTrackerPtr = PlayerTrackerByControllerMap.Find(InPlayerController);
    return PlayerTrackerPtr != nullptr
        ? PlayerTrackerPtr->Get()
        : nullptr;
}


UPlayerExp_PlayerTracker* UPlayerExp_MatchTracker::CreateOrGetPlayerTrackerForController(APlayerController* InPlayerController)
{
	if (InPlayerController == nullptr || !IsValid(InPlayerController) || (InPlayerController->PlayerState != nullptr && InPlayerController->PlayerState->IsOnlyASpectator()))
	{
		return nullptr;
	}

	UPlayerExp_PlayerTracker* pFoundPlayerTracker = GetPlayerTrackerForController(InPlayerController);
	if (pFoundPlayerTracker != nullptr)
	{
		return pFoundPlayerTracker;
	}

	FGuid PlayerUuid = ARHGameModeBase::GetRHPlayerUuidFromPlayer(InPlayerController->Player);
	if (PlayerUuid.IsValid())
	{
		pFoundPlayerTracker = GetPlayerTrackerForPlayerUuid(PlayerUuid);
		if (pFoundPlayerTracker != nullptr)
		{
			//TODO: check if the existing bound player controller is stale?
			pFoundPlayerTracker->BindToController(InPlayerController);
			return pFoundPlayerTracker;
		}
		else if (UPlayerExp_PlayerTracker* NewPlayerTracker = CreateNewPlayerTracker())
		{
			NewPlayerTracker->SetPlayerUuid(PlayerUuid);

			NewPlayerTracker->BindToController(InPlayerController);
			NewPlayerTracker->Init();
			return NewPlayerTracker;
		}
	}


	return nullptr;
}

UPlayerExp_PlayerTracker* UPlayerExp_MatchTracker::CreateOrGetPlayerTrackerFromPlayerUuid(const FGuid& InPlayerUuid)
{
	if (!InPlayerUuid.IsValid())
	{
		return nullptr;
	}

	if (UPlayerExp_PlayerTracker* pFoundPlayerTracker = GetPlayerTrackerForPlayerUuid(InPlayerUuid))
	{
		return pFoundPlayerTracker;
	}

	if (UPlayerExp_PlayerTracker* NewPlayerTracker = CreateNewPlayerTracker())
	{
		NewPlayerTracker->SetPlayerUuid(InPlayerUuid);

		NewPlayerTracker->Init();
		return NewPlayerTracker;
	}

	return nullptr;
}

UPlayerExp_PlayerTracker* UPlayerExp_MatchTracker::GetPlayerTrackerForPlayerUuid(const FGuid& InPlayerUuid) const
{
	TWeakObjectPtr<UPlayerExp_PlayerTracker> const* PlayerTrackerPtr = PlayerTrackerByPlayerUuid.Find(InPlayerUuid);
	return PlayerTrackerPtr != nullptr
		? PlayerTrackerPtr->Get()
		: nullptr;
}


void UPlayerExp_MatchTracker::Init()
{
    check(bInitialized);

    HostName = FPlatformProcess::ComputerName();

    GameModeInitializedHandle = FGameModeEvents::OnGameModeInitializedEvent().AddUObject(this, &UPlayerExp_MatchTracker::GameModeInitialized);
    GameModePostLoginHandle = FGameModeEvents::OnGameModePostLoginEvent().AddUObject(this, &UPlayerExp_MatchTracker::GameModePostLogin);
    GameModeLogoutHandle = FGameModeEvents::OnGameModeLogoutEvent().AddUObject(this, &UPlayerExp_MatchTracker::GameModeLogout);

    LoadedPlayerTrackerClass = PlayerTrackerClass;
    if(LoadedPlayerTrackerClass == nullptr)
    {
        LoadedPlayerTrackerClass = UPlayerExp_PlayerTracker::StaticClass();
    }

    if (UGameInstance* pGameInst = GetOwningGameInstance())
    {
        UWorld* pWorld = pGameInst->GetWorld();
        AGameModeBase* pActiveGameMode = pWorld != nullptr ? pWorld->GetAuthGameMode() : nullptr;
        if (pActiveGameMode != nullptr)
        {
            GameModeInitialized(pActiveGameMode);
        }
    }
}

void UPlayerExp_MatchTracker::Cleanup()
{
    check(!bInitialized);

    if (GameModeInitializedHandle.IsValid())
    {
        FGameModeEvents::OnGameModeInitializedEvent().Remove(GameModeInitializedHandle);
        GameModeInitializedHandle.Reset();
    }
    if (GameModePostLoginHandle.IsValid())
    {
        FGameModeEvents::OnGameModePostLoginEvent().Remove(GameModePostLoginHandle);
        GameModePostLoginHandle.Reset();
    }
    if (GameModeLogoutHandle.IsValid())
    {
        FGameModeEvents::OnGameModeLogoutEvent().Remove(GameModeLogoutHandle);
        GameModeLogoutHandle.Reset();
    }
    if (NetworkFailureHandle.IsValid())
    {
        if (GEngine)
        {
            GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
        }
        NetworkFailureHandle.Reset();
    }

    if (FMatchFrameTracker* pFrameTracker = FrameTrackerPtr.Get())
    {
        pFrameTracker->SetEnabled(false);
    }

    // Always print the match report in editor for debugging purposes
    if (GIsEditor)
    {
        if (IsMatchInProgress())
        {
            DoEndMatch(0.0f);
        }
    }

	UnregisterSessionCallbacks();
}

void UPlayerExp_MatchTracker::StartMatch()
{
    check(bMatchInProgress);
    MatchStartTimeSeconds = FApp::GetCurrentTime();
    MatchStartTime = FDateTime::UtcNow();
    FrameTrackerPtr.Reset(new FMatchFrameTracker(this));
    BindAllControllers();

	auto* Session = GetSession();
	if (Session != nullptr)
	{
		// record session basic data at this time
		SessionId = Session->GetSessionId();
		Region = Session->GetSessionData().GetRegionId(TEXT(""));

		auto* InstanceData = Session->GetInstanceData();
		if (InstanceData != nullptr)
		{
			InstanceId = InstanceData->GetInstanceId();
			AllocationId = InstanceData->GetAllocationId(TEXT(""));
		}		
	}

	RegisterSessionCallbacks();
	BindAllAddedPlayers();
}

void UPlayerExp_MatchTracker::EndMatch(float MatchScoreDeviation)
{
    check(!bMatchInProgress);

	UnregisterSessionCallbacks();

    MatchEndTime = FApp::GetCurrentTime();
    MatchDuration = FMath::Max(MatchEndTime-MatchStartTimeSeconds, 0.0);

    if (FMatchFrameTracker* pFrameTracker = FrameTrackerPtr.Get())
    {
        pFrameTracker->SetEnabled(false);
    }

    for (UPlayerExp_PlayerTracker* pPlayerTracker : Players)
    {
        pPlayerTracker->HandleMatchEnded();
    }

    CacheMatchReport();

    if(UE_LOG_ACTIVE(LogPlayerExperience, Verbose) && CachedMatchReport.IsValid())
    {
        FString ReportString;
        auto Writer = TJsonWriterFactory<>::Create(&ReportString);

        if (FJsonSerializer::Serialize(CachedMatchReport.ToSharedRef(), Writer))
        {
            UE_LOG(LogPlayerExperience, Verbose, TEXT("==========================================================="));
            UE_LOG(LogPlayerExperience, Verbose, TEXT("==============PlayerExperience Report Debug================"));
            UE_LOG(LogPlayerExperience, Verbose, TEXT("\n%s"), *ReportString);
            UE_LOG(LogPlayerExperience, Verbose, TEXT("==========================================================="));
        }
    }

    if (bAutoSendReportOnMatchEnded && !GIsEditor)
    {
        SendMatchReport();
    }
}

void UPlayerExp_MatchTracker::GameModeSwitched()
{
    check(bMatchInProgress);
    BindAllControllers();

	//It probably isn't necessary to BindAllPostedPlayers here, but better safe than sorry.
	BindAllAddedPlayers();
}

URH_JoinedSession* UPlayerExp_MatchTracker::GetSession() const
{
	if (UGameInstance* pGameInst = GetOwningGameInstance())
	{
		auto* GISS = pGameInst->GetSubsystem<URH_GameInstanceSubsystem>();

		if (GISS != nullptr && GISS->GetSessionSubsystem() != nullptr)
		{
			return GISS->GetSessionSubsystem()->GetActiveSession();
		}
	}

	return nullptr;
}


void UPlayerExp_MatchTracker::OnPlayerAdded(const FGuid& InPlayerUuid)
{
	// This player is active and should receive a tracker at this point.
	CreateOrGetPlayerTrackerFromPlayerUuid(InPlayerUuid);
}

void UPlayerExp_MatchTracker::BindAllAddedPlayers()
{
	check(IsMatchInProgress());

	// process any already posted players
	auto* Session = GetSession();
	if (Session != nullptr)
	{
		TArray<FGuid> PlayerUuids;

		const auto& SessionData = Session->GetSessionData();
		for (const auto& Team : SessionData.GetTeams())
		{
			for (const auto& Player : Team.GetPlayers())
			{
				OnPlayerAdded(Player.GetPlayerUuid());
			}
		}
	}
}

void UPlayerExp_MatchTracker::RegisterSessionCallbacks()
{
	auto* Session = GetSession();
	if (Session != nullptr)
	{
		Session->OnSessionUpdatedDelegate.AddUObject(this, &UPlayerExp_MatchTracker::OnSessionUpdated);
	}
}

void UPlayerExp_MatchTracker::UnregisterSessionCallbacks()
{
	if (UGameInstance* pGameInst = GetOwningGameInstance())
	{
		auto* GISS = pGameInst->GetSubsystem<URH_GameInstanceSubsystem>();

		if (GISS != nullptr && GISS->GetSessionSubsystem() != nullptr)
		{
			auto* ActiveSession = GISS->GetSessionSubsystem()->GetActiveSession();
			if (ActiveSession != nullptr)
			{
				ActiveSession->OnSessionUpdatedDelegate.RemoveAll(this);
			}
		}
	}
}

void UPlayerExp_MatchTracker::OnSessionUpdated(URH_SessionView* InSession)
{
	auto* Session = GetSession();
	check(InSession == Session);
	if (Session != nullptr)
	{
		// If we are in a match, we need to check if any players have been added to the session
		if (IsMatchInProgress())
		{
			BindAllAddedPlayers();
		}
	}
}

void UPlayerExp_MatchTracker::AddPlayerTrackerToPlayerUuidMap(UPlayerExp_PlayerTracker* InTracker)
{
	if (InTracker != nullptr && InTracker->GetPlayerUuid().IsValid())
	{
		PlayerTrackerByPlayerUuid.FindOrAdd(InTracker->GetPlayerUuid()) = InTracker;
	}
}

static TSharedPtr<FJsonValue> PlayerEx_ObjectJsonCallback(FProperty* Property, const void* Value)
{
    if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
    {
        // if (!ObjectProperty->HasAnyFlags(RF_Transient)) // We are taking Transient to mean we don't want to serialize to Json either (could make a new flag if nessasary)
        {
            TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

            FJsonObjectConverter::CustomExportCallback CustomCB;
            CustomCB.BindStatic(&PlayerEx_ObjectJsonCallback);

            void** PtrToValuePtr = (void**)Value;
            UObject** ValueAsObject = (UObject**)(Value);

            if((*PtrToValuePtr) != nullptr)
            {
                if (FJsonObjectConverter::UStructToJsonObject((*ValueAsObject)->GetClass(), (*PtrToValuePtr), Out, 0, UPlayerExperienceGlobals::JsonConverterSkipFlags, &CustomCB))
                {
                    return MakeShared<FJsonValueObject>(Out);
                }
            }
            else
            {
                return MakeShared<FJsonValueNull>();
            }
        }
    }
    else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        static const FName nmDateTime(TEXT("DateTime"));
        if (StructProperty->Struct->GetFName() == nmDateTime)
        {
            const FDateTime* AsDateTime = (const FDateTime*) Value;
            check(AsDateTime != nullptr);
            return MakeShared<FJsonValueString>(AsDateTime->ToIso8601());
        }
    }

    // invalid
    return TSharedPtr<FJsonValue>();
}

void UPlayerExp_MatchTracker::CaptureStatsForReport()
{
    FrameTimeStats.CaptureStatsForReport();
    for (UPlayerExp_PlayerTracker* Player : Players)
    {
        Player->CaptureStatsForReport();
    }
}

TSharedPtr<FJsonValue> UPlayerExp_MatchTracker::CreateReport_MatchInfo()
{
    CaptureStatsForReport();

    FJsonObjectConverter::CustomExportCallback CustomCB;
    CustomCB.BindStatic(&PlayerEx_ObjectJsonCallback);

    TSharedRef<FJsonObject> MatchInfoObject = MakeShared<FJsonObject>();
    FJsonObjectConverter::UStructToJsonObject(GetClass(), this, MatchInfoObject, 0, UPlayerExperienceGlobals::JsonConverterSkipFlags, &CustomCB);

    return MakeShared<FJsonValueObject>(MatchInfoObject);
}

void UPlayerExp_MatchTracker::SendMatchReport(bool bForceRecache /*= false*/)
{
    if (bForceRecache || !CachedMatchReport.IsValid())
    {
        CacheMatchReport();
    }

    if (!CachedMatchReport.IsValid())
    {
        UE_LOG(LogPlayerExperience, Warning, TEXT("Could not send match report since no report was generated."));
        return;
    }

	if (UPlayerExperienceGlobals::Get().MatchReportSenderClassName.IsValid())
	{
		UClass* MatchReportSenderClass = UPlayerExperienceGlobals::Get().MatchReportSenderClassName.TryLoadClass<UPlayerExp_MatchReportSender>();
		if (MatchReportSenderClass)
		{
			MatchReportSender = NewObject<UPlayerExp_MatchReportSender>(this, MatchReportSenderClass, NAME_None);
			MatchReportSender->DoInit();

			MatchReportSender->RequestSendReport(OwningGameInstance.Get(), CachedMatchReport.ToSharedRef());
		}
		else
		{
			UE_LOG(LogPlayerExperience, Warning, TEXT("Could not send match report. No match report sender class found."));
		}
	}
	else
	{
		UE_LOG(LogPlayerExperience, Warning, TEXT("Could not send match report. No match report sender configured."));
	}
}

UPlayerExp_PlayerTracker* UPlayerExp_MatchTracker::CreateNewPlayerTracker()
{
    UPlayerExp_PlayerTracker* NewPlayerTracker = nullptr;
    if (ensure(LoadedPlayerTrackerClass != nullptr))
    {
        NewPlayerTracker = NewObject<UPlayerExp_PlayerTracker>(this, LoadedPlayerTrackerClass, NAME_None);
    }

    if (NewPlayerTracker != nullptr)
    {
        Players.Add(NewPlayerTracker);
    }

    return NewPlayerTracker;
}

void UPlayerExp_MatchTracker::AddPlayerTrackerToControllerMap(UPlayerExp_PlayerTracker* InTracker, APlayerController* InController)
{
    if (InTracker != nullptr && InController != nullptr)
    {
        PlayerTrackerByControllerMap.FindOrAdd(InController) = InTracker;
    }
}

bool UPlayerExp_MatchTracker::RemovePlayerTrackerFromControllerMap(AController* InController, const UPlayerExp_PlayerTracker* InExpectedTracker)
{
    if (TWeakObjectPtr<UPlayerExp_PlayerTracker> const* FoundTrackerPtr = PlayerTrackerByControllerMap.Find(InController))
    {
        if (*FoundTrackerPtr == InExpectedTracker)
        {
            PlayerTrackerByControllerMap.Remove(InController);
            return true;
        }
    }
    return false;
}

void UPlayerExp_MatchTracker::SwapPlayerTrackerOnControllerMap(UPlayerExp_PlayerTracker* InTracker, APlayerController* InOldPC, APlayerController* InNewPC)
{
    if (InTracker == nullptr)
    {
        return;
    }

    if (InOldPC != nullptr)
    {
        RemovePlayerTrackerFromControllerMap(InOldPC, InTracker);
    }

    if (InNewPC != nullptr)
    {
        AddPlayerTrackerToControllerMap(InTracker, InNewPC);
    }
}

void UPlayerExp_MatchTracker::BindAllControllers()
{
    if (ActiveGameMode.IsValid())
    {
        UWorld* pWorld = ActiveGameMode->GetWorld();
        for (auto It = pWorld->GetPlayerControllerIterator(); It; ++It)
        {
            if(APlayerController* pPC = (*It).Get())
            {
                CreateOrGetPlayerTrackerForController(pPC);
            }
        }
    }
}

void UPlayerExp_MatchTracker::GameModeInitialized(AGameModeBase* InGameMode)
{
    if (InGameMode != nullptr && ActiveGameMode != InGameMode)
    {
        ActiveGameMode = InGameMode;
        if (bMatchInProgress)
        {
            GameModeSwitched();
        }
        else if (bAutoStartMatch)
        {
            DoStartMatch();
        }
    }
}

void UPlayerExp_MatchTracker::GameModePostLogin(AGameModeBase* InGameMode, APlayerController* InPlayerController)
{
    if (InGameMode != nullptr && InGameMode == ActiveGameMode && bMatchInProgress)
    {
        CreateOrGetPlayerTrackerForController(InPlayerController);
    }
}

void UPlayerExp_MatchTracker::GameModeLogout(AGameModeBase* InGameMode, AController* InController)
{
    if (UPlayerExp_PlayerTracker* FoundTracker = GetPlayerTrackerForController(Cast<APlayerController>(InController)))
    {
        FoundTracker->ControllerLogout(InController);
        ensure(!PlayerTrackerByControllerMap.Contains(InController));
    }
}

FMatchFrameTracker::FMatchFrameTracker(UPlayerExp_MatchTracker* InOwner)
    : FTickableGameObject()
    , bEnabled(true)
    , OwningMatchTracker(InOwner)
{

}

FMatchFrameTracker::~FMatchFrameTracker()
{
}

void FMatchFrameTracker::Tick(float DeltaTime)
{
    if (OwningMatchTracker.IsValid())
    {
        OwningMatchTracker->Tick(DeltaTime);
    }
}

TStatId FMatchFrameTracker::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(FMatchFrameTracker, STATGROUP_Tickables);
}

bool FMatchFrameTracker::IsTickable() const
{
    return bEnabled && OwningMatchTracker.IsValid();
}
