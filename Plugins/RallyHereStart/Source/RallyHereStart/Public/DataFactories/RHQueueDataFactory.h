#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/DataTable.h"
#include "RH_FriendSubsystem.h"
#include "RH_MatchmakingBrowser.h"
#include "RHQueueDataFactory.generated.h"

USTRUCT(BlueprintType)
struct FRHQueueDetails : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Queue Details")
	FText Name;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Queue Details")
	FText Description;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Queue Details")
	TSoftObjectPtr<UTexture2D> Image;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Queue Details")
	bool IsCustom;

	// #RHTODO - Session - This should come from the QueueJoinParamsOverride once it is surfaced to the Client.
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Queue Details")
	int32 LevelLock;

	FRHQueueDetails() :
		IsCustom(false),
		LevelLock(0)
	{
	}
};

USTRUCT(BlueprintType)
struct FRHMapDetails : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Map Details")
	TSoftObjectPtr<UWorld> Map;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Map Details")
	FText Name;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Map Details")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	FRHMapDetails()
	{
	}
};

USTRUCT(BlueprintType)
struct FRH_CustomMatchMember
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="Custom Match Member")
	URH_RHFriendAndPlatformFriend* Friend;

	UPROPERTY(BlueprintReadOnly, Category="Custom Match Member")
	int32 TeamId;

	UPROPERTY(BlueprintReadOnly, Category="Custom Match Member")
	FGuid PlayerUUID;

	UPROPERTY(BlueprintReadOnly, Category="Custom Match Member")
	bool bIsPendingInvite;

	FRH_CustomMatchMember() :
		Friend(nullptr),
		TeamId(0),
		PlayerUUID(FGuid()),
		bIsPendingInvite(false)
	{ }

	FORCEINLINE bool operator==(const FRH_CustomMatchMember& Other) const
	{
		return PlayerUUID == Other.PlayerUUID;
	};
};

// we're currently mapping Match Queue State enums so that we can expose them on Blueprint-- is there a better way to do this?
UENUM(BlueprintType)
enum class ERH_MatchStatus : uint8
{
	NotQueued                 UMETA(DisplayName = "Not Queued"),                 // Not in a queue.
	Declined                  UMETA(DisplayName = "Declined"),                   // We declined the invite.
	Queued                    UMETA(DisplayName = "Queued"),                     // In queue waiting for match.
	Invited                   UMETA(DisplayName = "Invited"),                    // Invite has been sent to us.
	Accepted                  UMETA(DisplayName = "Accepted"),                   // We accepted the invite.
	Matching                  UMETA(DisplayName = "Matching"),                   // Waiting for matching logic to complete.
	Waiting                   UMETA(DisplayName = "Waiting"),                    // In holding map waiting for next map in rotation.
	InGame                    UMETA(DisplayName = "In Game"),                    // We are in the match game instance.
	SpectatorLobby            UMETA(DisplayName = "Spectator in Match Lobby"),   // We are a spectator in match lobby.
	SpectatorGame             UMETA(DisplayName = "Spectator in Match Game"),    // We are a spectator in match game.
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHQueueJoined, FString, QueueId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHQueueLeft);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHQueueInviteReceived);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHQueueStatusChange, ERH_MatchStatus, MatchStatus);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHQueueDataUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHSetSelectedQueueId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHMatchStatusUpdatedError, FText, ErrorText);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHCustomMatchJoined);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHCustomMatchDataChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHCustomMatchMapChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHCustomMatchLeft);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHCustomSearchResultRecevied, TArray<URH_SessionView*>, CustomSessions);

/**
 * 
 */
UCLASS(config = game, BlueprintType)
class RALLYHERESTART_API URHQueueDataFactory : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	void Initialize(class ARHHUDCommon* InHud);
	void Uninitialize();
	void PostLogin();
	void PostLogoff();
	bool IsInitialized() const { return bInitialized; }

protected:
	class ARHHUDCommon* MyHud;

private:
	bool bInitialized;

