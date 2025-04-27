#pragma once
#include "Shared/HUD/RHHUDCommon.h"
#include "GameFramework/RHHUDInterface.h"
#include "Managers/RHStoreItemHelper.h"
#include "DataFactories/RHQueueDataFactory.h"
#include "DataFactories/RHLoginDataFactory.h"
#include "Managers/RHPartyManager.h"
#include "Managers/RHJsonDataFactory.h"
#include "Shared/Widgets/RHWidget.h"
#include "RHLobbyHUD.generated.h"

UENUM(BlueprintType)
enum class ESocialMessageType : uint8
{
    EInvite             UMETA(DisplayName="Invite"),
    EInviteResponse     UMETA(DisplayName="Invite Response"),
    EInviteExpired      UMETA(DisplayName="Invite Expired"),
    EInviteError        UMETA(DisplayName="Invite Error"),
    EGenericMsg         UMETA(DisplayName="Generic Message")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLobbyTriggerBlockerChange, bool, ShowBlocker, UUserWidget*, WidgetToBind);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLobbyWidgetReadyEvent);

// A common refresh pulse, 1 min, for utility if certain UI displays have to refresh on a timer but risk being undesirably out of sync by e.g. ~1 min.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHMinuteTimerUpdate);

UCLASS(Config=Game)
class RALLYHERESTART_API ARHLobbyHUD : public ARHHUDCommon, public IRHHUDInterface
{
    GENERATED_UCLASS_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Data Factories", meta = (DisplayName = "Get Queue Data Factory"))
    class URHQueueDataFactory* GetQueueDataFactory() const { return QueueDataFactory;  }

    UFUNCTION(BlueprintCallable, Category = "Data Factories", meta = (DisplayName = "Get Json Data Factory"))
    class URHJsonDataFactory* GetJsonDataFactory() const;

    // This needs to be defined here to avoid ambiguous access.
    virtual class URHLoginDataFactory* GetLoginDataFactory() const override { return Super::GetLoginDataFactory(); };

    // This needs to be defined here to avoid ambiguous access.
    virtual class URHPartyManager* GetPartyManager() const override { return Super::GetPartyManager(); };

    class URHStoreItemHelper* GetStoreItemHelper() const { return GetItemHelper(); };

	// System
public:
    UFUNCTION(BlueprintCallable)
    void ShowPopupConfirmation(FText Message, ESocialMessageType MessageType);

    UFUNCTION()
    void HandleAcceptPartyInvitation();
    UFUNCTION()
    void HandleDenyPartyInvitation();

    UFUNCTION(BlueprintCallable, Category = "Lobby HUD")
    void ForceEulaAccept();

	UFUNCTION(exec)
	void GMMF();

    virtual bool IsLobbyHUD() const override { return true; }

protected:
    void Tick(float DeltaSeconds);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void InstanceDataFactories() override;
    virtual void InitializeDataFactories() override;
    virtual void UninitializeDataFactories() override;
    virtual void OnQuit() override;

    UFUNCTION()
    void HandleLoginStateChange(ERHLoginState LoginState);

    UFUNCTION()
    void HandleLoginUserChange();
	
	UFUNCTION(BlueprintImplementableEvent)
	void HandleMatchStatusUpdated(ERH_MatchStatus MatchStatus);

	// Pass-through to get default Items assigned at the editor level
	UFUNCTION(BlueprintImplementableEvent)
	UPlatformInventoryItem* GetDefaultPlayerAccountItem(ERHLoadoutSlotTypes ItemSlot);

	// A 1 min timer to help certain widgets update in sync
	UPROPERTY(BlueprintAssignable, Category = "Platform UMG | Item Helper")
	FRHMinuteTimerUpdate OnMinuteTimerUpdate;

	FTimerHandle MinuteTimerHandle;
	
	// To sync widgets listening to this when one of them has been immediately refreshed
	UFUNCTION(BlueprintCallable)
	void ForceMinuteTimerUpdate() { OnMinuteTimerUpdate.Broadcast(); };

private:
    ERHLoginState CurrentLoginState;

	UPROPERTY(Transient)
	URH_PlayerInfo* LoggedInPlayerInfo;

    UPROPERTY()
    class URHQueueDataFactory* QueueDataFactory;
    
	void OnActiveBoostsTimer() { OnMinuteTimerUpdate.Broadcast(); };

/*
* Pending login change handling when data factories are not yet initialized
*/
protected:
    ERHLoginState PendingLoginState;
    bool CheckDataFactoryInitialization();

/*
* Evaluating focus override so that we can nativize this
*/
public:
    virtual void SetUIFocus_Implementation() override;

    UPROPERTY(BlueprintAssignable)
    FLobbyTriggerBlockerChange OnTriggerBlockerChange;

protected:
    // Called whenever a player attempts to purchase something without enough currency
    UFUNCTION()
    void OnNotEnoughCurrency(URHStorePurchaseRequest* PurchaseRequest);

public:

    /**
    * LobbyWidget Getter
    * Useful for exposing the lobby widget instance itself for external use
    * eg, to retrieve the ViewManager reference and bind to its route change delegate
    **/
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Lobby HUD")
    URHLobbyWidget* GetLobbyWidget();

    void SetSafeFrameScale_Implementation(float SafeFrameScale);

    /*
    * Muting Players
    */
public:
    UFUNCTION(BlueprintPure, Category = "Voice Chat")
    bool IsPlayerMuted(URH_PlayerInfo* PlayerData);

	virtual TScriptInterface<IRHSettingsCallbackInterface> GetSettingsCallbackInterface_Implementation() const override { return GetSettingsDataFactory(); }
};
