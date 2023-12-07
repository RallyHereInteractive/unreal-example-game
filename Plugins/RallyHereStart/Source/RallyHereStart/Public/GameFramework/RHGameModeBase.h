#pragma once
#include "GameFramework/GameMode.h"

#include "RHGameModeBase.generated.h"

USTRUCT()
struct FRHInactivePlayerStateEntry
{
    GENERATED_BODY()

public:
	UPROPERTY()
	FGuid RHPlayerUuid;

    UPROPERTY()
    class APlayerState* PlayerState;

	FRHInactivePlayerStateEntry()
		: RHPlayerUuid()
		, PlayerState(nullptr)
	{}
};

USTRUCT()
struct FRHSonyMatchData
{
    GENERATED_BODY()

    UPROPERTY()
    FString MatchId;

    FUniqueNetIdRepl MatchOwner;
};

USTRUCT()
struct RALLYHERESTART_API FRHPlayerProfile
{
    GENERATED_BODY()

public:

	UPROPERTY()
	FGuid RHPlayerUuid;

    UPROPERTY()
    FString PlayerName;

    UPROPERTY()
    bool bSpectator;

    // Debug player flag means that there is no actual backing profile data, just this stub
    UPROPERTY()
    bool bDebugPlayer;

	FRHPlayerProfile() :
		bSpectator(false),
		bDebugPlayer(false)
	{
		RHPlayerUuid.Invalidate();
	}
};

UCLASS(config=Game)
class RALLYHERESTART_API ARHGameModeBase : public AGameMode
{
	GENERATED_UCLASS_BODY()

public:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void PostSeamlessTravel() override;

	virtual void BeginDestroy() override;

	virtual void StartMatch() override;

	virtual void GameWelcomePlayer(UNetConnection* Connection, FString& RedirectURL) override;

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	virtual void Logout(AController* Exiting) override;

	virtual APlayerController* Login(class UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage);

	/** Called after a successful login.  This is the first place it is safe to call replicated functions on the PlayerAController. */
	virtual void PostLogin(APlayerController* NewPlayer);

	/** Add PlayerState to the inactive list, remove from the active list */
	virtual void AddInactivePlayer(APlayerState* PlayerState, APlayerController* PC) override;

	/** Attempt to find and associate an inactive PlayerState with entering PC.
	* @Returns true if a PlayerState was found and associated with PC. */
	virtual bool FindInactivePlayer(APlayerController* PC) override;

	// Marks a player state as stale to prevent it from being added to the inactive players list.
	virtual void MarkPlayerStateAsStale(APlayerState* InPlayerState);

	// Checks if a player state is stale
	virtual bool IsPlayerStateStale(const APlayerState* InPlayerState) const;

	// Attempt to find the Player Controller based on the Unique Net Id
	class ARHPlayerController* GetPlayerControllerFromUniqueId(const FUniqueNetIdRepl& InUniqueId) const;

	// Attempt to find the Player Id that a controller represents (for both humans and backfilling bots).
	static FGuid GetRHPlayerUuid(AController* C);
	static FGuid GetRHPlayerUuidFromPlayer(const UPlayer* pPlayer);

	FRHPlayerProfile* GetPlayerProfile(UPlayer* pPlayer);
	FRHPlayerProfile* GetPlayerProfile(const FGuid& PlayerUuid);

	TMap<TWeakObjectPtr<UPlayer>, TUniquePtr<FRHPlayerProfile>>::TConstIterator CreatePlayerProfileIterator() const { return m_PlayerProfiles.CreateConstIterator(); }

public:
    bool AllowDebugPlayers() const;

protected:
	virtual bool ShouldAllowMultipleConnections(const class URH_IpConnection* InNetConnection) const;

	void CloseConflictingNetConnections(class URH_IpConnection* InNetConnection);

    TMap<TWeakObjectPtr<UPlayer>, TUniquePtr<FRHPlayerProfile> > m_PlayerProfiles;

    virtual FRHPlayerProfile* CreatePlayerProfile();
    virtual FString GetPlayerNameForProfile(const FRHPlayerProfile* pProfile) const;
    virtual FString InitNewPlayerFromProfile(class UPlayer* NewPlayer, class ARHPlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, FRHPlayerProfile* rpProfile);

