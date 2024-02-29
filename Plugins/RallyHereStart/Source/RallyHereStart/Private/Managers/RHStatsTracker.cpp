#include "RallyHereStart.h"

#include "RH_GameInstanceSubsystem.h"
#include "RH_PlayerInventory.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RH_MatchSubsystem.h"
#include "Managers/RHStatsTracker.h"

#include "Analytics.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "RH_Events.h"

FRHStatsTracker::FRHStatsTracker(class URHStatsMgr* pMgr)
	: m_pStatsMgr(pMgr)
{
	m_bStarted = false;
	m_fEarnedPlayerXp = 0;
	m_fEarnedBattlepassXp = 0;
}

void FRHStatsTracker::Begin(class ARHPlayerState* pPlayerState)
{
	if (m_pOwningPlayerState == nullptr)
	{
		m_pOwningPlayerState = pPlayerState;
	}

	if (m_bStarted)
	{
		UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::Begin called while already started (PlayerId=%s"), *m_PlayerUuid.ToString());
		return;
	}

	m_dtStarted = FDateTime::UtcNow();
	m_bStarted = true;

	UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::Begin (PlayerId=%s"), *m_PlayerUuid.ToString());
}

void FRHStatsTracker::End()
{
	m_dtEnded = FDateTime::UtcNow();
	m_bStarted = false;
	m_pOwningPlayerState = nullptr;

	UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::End (PlayerId=%s)"), *m_PlayerUuid.ToString());
}

FTimespan FRHStatsTracker::GetTimespan()
{
	FTimespan TimeDiff;

	if (m_dtStarted.GetTicks() > 0)
	{
		FDateTime EndTime;
		if (m_dtEnded.GetTicks() > 0)
		{
			EndTime = m_dtEnded;
		}
		else
		{
			EndTime = FDateTime::UtcNow();
		}

		TimeDiff = EndTime - m_dtStarted;
	}

	return TimeDiff;
}

void FRHStatsTracker::SetPlayerXpEarned(float fXpPerMin)
{
	if (m_pStatsMgr->ShouldSendPlayerRewards(this))
	{
		m_fEarnedPlayerXp = FMath::CeilToFloat(fXpPerMin * (m_pStatsMgr->GetGameTimeElapsed(this) / 60.f));
		UE_LOG(RallyHereStart, Log, TEXT("FRHStatsTracker::SetPlayerXpEarned (fXpPerMin=%f) (PlayerUuid=%s) (SecondsInMatch=%f)"), fXpPerMin, *m_PlayerUuid.ToString(), m_pStatsMgr->GetGameTimeElapsed(this));
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("FRHStatsTracker::SetPlayerXpEarned called when ShouldSendPlayerRewards is false (PlayerId=%s)"), *m_PlayerUuid.ToString());
	}
}

void FRHStatsTracker::SetBattlepassXpEarned(float fXpPerMin)
{
	if (m_pStatsMgr->ShouldSendPlayerRewards(this))
	{
		m_fEarnedBattlepassXp = FMath::CeilToFloat(fXpPerMin * (m_pStatsMgr->GetGameTimeElapsed(this) / 60.f));
		UE_LOG(RallyHereStart, Log, TEXT("FRHStatsTracker::SetBattlepassXpEarned (fXpPerMin=%f) (PlayerId=%s) (SecondsInMatch=%f)"), fXpPerMin, *m_PlayerUuid.ToString(), m_pStatsMgr->GetGameTimeElapsed(this));
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("FRHStatsTracker::SetBattlepassXpEarned called when ShouldSendPlayerRewards is false (PlayerId=%s)"), *m_PlayerUuid.ToString());
	}
}

bool FRHStatsTracker::GetDropped()
{
	return m_pOwningPlayerState.IsValid();
}

///////////////////////////////////////////////////////////////////////////////
// URHStatsMgr class code:
///////////////////////////////////////////////////////////////////////////////

URHStatsMgr::URHStatsMgr()
{
	Clear();
}

URHStatsMgr::~URHStatsMgr()
{
	Clear();
}

void URHStatsMgr::BeginTracker(class ARHPlayerState* pPlayerState)
{
	TSharedPtr<FRHStatsTracker> StatsTracker = GetStatsTracker(pPlayerState->GetRHPlayerUuid());
	if (StatsTracker.IsValid())
	{
		StatsTracker.Get()->Begin(pPlayerState);
	}
}

