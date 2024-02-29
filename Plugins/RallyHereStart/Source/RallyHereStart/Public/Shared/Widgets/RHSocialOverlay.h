// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shared/Widgets/RHWidget.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_LocalPlayerSessionSubsystem.h"
#include "Shared/Social/RHDataSocialCategory.h"
#include "RHSocialOverlay.generated.h"

namespace RHSocial
{
	bool PlayerSortByName(const URH_RHFriendAndPlatformFriend* A, const URH_RHFriendAndPlatformFriend* B);
	bool CategorySort(URHDataSocialCategory* A, URHDataSocialCategory* B);
}

UENUM(BlueprintType)
enum class ERHSocialOverlaySection : uint8
{
	Invalid					UMETA(DisplayName = "Invalid"),

	// Friends Panel
	SessionMembers			UMETA(DisplayName = "Session Members"),
	SessionInvitations		UMETA(DisplayName = "Session Invitations"),
	FriendInvites			UMETA(DisplayName = "Friend Invites"),
	OnlineRHFriends			UMETA(DisplayName = "RH Friends Online"),
	OnlinePlatformFriends	UMETA(DisplayName = "Platform Friends Online"),
	OfflineRHFriends		UMETA(DisplayName = "RH Friends Offline"),
	Blocked					UMETA(DisplayName = "Blocked"),

	// Search Panel
	SearchResults			UMETA(DisplayName = "Search Results"),
	Pending					UMETA(DisplayName = "Pending"),
	RecentlyPlayed			UMETA(DisplayName = "Recently Played"),
	SuggestedFriends		UMETA(DisplayName = "Suggested Friends"),
	MAX
};

USTRUCT(BlueprintType)
struct FRHSocialOverlaySectionInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	ERHSocialOverlaySection Section;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FString SubSection;

	FRHSocialOverlaySectionInfo()
		: Section(ERHSocialOverlaySection::Invalid),
		  SubSection(TEXT(""))
	{}

	FRHSocialOverlaySectionInfo(ERHSocialOverlaySection InSection)
		: Section(InSection),
		  SubSection(TEXT(""))
	{}

	FRHSocialOverlaySectionInfo(ERHSocialOverlaySection InSection, FString InSubSection)
		: Section(InSection),
		  SubSection(InSubSection)
	{}

	bool operator==(const FRHSocialOverlaySectionInfo& Other) const
	{
		return Other.Section == Section && (Other.SubSection.Compare(SubSection) == 0);
	}
};

static FORCEINLINE uint32 GetTypeHash(const FRHSocialOverlaySectionInfo& Section) { return HashCombine(GetTypeHash(Section.Section), GetTypeHash(Section.SubSection)); }

// Dispatched when any friend info changes - Sections will be empty if it's all
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHSocialListsChangedDelegate, const TArray<FRHSocialOverlaySectionInfo>&, SectionsChanged);

/**
 *
 */
UCLASS(Config=Game)
class RALLYHERESTART_API URHSocialOverlay : public URHWidget
{
	GENERATED_BODY()

public:
	virtual void InitializeWidget_Implementation() override;
	virtual void UninitializeWidget_Implementation() override;
	virtual void OnShown_Implementation() override;
	virtual void OnHide_Implementation() override;

	UFUNCTION(BlueprintCallable)
	void PlayTransition(class UWidgetAnimation* Animation, bool TransitionOut);

	// Dispatched when any friend info changes - Sections will be empty if it's all
	UPROPERTY(BlueprintAssignable)
	FRHSocialListsChangedDelegate OnDataChanged;

	UFUNCTION(BlueprintPure)
	const TArray<URHDataSocialCategory*>& GetData() const { return CategoriesList; }

	UFUNCTION(BlueprintCallable)
	TArray<URHDataSocialCategory*> GetCategories(TArray<ERHSocialOverlaySection> Categories) const;

	UFUNCTION(BlueprintCallable)
	URHDataSocialCategory* GetCategory(FRHSocialOverlaySectionInfo Section) const;

	UFUNCTION()
	void HandleUpdatePlayers();

	void OnFriendsListChange(const TArray<URH_RHFriendAndPlatformFriend*>& UpdatedFriends);
	void OnFriendDataChangeGeneric(URH_RHFriendAndPlatformFriend* Friend);
	void OnBlockedPlayerUpdated(const FGuid& PlayerUuid, bool Blocked);

	UFUNCTION()
	void OnPartyMemberChanged(const FGuid& PlayerUuid);

protected:
	UFUNCTION(BlueprintImplementableEvent)
	void OnFriendsReceived();

	/* OSS Friends List refresh interval (in seconds) when social is shown. */
	UPROPERTY(EditDefaultsOnly, Category = "Platform Friends List")
	float OSSFriendsListRefreshInterval;

private:
	UFUNCTION(BlueprintCallable)
	void RepopulateAll();

	void OnCloseTransitionComplete(UUMGSequencePlayer& SeqInfo);

	UPROPERTY()
	UUMGSequencePlayer* TransitionOutSequencePlayer;

	UPROPERTY()
	TArray<URHDataSocialCategory*> CategoriesList;

	/// Updates optimization data
	void SchedulePlayersUpdate();
	void EnqueuePlayerUpdate(URH_RHFriendAndPlatformFriend* Friend);
	void TryChangePartyLeaderAndReSort(const FGuid& PlayerUuid);

	bool AddPlayerToSection(URH_RHFriendAndPlatformFriend* Friend, FRHSocialOverlaySectionInfo Section);
	URHDataSocialPlayer* MakePlayerContainer();

	FTimerHandle NextUpdateTimer;

	UPROPERTY()
	TMap<TWeakObjectPtr<URH_RHFriendAndPlatformFriend>, FRHSocialOverlaySectionInfo> PlayerCategoryMap;

	UPROPERTY()
	TArray<TWeakObjectPtr<URH_RHFriendAndPlatformFriend>> PlayersToUpdate;

	FGuid OldPartyLeaderUuid;

	UPROPERTY()
	TArray<URHDataSocialPlayer*> UnusedEntries; // pool so we don't need to make more every update
	
	URH_LocalPlayerSubsystem* GetRH_LocalPlayerSubsystem() const;
	URH_FriendSubsystem* GetRH_LocalPlayerFriendSubsystem() const;
	URH_LocalPlayerSessionSubsystem* GetRH_LocalPlayerSessionSubsystem() const;
	URH_PlayerInfoSubsystem* GetRH_PlayerInfoSubsystem() const;

	FRHSocialOverlaySectionInfo GetSectionForSocialFriend(const URH_RHFriendAndPlatformFriend* Friend);

	UPROPERTY(EditDefaultsOnly)
	TArray<FString> SessionsTypesToDisplay;

	bool HasInitialFriendsData;

	void RequestPresenceForPlayer(const URH_PlayerInfo* PlayerInfo);
	void HandleGetPresence(bool bSuccessful, URH_PlayerInfoSubobject* PresenceInfo);
	void SortPlayerToSocialSection(URH_RHFriendAndPlatformFriend* Friend);

	UFUNCTION()
	void HandleLogInUserChanged();

	void RequestOSSFriendsList() const;
	FTimerHandle RequestOSSFriendsListTimerHandle;

	bool IsCrossplayEnabled() const;
};
