// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.
#include "RallyHereStart.h"
#include "GameFramework/RHGameModeBase.h"
#include "GameFramework/RHGameState.h"
#include "Player/Controllers/RHPlayerState.h"
#include "RH_LocalPlayer.h"
#include "UnrealEngine.h"
#include "Engine/LocalPlayer.h"
#include "Player/Controllers/RHPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "PlayerExperience/PlayerExperienceGlobals.h"
#include "PlayerExperience/PlayerExp_MatchTracker.h"
#include "AIController.h"
#include "RallyHereIntegration/Public/RH_OnlineSubsystemNames.h"
#include "Engine/GameInstance.h"
#include "RallyHereIntegration/Public/RH_GameInstanceSubsystem.h"
#include "RallyHereIntegration/Public/RH_GameInstanceSessionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogRHGameMode, All, All);

ARHGameModeBase::ARHGameModeBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
	PlayerControllerClass = ARHPlayerController::StaticClass();

	MatchSpindownDelay = 60;
	MatchEndReturnToLobbyDelay = 10;

	InactivePlayerStateLifeSpan = 0;    // default to preserving playerstates indefinitely

	bMatchClosedByCore = false;

	bGlobalDisableAIBackfill = false;
	bAllowAIBackfill = false;
	bHasPerformedInitialAIBackfill = false;
}

void ARHGameModeBase::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
}

void ARHGameModeBase::InitGameState()
{
	Super::InitGameState();
}

void ARHGameModeBase::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
}

void ARHGameModeBase::BeginDestroy()
{
	Super::BeginDestroy();

	if (GEngine)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
	}
}

// BEGIN - PS5 Match Handling
void ARHGameModeBase::HandleMatchIdUpdateSony(const FUniqueNetIdRepl& InRepId, const FString& InMatchId)
{
	if (InRepId == SonyMatchData.MatchOwner)
	{
		SonyMatchData.MatchId = InMatchId;

		if (const ARHPlayerController* MatchOwner = GetPlayerControllerFromUniqueId(InRepId))
		{
			if (ARHPlayerState* MatchOwnerPlayerState = Cast<ARHPlayerState>(MatchOwner->PlayerState))
			{
				MatchOwnerPlayerState->SavedSonyMatchId = InMatchId;
			}
		}
	}
}

ARHPlayerController* ARHGameModeBase::GetPlayerControllerFromUniqueId(const FUniqueNetIdRepl& InUniqueId) const
{
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* pPC = Iterator->Get();
		if (pPC != nullptr && pPC->PlayerState != nullptr && pPC->PlayerState->GetUniqueId() == InUniqueId)
		{
			return Cast<ARHPlayerController>(pPC);
		}
	}

	return nullptr;
}