public:
	void							StartQueueUpdatePollTimer();
	void							StopQueueUpdatePollTimer();

	void							ClearCustomMatchData();

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory")
	bool							GetQueueDetailsByQueue(URH_MatchmakingQueueInfo* Queue, FRHQueueDetails& QueueDetails) const { return (Queue != nullptr) ? GetQueueDetailsByQueueId(Queue->GetQueueId(), QueueDetails) : false; }

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory")
	bool							GetQueueDetailsByQueueId(const FString& QueueId, FRHQueueDetails& QueueDetails) const;

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	TArray<URH_MatchmakingQueueInfo*> GetQueues() const;

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	URH_MatchmakingQueueInfo*	    GetQueueInfoById(const FString& QueueId) const;

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	virtual bool                    IsQueueActive(const FString& QueueId) const;

	bool							IsQueueActiveHelper(const FString& QueueId, bool bAllowHidden) const;

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	virtual bool                    CanQueue() const;

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	virtual bool                    JoinQueue(FString QueueId);

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	virtual bool                    LeaveQueue();

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	virtual bool 					LeaveMatch();

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	virtual bool 					AttemptRejoinMatch();

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory")
	float							GetTimeInQueueSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Maps")
	UDataTable*						GetMapsDetailsDT() const { return MapsDetailsDT; };

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	int32							GetPlayerTeamId(const FGuid& PlayerId) const;

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	bool							IsCustomGameSession(const URH_SessionView* pSession) const;

	// Creates the Custom Match session
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	virtual void                    CreateCustomMatchSession();

	// Leave the current Custom Match session
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	virtual void					LeaveCustomMatchSession();

	// Join a public Custom Match session
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	bool							JoinCustomMatchSession(const URH_SessionView* InSession) const;

	// Invites a given player to the Custom Match
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void                            InviteToCustomMatch(const FGuid& PlayerId, int32 TeamNum);

	// Checks if a custom invitation is pending (or spam prevented)
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	bool                            IsCustomInvitePending(const FGuid& PlayerId) const;

	// Kicks a given player from the Custom Match
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void                            KickFromCustomMatch(const FGuid& PlayerId);

	// Promotes a given player to being the host of the Custom Match
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void                            PromoteToCustomMatchHost(const FGuid& PlayerId);

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	bool							CanLocalPlayerControlCustomLobbyPlayer(const FGuid& PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	bool							CanLocalPlayerPromoteCustomLobbyPlayer(const FGuid& PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	bool							CanLocalPlayerKickCustomLobbyPlayer(const FGuid& PlayerId) const;

	// Accepts the current invite to a match
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void							AcceptMatchInvite();

	// Declines the current invite to a match
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	virtual void					DeclineMatchInvite();

	// Launch the match and join
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void							StartCustomMatch(bool bDedicatedInstance = false);

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	int32							GetTeamMemberCount(int32 TeamId) const;

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void							SetPlayerTeamCustomMatch(const FGuid& PlayerId, int32 TeamId);

	// Selects the Map for the given Custom Match
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void							SetMapForCustomMatch(FName MapRowName);

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	bool							GetMapDetailsFromRowName(FName MapRowName, FRHMapDetails& OutMapDetails) const;

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	FName							GetSelectedCustomMap() const { return SelectedCustomMatchMapRowName; }

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	TArray<FRH_CustomMatchMember>&	GetCustomMatchMembers() { return CustomMatchMembers; }

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	URH_JoinedSession*				GetCustomMatchSession() const { return CustomMatchSession; }

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	bool							IsPlayerCustomLobbyLeader(const FGuid& PlayerUUID) const;
	bool							IsPlayerInCustomMatch(const FGuid& PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
	bool							IsLocalPlayerCustomLobbyLeader() const;
	
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory|Custom")
	void							DoSearchForCustomGames();
	bool							GetBrowserSessionsLeaderName(URH_SessionView* InSession, FString& OutLeaderName);

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory")
	FRHQueueJoined					OnQueueJoined;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory")
	FRHQueueLeft					OnQueueLeft;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory")
	FRHQueueStatusChange			OnQueueStatusChange;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory")
	FRHQueueDataUpdated				OnQueueDataUpdated;

    UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory")
    FRHSetSelectedQueueId			OnSetQueueId;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory")
	FRHMatchStatusUpdatedError		OnMatchStatusUpdatedError;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory")
	FRHCustomMatchJoined			OnCustomMatchJoined;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory|Custom")
	FRHCustomMatchDataChanged		OnCustomMatchDataChanged;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory|Custom")
	FRHCustomMatchMapChanged		OnCustomMatchMapChanged;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory|Custom")
	FRHCustomMatchLeft				OnCustomMatchLeft;

	UPROPERTY(BlueprintAssignable, Category = "Queue Data Factory|Custom")
	FRHCustomSearchResultRecevied	OnCustomSearchResultReceived;

    UPROPERTY(transient)
    bool bCheckForAutoRejoin;

protected:
	UPROPERTY(BlueprintReadOnly, config, Category = "Queue Data Factory")
	FString							RHSessionType;

	UPROPERTY(Config)
	FString							SessionLeaderNameFieldName;

	UPROPERTY(BlueprintReadOnly, Category = "Queue Data Factory")
	URH_JoinedSession*				CustomMatchSession;
	
	UPROPERTY()
	TArray<FRH_CustomMatchMember>	CustomMatchMembers;

	FName							SelectedCustomMatchMapRowName;

	/* Transient */
	FGuid							PendingConfirmPlayerId;
	FGuid							LastLoginPlayerGuid;
	FString							PendingSessionId;

	void							PromptMatchRejoin(FString SessionId);
	
	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	void							AcceptMatchRejoin();

	UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
	void							DeclineMatchRejoin();

	void							UpdateCustomMatchInfo();
	void							UpdateCustomSessionBrowserInfo();

	void							HandleCustomMatchSessionCreated(bool bSuccess, URH_SessionView* JoinedSession, const FRH_ErrorInfo& ErrorInfo);
	void							HandleLoginPollSessionsComplete(bool bSuccess);
	void							HandleSessionAdded(URH_SessionView* pSession);
	void							HandleSessionRemoved(URH_SessionView* pSession);
	void							HandleSessionUpdated(URH_SessionView* pSession);
	void							HandleCustomSessionSearchResult(bool bSuccess, const struct FRH_SessionBrowserSearchResult& Result);

	void							ProcessCustomGameInvite(URH_InvitedSession* pSession);
	TArray<URH_InvitedSession*>		GetSessionInvites() const;

	UFUNCTION()
	void							HandleConfirmLeaveCustomLobby();

	UFUNCTION()
	void							HandleConfirmKickCustomPlayer();

	UFUNCTION()
	void							HandleCancelKickCustomPlayer();

	UFUNCTION()
	void							HandleConfirmPromoteCustomPlayer();

	UFUNCTION()
	void							HandleCancelPromoteCustomPlayer();

    /*
    * Queue status info helpers
    */
public:
    UFUNCTION(BlueprintPure, Category = "Queue Data Factory")
    bool							IsInQueue() const;

    UFUNCTION(BlueprintPure, Category = "Queue Data Factory|Custom")
    bool							IsInCustomMatch() const;

    UFUNCTION(BlueprintPure, Category = "Queue Data Factory")
    ERH_MatchStatus					GetCurrentQueueMatchState() const;

    UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
    bool                            SetSelectedQueueId(const FString& QueueId);

    UFUNCTION(BlueprintPure, Category = "Queue Data Factory")
	FString	   						GetSelectedQueueId() const;

    UFUNCTION(BlueprintCallable, Category = "Queue Data Factory")
    FORCEINLINE bool				JoinSelectedQueue() { return JoinQueue(SelectedQueueId); }

protected:
	URH_MatchmakingBrowserCache*	GetMatchmakingCache() const;

	bool							GetPreferredRegionId(FString& OutRegionId);

	void							PollQueues();

	void							SendMatchStatusUpdateNotify(ERH_MatchStatus MatchStatus);

	void							HandleQueuesPopulated(bool bSuccess, const FRH_QueueSearchResult& SearchResult, const FRH_ErrorInfo& ErrorInfo);

	void							OnMatchStatusError(FText ErrorMsg) { }

	UPROPERTY()
	TArray<FString>                 QueueIds;

	UPROPERTY(Config)
	FString							DefaultQueueId;

    UPROPERTY()
	FString                         SelectedQueueId;

	UPROPERTY()
	float                           QueueUpdatePollInterval;

	UPROPERTY()
	FTimerHandle                    QueueUpdateTimerHandle;

	FDateTime QueuedStartTime;

	// Table of all queue metadata
	UPROPERTY(Transient)
	UDataTable* QueueDetailsDT;

	// Table of all map metadata
	UPROPERTY(Transient)
	UDataTable* MapsDetailsDT;

private:

	/** Path to the DataTable to load, configurable per game. If empty, it will not spawn one */
	UPROPERTY(Config)
	FSoftObjectPath QueuesDataTableClassName;

	UPROPERTY(Config)
	FSoftObjectPath MapsDataTableClassName;

	class URHPartyManager* GetPartyManager() const;
	class URH_GameInstanceSessionSubsystem* GetGameInstanceSessionSubsystem() const;
	class URH_LocalPlayerSessionSubsystem* GetLocalPlayerSessionSubsystem() const;
};
