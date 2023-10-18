
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "RH_FriendSubsystem.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RH_LocalPlayerSessionSubsystem.h"
#include "RHPartyManager.generated.h"

// #RHTODO - Session - Remove this and use session members directly
USTRUCT(BlueprintType)
struct FRH_PartyMemberData
{
    GENERATED_USTRUCT_BODY()
public:

	UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
	URH_PlayerInfo* PlayerData;

    UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
    bool    IsFriend;

    UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
    FText   StatusMessage;

    UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
    bool    Online;

    UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
    bool    IsPending;

    UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
    bool    CanInvite;

    UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
    bool    IsLeader;

    UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Party Member Data")
    bool    IsReady;

    FRH_PartyMemberData()
        : PlayerData(nullptr)
        , IsFriend(false)
        , StatusMessage()
        , Online(false)
        , IsPending(false)
        , CanInvite(false)
        , IsLeader(false)
        , IsReady(false)
    {
    }

    FORCEINLINE bool operator ==(const FRH_PartyMemberData& OtherPartyMemberData) const
    {
        return (PlayerData != nullptr && OtherPartyMemberData.PlayerData != nullptr && PlayerData->GetRHPlayerUuid() == OtherPartyMemberData.PlayerData->GetRHPlayerUuid())
            && OtherPartyMemberData.IsFriend == IsFriend
            && OtherPartyMemberData.StatusMessage.ToString() == StatusMessage.ToString()
            && OtherPartyMemberData.Online == Online
            && OtherPartyMemberData.IsPending == IsPending
            && OtherPartyMemberData.CanInvite == CanInvite
            && OtherPartyMemberData.IsLeader == IsLeader
            && OtherPartyMemberData.IsReady == IsReady
            ;
    }

};

UENUM(BlueprintType)
enum class ERH_PartyInviteRightsMode : uint8
{
    ERH_PIRM_OnlyLeader                 UMETA(DisplayName = "Only Leader Can Invite"),
    ERH_PIRM_LeaderStartingCanGrant     UMETA(DisplayName = "Starts as Only Leader-- leader can grant"),
    ERH_PIRM_AllMembers                 UMETA(DisplayName = "All Party Members Can Invite"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyDataUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyInfoUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyLocalPlayerLeft);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyLocalPlayerPromoted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyDisbanded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyInvitationAccepted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyInvitationRejected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRH_PartyInvitationExpired);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyMemberStatusChanged, const FGuid&, PlayerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyMemberPromoted, const FGuid&, PlayerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyMemberDataUpdated, FRH_PartyMemberData, PartyMember);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PendingPartyMemberDataAdded, FRH_PartyMemberData, PartyMember);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PendingPartyMemberAccepted, FRH_PartyMemberData, PartyMember);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyMemberRemoved, const FGuid&, PartyMemberId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyMemberLeft, FRH_PartyMemberData, PartyMember);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyInvitationError, FText, ErrorMsg);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyInvitationSent, URH_PlayerInfo*, Invitee);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_PartyInvitationReceived, URH_PlayerInfo*, Inviter);

/**
 * 
 */
UCLASS(config = game, BlueprintType)
class RALLYHERESTART_API URHPartyManager : public UObject
{
	GENERATED_BODY()
	
public:
	URHPartyManager(const FObjectInitializer& ObjectInitializer);
	void Initialize(class ARHHUDCommon* InHud);
	void Uninitialize();
	void PostLogin();
	void PostLogoff();

protected:
	UPROPERTY()
	class ARHHUDCommon* MyHud;

#pragma region GENERAL UTILITIES
/*
* General utility
*/
public:
    // get party members
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    TArray<FRH_PartyMemberData>			GetPartyMembers() const { return PartyMembers; }

	// get party member by ID
	UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
	FRH_PartyMemberData					GetPartyMemberByID(const FGuid& PlayerID);

    // is in party?
	UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
	bool								IsInParty() const;

    // is maxed out
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    bool								IsPartyMaxed() const { return PartyMaxed; }

	// get RH session type
	UFUNCTION(BlueprintPure, Category = "RH Party Manager")
	FString								GetRHSessionType() const { return RHSessionType; }

    // get max party members
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    int32								GetMaxPartyMembers() const { return MaxPartySize; }

    // get current number of party members
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    int32								GetPartyMemberCount() const { return PartyMembers.Num(); }