void ARHGameModeBase::DetermineSonyMatchDataOwner()
{
	if (!HasMatchStarted())
	{
		return;
	}

	ARHPlayerController* PreviousMatchOwner = GetPlayerControllerFromUniqueId(SonyMatchData.MatchOwner);

	// Right now, if we have a previous match owner, but they have
	// not responded with a match ID, just exit to avoid creating too
	// many Matches in Sony's system.
	// We may want to have some sort of retry mechanism for this.
	if (PreviousMatchOwner && SonyMatchData.MatchId.IsEmpty())
	{
		return;
	}

	// Find the first available PS5 player
	ARHPlayerController* AvailableMatchOwner = nullptr;

	UWorld* CurWorld = GetWorld();

	for (FConstPlayerControllerIterator Iterator = CurWorld->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		auto PlayerActor = Cast<ARHPlayerController>(Iterator->Get());
		if (!PlayerActor || !PlayerActor->PlayerState)
		{
			continue;
		}

		if (PlayerActor->IsActorBeingDestroyed() || !PlayerActor->IsEligibleSonyMatchOwner())
		{
			continue;
		}

		const FUniqueNetIdRepl& UniqueId = PlayerActor->PlayerState->GetUniqueId();
		if (SonyIneligibleMatchOwners.Contains(UniqueId))
		{
			continue;
		}

		if (!UniqueId.IsValid() || UniqueId->GetType() != PS5_SUBSYSTEM)
		{
			continue;
		}

		if (AvailableMatchOwner == nullptr || PlayerActor == PreviousMatchOwner)
		{
			AvailableMatchOwner = PlayerActor;
		}
	}

	// If the PreviousMatchOwner became ineligible, but
	// we have no one better - keep them around.
	if (!AvailableMatchOwner && PreviousMatchOwner)
	{
		AvailableMatchOwner = PreviousMatchOwner;
	}

	if (AvailableMatchOwner)
	{
		// If we had a match owner before - tell them to stop sending updates
		if (PreviousMatchOwner && AvailableMatchOwner != PreviousMatchOwner)
		{
			PreviousMatchOwner->ClientUpdateSonyMatchData(FString(), FString());

			if (ARHPlayerState* RHPlayerState = Cast<ARHPlayerState>(PreviousMatchOwner->PlayerState))
			{
				RHPlayerState->SavedSonyActivityId = FString();
				RHPlayerState->SavedSonyMatchId = FString();
			}
		}

		check(AvailableMatchOwner->PlayerState != nullptr); // PlayerState is nullchecked as part of finding AvailableMatchOwner
		SonyMatchData.MatchOwner = AvailableMatchOwner->PlayerState->GetUniqueId();

		if (GetNetMode() != NM_Standalone && SonyActivityIdNetworkedMatch.Len() > 0)
		{
			AvailableMatchOwner->ClientUpdateSonyMatchData(SonyMatchData.MatchId, SonyActivityIdNetworkedMatch);

			if (ARHPlayerState* RHPlayerState = Cast<ARHPlayerState>(AvailableMatchOwner->PlayerState))
			{
				RHPlayerState->SavedSonyActivityId = SonyActivityIdNetworkedMatch;
				RHPlayerState->SavedSonyMatchId = SonyMatchData.MatchId;
			}
		}
		else
		{
			AvailableMatchOwner->ClientUpdateSonyMatchData(SonyMatchData.MatchId, SonyActivityId);
		}

		if (SonyMatchOwnerNetTimeout > KINDA_SMALL_NUMBER)
		{
			CurWorld->GetTimerManager().SetTimer(SonyCheckTimeoutHandle, this, &ARHGameModeBase::CheckSonyMatchOwnerNetTimeout, 1.0f, true);
		}
	}
}

void ARHGameModeBase::CheckSonyMatchOwnerNetTimeout()
{
	auto IsSonyMatchOwnerValid = [&](const FUniqueNetIdRepl& MatchOwner)
	{
		for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			const APlayerController* pPC = Iterator->Get();
			if (pPC != nullptr && pPC->PlayerState != nullptr && pPC->PlayerState->GetUniqueId() == MatchOwner)
			{
				return true;
			}
		}

		return false;
	};

	if (!IsSonyMatchOwnerValid(SonyMatchData.MatchOwner))
	{
		// Don't reconsider this actor for being the sony match owner in the future
		SonyIneligibleMatchOwners.Add(SonyMatchData.MatchOwner);
		DetermineSonyMatchDataOwner();
	}
}
// END - PS5 Match Handling

FRHPlayerProfile* ARHGameModeBase::CreatePlayerProfile()
{
    return new FRHPlayerProfile();
}

bool ARHGameModeBase::AllowDebugPlayers() const
{
    return GetWorld()->IsPlayInEditor() || GetWorld()->IsPlayInPreview() || GetWorld()->IsPreviewWorld();
}

FGuid ARHGameModeBase::GetRHPlayerUuid(AController* C)
{
	APlayerController* PC = Cast<APlayerController>(C);
	return PC != nullptr ? GetRHPlayerUuidFromPlayer(PC->Player) : FGuid();
}

FGuid ARHGameModeBase::GetRHPlayerUuidFromPlayer(const UPlayer* pPlayer)
{
	FGuid PlayerUuid;

	if (const URH_IpConnection* pConnection = Cast<const URH_IpConnection>(pPlayer))
	{
		PlayerUuid = pConnection->GetRHPlayerUuid();
	}
	else if (const URH_LocalPlayer* pLocalPlayer = Cast<const URH_LocalPlayer>(pPlayer))
	{
		PlayerUuid = pLocalPlayer->GetRHPlayerUuid();
	}

	return PlayerUuid;
}

FRHPlayerProfile* ARHGameModeBase::GetPlayerProfile(const FGuid& PlayerUuid)
{
	for (auto& Pair : m_PlayerProfiles)
	{
		if (Pair.Value->RHPlayerUuid == PlayerUuid)
		{
			return Pair.Value.Get();
		}
	}

	return nullptr;
}

