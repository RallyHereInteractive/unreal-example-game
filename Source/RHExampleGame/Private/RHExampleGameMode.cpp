// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RHExampleGame.h"
#include "RHExampleGameMode.h"
#include "RHExampleStatsMgr.h"
#include "Managers/RHStatsTracker.h"
#include "Player/Controllers/RHPlayerController.h"
#include "Lobby/HUD/RHLobbyHUD.h"

ARHExampleGameMode::ARHExampleGameMode(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
    : Super(ObjectInitializer)
{
    PlayerControllerClass = ARHPlayerController::StaticClass();
	StatsMgr = ObjectInitializer.CreateDefaultSubobject<URHExampleStatsMgr>(this, TEXT("StatsManager"));
	StatsMgr->SetGameMode(this);
	MatchStartTime = -1.0f;
	MatchEndTime = 0.0f;

	ShutdownOnEmptyDelay = 20 * 60; // 20 minutes
}

void ARHExampleGameMode::PostInitializeComponents()
{
	if (StatsMgr != nullptr)
	{
		StatsMgr->Clear();
	}

	Super::PostInitializeComponents();
}

void ARHExampleGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (NewPlayer != nullptr)
	{
		if (ARHPlayerState* const NewPlayerState = Cast<ARHPlayerState>(NewPlayer->PlayerState))
		{
			if (StatsMgr != nullptr)
			{
				StatsMgr->AddTracker(GetRHPlayerUuid(NewPlayer));
				StatsMgr->SetStopTrackerRewards(NewPlayerState, false);

				if (HasMatchStarted())
				{
					// Stats should begin on PlayerStats::BeginPlay.
					// This tracker started late, but should start nonetheless
					StatsMgr->BeginTracker(NewPlayerState);
				}
			}
		}
	}

	// if a new player has joined, stop the empty timer
	CheckEmptyTimer();
}

void ARHExampleGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	// if a player has left, check if we need to start the empty timer
	CheckEmptyTimer();
}

float ARHExampleGameMode::GetMatchTimeElapsed() const
{
	if (MatchStartTime >= 0.0f)
	{
		if (MatchEndTime > MatchStartTime)
		{
			//Match over
			return MatchEndTime - MatchStartTime;
		}
		return GetWorld()->GetTimeSeconds() - MatchStartTime;
	}
	return 0.0f;
}

void ARHExampleGameMode::HandleMatchIsWaitingToStart()
{
	Super::HandleMatchIsWaitingToStart();

	CheckEmptyTimer();
}

void ARHExampleGameMode::HandleMatchHasStarted()
{
	UWorld* MyWorld = GetWorld();
	check(MyWorld != nullptr);

	MatchStartTime = MyWorld->GetTimeSeconds();

	bool bHasPlayers = false;

	// Begin Stats Tracking
	for (FConstControllerIterator It = MyWorld->GetControllerIterator(); It; ++It)
	{
		if (ARHPlayerController* const pRHPlayerController = Cast<ARHPlayerController>((*It).Get()))
		{
			bHasPlayers = true;
			ARHPlayerState* pRHPlayerState = pRHPlayerController->GetPlayerState<ARHPlayerState>();
			if (pRHPlayerState != nullptr && StatsMgr != nullptr)
			{
				StatsMgr->BeginTracker(pRHPlayerState);
			}
		}
	}

	Super::HandleMatchHasStarted();

	CheckEmptyTimer();
}

void ARHExampleGameMode::HandleMatchHasEnded()
{
	UWorld* MyWorld = GetWorld();
	check(MyWorld != nullptr);

	MatchEndTime = MyWorld->GetTimeSeconds();

	// empty empty timer is cleared, even if it's not active
	CheckEmptyTimer(true);

	// End Stats Tracking
	if (StatsMgr != nullptr)
	{
		for (FConstControllerIterator It = MyWorld->GetControllerIterator(); It; ++It)
		{
			if (ARHPlayerController* const pRHPlayerController = Cast<ARHPlayerController>((*It).Get()))
			{
				ARHPlayerState* pRHPlayerState = pRHPlayerController->GetPlayerState<ARHPlayerState>();
				if (pRHPlayerState != nullptr)
				{
					StatsMgr->EndTracker(pRHPlayerState);
				}
			}
		}

		StatsMgr->FinishStats(this);
	}
	
	Super::HandleMatchHasEnded();
}

void ARHExampleGameMode::HandleMatchAborted()
{
	UWorld* MyWorld = GetWorld();
	check(MyWorld != nullptr);

	// empty empty timer is cleared, even if it's not active
	CheckEmptyTimer(true);

	// End Stats Tracking
	if (StatsMgr != nullptr)
	{
		for (FConstControllerIterator It = MyWorld->GetControllerIterator(); It; ++It)
		{
			if (ARHPlayerController* const pRHPlayerController = Cast<ARHPlayerController>((*It).Get()))
			{
				ARHPlayerState* pRHPlayerState = pRHPlayerController->GetPlayerState<ARHPlayerState>();
				if (pRHPlayerState != nullptr)
				{
					StatsMgr->EndTracker(pRHPlayerState);
				}
			}
		}

		StatsMgr->FinishStats(this);
	}

	Super::HandleMatchAborted();
}

void ARHExampleGameMode::CheckEmptyTimer(bool bForceStop)
{
	if (GetNumPlayers() == 0 && !bForceStop)
	{
		// if timer delay is set and is not active, activate it
		if (ShutdownOnEmptyDelay > 0 && !EmptyServerTimerHandle.IsValid())
		{
			UE_LOG(RHExampleGame, Log, TEXT("Starting empty timer (Delay = %ds"), ShutdownOnEmptyDelay);
			GetWorldTimerManager().SetTimer(EmptyServerTimerHandle, this, &ARHExampleGameMode::EmptyTimer, ShutdownOnEmptyDelay, false);
		}
	}
	else
	{
		// stop the timer if it is running
		if (EmptyServerTimerHandle.IsValid())
		{
			UE_LOG(RHExampleGame, Log, TEXT("Clearing empty timer (bForceStop = %d)"), bForceStop ? 1 : 0);
			GetWorldTimerManager().ClearTimer(EmptyServerTimerHandle);
			EmptyServerTimerHandle.Invalidate();
		}
	}
}

void ARHExampleGameMode::EmptyTimer()
{
	// if empty timer goes off, end the match
	UE_LOG(RHExampleGame, Warning, TEXT("EmptyTimer triggered due to not enough players, ending match"));
	EndMatch();
}