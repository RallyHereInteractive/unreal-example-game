#pragma once
#include "DataFactories/RHDataFactory.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "RallyHereIntegrationModule.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_LocalPlayerLoginSubsystem.h"
#include "RHLoginDataFactory.generated.h"

UENUM(BlueprintType)
enum class ERHLoginState : uint8
{
	ELS_LoggedOut           UMETA(DisplayName = "Logged Out"),
	ELS_Eula                UMETA(DisplayName = "Needs Eula"),
	ELS_CreateName          UMETA(DisplayName = "Create Name"),
	ELS_LoggingIn           UMETA(DisplayName = "Logging In"),
	ELS_LoggedIn            UMETA(DisplayName = "Logged In"),
	ELS_TwoFactor           UMETA(DisplayName = "Awaiting Two Factor Authentication"),
	ELS_LinkOffer           UMETA(DisplayName = "Navigating Link Offer"),
	ELS_Unknown             UMETA(DisplayName = "Unknown"),
};

UENUM(BlueprintType)
enum class ERHCannotLoginNowReason : uint8
{
	None,
	PartialInstall,
};

USTRUCT(BlueprintType)
struct FErrorMessage : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Error")
	int32 ErrorMsgId;
	UPROPERTY(EditAnywhere, Category="Error")
	FText ErrorMsg;

	FErrorMessage()
		: ErrorMsgId(0)
	{
	}
	~FErrorMessage() {}
};

struct FLastLoginAttempt
{
	bool bLoginWithCredentials;
	int32 ControllerId;
	FOnlineAccountCredentials Credentials;

	FLastLoginAttempt()
		: bLoginWithCredentials(false),
		  ControllerId(0)
	{
	}
};

USTRUCT(BlueprintType)
struct FLoginQueueInfo
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Login")
	FText QueueMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Login")
	int32 QueuePosition;

	UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Login")
	int32 QueueSize;

	UPROPERTY(BlueprintReadOnly, Category = "Platform UMG | Login")
	int32 EstimatedWaitTime;

	FLoginQueueInfo()
		: QueuePosition(-1)
		, QueueSize(-1)
		, EstimatedWaitTime(-1)
	{
	}

	bool operator ==(const FLoginQueueInfo& OtherLoginQueueInfo) const
	{
		return (OtherLoginQueueInfo.QueuePosition == QueuePosition
		&& OtherLoginQueueInfo.QueueSize == QueueSize
		&& OtherLoginQueueInfo.EstimatedWaitTime == EstimatedWaitTime);
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHLoginUserChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHLoginsLimitedChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHLoginStateChanged, ERHLoginState, LoginState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHLoginErrorMessage, FText, MessageText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHLoginQueueMessage, FLoginQueueInfo, LoginQueueInfo);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHControllerDisconnected);

UCLASS(Config=Game)
class RALLYHERESTART_API URHLoginDataFactory : public URHDataFactory
{
	GENERATED_UCLASS_BODY()

public:
	virtual void Initialize(ARHHUDCommon* InHud) override;
	virtual void Uninitialize() override;

	UFUNCTION(BlueprintCallable, Category="Login")
	virtual void UIX_TriggerAutoLogin();

	UFUNCTION()
	virtual void TriggerAutoLogin();

	/*
	* Current Login state interface
	*/
public:
	UFUNCTION(BlueprintCallable, Category = "Login")
	ERHLoginState GetCurrentLoginState() { return LoginState; }

	UPROPERTY(BlueprintAssignable, Category = "Login")
	FRHLoginUserChanged OnLoginUserChanged;

	UPROPERTY(BlueprintAssignable, Category = "Login")
	FRHLoginStateChanged OnLoginStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Login")
	FRHLoginErrorMessage OnLoginError;
private:

	UFUNCTION()
	void RecordLoginState(ERHLoginState NewState);

	//never set this directly!  This gets set off OnLoginStateChanged broadcasts
	ERHLoginState LoginState;

	/*
	* Login flow and state management
	*/
public:

	UPROPERTY(BlueprintAssignable, Category = "Controller")
	FRHControllerDisconnected OnControllerDisconnected;

	UFUNCTION(BlueprintPure, Category = "Login")
	bool ShouldDisplayDisconnectError() const;