FRHPlayerProfile* ARHGameModeBase::GetPlayerProfile(UPlayer* pPlayer)
{
	if (pPlayer == nullptr)
	{
		return nullptr;
	}

    TUniquePtr<FRHPlayerProfile>* pProfile = m_PlayerProfiles.Find(pPlayer);

    if (pProfile)
    {
        return pProfile->Get();
    }

	// create a new profile
    m_PlayerProfiles.Add(pPlayer, TUniquePtr<FRHPlayerProfile>(CreatePlayerProfile()));
    pProfile = m_PlayerProfiles.Find(pPlayer);

	InitDebugPlayerProfile(pProfile->Get(), pPlayer);
	return pProfile->Get();
}

FString ARHGameModeBase::GetPlayerNameForProfile(const FRHPlayerProfile* pProfile) const
{
    check(pProfile != nullptr);
    FString ret = pProfile->PlayerName;
    if (ret.IsEmpty())
    {
        ret = *DefaultPlayerName.ToString();
    }

    return ret;
}

FString ARHGameModeBase::InitNewPlayerFromProfile(UPlayer* NewPlayer, ARHPlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, FRHPlayerProfile* pProfile)
{
    FString ErrorMessage;

    check(NewPlayerController);


    //pPlayerMarshal->GetField(MFTOK_SPAWN_POINT_NAME, spawnname, _countof(spawnname));  // TODO: might not be correct token, code has been removed from Smite since GA

    // Register the player with the session
    GameSession->RegisterPlayer(NewPlayerController, UniqueId, false);

    // Init player's name
    FString InName = GetPlayerNameForProfile(pProfile);

    ChangeName(NewPlayerController, InName, false);

    // Find a starting spot
    AActor* const StartSpot = FindPlayerStart(NewPlayerController);
    if (StartSpot != NULL)
    {
        // Set the player controller / camera in this new location
        FRotator InitialControllerRot = StartSpot->GetActorRotation();
        InitialControllerRot.Roll = 0.f;
        NewPlayerController->SetInitialLocationAndRotation(StartSpot->GetActorLocation(), InitialControllerRot);
        NewPlayerController->StartSpot = StartSpot;
    }
    else
    {
        ErrorMessage = FString::Printf(TEXT("Failed to find PlayerStart"));
    }

    return ErrorMessage;
}

void ARHGameModeBase::InitDebugPlayerProfile(FRHPlayerProfile* pProfile, const UPlayer* pPlayer)
{
    pProfile->PlayerName = TEXT("Player");
    pProfile->bSpectator = false;
	pProfile->RHPlayerUuid = GetRHPlayerUuidFromPlayer(pPlayer);;
    pProfile->bDebugPlayer = true;
}

float ARHGameModeBase::CalculateMatchCloseness_Implementation()
{
    return 0.0f;
}

void ARHGameModeBase::HandleMatchIsWaitingToStart()
{
    Super::HandleMatchIsWaitingToStart();

    if (UPlayerExp_MatchTracker* pMatchTracker = UPlayerExperienceGlobals::Get().GetMatchTracker(GetWorld()))
    {
        pMatchTracker->DoStartMatch();
    }
}

void ARHGameModeBase::HandleMatchHasStarted()
{
    Super::HandleMatchHasStarted();

    if (UPlayerExp_MatchTracker* pMatchTracker = UPlayerExperienceGlobals::Get().GetMatchTracker(GetWorld()))
    {
        pMatchTracker->DoStartMatch();
    }
}