    // get the party invite rights mode
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    ERH_PartyInviteRightsMode			GetPartyInviteMode() const { return CurrentInviteMode; }

	//get the party inviter
	UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
	URH_PlayerInfo*						GetPartyInviter() const { return PartyInviter; }

    // set the party invite rights mode
    void								SetPartyInviteMode(ERH_PartyInviteRightsMode PartyInviteMode) { CurrentInviteMode = PartyInviteMode;  };

	// Allows the party leader to set info specific to the party
	UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
	void								SetPartyInfo(const FString& Key, const FString& Value);

	// Gets the Party Info for a given Key
	UFUNCTION(BlueprintPure, Category = "RH Party Manager")
	FString								GetPartyInfo(const FString& Key) const;

	virtual void						OnNewPartyMemberAdded(FRH_PartyMemberData* NewPartyMemberData);

    UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
    FRH_PartyDataUpdated				OnPartyDataUpdated;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyLocalPlayerLeft			OnPartyLocalPlayerLeft;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyLocalPlayerPromoted		OnPartyLocalPlayerPromoted;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyMemberPromoted				OnPartyMemberPromoted;

    UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
    FRH_PartyMemberDataUpdated			OnPartyMemberDataUpdated;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PendingPartyMemberDataAdded		OnPendingPartyMemberDataAdded;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PendingPartyMemberAccepted		OnPendingPartyMemberAccepted;

    UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
    FRH_PartyMemberRemoved				OnPartyMemberRemoved;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyMemberLeft					OnPartyMemberLeft;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyDisbanded					OnPartyDisbanded;

    UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
    FRH_PartyInvitationError			OnPartyInvitationError;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyInvitationSent				OnPartyInvitationSent;

    UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
    FRH_PartyInvitationReceived			OnPartyInvitationReceived;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyInvitationAccepted			OnPartyInvitationAccepted;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyInvitationRejected			OnPartyInvitationRejected;

    UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
    FRH_PartyInvitationExpired			OnPartyInvitationExpired;

	// Listen for Updates from leader specified party info
	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyInfoUpdated				OnPartyInfoUpdated;

	UPROPERTY(BlueprintAssignable, Category = "RH Party Manager")
	FRH_PartyMemberStatusChanged OnPartyMemberStatusChanged;

	UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
	// check if party member is the leader of the party
	bool								CheckPartyMemberIsLeader(const FGuid& PlayerId) const;

	// check if the local player is the party leader
	UFUNCTION(BlueprintPure, Category = "RH Party Manager")
	bool								IsLeader() const;

	UFUNCTION(BlueprintPure, Category = "RH Party Manager")
	bool								GetPartyLeader(FRHAPI_SessionPlayer& OutPlayer) const;

	UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
	bool								HasInvitePrivileges(const FGuid& PlayerId) const;

	UFUNCTION(BlueprintCallable, Category = "RH Party Data Factory")
	FORCEINLINE void BroadcastPartyInvitationError(FText InvitationError)
	{
		OnPartyInvitationError.Broadcast(InvitationError);
	}

	// Lets the leader of the party set the parties selected queueId
	UFUNCTION(BlueprintCallable, Category = "RH Party Data Factory")
	void SetSelectedQueueId(const FString& QueueId);

	// Returns the selected queue of the party
	UFUNCTION(BlueprintPure, Category = "RH Party Data Factory")
	FString GetSelectedQueueId() const;

	UFUNCTION(BlueprintPure, Category = "RH Party Data Factory")
	URH_JoinedSession* GetPartySession() const { return PartySession; }

    UFUNCTION()
    void PartyKickResponse();
    UFUNCTION()
    void PartyLeaveResponse();
    UFUNCTION()
    void PartyPromoteResponse();

protected:
    // local player's party members
    UPROPERTY(Transient)
    TArray<FRH_PartyMemberData>			PartyMembers;

    // local player party inviter
    UPROPERTY(Transient)
    URH_PlayerInfo*						PartyInviter;

    // last invite sent error message received
    UPROPERTY(Transient)
    FString								LastInviteSentErrorMessage;
	    
    // is local player's party maxed out
    bool								PartyMaxed;

    // is player pending leaving the party
    bool								IsPendingLeave;

	// is local player kicked from party
	bool								IsKicked;

    // current invite rights mode
    ERH_PartyInviteRightsMode			CurrentInviteMode;