    UFUNCTION()
    class URH_LocalPlayerLoginSubsystem* GetRH_LocalPlayerLoginSubsystem() const;

private:
	UFUNCTION()
	void LoginEvent_ShowAgreements(bool bNeedsEULA, bool bNeedsTOS, bool bNeedsPP);

	UFUNCTION()
	void LoginEvent_LoggedIn();

	UFUNCTION()
	void LoginEvent_Queued(uint32 queuePosition, uint32 queueSize, uint32 queueEstimatedWait);

	UFUNCTION()
	void LoginEvent_LoginRequested();

	UFUNCTION()
	void LoginEvent_FailedClient(FText errorMsg);

	UFUNCTION()
	void HandleControllerConnectionChange(EInputDeviceConnectionState NewConnectionState, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId);

	UFUNCTION()
	void HandleControllerPairingChange(FInputDeviceId InputDeviceId, FPlatformUserId NewUserPlatformId, FPlatformUserId OldUserPlatformId);

	UFUNCTION()
	void HandlePlayerLoggedOut(bool bAuthTokenExpired);

	UFUNCTION(BlueprintPure, Category = "Login")
	bool AllowUserSwitching() const;

	/*
	* User interaction
	*/
public:
	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnSubmitLogin(FString Username, FString Password);

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnSubmitAutoLogin(const FPlatformUserId& PlatformId);

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnSignInWithApple(const FPlatformUserId& PlatformId);

    UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
    void UIX_OnSignInWithGoogle(const FPlatformUserId& PlatformId);

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnEulaAccept();

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnEulaDecline();

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnChangeUserAccount();

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnLinkDecline();

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnLinkExistingAccount(FString Username, FString Password);

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	void UIX_OnCancelLogin();

	UFUNCTION(BlueprintCallable, Category = "Login | User Interaction")
	bool GetLastDisconnectReason(FText& ErrorMsg);

	/*
	* Generic login handling
	*/
	void LogOff(bool bRetry);

protected:
	UPROPERTY(GlobalConfig)
	bool bAllowLoginDuringPartialInstall;

	UPROPERTY(BlueprintReadOnly, Category="Error")
	UDataTable* ErrorMsgsDT;

	void OnRHLoginResponse(const FRH_LoginResult& LR);

	UFUNCTION(BlueprintPure)
	bool AreLoginsLimited() const { return CachedLoginsLimited; }

	UPROPERTY(BlueprintAssignable)
	FRHLoginsLimitedChanged OnLoginsLimitedChanged;

private:
	void SubmitAutoLogin(bool bAllowLogingDuringLimited = false);
	void SubmitLogin(int32 ControllerId, const FOnlineAccountCredentials& Credentials, bool bAllowLogingDuringLimited = false);
	void ShowProfileSelectionUI();
    void OnProfileSelected(TSharedPtr<const FUniqueNetId> UniqueId, const FOnlineError& Error);

	void OnlineIdentity_HandleLoginStatusChanged(const FPlatformUserId& PlatformId, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const FUniqueNetId& NewId);

	FDelegateHandle OnLoginCompleteDelegateHandle;

	TMap<int32, FString> ErrorMsgMap;

	bool CachedLoginsLimited;

	/*
	* EULA
	*/
public:
	UFUNCTION(BlueprintCallable, Category = "Login")
	static bool LoadEULAFile(FString& SaveText);

private:
	bool bHasAcceptedEULA;
	bool bHasAcceptedTOS;
	bool bHasAcceptedPP;

	UPROPERTY(Config)
	FString SavedCredentialPrefix;

	FLastLoginAttempt StoredLoginAttempt;

	void ResetAgreements();

	/*
	* Misc
	*/
public:
	UFUNCTION(BlueprintCallable, Category = "Player Display Name")
	bool GetCurrentPlayerName(FText& NameText);

	UFUNCTION(BlueprintPure, Category = "Player Display Platform Id")
	bool GetCurrentPlayerId(FText& Id) const;

	/*
	* Waiting Queue
	*/
public:
	UPROPERTY(BlueprintAssignable, Category = "Login Queue")
	FRHLoginQueueMessage OnLoginWaitQueueMessage;
};