void ARHGameModeBase::HandleMatchHasEnded()
{
	Super::HandleMatchHasEnded();

	AllPlayersEndGame();

	if (MatchEndReturnToLobbyDelay > 0.f)
	{
		// start timer for return to lobby
		GetWorldTimerManager().SetTimer(
			ReturnToLobbyTimer,
			this,
			&ARHGameModeBase::AllPlayersReturnToLobby,
			MatchEndReturnToLobbyDelay);
	}
	else
	{
		AllPlayersReturnToLobby();
	}

	if (UPlayerExp_MatchTracker* pMatchTracker = UPlayerExperienceGlobals::Get().GetMatchTracker(GetWorld()))
	{
		pMatchTracker->DoEndMatch(CalculateMatchCloseness());
	}

	// mark session as closed to prevent new joins
	auto* GameInstance = GetGameInstance();
	if (GameInstance != nullptr)
	{
		auto* GISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
		if (GISS != nullptr)
		{
			auto GISession = GISS->GetSessionSubsystem();
			if (GISession != nullptr && GISession->GetActiveSession() != nullptr)
			{ 
				/* This will mark the session to not be joinable.  Unfortunately, closed will cause a dedicated server to close itself out and recycle itself, we will need a new state to handle that
				auto* RHSession = GISession->GetActiveSession();

				FRHAPI_InstanceInfoUpdate InstanceInfo = RHSession->GetInstanceUpdateInfoDefaults();
				InstanceInfo.SetJoinStatus(ERHAPI_InstanceJoinableStatus::Closed);
				RHSession->UpdateInstanceInfo(InstanceInfo);
				*/
			}
		}
	}

	// start timer for instance shutdown (network loop)
	if (MatchSpindownDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(
			ShutdownTimer,
			this,
			&ARHGameModeBase::FinalizeMatchEnded,
			MatchSpindownDelay);
	}
	else
	{
		FinalizeMatchEnded();
	}
}

void ARHGameModeBase::HandleMatchAborted()
{
	Super::HandleMatchAborted();

	// mark session as closed to prevent new joins
	auto* GameInstance = GetGameInstance();
	if (GameInstance != nullptr)
	{
		auto* GISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
		if (GISS != nullptr)
		{
			auto GISession = GISS->GetSessionSubsystem();
			if (GISession != nullptr && GISession->GetActiveSession() != nullptr)
			{
				/* This will mark the session to not be joinable.  Unfortunately, closed will cause a dedicated server to close itself out and recycle itself, we will need a new state to handle that
				auto* RHSession = GISession->GetActiveSession();

				FRHAPI_InstanceInfoUpdate InstanceInfo = RHSession->GetInstanceUpdateInfoDefaults();
				InstanceInfo.SetJoinStatus(ERHAPI_InstanceJoinableStatus::Closed);
				RHSession->UpdateInstanceInfo(InstanceInfo);
				*/
			}
		}
	}

	// start timer for instance shutdown (network loop)
	if (MatchSpindownDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(
			ShutdownTimer,
			this,
			&ARHGameModeBase::FinalizeMatchEnded,
			MatchSpindownDelay);
	}
	else
	{
		FinalizeMatchEnded();
	}
}

void ARHGameModeBase::StartMatch()
{
	Super::StartMatch();

	ensure(HasMatchStarted());
	DetermineSonyMatchDataOwner();
}

void ARHGameModeBase::GameWelcomePlayer(UNetConnection* Connection, FString& RedirectURL)
{
	Super::GameWelcomePlayer(Connection, RedirectURL);

	CloseConflictingNetConnections(Cast<URH_IpConnection>(Connection));
}

void ARHGameModeBase::Logout(AController* Exiting)
{
	AttemptToBackfillPlayer(Exiting);

	Super::Logout(Exiting);
}

void ARHGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	check(NewPlayer != nullptr);

	if (!TryToRecoverFromDisconnect(NewPlayer))
	{
		Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	}
}