void URHStatsMgr::EndTracker(class ARHPlayerState* pPlayerState)
{
	TSharedPtr<FRHStatsTracker> StatsTracker = GetStatsTracker(pPlayerState->GetRHPlayerUuid());
	if (StatsTracker.IsValid())
	{
		StatsTracker.Get()->End();
	}
}

void URHStatsMgr::SetStopTrackerRewards(class ARHPlayerState* pPlayerState, bool bStopRewards)
{
	TSharedPtr<FRHStatsTracker> StatsTracker = GetStatsTracker(pPlayerState->GetRHPlayerUuid());
	if (StatsTracker.IsValid())
	{
		StatsTracker.Get()->SetStopRewards(bStopRewards);
	}
}

void URHStatsMgr::FinishStats(class ARHGameModeBase* pGameMode)
{
	UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::FinishStats"));

	if (m_bStatsFinished)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStatsMgr::FinishStats attempted while StatsFinished is TRUE."));
		return;
	}

	if (pGameMode == nullptr)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStatsMgr::FinishStats shutting down. pGameMode is a nullptr"));
		return;
	}

	// Add trackers for any players that did not enter the game, but should have.
	for (auto It = pGameMode->CreatePlayerProfileIterator(); It; ++It)
	{
		FRHPlayerProfile* pProfile = It.Value().Get();
		if (pProfile == nullptr || !pProfile->RHPlayerUuid.IsValid() || pProfile->bSpectator)
		{
			continue;
		}

		GetStatsTracker(pProfile->RHPlayerUuid);
	}

	URH_PlayerInfoSubsystem* PlayerInfoSubsystem = nullptr;
	URH_JoinedSession* ActiveSession = nullptr;
	UGameInstance* GameInstance = nullptr;
	URH_GameInstanceSubsystem* GISubsystem = nullptr;

	if (GetWorld() != nullptr)
	{
		GameInstance = GetWorld()->GetGameInstance();

		if (GameInstance != nullptr)
		{
			GISubsystem = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
			if (GISubsystem != nullptr)
			{
				ActiveSession = GISubsystem->GetSessionSubsystem()->GetActiveSession();
				PlayerInfoSubsystem = GISubsystem->GetPlayerInfoSubsystem();
			}
		}
	}

	if (PlayerInfoSubsystem != nullptr)
	{
		// create a correlation provider (TODO - have a provider at the game instance level)
		TSharedPtr<IAnalyticsProvider> AnalyticsProvider = RHStandardEvents::AutoCreateAnalyticsProvider();
		if (AnalyticsProvider.IsValid())
		{
			// record to the analytics provider
			AnalyticsProvider->StartSession();

			// emit a correlation id for the following events so they can be crossreferenced
			RHStandardEvents::FCorrelationStartEvent::AutoEmit(AnalyticsProvider.Get(), GameInstance);
		}

		for (const auto& TrackerPair : m_StatsTrackers)
		{
			const TSharedPtr<FRHStatsTracker>& Tracker = TrackerPair.Value;
			if (Tracker.IsValid())
			{
				Tracker->SetPlayerXpEarned(50); // using an example xp/second of 50
				Tracker->SetBattlepassXpEarned(50); // using an example xp/second of 50

				if (URH_PlayerInfo* PlayerInfo = PlayerInfoSubsystem->GetOrCreatePlayerInfo(Tracker->GetPlayerUuid()))
				{
					TArray<URH_PlayerOrderEntry*> PlayerOrderEntries;

					URH_PlayerOrderEntry* NewPlayerOrderEntry = NewObject<URH_PlayerOrderEntry>();
					NewPlayerOrderEntry->FillType = ERHAPI_PlayerOrderEntryType::FillLoot;
					NewPlayerOrderEntry->LootId = GetPlayerXpLootId();
					NewPlayerOrderEntry->Quantity = Tracker->GetEarnedPlayerXp();
					NewPlayerOrderEntry->ExternalTransactionId = "End Of Match Xp Rewards";
					PlayerOrderEntries.Push(NewPlayerOrderEntry);
					UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::FinishStats -- Added Loot Reward -- PlayerId=%s, LootId=%s, Count=%d"), *Tracker->GetPlayerUuid().ToString(), *GetPlayerXpLootId().ToString(), Tracker->GetEarnedPlayerXp());

					NewPlayerOrderEntry = NewObject<URH_PlayerOrderEntry>();
					NewPlayerOrderEntry->FillType = ERHAPI_PlayerOrderEntryType::FillLoot;
					NewPlayerOrderEntry->LootId = GetBattlepassXpLootId();
					NewPlayerOrderEntry->Quantity = Tracker->GetEarnedBattlepassXp();
					NewPlayerOrderEntry->ExternalTransactionId = "End Of Match Battlepass Xp Rewards";
					PlayerOrderEntries.Push(NewPlayerOrderEntry);
					UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::FinishStats -- Added Loot Reward -- PlayerId=%s, LootId=%s, Count=%d"), *Tracker->GetPlayerUuid().ToString(), *GetBattlepassXpLootId().ToString(), Tracker->GetEarnedBattlepassXp());


					PlayerInfo->GetPlayerInventory()->CreateNewPlayerOrder(ERHAPI_Source::Instance, false, PlayerOrderEntries);
				}

				if (AnalyticsProvider.IsValid())
				{
					// emit standard match result
					{
						RHStandardEvents::FPlayerGameResultEvent Event;

						if (ActiveSession != nullptr)
						{
							Event.GameSessionId = ActiveSession->GetSessionId();

							if (const FRHAPI_InstanceInfo* InstanceData = ActiveSession->GetInstanceData())
							{
								Event.InstanceId = InstanceData->GetInstanceId();
							}
						}

						Event.DurationSeconds = Tracker->GetTimespan().GetTotalSeconds();

						Event.EmitTo(AnalyticsProvider.Get());
					}

					// emit custom old style match result
					{
						RHStandardEvents::FCustomEvent Event;

						Event.EventName = TEXT("match_result");

						Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("xpEarned"), Tracker->GetEarnedPlayerXp()));
						Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("duration"), Tracker->GetTimespan().GetTotalSeconds()));
						Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("matchStartTime"), Tracker->GetStartTime().ToIso8601()));
						Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("matchEndTime"), FDateTime::UtcNow().ToIso8601()));
						Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("hostName"), TEXT("RallyTestServer")));

						if (ActiveSession != nullptr)
						{
							Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("totalPlayers"), ActiveSession->GetSessionPlayerCount()));
							Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("serverSessionId"), ActiveSession->GetSessionId()));

							if (const FRHAPI_InstanceInfo* InstanceData = ActiveSession->GetInstanceData())
							{
								Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("instanceId"), InstanceData->GetInstanceId()));
							}

							FString RegionId;
							if (ActiveSession->GetSessionData().GetRegionId(RegionId))
							{
								Event.Attributes.Add(FAnalyticsEventAttribute(TEXT("regionId"), RegionId));
							}
						}

						auto PlayerUuid = Tracker->GetPlayerUuid();
						AnalyticsProvider->SetUserID(PlayerUuid.IsValid() ? PlayerUuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT(""));
						Event.EmitTo(AnalyticsProvider.Get());
					}
				}
			}

			if (AnalyticsProvider.IsValid())
			{
				// clear out the user id, so we dont emit events on behalf of a user
				AnalyticsProvider->SetUserID(TEXT(""));

				// emit a correlation end event
				{
					RHStandardEvents::FCorrelationEndEvent Event;
					Event.EmitTo(AnalyticsProvider.Get());
				}
				
				// close out the analytics session
				AnalyticsProvider->EndSession();
			}
		}
	}

	// record the match history data
	if (GISubsystem != nullptr)
	{
		// assume we are using automatic match updates, which will keep track of players entering and leaving the match, and has an active match id
		auto MatchSubsystem = GISubsystem->GetMatchSubsystem();
		if (MatchSubsystem != nullptr && MatchSubsystem->HasActiveMatchId())
		{
			// update the match player data (individual calls to patch the data)
			for (auto Tracker : m_StatsTrackers)
			{
				auto PlayerUuid = Tracker.Key;
				FRHAPI_MatchPlayerRequest MatchPlayerRequest;
				MatchPlayerRequest.SetPlayerUuid(PlayerUuid);
				MatchPlayerRequest.SetPlacement(1);
				MatchPlayerRequest.SetDurationSeconds(Tracker.Value->GetTimespan().GetTotalSeconds());

				TMap<FString, FString> CustomData;
				CustomData.Add(TEXT("XPEarned"), FString::FromInt(Tracker.Value->GetEarnedPlayerXp()));
				CustomData.Add(TEXT("BattlePassXPEarned"), FString::FromInt(Tracker.Value->GetEarnedBattlepassXp()));
				
				//MatchPlayerRequest.SetCustomData(CustomData);

				MatchSubsystem->UpdateMatchPlayer(MatchSubsystem->GetActiveMatchId(), PlayerUuid, MatchPlayerRequest);
			}

			// update the match data (single call to patch the data)
			{
				FRHAPI_MatchRequest MatchRequest;

				MatchRequest.SetType(TEXT("TestMatch"));
				MatchRequest.SetState(ERHAPI_MatchState::Closed);

				// set ending time
				MatchRequest.SetEndTimestamp(FDateTime::UtcNow());

				// calculate the duration of the match if we can
				FRHAPI_MatchWithPlayers ExistingMatch;
				if (MatchSubsystem->GetMatch(MatchSubsystem->GetActiveMatchId(), ExistingMatch))
				{
					if (auto StartTime = ExistingMatch.GetStartTimestampOrNull())
					{
						MatchRequest.SetDurationSeconds((*StartTime - MatchRequest.GetEndTimestamp()).GetTotalSeconds());
					}
				}

				//MatchRequest.SetCustomData(CustomData);

				MatchSubsystem->UpdateMatch(MatchSubsystem->GetActiveMatchId(), MatchRequest);
			}
		}
	}

	m_dtStatsFinished = FDateTime::UtcNow();
	m_bStatsFinished = true;
}

