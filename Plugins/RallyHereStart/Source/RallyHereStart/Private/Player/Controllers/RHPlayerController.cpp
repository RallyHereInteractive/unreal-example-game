// Copyright 2016-2049 Hi-Rez Studios, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Player/Controllers/RHPlayerController.h"
#include "Player/Controllers/RHPlayerState.h"

#include "RH_OnlineSubsystemNames.h"
#include "Engine/InputDelegateBinding.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/HUD.h"
#include "GameFramework/RHPlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/RHGameUserSettings.h"
#include "GameFramework/RHHUDInterface.h"
#include "GameFramework/RHGameEngine.h"
#include "Net/UnrealNetwork.h"

#if PLATFORM_DESKTOP
#include "Framework/Application/SlateApplication.h"
#endif
#include "GameFramework/RHGameInstance.h"

#include "RH_LocalPlayerSessionSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "Net/OnlineEngineInterface.h"
#include "Shared/HUD/RHHUDCommon.h"

DEFINE_LOG_CATEGORY_STATIC(LogFRHSonyMatch, Log, All);

static int32 GSonyMatchSendCrossPlayNames = 0;
static FAutoConsoleVariableRef CVarSonyMatchSendCrossPlayNames(
	TEXT("rh.SonyMatchSendCrossPlayNames"),
	GSonyMatchSendCrossPlayNames,
	TEXT(" 0: Send generic names for cross play users to Sony Match API (default)\n")
	TEXT(" 1: Send real names for cross play users to Sony Match API"),
	ECVF_Default
);


// This must be set to at least FOnlineAsyncTaskCreateMatch::MinExpirationTime + 1
// as there is bug in the match creation code
static int32 GSonyMatchInactivityTimeoutSeconds = 61;
static FAutoConsoleVariableRef CVarSonyMatchInactivityTimeoutSeconds(
	TEXT("rh.SonyMatchInactivityTimeoutSeconds"),
	GSonyMatchInactivityTimeoutSeconds,
	TEXT("Timeout for a match to be cancelled in seconds when it is in a Paused state"),
	ECVF_Default
);

static bool PlatformRequiresSonyMatchApi()
{
	return UGameplayStatics::GetPlatformName() == TEXT("PS5");
}

struct FApplicationBackgroundStateTracker
{
	FApplicationBackgroundStateTracker()
		: bIsInBackgound(false)
	{
		FCoreDelegates::ApplicationHasReactivatedDelegate.AddRaw(this, &FApplicationBackgroundStateTracker::HandleApplicationActivate);
		FCoreDelegates::ApplicationWillDeactivateDelegate.AddRaw(this, &FApplicationBackgroundStateTracker::HandleApplicationDeactivate);
	}

	void HandleApplicationActivate()
	{
		bIsInBackgound = false;
	}

	void HandleApplicationDeactivate()
	{
		bIsInBackgound = true;
	}

	bool IsApplicationInBackground() const
	{
		return bIsInBackgound;
	}

private:
	bool bIsInBackgound;
};
FApplicationBackgroundStateTracker GApplicationBackgroundStateTracker;

static FORCENOINLINE void HandleSonyMatchUpdateError(const TCHAR* Location, const FOnlineError& Result)
{
	if (!Result.WasSuccessful())
	{
		UE_LOG(LogFRHSonyMatch, Warning, TEXT("SonyMatchUpdateError: [%s] : [%s]"), Location, *Result.ErrorCode);
	}
}

ARHPlayerController::ARHPlayerController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	InputComponentClass = UInputComponent::StaticClass();
}

void ARHPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARHPlayerController, RHPlayerUuid);
}

URHPlayerInput* ARHPlayerController::GetRHPlayerInput() const
{
	return Cast<URHPlayerInput>(PlayerInput);
}

void ARHPlayerController::ClientReturnToMainMenuWithTextReason_Implementation(const FText& ReturnReason)
{
	// Only call super if we can't find the MCTS client
	Super::ClientReturnToMainMenuWithTextReason_Implementation(ReturnReason);
}