APlayerController* ARHGameModeBase::Login(class UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	ErrorMessage = GameSession->ApproveLogin(Options);
	if (!ErrorMessage.IsEmpty())
	{
		return nullptr;
	}

	URH_IpConnection* NetConnection = Cast<URH_IpConnection>(NewPlayer);
	CloseConflictingNetConnections(NetConnection);

	FRHPlayerProfile* rpProfile = GetPlayerProfile(NewPlayer);

	if (rpProfile == nullptr)
	{
		auto ConnectionNetId = NetConnection != nullptr ? NetConnection->GetRHPlayerUuid() : FGuid();
		ErrorMessage = FString::Printf(TEXT("Couldn't find or create game profile for player %s, preventing login completion"), *ConnectionNetId.ToString());
		UE_LOG(LogRHGameMode, Error, TEXT("%s"), *ErrorMessage);
		return nullptr;
	}

	const FGuid& RHPlayerUuid = rpProfile->RHPlayerUuid;

	ARHPlayerController* NewPlayerController = Cast<ARHPlayerController>(SpawnPlayerController(InRemoteRole, TEXT("")));

	// Handle spawn failure.
	if (NewPlayerController == NULL)
	{
		UE_LOG(LogRHGameMode, Log, TEXT("Couldn't spawn RHPlayerController derived player controller of class %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("NULL"));
		ErrorMessage = FString::Printf(TEXT("Failed to spawn player controller"));
		return nullptr;
	}

	check(NewPlayerController->PlayerState != nullptr);

	NewPlayerController->SetRHPlayerUuid(RHPlayerUuid);

	// Customize incoming player based on URL options
	UE_LOG(LogRHGameMode, Log, TEXT("Initializing player %s from profile"), *RHPlayerUuid.ToString());
	ErrorMessage = InitNewPlayerFromProfile(NewPlayer, NewPlayerController, UniqueId, rpProfile);

	// Set up spectating
	bool bSpectator = FCString::Stricmp(*UGameplayStatics::ParseOption(Options, TEXT("SpectatorOnly")), TEXT("1")) == 0;
	if (bSpectator || MustSpectate(NewPlayerController))
	{
		NewPlayerController->StartSpectatingOnly();
		return NewPlayerController;
	}

	return NewPlayerController;
}

void ARHGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// our cheat add check does not work until the UPlayer has been assigned to the player controller
	if (NewPlayer)
	{
		if (auto RHPlayerController = Cast<ARHPlayerController>(NewPlayer))
		{
			RHPlayerController->ClientCheckSonyMatchOwnerEligibility();
		}

		if (ARHPlayerState* const NewPlayerState = Cast<ARHPlayerState>(NewPlayer->PlayerState))
		{
			NewPlayerState->SetRHPlayerUuid(GetRHPlayerUuid(NewPlayer));
		}
	}

	DetermineSonyMatchDataOwner();
}

void ARHGameModeBase::InactivePlayerStateDestroyed(AActor* InActor)
{
	if (IsValid(this))
	{
		for (int32 nIndex = 0; nIndex < RHInactivePlayerArray.Num(); ++nIndex)
		{
			if (RHInactivePlayerArray[nIndex].PlayerState == InActor)
			{
				RHInactivePlayerArray.RemoveAtSwap(nIndex);
				return;
			}
		}
	}
}

FRHInactivePlayerStateEntry& ARHGameModeBase::FindOrAddInactivePlayerArrayEntry(const FGuid& RHPlayerUuid)
{
	for (FRHInactivePlayerStateEntry& Entry : RHInactivePlayerArray)
	{
		if (RHPlayerUuid == Entry.RHPlayerUuid)
		{
			return Entry;
		}
	}

	int32 nNewIndex = RHInactivePlayerArray.AddZeroed();
	RHInactivePlayerArray[nNewIndex].RHPlayerUuid = RHPlayerUuid;
	return RHInactivePlayerArray[nNewIndex];
}

bool ARHGameModeBase::CanBackfillDropPlayersWithAI() const
{
	return !bGlobalDisableAIBackfill && bAllowAIBackfill;
}

void ARHGameModeBase::TransferControl(AController* Src, AController* Dest)
{
	if (Src == Dest || Src == nullptr || Dest == nullptr)
	{
		return;
	}

	TransferControl_PrePawnTransfer(Src, Dest);

	APawn* ExistingPawn = Src->GetPawn();
	if (ExistingPawn != nullptr)
	{
		FRotator NewControllerRot = ExistingPawn->GetActorRotation();
		NewControllerRot.Roll = 0.f;

		// Set initial control rotation to starting rotation rotation
		Dest->ClientSetRotation(NewControllerRot, true);
		Dest->SetControlRotation(NewControllerRot);

		Src->UnPossess();
	}

	TransferControl_PawnTransfer(Src, Dest, ExistingPawn);
	TransferControl_PostPawnTransfer(Src, Dest);
}

bool ARHGameModeBase::AttemptToBackfillPlayer(AController* Controller)
{
	if (!bHasPerformedInitialAIBackfill)
	{
		return false;
	}

	if (HasMatchEnded())
	{
		return false;
	}

	if (!CanBackfillDropPlayersWithAI())
	{
		return false;
	}

	if (!CanBeAIBackfilled(Controller))
	{
		return false;
	}

	return SpawnBackfillAI(Controller);
}

bool ARHGameModeBase::TryToRecoverFromDisconnect(class APlayerController* NewPlayer)
{
	check(NewPlayer != nullptr);
	check(NewPlayer->Player != nullptr);

	if (!bHasPerformedInitialAIBackfill)
	{
		return false;
	}

	if (!CanBackfillDropPlayersWithAI())
	{
		return false;
	}

	if (MustSpectate(NewPlayer))
	{
		//Spectators do not need to recover.
		return false;
	}

	UWorld* MyWorld = GetWorld();
	check(MyWorld != nullptr);

	FGuid NewPlayerUuid = GetRHPlayerUuidFromPlayer(NewPlayer->Player);
	if (!NewPlayerUuid.IsValid())
	{
		// cannot recover without a PlayerId;
		return false;
	}

	AController* BackfillController = nullptr;
	for (FConstControllerIterator It = MyWorld->GetControllerIterator(); It; ++It)
	{
		auto OtherC = It->Get();
		ARHPlayerState* PS = OtherC ? OtherC->GetPlayerState<ARHPlayerState>() : nullptr;
		if (OtherC != NewPlayer && PS != nullptr && PS->GetRHPlayerUuid() == NewPlayerUuid)
		{
			if (ensureMsgf((IsPlayerStateStale(PS)) || CheckIfBackfilling(OtherC), TEXT("Expecting a stale or backfilling PlayerState during reconnection recovery.")))
			{
				BackfillController = OtherC;
			}
			break;
		}
	}

	if (BackfillController != nullptr)
	{
		TransferControl(BackfillController, NewPlayer);

		// Kill the backfilling bot.
		if (BackfillController->IsA<AAIController>())
		{
			BackfillController->Destroy();
		}
		return true;
	}

	return false;
}

bool ARHGameModeBase::CheckIfBackfilling(AController* Controller) const
{
	return false;
}

bool ARHGameModeBase::CanBeAIBackfilled(const FRHPlayerProfile* InPlayerProfile) const
{
	return InPlayerProfile != nullptr && !InPlayerProfile->bSpectator;
}

bool ARHGameModeBase::SpawnBackfillAI(const FRHPlayerProfile* InPlayerProfile)
{
	return false;
}

bool ARHGameModeBase::CanBeAIBackfilled(AController* SrcController) const
{
	APlayerController* HumanController = Cast<APlayerController>(SrcController);
	if (HumanController == nullptr || MustSpectate(HumanController) || IsPlayerStateStale(HumanController->PlayerState))
	{
		return false;
	}
	return true;
}

bool ARHGameModeBase::SpawnBackfillAI(AController* SrcController)
{
	return false;
}

void ARHGameModeBase::TransferControl_PrePawnTransfer(AController* Src, AController* Dest)
{
	check(Src != nullptr);
	check(Dest != nullptr);

	APlayerState* SrcPlayerState = Src->PlayerState;
	APlayerState* DestPlayerState = Dest->PlayerState;

	if (SrcPlayerState != nullptr && DestPlayerState != nullptr)
	{
		// Copy PlayerState Properties.
		SrcPlayerState->DispatchCopyProperties(DestPlayerState);

		// Ensure this state is not reused if possible.
		MarkPlayerStateAsStale(SrcPlayerState);

		DestPlayerState->OnReactivated();
	}
}

void ARHGameModeBase::TransferControl_PawnTransfer(AController* Src, AController* Dest, APawn* Pawn)
{
	check(Src != nullptr);
	check(Dest != nullptr);

	if (Pawn != nullptr)
	{
		Dest->Possess(Pawn);
	}
}

void ARHGameModeBase::TransferControl_PostPawnTransfer(AController* Src, AController* Dest)
{
	check(Src != nullptr);
	check(Dest != nullptr);
}

void ARHGameModeBase::AddInactivePlayer(APlayerState* PlayerState, APlayerController* PC)
{
	check(PlayerState);


	ARHPlayerController* RHPC = Cast<ARHPlayerController>(PC);
	if (RHPC == nullptr)
	{
		Super::AddInactivePlayer(PlayerState, PC);
		return;
	}

	if (IsPlayerStateStale(PlayerState))
	{
		return;
	}

	FGuid RHPlayerUuid = RHPC->GetRHPlayerUuid();
	if (!RHPlayerUuid.IsValid())
	{
		// No Id to work with.
		return;
	}

	// This will always be called on a server, so we do not need to send the local controller id.
	if (FRHPlayerProfile* rpProfile = GetPlayerProfile(PC->Player))
	{
		if (rpProfile->bSpectator)
		{
			// This is player controller that either has no match composition marshal or a spectator. We don't need to bother storing there PlayerState.
			return;
		}
	}

	UWorld* LocalWorld = GetWorld();
	// don't store if it's an old PlayerState from the previous level or if it's a spectator... or if we are shutting down
	if (!PlayerState->IsFromPreviousLevel() && !MustSpectate(PC) && !LocalWorld->bIsTearingDown)
	{
		APlayerState* const NewPlayerState = PlayerState->Duplicate();
		if (NewPlayerState)
		{
			// Side effect of Duplicate() adding PlayerState to PlayerArray (see APlayerState::PostInitializeComponents)
			GameState->RemovePlayerState(NewPlayerState);

			// make PlayerState inactive
			NewPlayerState->SetReplicates(false);

			// delete after some time
			NewPlayerState->SetLifeSpan(InactivePlayerStateLifeSpan);

			FRHInactivePlayerStateEntry& ArrayEntry = FindOrAddInactivePlayerArrayEntry(RHPlayerUuid);
			if (ArrayEntry.PlayerState != nullptr && IsValid(ArrayEntry.PlayerState))
			{
				// Delete the old entry.
				ArrayEntry.PlayerState->OnDestroyed.RemoveDynamic(this, &ARHGameModeBase::InactivePlayerStateDestroyed);
				ArrayEntry.PlayerState->Destroy();
			}

			ArrayEntry.PlayerState = NewPlayerState;
			ArrayEntry.PlayerState->OnDestroyed.AddDynamic(this, &ARHGameModeBase::InactivePlayerStateDestroyed);

			//NOTE: We are only storing entires from the Match Composition Marshal, so I'm not worried about space.
			//So I'm not going to bother with the size management present in the Engine version of this function.
		}
	}
}

bool ARHGameModeBase::FindInactivePlayer(APlayerController* PC)
{
	check(PC && PC->PlayerState);

	ARHPlayerController* RHPC = Cast<ARHPlayerController>(PC);

	if (RHPC == nullptr)
	{
		return Super::FindInactivePlayer(PC);
	}

	// don't bother for spectators
	if (MustSpectate(PC))
	{
		return false;
	}

	if (RHPC->Player == nullptr)
	{
		// Can't do anything without a player.
		return false;
	}


	FGuid PlayerUuid = GetRHPlayerUuidFromPlayer(RHPC->Player);
	if (!PlayerUuid.IsValid())
	{
		// Cant' do anything without a NetId;
		return false;
	}

	APlayerState* FoundInactiveState = nullptr;
	bool FoundPlayerStateIsInUse = false;

	{
		for (int32 nIndex = 0; nIndex < RHInactivePlayerArray.Num(); nIndex++)
		{
			if (RHInactivePlayerArray[nIndex].RHPlayerUuid == PlayerUuid)
			{
				APlayerState* PlayerStateToCheck = RHInactivePlayerArray[nIndex].PlayerState;
				RHInactivePlayerArray.RemoveAtSwap(nIndex);
				if (!IsPlayerStateStale(PlayerStateToCheck))
				{
					FoundInactiveState = PlayerStateToCheck;
					break;
				}
			}
		}
	}

	// Iterate through all connected players and see if they match this incoming player.
	// Use the connected player as your template and mark them stale so it does not get reused.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->IsValid())
		{
			auto C = It->Get();
			ARHPlayerState* OtherPlayerState = C->GetPlayerState<ARHPlayerState>();
			FGuid OtherUuid = OtherPlayerState != nullptr ? OtherPlayerState->GetRHPlayerUuid() : FGuid();

			if (OtherUuid == PlayerUuid && OtherPlayerState != PC->PlayerState && !IsPlayerStateStale(OtherPlayerState))
			{
				FoundInactiveState = OtherPlayerState;
				FoundPlayerStateIsInUse = true;
				break;
			}
		}
	}

	if (FoundInactiveState != nullptr && IsValid(FoundInactiveState) && !IsPlayerStateStale(FoundInactiveState))
	{
		FoundInactiveState->DispatchCopyProperties(RHPC->PlayerState);

		// Ensure this state is not reused if possible.
		MarkPlayerStateAsStale(FoundInactiveState);

		if (!FoundPlayerStateIsInUse)
		{
			FoundInactiveState->OnDestroyed.RemoveDynamic(this, &ARHGameModeBase::InactivePlayerStateDestroyed);
			FoundInactiveState->Destroy();
			FoundInactiveState = nullptr;
		}


		// Use the method to signify a copy from in
		RHPC->PlayerState->OnReactivated();
		return true;
	}

	return false;
}