    // This version of InitPlayerProfile is used in coreless games (such as PIE).
    virtual void InitDebugPlayerProfile(FRHPlayerProfile* pProfile, const UPlayer* pPlayer);

    //Player Experience
protected:
    UFUNCTION(BlueprintNativeEvent, Category = "Player Experience")
    float CalculateMatchCloseness();

    virtual void HandleMatchIsWaitingToStart() override;
    virtual void HandleMatchHasStarted() override;
    virtual void HandleMatchHasEnded() override;

	 /** This is where you should notify all players that you have ended the game, and send potential match results */
    virtual void AllPlayersEndGame();
    virtual void AllPlayersReturnToLobby();

    UFUNCTION()
    virtual void FinalizeMatchEnded();
    UFUNCTION()
    void FinalShutdown();

    FTimerHandle ShutdownTimer;
    FTimerHandle ReturnToLobbyTimer;
    UPROPERTY(config)
    float MatchEndReturnToLobbyDelay;
    UPROPERTY(config)
    float MatchSpindownDelay;

    // BEGIN - Sony Match Handling
    UPROPERTY(config, EditDefaultsOnly, Category=Playstation)
    FString SonyActivityId;

    UPROPERTY(config, EditDefaultsOnly, Category = Playstation)
    FString SonyActivityIdNetworkedMatch;

    UPROPERTY(config, EditDefaultsOnly, Category=Playstation)
    float SonyMatchOwnerNetTimeout;

    UPROPERTY()
    FRHSonyMatchData SonyMatchData;

    UPROPERTY()
    TArray<FUniqueNetIdRepl> SonyIneligibleMatchOwners;

    FTimerHandle SonyCheckTimeoutHandle;

    bool bMatchClosedByCore;
	 
public:
	 void HandleMatchIdUpdateSony(const FUniqueNetIdRepl& InRepId, const FString& MatchId);
	 void DetermineSonyMatchDataOwner();
	 void CheckSonyMatchOwnerNetTimeout();
	 // END - Sony Match Handling

public:
	const TArray<FRHInactivePlayerStateEntry>& GetInactivePlayerArray() const { return RHInactivePlayerArray; }

private:

	UFUNCTION()
	void InactivePlayerStateDestroyed(AActor* InActor);

	FRHInactivePlayerStateEntry& FindOrAddInactivePlayerArrayEntry(const FGuid& RHPlayerUuid);

	UPROPERTY(Transient)
	TArray<FRHInactivePlayerStateEntry> RHInactivePlayerArray;

	///////////////////////////////////
	// AI Backfill support
	///////////////////////////////////
public:
	virtual bool CanBackfillDropPlayersWithAI() const;

	virtual void TransferControl(AController* Src, AController* Dest);
protected:
	virtual bool AttemptToBackfillPlayer(AController* Controller);
	virtual bool TryToRecoverFromDisconnect(class APlayerController* NewPlayer);

	virtual bool CheckIfBackfilling(AController* Controller) const;

	// Initial AI Backfill functions.
	virtual bool CanBeAIBackfilled(const FRHPlayerProfile* InPlayerProfile) const;
	virtual bool SpawnBackfillAI(const FRHPlayerProfile* InPlayerProfile);

	// Secondary AI Backfill funcytions.
	virtual bool CanBeAIBackfilled(AController* SrcController) const;
	virtual bool SpawnBackfillAI(AController* SrcController);
	virtual void TransferControl_PrePawnTransfer(AController* Src, AController* Dest);
	virtual void TransferControl_PawnTransfer(AController* Src, AController* Dest, APawn* Pawn);
	virtual void TransferControl_PostPawnTransfer(AController* Src, AController* Dest);

	// Disable Bot Backfilling across the whole game as a fail-safe.
	UPROPERTY(GlobalConfig)
	bool bGlobalDisableAIBackfill;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Game|AI", meta = (DisplayName = "Allow AI Backfill"))
	bool bAllowAIBackfill;

	UPROPERTY(Transient)
	bool bHasPerformedInitialAIBackfill;
};