//$$dweiss - Override of implementation in APlayerController to allow for custom override of the input component class
//           Default class type maintains the behavior of the engine level path with UInputComponent
void ARHPlayerController::SetupInputComponent()
{
	// A subclass could create a different InputComponent class but still want the default bindings
	if (InputComponent == NULL)
	{
		InputComponent = NewObject<UInputComponent>(this, InputComponentClass, TEXT("PC_InputComponent0"));
		InputComponent->RegisterComponent();
	}

	if (UInputDelegateBinding::SupportsInputDelegate(GetClass()))
	{
		InputComponent->bBlockInput = bBlockInput;
		UInputDelegateBinding::BindInputDelegates(GetClass(), InputComponent);
	}
}

void ARHPlayerController::SetInputBinds()
{
	InputComponent->TouchBindings.Reset();
	InputComponent->AxisBindings.Reset();
	InputComponent->AxisKeyBindings.Reset();
	InputComponent->VectorAxisBindings.Reset();
	InputComponent->GestureBindings.Reset();
	InputComponent->ClearActionBindings();

    InputComponent->BindAction("PushToTalk", IE_Pressed, this, &ARHPlayerController::PushToTalkPressed);
	InputComponent->BindAction("PushToTalk", IE_Released, this, &ARHPlayerController::PushToTalkReleased);
}

void ARHPlayerController::PushToTalkPressed()
{
	/* #RHTODO - Voice
    IPComVoiceInterface* pVoice = pcomGetVoice();
    if (pVoice != nullptr)
    {
        pVoice->PushToTalk(true);
    }
	*/
}

void ARHPlayerController::PushToTalkReleased()
{
	/* #RHTODO - Voice
    IPComVoiceInterface* pVoice = pcomGetVoice();
    if (pVoice != nullptr)
    {
        pVoice->PushToTalk(false);
    }
	*/
}


void ARHPlayerController::SetUseForceFeedback(bool bUseForceFeedback)
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName != TEXT("XSX") && PlatformName != TEXT("PS5"))
	{
		if (bUseForceFeedback != bForceFeedbackEnabled)
		{
			bForceFeedbackEnabled = bUseForceFeedback;

			// TODO: Hook up force feedback
			/*		if (FAkAudioDevice* AkAudioDevice = FAkAudioDevice::Get())
					{
						AkAudioDevice->SetRTPCValue(*ForceFeedbackRTPCName, bForceFeedbackEnabled ? 1.0f : 0.0f, 0, nullptr);
					}
			*/
		}
	}
}

void ARHPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (bIsSonyMatchOwner)
	{
		// If the match isn't completed, just "Pause" it and allow the expiration timer
		// to cancel it or another player pick up the match
		RequestUpdateSonyMatchState(ESonyMatchState::SendPauseOrCancelMatch);
	}
}

void ARHPlayerController::ClientCheckSonyMatchOwnerEligibility_Implementation()
{
	if (!PlatformRequiresSonyMatchApi())
	{
		return;
	}

	bIsEligibleSonyMatchOwner = PlatformRequiresSonyMatchApi() && !GApplicationBackgroundStateTracker.IsApplicationInBackground();

	ServerUpdateSonyMatchOwnerEligibility(bIsEligibleSonyMatchOwner);

	FCoreDelegates::ApplicationHasReactivatedDelegate.AddWeakLambda(this, [this]
		{
			if (SonyMatchState != ESonyMatchState::Complete)
			{
				bIsEligibleSonyMatchOwner = true;
				ServerUpdateSonyMatchOwnerEligibility(bIsEligibleSonyMatchOwner);
			}
		});

	FCoreDelegates::ApplicationWillDeactivateDelegate.AddWeakLambda(this, [this] {
		{
			bIsEligibleSonyMatchOwner = false;
			ServerUpdateSonyMatchOwnerEligibility(bIsEligibleSonyMatchOwner);
		}
		});
}

void ARHPlayerController::ServerUpdateSonyMatchOwnerEligibility_Implementation(bool bIsEligible)
{
	bIsEligibleSonyMatchOwner = bIsEligible;

	UWorld* CurWorld = GetWorld();
	if (!CurWorld)
	{
		return;
	}

	auto CurGameMode = CurWorld->GetAuthGameMode<ARHGameModeBase>();
	if (CurGameMode)
	{
		CurGameMode->DetermineSonyMatchDataOwner();
	}
}