bool ARHGameModeBase::ShouldAllowMultipleConnections(const class URH_IpConnection* InNetConnection) const
{
	return false;
}

void ARHGameModeBase::MarkPlayerStateAsStale(APlayerState* InPlayerState)
{
}

bool ARHGameModeBase::IsPlayerStateStale(const APlayerState* InPlayerState) const
{
	return false;
}

void ARHGameModeBase::CloseConflictingNetConnections(URH_IpConnection* InNetConnection)
{
	if (InNetConnection == nullptr)
	{
		return;
	}

	if (ShouldAllowMultipleConnections(InNetConnection))
	{
		return;
	}

	// if the uuid is not valid but it is a RH connection, the player is not logged in or did not provide login information, so we cannot check for conflicts
	auto playerUuid = InNetConnection->GetRHPlayerUuid();
	if (!playerUuid.IsValid())
	{
		return;
	}

	// find the net connection and drop it
	UNetDriver* pNetDriver = GetWorld()->GetNetDriver();

	if (pNetDriver != nullptr)
	{
		for (auto conn : pNetDriver->ClientConnections)
		{
			URH_IpConnection* pRHConn = Cast<URH_IpConnection>(conn);
			if (pRHConn != nullptr && pRHConn != InNetConnection && pRHConn->GetRHPlayerUuid() == playerUuid && pRHConn->GetConnectionState() != USOCK_Closed)
			{
				UE_LOG(LogRHGameMode, Log, TEXT("Closing conflicting net connection for player %s, from Remote Add: %s"), *playerUuid.ToString(), *(pRHConn->RemoteAddressToString()));
				// if we found them, clear the ids and close the connection
				pRHConn->ClearLocalIds();
				pRHConn->Close();
			}
		}
	}
}

