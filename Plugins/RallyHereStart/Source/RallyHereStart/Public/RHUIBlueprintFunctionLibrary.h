#pragma once

#include "PlatformInventoryItem/PlatformInventoryItem.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "DataFactories/RHSettingsDataFactory.h"
#include "Inventory/RHBattlepass.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RHUIBlueprintFunctionLibrary.generated.h"

// Report Player Reasons
UENUM(BlueprintType)
enum class EReportPlayerReason :uint8
{
	Unknown_None          UMETA(DisplayName = "Unknown or None"),
	Harassment            UMETA(DisplayName = "Harassment"),
	Cheating              UMETA(DisplayName = "Cheating"),
	Teaming               UMETA(DisplayName = "Teaming"),
	IntentionalFeeding    UMETA(DisplayName = "Intentional Feeding"),
	StreamSniping         UMETA(DisplayName = "Stream Sniping"),
	LeavingTheGame_AFK    UMETA(DisplayName = "Leaving the game / AFK"),
	OtherReason           UMETA(DisplayName = "Other"),
	AFK                   UMETA(DisplayName = "AFK")
};

UENUM(BlueprintType)
enum class ERHPlayerOnlineStatus : uint8
{
	FGS_InParty		  UMETA(DisplayName = "In Party"),	// The player is in the local player's party (external parties are not considered)
	FGS_PendingParty  UMETA(DisplayName = "Pending Party Invite"),
	FGS_InGame		  UMETA(DisplayName = "In Game"),
	FGS_InMatch		  UMETA(DisplayName = "In Match"),
	FGS_InQueue		  UMETA(DisplayName = "In Queue"),
	FGS_Online		  UMETA(DisplayName = "Online"),
	FGS_DND			  UMETA(DisplayName = "DND"),
	FGS_Away		  UMETA(DisplayName = "Away"),
	FGS_Offline		  UMETA(DisplayName = "Offline"),
	FGS_FriendRequest UMETA(DisplayName = "Friendship Requested"),
	FGS_PendingInvite UMETA(DisplayName = "Pending Invite"),
};

UENUM(BlueprintType)
enum class ERHPlatformDisplayType : uint8
{
	PC,
	Xbox,
	Playstation,
	Switch,
	ConsoleGeneric,
	Epic,
	Steam,
	IOS,
	Android,
	MobileGeneric,
	Unknown
};

/**
* Parameters for the ReportPlayer functionality
*	Only create this struct via SetupReportPlayer- variants
*/
USTRUCT(BlueprintType)
struct FReportPlayerParams
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	int64 PlayerId;
	UPROPERTY(BlueprintReadOnly)
	FString EOSProductUserId;

	UPROPERTY()
	FString MatchId;
	UPROPERTY(BlueprintReadOnly)
	int32 LocalPlayerTeamId;
	UPROPERTY(BlueprintReadOnly)
	int32 ReportedPlayerTeamId;
	UPROPERTY(BlueprintReadOnly)
	int32 TotalPlayerCount;
	UPROPERTY(BlueprintReadOnly)
	bool FromBackfillEnabledGame;

    UPROPERTY(BlueprintReadOnly)
    FString PlayerName;

	UPROPERTY(BlueprintReadWrite)
	EReportPlayerReason Reason;

	UPROPERTY(BlueprintReadWrite)
	FString ReportComment;

	FReportPlayerParams() :
		PlayerId(0),
		LocalPlayerTeamId(0),
		ReportedPlayerTeamId(0),
		TotalPlayerCount(0),
		FromBackfillEnabledGame(false),
		Reason(EReportPlayerReason::Unknown_None)
	{ }
};