void ARHPlayerController::SetSonyMatchId(const FString& InSonyMatchId)
{
	SonyMatchId = InSonyMatchId;

	if (ARHPlayerState* RHPlayerState = Cast<ARHPlayerState>(PlayerState))
	{
		RHPlayerState->SavedSonyMatchId = SonyMatchId;
	}
}

void ARHPlayerController::ClientUpdateSonyMatchData_Implementation(const FString& InMatchId, const FString& InActivityId)
{
	SonyActivityId = InActivityId;
	if (SonyMatchId.IsEmpty())
	{
		SetSonyMatchId(InMatchId);
	}

	bIsSonyMatchOwner = !SonyActivityId.IsEmpty() || !SonyMatchId.IsEmpty();

	if (ARHPlayerState* RHPlayerState = Cast<ARHPlayerState>(PlayerState))
	{
		RHPlayerState->SavedSonyActivityId = SonyActivityId;
		RHPlayerState->SavedSonyMatchId = SonyMatchId;
	}

	if (bIsSonyMatchOwner)
	{
		if (SonyMatchId.IsEmpty()) // No Match ID yet, get it even if we think we are ineligible
		{
			RequestUpdateSonyMatchState(ESonyMatchState::MatchIdRequested);
		}
		else if (bIsEligibleSonyMatchOwner)
		{
			// Only try to resume the game if we still think we are eligible.
			// otherwise, the server will be actively
			// looking for someone else to take over.
			RequestUpdateSonyMatchState(ESonyMatchState::Playing);
		}
	}
}

void ARHPlayerController::ServerUpdateSonyMatchData_Implementation(const FString& InMatchId)
{
	UWorld* CurWorld = GetWorld();
	if (!CurWorld)
	{
		return;
	}

	auto CurGameMode = CurWorld->GetAuthGameMode<ARHGameModeBase>();
	if (!CurGameMode)
	{
		return;
	}

	if (!PlayerState)
	{
		return;
	}

	CurGameMode->HandleMatchIdUpdateSony(PlayerState->GetUniqueId(), InMatchId);
}

bool ARHPlayerController::PlatformSupportsSonyMatchApi()
{
	return UGameplayStatics::GetPlatformName() == TEXT("PS5");
}

FString ARHPlayerController::GetSonyTeamIdFromActorTeamNum(int32 ActorTeamNum)
{
	return FString::Printf(TEXT("Team%d"), ActorTeamNum);
}

FString ARHPlayerController::GetSonyTeamDisplayNameFromActorTeamNum(int32 ActorTeamNum)
{
	return FString::Printf(TEXT("Team #%d"), ActorTeamNum);
}

void ARHPlayerController::UpdateSonyMatchResult(const FFinalGameMatchReport& InSonyMatchResult, bool bShouldCompleteMatch)
{
	SonyMatchResult = InSonyMatchResult;
	RequestUpdateSonyMatchState(bShouldCompleteMatch ? ESonyMatchState::SendCompleteMatch : ESonyMatchState::Playing);
}

static TSharedRef<const FUniqueNetId> CreateNetIdForBot(APlayerState* CurPlayerState)
{
	struct FUniqueNetIdBot : public FUniqueNetId
	{
		FUniqueNetIdBot(APlayerState* CurPlayerState)
		{
			check(CurPlayerState && CurPlayerState->IsABot());
			GeneratedBotName = FString::Printf(TEXT("FUniqueNetIdBot%d"), CurPlayerState->GetPlayerId());
		}

		virtual const uint8* GetBytes() const
		{
			return (uint8*)*GeneratedBotName;
		}

		virtual int32 GetSize() const
		{
			return GeneratedBotName.Len() * sizeof(TCHAR);
		}

		virtual bool IsValid() const
		{
			return true;
		}

		virtual FString ToString() const
		{
			return GeneratedBotName;
		}

		virtual FString ToDebugString() const
		{
			return GeneratedBotName;
		}

	protected:
		FString GeneratedBotName;
	};

	return MakeShared<FUniqueNetIdBot>(CurPlayerState);
}