void ARHGameModeBase::AllPlayersEndGame()
{
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Iterator->Get();
		if (PC)
		{
			PC->GameHasEnded();
		}
	}
}

void ARHGameModeBase::AllPlayersReturnToLobby()
{
	GetWorldTimerManager().ClearTimer(ReturnToLobbyTimer);

	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Iterator->Get();
		if (PC)
		{
			PC->ClientReturnToMainMenuWithTextReason(FText::AsCultureInvariant(TEXT("Game Has Ended")));
		}
	}
}

void ARHGameModeBase::FinalizeMatchEnded()
{
	// make sure everyone is out
	AllPlayersReturnToLobby();

	GetWorldTimerManager().ClearTimer(ShutdownTimer);

	auto* GameInstance = GetGameInstance();
	if (GameInstance != nullptr)
	{
		auto* GISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
		if (GISS != nullptr)
		{
			auto GISession = GISS->GetSessionSubsystem();
			if (GISession != nullptr && GISession->GetActiveSession() != nullptr)
			{
				auto* Session = GISession->GetActiveSession();

				// start leaving the instance, this should mark the instance as closed, which should start a recycle for a dedicated server
				GISession->StartLeaveInstanceFlow(false, false);
			}
		}
	}

	// start timer for instance shutdown fallback
	GetWorldTimerManager().SetTimer(
		ShutdownTimer,
		this,
		&ARHGameModeBase::FinalShutdown,
		5.0f);
}

// It's the fi-nal shut-down!
void ARHGameModeBase::FinalShutdown()
{
	// if we have reached this point where the gamemode is still active, ending the instance either did not succeed quickly, or failed, in which case we need to use a backup routine
	if (IsRunningDedicatedServer())
	{
		// if running a dedicated server, shutdown.  This should go through normal instance shutdown-and-recycle logic.
		RequestEngineExit(TEXT("RHGamemode FinalShutdown"));
	}
	else
	{
		// for non dedicated servers, run the leave flow if we can
		bool bHandledViaLeaveFlow = false;
		auto* GameInstance = GetGameInstance();
		if (GameInstance != nullptr)
		{
			auto* GISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
			if (GISS != nullptr)
			{
				auto GISession = GISS->GetSessionSubsystem();
				if (GISession != nullptr && GISession->GetActiveSession() != nullptr)
				{
					GISession->StartLeaveInstanceFlow();
					bHandledViaLeaveFlow = true;
				}
			}
		}

		// if the leave flow could not be triggered, run the engine disconnect handler
		if (!bHandledViaLeaveFlow)
		{
			GEngine->HandleDisconnect(GetWorld(), GetWorld()->GetNetDriver());
		}
	}
}