void URHStatsMgr::Clear()
{
	m_bStatsFinished = false;
	m_StatsTrackers.Empty();
}

void URHStatsMgr::ClearTracker(class ARHPlayerState* pPlayerState)
{
	FRHStatsTracker* tracker = nullptr;
	TSharedPtr<FRHStatsTracker> StatsTracker = GetStatsTracker(pPlayerState->GetRHPlayerUuid());
	if (StatsTracker.IsValid())
	{
		tracker = StatsTracker.Get();
	}

	if (m_bStatsFinished)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStatsMgr::ClearTracker requested after FinishStats()"));
		return;
	}

	if (!tracker || !tracker->GetPlayerUuid().IsValid())
	{
		return;
	}

	FGuid PlayerUuid = tracker->GetPlayerUuid();
	if (m_StatsTrackers.Contains(PlayerUuid))
	{
		m_StatsTrackers.Remove(PlayerUuid);
		UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::ClearTracker Clearing found Tracker (PlayerUuid=%s)"), *PlayerUuid.ToString());
	}
}

FDateTime* URHStatsMgr::GetFinishedDateTime()
{
	return &m_dtStatsFinished;
}

bool URHStatsMgr::ShouldSendPlayerRewards(FRHStatsTracker* tracker)
{
	return  !tracker->ShouldStopRewards() && !tracker->GetDropped();
}