void ARHPlayerController::UpdateSonyMatchRoster()
{
	UWorld* CurWorld = GetWorld();
	if (!CurWorld)
	{
		return;
	}

	AGameStateBase* GameState = CurWorld->GetGameState();
	if (!GameState)
	{
		if (QueuedSonyMatchState == ESonyMatchState::MatchIdRequested)
		{
			if (UWorld* MyWorld = GetWorld())
			{
				MyWorld->GameStateSetEvent.AddWeakLambda(this, [this](AGameStateBase* GameState)
					{
						RequestUpdateSonyMatchState(ESonyMatchState::MatchIdRequested);
					});
			}
		}

		return;
	}

	int32 NumPS5Players = 0;
	SonyMatchRoster.Players.Reset();
	SonyMatchRoster.Teams.Reset();

	for (APlayerState* CurPlayerState : GameState->PlayerArray)
	{
		if (!CurPlayerState)
		{
			continue;
		}

		const bool bIsABot = CurPlayerState->IsABot();

		FUniqueNetIdRepl PlayerUniqueId = CurPlayerState->GetUniqueId();
		if (!PlayerUniqueId.IsValid() && !bIsABot)
		{
			if (const ARHPlayerState* RHPlayerState = Cast<ARHPlayerState>(CurPlayerState))
			{
				if (const URH_PlayerInfo* PlayerInfo = RHPlayerState->GetPlayerInfo(Cast<ARHHUDCommon>(MyHUD.Get())))
				{
					for (const auto& PlatformId : PlayerInfo->GetPlayerPlatformIds())
					{
						if (PlatformId.PlatformType == ERHAPI_Platform::Psn)
						{
							PlayerUniqueId = UOnlineEngineInterface::Get()->CreateUniquePlayerIdWrapper(PlatformId.UserId);
						}
					}
				}
			}

			if (!PlayerUniqueId.IsValid())
			{
				continue;
			}
		}

		TSharedRef<const FUniqueNetId> UniqueId = bIsABot ? CreateNetIdForBot(CurPlayerState) : PlayerUniqueId->AsShared();

		FGameMatchPlayer GameMatchPlayer;
		GameMatchPlayer.bIsNpc = bIsABot;
		GameMatchPlayer.PlayerId = UniqueId;
		GameMatchPlayer.PlayerName = CurPlayerState->GetPlayerName();

		if (GameMatchPlayer.PlayerName.IsEmpty() || GameMatchPlayer.PlayerName == TEXT("Player"))
		{
			if (const ARHPlayerState* RHPlayerState = Cast<ARHPlayerState>(CurPlayerState))
			{
				if (const URH_PlayerInfo* PlayerInfo = RHPlayerState->GetPlayerInfo(Cast<ARHHUDCommon>(MyHUD.Get())))
				{
					PlayerInfo->GetLastKnownDisplayName(GameMatchPlayer.PlayerName);
				}
			}
		}

		FName CurPlayerOssType = UniqueId->GetType();

		if (!GSonyMatchSendCrossPlayNames &&
			!bIsABot &&
			CurPlayerOssType != PS5_SUBSYSTEM &&
			CurPlayerOssType != PS4_SUBSYSTEM)
		{
			int32 CrossPlayPlayerIndex = SonyCrossPlayPlayerIds.AddUnique(UniqueId);
			GameMatchPlayer.PlayerName = FString::Printf(TEXT("Player %d"), CrossPlayPlayerIndex + 1);
		}

		if (CurPlayerOssType == PS5_SUBSYSTEM)
		{
			NumPS5Players++;
		}

		if (!GameMatchPlayer.PlayerName.IsEmpty())
		{
			// Find the team we belong to
			int32 TeamNum = INDEX_NONE;

			if (const ARHPlayerState* RHPlayerState = Cast<ARHPlayerState>(CurPlayerState))
			{
				if (const ARHHUDCommon* RHHUD = Cast<ARHHUDCommon>(MyHUD))
				{
					if (const URH_LocalPlayerSubsystem* LPS = RHHUD->GetLocalPlayerSubsystem())
					{
						if (const URH_LocalPlayerSessionSubsystem* LPSS = LPS->GetSessionSubsystem())
						{
							if (const URH_JoinedSession* JoinedSession = LPSS->GetFirstActiveSession())
							{
								TeamNum = JoinedSession->GetSessionPlayerTeamId(RHPlayerState->GetRHPlayerUuid());
							}
						}
					}
				}
			}

			if (TeamNum > INDEX_NONE)
			{
				bool bFoundTeam = false;

				FString TeamId = GetSonyTeamIdFromActorTeamNum(TeamNum);
				for (FGameMatchTeam& Team : SonyMatchRoster.Teams)
				{
					if (Team.TeamId == TeamId)
					{
						bFoundTeam = true;
						Team.TeamMemberIds.Add(UniqueId);
						break;
					}
				}

				if (!bFoundTeam)
				{
					FGameMatchTeam NewTeam;
					NewTeam.TeamId = TeamId;
					NewTeam.TeamName = GetSonyTeamDisplayNameFromActorTeamNum(TeamNum);
					NewTeam.TeamMemberIds.Add(UniqueId);
					SonyMatchRoster.Teams.Add(NewTeam);
				}

				SonyMatchRoster.Players.Add(GameMatchPlayer);
			}
		}
	}

	// Keep track of whether or not we are the last PS5 player
	// in the match
	bIsExclusiveSonyMatchOwner = NumPS5Players < 2;
}