	UPROPERTY(BlueprintReadOnly, config, Category = "RH Party Manager")
	int32								MaxPartySize;
	
	UPROPERTY(BlueprintReadOnly, config, Category = "RH Party Manager")
	FString								RHSessionType;

	virtual void						HandleSessionUpdated(URH_SessionView* pSession);
	virtual void						HandleSessionAdded(URH_SessionView* pSession);
	virtual void						HandleSessionRemoved(URH_SessionView* pSession);
	virtual void						HandleSessionCreated(bool bSuccess, URH_JoinedSession* JoinedSession, const FRH_ErrorInfo& ErrorInfo);
	virtual void						HandleLoginPollSessionsComplete(bool bSuccess);
	virtual void						HandlePartyMemberStateChanged(URH_SessionView* UpdatedSession, const FRH_SessionMemberStatusState& OldStatus, const FRH_SessionMemberStatusState& NewStatus);

	UPROPERTY(BlueprintReadOnly, Category = "RH Party Manager")
	URH_JoinedSession*					PartySession;

	virtual TArray<URH_InvitedSession*> GetSessionInvites() const;

	void								LeaveQueue();

private:
	FGuid MemberKickId;
	FGuid MemberPromotedId;
	FGuid LastLoginPlayerGuid;

	URH_LocalPlayerSubsystem* GetLocalPlayerSubsystem() const;
	URH_LocalPlayerSessionSubsystem* GetPlayerSessionSubsystem() const;
    URH_FriendSubsystem* GetFriendSubsystem() const;

	/* For the default not-in-party state. */
	void CreateSoloParty();

	UFUNCTION()
	void HandlePreferredRegionUpdated();
	void HandleUpdateSessionRegionIdResponse(bool bSuccess, URH_JoinedSession* pSession, const FRH_ErrorInfo& ErrorInfo);
#pragma endregion

#pragma region SETTING UP THE PARTY AND HANDLING UPDATES
/*
* Setting up the party and handling updates
*/
protected:
	void								HandleFriendUpdate(URH_RHFriendAndPlatformFriend* Friend);

	// updates party info
	virtual void						UpdatePartyFromSubsystem();
	virtual void						UpdateParty(URH_JoinedSession* pSession);
	virtual void						UpdatePartyInvites(TArray<URH_InvitedSession*> pSessions);

	// updates the party member struct with new member data struct, uses player id for comparison
	bool								UpdatePartyMember(FRH_PartyMemberData& PartyMemberData);

	// removes a party member struct from the local array using the provided player id
	bool								RemovePartyMemberById(const FGuid& PlayerId);

	// flesh out a party member struct for use by the UI
	bool								PopulatePartyMemberData(const struct FRHAPI_SessionPlayer* RHPartyMember, FRH_PartyMemberData& PartyMemberData);

	// populates an array for each member in the party
	TArray<FRH_PartyMemberData>			GetNewPartyMemberData();

	// check if party members are also friends
	bool								CheckPartyMemberIsFriend(const FGuid& PlayerId);

	// force clean up of party - declines an existing party invite and leaves a party
	void								ForcePartyCleanUp(bool ForceLeave = false);

	// handle a session update event
	void								HandleSessionUpdate(bool bSuccess, URH_JoinedSession* pSession, const FRH_ErrorInfo& ErrorInfo);
#pragma endregion

#pragma region USER INTERFACE
/*
* User interface stuff
*/
public:
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    virtual bool						IsPlayerInParty(const FGuid& PlayerId);

    // UIX : Invite Member to Party
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    virtual void						UIX_InviteMemberToParty(const FGuid& PlayerId);

    // UIX : Accept a pending invite
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    void								UIX_AcceptPartyInvitation();

    // UIX : Deny a pending invite
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    void								UIX_DenyPartyInvitation();

    // UIX : Promote Member to Leader
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    virtual void						UIX_PromoteMemberToLeader(const FGuid& PlayerId);

    // UIX : Disband Party
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    virtual void						UIX_DisbandParty() {}

    // UIX : Leave Party
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    virtual void						UIX_LeaveParty();

    // UIX : Kick Member from Party
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    virtual void						UIX_KickMemberFromParty(const FGuid& PlayerId);

    // UIX : Inform player that they were kicked
    UFUNCTION(BlueprintCallable, Category = "RH Party Manager")
    virtual void						UIX_PlayerKickedFromParty();
#pragma endregion
};