float URHStatsMgr::GetGameTimeElapsed(FRHStatsTracker* tracker)
{
	return tracker->GetTimespan().GetSeconds();
}

TSharedPtr<FRHStatsTracker> URHStatsMgr::GetStatsTracker(const FGuid& PlayerUuid)
{
	if (!PlayerUuid.IsValid())
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStatsMgr::GetStatsTracker PlayerUuid is not valid"));
		return nullptr;
	}

	TSharedPtr<FRHStatsTracker>* ppStatsTracker = m_StatsTrackers.Find(PlayerUuid);
	TSharedPtr<FRHStatsTracker> pStatsTracker = nullptr;

	if (ppStatsTracker == nullptr)
	{
		if (m_bStatsFinished)
		{
			UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::GetStatsTracker(%s) returned nullptr after Stats Finished"), *PlayerUuid.ToString());
			return nullptr;
		}

		pStatsTracker = TSharedPtr<FRHStatsTracker>(new FRHStatsTracker(this));
		pStatsTracker->SetPlayerUuid(PlayerUuid);
		m_StatsTrackers.Add(PlayerUuid, pStatsTracker);

		UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::GetStatsTracker(%s) created a new stats tracker"), *PlayerUuid.ToString());
	}
	else
	{
		UE_LOG(RallyHereStart, Log, TEXT("URHStatsMgr::GetStatsTracker(%s) found an existing stats tracker"), *PlayerUuid.ToString());
		pStatsTracker = *ppStatsTracker;
	}

	return pStatsTracker;
}