void ARHPlayerController::RequestUpdateSonyMatchState(ESonyMatchState NewSonyMatchState)
{
	// Match is complete or will be complete, no need to continue sending updates
	if (SonyMatchState == ESonyMatchState::Complete ||
		SonyMatchState == ESonyMatchState::SendCompleteMatch)
	{
		return;
	}

	// If we were going to complete the match, then just keep on with that.
	QueuedSonyMatchState = NewSonyMatchState;

	// Not the owner. Just hold on to the state
	// in case we become it and need to send the final
	// score, etc.
	if (!bIsSonyMatchOwner)
	{
		return;
	}

	// Still getting the match ID, queue the next update
	if (SonyMatchState == ESonyMatchState::MatchIdRequested)
	{
		return;
	}


	const bool bNeedsRosterUpdate = SonyMatchId.IsEmpty() || QueuedSonyMatchState == ESonyMatchState::Playing;
	if (bNeedsRosterUpdate)
	{
		UpdateSonyMatchRoster();
	}

	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	if (!OSS)
	{
		return;
	}

	TSharedPtr<const FUniqueNetId> LocalUserId = GetFirstSignedInUser(OSS->GetIdentityInterface());
	if (!LocalUserId.IsValid())
	{
		return;
	}

	IOnlineGameMatchesPtr GameMatches = OSS->GetGameMatchesInterface();
	if (!GameMatches.IsValid())
	{
		return;
	}

	// Filter out any teams that aren't in the last roster update
	// to prevent web API errors
	SonyMatchResult.Results.CompetitiveResult.TeamsResult.RemoveAll([&](const FGameMatchTeamResult& TeamResult)
		{
			return SonyMatchRoster.Teams.FindByPredicate([&](const FGameMatchTeam& Team) { return Team.TeamId == TeamResult.TeamId; }) == nullptr;
		});

	// Special case, if we don't have a match Id yet, then we must create one
	// use the result for future updates
	if (SonyMatchId.IsEmpty())
	{
		if (SonyMatchRoster.Players.Num() > 0)
		{
			SonyMatchState = ESonyMatchState::MatchIdRequested;

			FGameMatchesData MatchData;
			MatchData.ActivityId = SonyActivityId;
			MatchData.MatchesRoster = SonyMatchRoster;
			MatchData.InactivityExpirationTimeSeconds = GSonyMatchInactivityTimeoutSeconds;

			FOnCreateGameMatchComplete CreateGameMatchCompleteDelegate = FOnCreateGameMatchComplete::CreateWeakLambda(this,
				[this](const FUniqueNetId& InLocalUserId, const FString& MatchId, const FOnlineError& Result)
				{
					if (Result.WasSuccessful())
					{
						UE_LOG(LogFRHSonyMatch, Log, TEXT("Create Match Success: [%s]"), *MatchId);
						SetSonyMatchId(MatchId);

						ServerUpdateSonyMatchData(SonyMatchId);

						ESonyMatchState LastQueuedState = QueuedSonyMatchState;

						// Transition to playing state
						SonyMatchState = ESonyMatchState::Playing;
						RequestUpdateSonyMatchState(ESonyMatchState::Playing);

						// Complete the match if neeed
						if (LastQueuedState == ESonyMatchState::SendCompleteMatch ||
							LastQueuedState == ESonyMatchState::SendPauseOrCancelMatch)
						{
							RequestUpdateSonyMatchState(LastQueuedState);
						}
					}
					else
					{
						HandleSonyMatchUpdateError(TEXT("CreateMatchRequest"), Result);
					}
				});
			GameMatches->CreateGameMatch(*LocalUserId, MatchData, CreateGameMatchCompleteDelegate);
		}
	}
	else
	{
		// If we already have a match ID we can take the requested state change right away.
		// All future messages are 'fire and forget' outside of logging errors
		SonyMatchState = QueuedSonyMatchState;

		if (SonyMatchState == ESonyMatchState::SendPauseOrCancelMatch)
		{
			FOnGameMatchStatusUpdateComplete StatusUpdateComplete = FOnGameMatchStatusUpdateComplete::CreateLambda(
				[](const FUniqueNetId& LocalUserId, const EUpdateGameMatchStatus& Status, const FOnlineError& Result)
				{
					HandleSonyMatchUpdateError(TEXT("PauseRequest"), Result);
				});

			if (bIsExclusiveSonyMatchOwner)
			{
				// If we are the only PS5 player in the match,
				// then go ahead and just cancel the match directly
				GameMatches->UpdateGameMatchStatus(*LocalUserId, SonyMatchId, EUpdateGameMatchStatus::Aborted, StatusUpdateComplete);
			}
			else
			{
				// If there are more PS5 players in the match, just 'pause' the match
				// to allow the expiration timer to clean it up if no other player does
				GameMatches->UpdateGameMatchStatus(*LocalUserId, SonyMatchId, EUpdateGameMatchStatus::Paused, StatusUpdateComplete);
			}

			SonyMatchState = ESonyMatchState::Complete; // Prevent further updates
		}
		else if (SonyMatchState == ESonyMatchState::Playing)
		{
			SonyMatchRoster.Results = SonyMatchResult.Results; // Send intermediate result update along with roster

			FOnGameMatchStatusUpdateComplete StatusUpdateComplete = FOnGameMatchStatusUpdateComplete::CreateLambda(
				[](const FUniqueNetId& LocalUserId, const EUpdateGameMatchStatus& Status, const FOnlineError& Result)
				{
					HandleSonyMatchUpdateError(TEXT("InProgressRequest"), Result);
				});
			GameMatches->UpdateGameMatchStatus(*LocalUserId, SonyMatchId, EUpdateGameMatchStatus::InProgress, StatusUpdateComplete);


			FOnUpdateGameMatchDetailsComplete UpdateMatchDetailsComplete = FOnUpdateGameMatchDetailsComplete::CreateLambda(
				[](const FUniqueNetId& LocalUserId, const FOnlineError& Result)
				{
					HandleSonyMatchUpdateError(TEXT("UpdateGameMatchDetails"), Result);
				});
			GameMatches->UpdateGameMatchDetails(*LocalUserId, SonyMatchId, SonyMatchRoster, UpdateMatchDetailsComplete);
		}
		else if (SonyMatchState == ESonyMatchState::SendCompleteMatch)
		{
			FOnGameMatchReportComplete OnGameMatchReportComplete = FOnGameMatchReportComplete::CreateLambda(
				[](const FUniqueNetId& LocalUserId, const FOnlineError& Result)
				{
					HandleSonyMatchUpdateError(TEXT("ReportGameMatchResults"), Result);
				});
			GameMatches->ReportGameMatchResults(*LocalUserId, SonyMatchId, SonyMatchResult, OnGameMatchReportComplete);
			SonyMatchState = ESonyMatchState::Complete; // Prevent further updates
		}
	}
}