UCLASS()
class RALLYHERESTART_API URHUIBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Game")
    static bool IsPlatformType(
        UPARAM() bool IsConsole,
        UPARAM() bool IsPC,
        UPARAM() bool IsMobile
    );

	UFUNCTION(BlueprintPure, Category = "Game")
	static bool IsAnonymousLogin(ARHHUDCommon* pHUD);

    // Helper to register a rectangular group of widgets
    UFUNCTION(BlueprintCallable, Category = "Widget Navigation")
    static void RegisterGridNavigation(URHWidget* ParentWidget, int32 FocusGroup, const TArray<UWidget*> NavWidgets, int32 GridWidth, bool NavToLastElementOnDown = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Navigation")
	static void RegisterLinearNavigation(URHWidget* ParentWidget, const TArray<URHWidget*> NavWidgets, int32 FocusGroup, bool bHorizontal = true, bool bLooping = false);

	UFUNCTION(BlueprintPure, Category = "Player Info")
	static URH_PlayerInfo* GetLocalPlayerInfo(ARHHUDCommon* HUD);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "String")
	static int CompareStrings(const FString& LeftString, const FString& RightString);

    // Returns the short display name of the key.
    UFUNCTION(BlueprintPure, meta = (DisplayName = "Get Key Short Display Name"), Category="Utilities|Key")
    static FText Key_GetShortDisplayName(const FKey& Key);

	//Widget Creation Helpers

	UFUNCTION(BlueprintCallable, Category = "Settings Widget")
	static class URHSettingsWidget* CreateSettingsWidget(ARHHUDCommon* HUD, const TSubclassOf<class URHSettingsWidget>& SettingsWidgetClass);

	UFUNCTION(BlueprintCallable, Category = "Settings Widget")
	static class URHSettingsWidget* CreateSettingsWidgetWithConfig(ARHHUDCommon* HUD, FRHSettingsWidgetConfig SettingsWidgetConfig);

	UFUNCTION(BlueprintCallable, Category = "Settings Widget")
	static class URHSettingsPreview* CreateSettingsPreview(ARHHUDCommon* HUD, const TSubclassOf<class URHSettingsPreview>& SettingsPreviewClass);

	UFUNCTION(BlueprintCallable, Category = "Utilities|Player")
	static FReportPlayerParams SetupReportPlayerFromGameState(int64 PlayerId, const class ARHGameState* State);

	// TODO: Hook up reporting player from your scoreboard
	//UFUNCTION(BlueprintCallable, Category = "Utilities|Player")
	//static FReportPlayerParams SetupReportPlayerFromScoreboardStats(int64 PlayerId, const struct FScoreboardStats& State, ARHHUDCommon* InHud);

	UFUNCTION(BlueprintCallable, Category = "Utilities|Player")
	static bool UIX_ReportPlayer(const UObject* WorldContextObject, const FReportPlayerParams& Params);

	UFUNCTION(BlueprintCallable, Category = "Utilities")
	static bool CanReportServer(const UObject* WorldContextObject);

	UFUNCTION()
	static URHStoreItemHelper* GetStoreItemHelper(const UObject* WorldContextObject);

	// Returns the given key for a specified action
	UFUNCTION(BlueprintPure, Category = "Bindings")
	static FKey GetKeyForBinding(class APlayerController* PlayerController, FName Binding, bool SecondaryKey, bool FallbackToDefault, bool IsGamepadDoubleTap);

	UFUNCTION(BlueprintPure, Category = "Utilities")
	static const FText& GetTextByPlatform(const FText& DefaultText, const TMap<FString, FText>& PlatformTexts);

    UFUNCTION(BlueprintPure, Category = Game, meta = (WorldContext = "WorldContextObject"))
    static class APlayerController* GetLocalPlayerController(const class UObject* WorldContextObject, int32 PlayerIndex);

    UFUNCTION(BlueprintPure, Category = Game, meta = (WorldContext = "WorldContextObject"))
    static class ARHGameState* GetGameState(const class UObject* WorldContextObject);
	
    UFUNCTION(BlueprintPure, Category = "Input Function Library")
    static EGamepadIcons GetGamepadIconSet();

	UFUNCTION(BlueprintPure, Category = "Battlepass", meta = (WorldContext = "WorldContextObject"))
	static URHBattlepass* GetActiveBattlepass(const class UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Battlepass", meta = (WorldContext = "WorldContextObject"))
	static bool HasCinematicToPlay(class UDataTable* CinematicDataTable);

	UFUNCTION(BlueprintPure, Category = "Platform")
	static ERHAPI_Platform GetLoggedInPlatformId(ARHHUDCommon* pHUD);

	UFUNCTION(BlueprintPure, Category = "Platform")
	static ERHAPI_Platform GetPlatformIdByOSSName(FName OSSName);
	static ERHAPI_Platform GetPlatformIdByOSS(IOnlineSubsystem* OSS) { return URHUIBlueprintFunctionLibrary::GetPlatformIdByOSSName(OSS ? OSS->GetSubsystemName() : NULL_SUBSYSTEM); }

	UFUNCTION(BlueprintPure, Category = "Platform")
	static ERHPlatformDisplayType ConvertPlatformTypeToDisplayType(ARHHUDCommon* pHUD, ERHAPI_Platform PlatformType);

	UFUNCTION(BlueprintPure, Category = "Social", meta = (WorldContext = "WorldContextObject"))
	static ERHPlayerOnlineStatus GetPlayerOnlineStatus(const class UObject* WorldContextObject, URH_PlayerInfo* PlayerInfo, const URH_LocalPlayerSubsystem* LocalPlayerSS, bool bAllowPartyStatus = true, bool bAllowFriendRequestStatus = true);

	UFUNCTION(BlueprintPure, Category = "Social", meta = (WorldContext = "WorldContextObject"))
	static FText GetPlayerStatusMessage(const class UObject* WorldContextObject, URH_PlayerInfo* PlayerInfo, const URH_LocalPlayerSubsystem* LocalPlayerSS);

	UFUNCTION(BlueprintPure, Category = "Social", meta = (WorldContext = "WorldContextObject"))
	static ERHPlayerOnlineStatus GetFriendOnlineStatus(const class UObject* WorldContextObject, const URH_RHFriendAndPlatformFriend* Friend, const URH_LocalPlayerSubsystem* LocalPlayerSS, bool bAllowPartyStatus = true, bool bAllowFriendRequestStatus = true);

	UFUNCTION(BlueprintPure, Category = "Social", meta = (WorldContext = "WorldContextObject"))
	static FText GetFriendStatusMessage(const class UObject* WorldContextObject, const URH_RHFriendAndPlatformFriend* Friend, const URH_LocalPlayerSubsystem* LocalPlayerSS);

	UFUNCTION(BlueprintPure, Category = "Social", meta = (WorldContext = "WorldContextObject"))
	static FText GetStatusMessage(ERHPlayerOnlineStatus PlayerStatus);

	UFUNCTION(BlueprintPure, Category = "Items")
	static class URHCurrency* GetCurrencyItemByItemId(const FRH_ItemId& CurrencyItemId);

	UFUNCTION(BlueprintPure, Category = "Utility")
    static FName GetKeyName(FKey Key);

    //This function should always be called for menu-related functionality, otherwise Japanese PS4s and Switches won't have correct confirm buttons
    UFUNCTION(BlueprintPure, Category = "Key Binding")
    static FKey GetGamepadConfirmButton();

    //This function should always be called for menu-related functionality, otherwise Japanese PS4s and Switches won't have correct cancel buttons
    UFUNCTION(BlueprintPure, Category = "Key Binding")
    static FKey GetGamepadCancelButton();

	// Simple DPI scaling getter
	UFUNCTION(BlueprintPure, Category = "Resolution")
	static float GetUMG_DPI_Scaling();

	// returns true if running with Editor
	UFUNCTION(BlueprintPure, Category = "Editor")
	static bool IsWithEditor();

	UFUNCTION(BlueprintPure, Category = "AB Testing")
	static int32 GetPlayerCohortGroup(URH_PlayerInfo* PlayerInfo, int32 NumberOfGroups);
	
private:

    template <typename T>
    static bool GetItemByItemId(const FRH_ItemId& ItemId, T*& Item)
    {
        Item = nullptr;

        UPInv_AssetManager* pManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
        if (pManager != nullptr)
        {
            auto SoftItem = pManager->GetSoftPrimaryAssetByItemId<UPlatformInventoryItem>(ItemId);

            if (SoftItem.IsValid())
            {
                Item = Cast<T>(SoftItem.Get());
            }
        }

        return (Item != nullptr);
    }
};