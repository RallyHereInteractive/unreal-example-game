#include "RallyHereStart.h"
#include "GameFramework/RHGameInstance.h"
#include "RH_GameInstanceSubsystem.h"
#include "Online.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Engine/LocalPlayer.h"
#include "DataFactories/RHLoginDataFactory.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "Framework/Application/SlateApplication.h"
#include "PlatformFeatures.h"
#include "RH_LocalPlayerLoginSubsystem.h"
#include "RallyHereIntegration/Public/RH_OnlineSubsystemNames.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#if RALLYSTART_USE_WEBAUTH_MODULE
#include "WebAuthModule.h"
#endif

namespace
{
    bool ForceInitialLoginAttempt()
    {
        IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
        if (!OSS)
        {
            return false;
        }

        auto OSSName = OSS->GetSubsystemName();
        if (OSSName == STEAM_SUBSYSTEM || OSSName == STEAMV2_SUBSYSTEM
			|| OSSName == EOS_SUBSYSTEM || OSSName == EOSPLUS_SUBSYSTEM
			)
        {
            return true;
        }

        return false;
    }
}

URHLoginDataFactory::URHLoginDataFactory(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    OnLoginCompleteDelegateHandle.Reset();
    LoginState = ERHLoginState::ELS_LoggedOut;
    bAllowLoginDuringPartialInstall = false;

    SavedCredentialPrefix = TEXT("HirezLogin_");
}

void URHLoginDataFactory::Initialize(ARHHUDCommon* InHud)
{
    UE_LOG(RallyHereStart, Log, TEXT("URHLoginDataFactory::Initialize()"));
    Super::Initialize(InHud);

    // bind to Controller events
    IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(this, &URHLoginDataFactory::HandleControllerConnectionChange);
	IPlatformInputDeviceMapper::Get().GetOnInputDevicePairingChange().AddUObject(this, &URHLoginDataFactory::HandleControllerPairingChange);

    if (MyHud == nullptr || MyHud->GetLocalPlayerSubsystem() == nullptr)
    {
        RecordLoginState(ERHLoginState::ELS_LoggedOut);
        return;
    }

    if (!IsLoggedIn())
    {
        RecordLoginState(ERHLoginState::ELS_LoggedOut);
    }
    else
    {
        RecordLoginState(ERHLoginState::ELS_LoggedIn);
    }

	if (MyHud != nullptr)
	{
		if (auto GameInstance = Cast<URHGameInstance>(MyHud->GetGameInstance()))
		{
			if (auto* pGISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
			{
				if (URH_ConfigSubsystem* pRH_ConfigSubsystem = pGISS->GetConfigSubsystem())
				{
					FString LimitLogins;
					CachedLoginsLimited = pRH_ConfigSubsystem->GetAppSetting("game.limit_logins", LimitLogins) && LimitLogins == "1";

					pRH_ConfigSubsystem->OnSettingsUpdated.AddWeakLambda(this, [this](const URH_ConfigSubsystem* pRH_ConfigSubsystem)
						{
							FString LimitLogins;
							bool NewLoginsLimited = pRH_ConfigSubsystem->GetAppSetting("game.limit_logins", LimitLogins) && LimitLogins == "1";
							
							if (CachedLoginsLimited != NewLoginsLimited)
							{
								CachedLoginsLimited = NewLoginsLimited;
								OnLoginsLimitedChanged.Broadcast();
							}
						});
				}
			}
		}

		if (URH_LocalPlayerLoginSubsystem* pLoginSubsystem = GetRH_LocalPlayerLoginSubsystem())
		{
			pLoginSubsystem->OnLoginComplete.AddUObject(this, &URHLoginDataFactory::OnRHLoginResponse);
		}
	}
}

void URHLoginDataFactory::Uninitialize()
{
    UE_LOG(RallyHereStart, Log, TEXT("URHLoginDataFactory::Uninitialize()"));

    Super::Uninitialize();

	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().RemoveAll(this);
	IPlatformInputDeviceMapper::Get().GetOnInputDevicePairingChange().RemoveAll(this);
    
    auto Identity = Online::GetIdentityInterface();
    if (MyHud != nullptr && Identity.IsValid())
    {
		auto PlatformUserId = MyHud->GetPlatformUserId();
        Identity->ClearOnLoginStatusChangedDelegates(PlatformUserId, this);
        if (OnLoginCompleteDelegateHandle.IsValid()) // just in case
        {
            Identity->ClearOnLoginCompleteDelegate_Handle(PlatformUserId, OnLoginCompleteDelegateHandle);
        }
    }
}

// Auto login attempt; instigated by the UI view layer where appropriate
void URHLoginDataFactory::UIX_TriggerAutoLogin()
{
    static bool DoOnce = true;
    if (DoOnce)
    {
        if (MyHud != nullptr)
        {
            // frame delayed here, to ensure engine is available and running
            MyHud->GetWorldTimerManager().SetTimerForNextTick(this, &URHLoginDataFactory::TriggerAutoLogin);
        }
        DoOnce = false;
    }
}

void URHLoginDataFactory::TriggerAutoLogin()
{
    check(GIsRunning);	// do not try to log in when engine is not initialized

    // set the current state
    if (!IsLoggedIn())
    {
        RecordLoginState(ERHLoginState::ELS_LoggedOut);
        if (ForceInitialLoginAttempt() && !AllowUserSwitching() && MyHud != nullptr)
        {
            UIX_OnSubmitAutoLogin(MyHud->GetPlatformUserId());
        }
    }
}

/*
* Current Login state interface
*/
void URHLoginDataFactory::RecordLoginState(ERHLoginState NewState)
{
    if (LoginState != NewState)
    {
		auto RH_LoginSubsystem = GetRH_LocalPlayerLoginSubsystem();
		auto RH_LPSubsystem = RH_LoginSubsystem != nullptr ? RH_LoginSubsystem->GetLocalPlayerSubsystem() : nullptr;

		if (NewState == ERHLoginState::ELS_LoggingIn)
		{
			if (RH_LPSubsystem != nullptr && RH_LPSubsystem->GetAnalyticsProvider().IsValid())
			{
				RH_LPSubsystem->GetAnalyticsProvider()->RecordEvent(TEXT("loginRequested"));
			}
		}

		if (NewState == ERHLoginState::ELS_LoggedIn)
		{
			if (RH_LoginSubsystem != nullptr)
			{
				RH_LoginSubsystem->GetAuthContext().Get()->OnLogout().AddUObject(this, &URHLoginDataFactory::HandlePlayerLoggedOut);
			}
		}

        LoginState = NewState;
        OnLoginStateChanged.Broadcast(LoginState);
    }
}

void URHLoginDataFactory::HandlePlayerLoggedOut()
{
	RecordLoginState(ERHLoginState::ELS_LoggedOut);
}

/*
* Login flow and state management
*/
void URHLoginDataFactory::LoginEvent_ShowAgreements(bool bNeedsEULA, bool bNeedsTOS, bool bNeedsPP)
{
    if (bNeedsEULA)
    {
        RecordLoginState(ERHLoginState::ELS_Eula);
    }
}

void URHLoginDataFactory::LoginEvent_LoggedIn()
{
    ResetAgreements();
    RecordLoginState(ERHLoginState::ELS_LoggedIn);
}

void URHLoginDataFactory::LoginEvent_Queued(uint32 queuePosition, uint32 queueSize, uint32 queueEstimatedWait)
{
    bool bIsQueued = queuePosition > 0;
    FLoginQueueInfo LoginQueueInfo;

    if (bIsQueued)
    {
        LoginQueueInfo.QueueMessage = NSLOCTEXT("RHLogin", "GenericQueueWait", "You are currently in line to log in.");
        LoginQueueInfo.QueuePosition = queuePosition;
        LoginQueueInfo.QueueSize = queueSize;
        LoginQueueInfo.EstimatedWaitTime = queueEstimatedWait;

        OnLoginWaitQueueMessage.Broadcast(LoginQueueInfo);
    }
}

void URHLoginDataFactory::LoginEvent_LoginRequested()
{
    RecordLoginState(ERHLoginState::ELS_LoggingIn);
}

void URHLoginDataFactory::LoginEvent_FailedClient(FText ErrorMsg)
{
    RecordLoginState(ERHLoginState::ELS_LoggedOut);

	/* #RHTODO - Voice
    if (FPComClient* pcomClient = pcomGetClient(0))
    {
        pcomClient->SetCommunicationPermissionStatus(EPCOM_PrivilegeStatus::Unchecked);
    }
	*/

    // send up the failure message
    OnLoginError.Broadcast(ErrorMsg);
}

URH_LocalPlayerLoginSubsystem* URHLoginDataFactory::GetRH_LocalPlayerLoginSubsystem() const
{
    if(!MyHud)
    {
        return nullptr;
    }

    APlayerController* PC = MyHud->GetOwningPlayerController();
    if(!PC)
    {
        UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get owning player controller"), ANSI_TO_TCHAR(__FUNCTION__));
        return nullptr;
    }

    ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
    if (!LocalPlayer)
    {
        UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get local player for controller"), ANSI_TO_TCHAR(__FUNCTION__));
        return nullptr;
    }

    URH_LocalPlayerSubsystem* RHSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>();
    if (!RHSS)
    {
        UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
        return nullptr;
    }

    return RHSS->GetLoginSubsystem();
}

void URHLoginDataFactory::SubmitAutoLogin(bool bAllowLogingDuringLimited)
{
    if (MyHud == nullptr || (LoginState != ERHLoginState::ELS_LoggedOut && LoginState != ERHLoginState::ELS_Eula) || (!bAllowLogingDuringLimited && AreLoginsLimited()))
    {
        return;
    }

    // clear any network errors
    if (auto GameInstance = Cast<URHGameInstance>(MyHud->GetGameInstance()))
    {
        GameInstance->ClearCurrentNetworkError();
    }

    auto RH_LoginSubsystem = GetRH_LocalPlayerLoginSubsystem();
    if (!RH_LoginSubsystem)
    {
		LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "LoginNotAvailable", "Not available for login"));
        return;
    }

	// #RHTODO - Voice - mctsClient->SetCommunicationPermissionStatus(EPCOM_PrivilegeStatus::Pending);
	StoredLoginAttempt.bLoginWithCredentials = false;
	StoredLoginAttempt.ControllerId = 0;
	StoredLoginAttempt.Credentials = FOnlineAccountCredentials();

    RecordLoginState(ERHLoginState::ELS_LoggingIn);
    RH_LoginSubsystem->SubmitAutoLogin(bHasAcceptedEULA,
        bHasAcceptedTOS,
		bHasAcceptedPP,
		FRH_OnLoginComplete::CreateUObject(this, &URHLoginDataFactory::OnRHLoginResponse));
}

void URHLoginDataFactory::SubmitLogin(int32 ControllerId, const FOnlineAccountCredentials& Credentials, bool bAllowLogingDuringLimited)
{
    if (MyHud == nullptr || (LoginState != ERHLoginState::ELS_LoggedOut && LoginState != ERHLoginState::ELS_Eula) || (!bAllowLogingDuringLimited && AreLoginsLimited()))
    {
        return;
    }

    // clear any network errors
    if (auto GameInstance = Cast<URHGameInstance>(MyHud->GetGameInstance()))
    {
        GameInstance->ClearCurrentNetworkError();
    }

    auto RH_LoginSubsystem = GetRH_LocalPlayerLoginSubsystem();
    if (!RH_LoginSubsystem)
    {
		LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "LoginNotAvailable", "Not available for login"));
        return;
    }

    // #RHTODO - Voice - mctsClient->SetCommunicationPermissionStatus(EPCOM_PrivilegeStatus::Pending);

	StoredLoginAttempt.bLoginWithCredentials = true;
	StoredLoginAttempt.ControllerId = ControllerId;
	StoredLoginAttempt.Credentials = Credentials;

    RecordLoginState(ERHLoginState::ELS_LoggingIn);

    RH_LoginSubsystem->SubmitLogin(Credentials,
        FString{},
        bHasAcceptedEULA,
        bHasAcceptedTOS,
        bHasAcceptedPP,
        FRH_OnLoginComplete::CreateUObject(this, &URHLoginDataFactory::OnRHLoginResponse));
}

void URHLoginDataFactory::OnRHLoginResponse(const FRH_LoginResult& LR)
{
    UE_LOG(RallyHereStart, Log, TEXT("[%s] Result=%s OSSType=%s OSSError=%s"), ANSI_TO_TCHAR(__FUNCTION__), *ToString(LR.Result), *ToString(LR.OSSType), *LR.OSSErrorMessage);
    switch (LR.Result)
    {
    case ERHAPI_LoginResult::Success:
		StoredLoginAttempt.bLoginWithCredentials = false;
		StoredLoginAttempt.ControllerId = 0;
		StoredLoginAttempt.Credentials = FOnlineAccountCredentials();
		LoginEvent_LoggedIn();
        break;
    case ERHAPI_LoginResult::Fail_PartialInstall:
        LoginEvent_FailedClient(FText(NSLOCTEXT("RHErrorMsg", "PartialInstall", "PartialInstall")));
        break;
    case ERHAPI_LoginResult::Fail_OSSLogin:
    case ERHAPI_LoginResult::Fail_OSSLoginUINotShown:
    case ERHAPI_LoginResult::Fail_OSSLoginUINoUserSelected:
    case ERHAPI_LoginResult::Fail_OSSUserNotFound:
	case ERHAPI_LoginResult::Fail_OSSAuthToken:
    {
        const auto OSS = GetRH_LocalPlayerLoginSubsystem()->GetOSS(LR.OSSType);
        const auto OSSName = OSS ? OSS->GetSubsystemName() : NAME_None;
        if (OSSName == APPLE_SUBSYSTEM)
        {
            LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "SignInWithAppleErrorMsg", "SignInWithAppleErrorMsg"));
        }
        else if (OSSName == IOS_SUBSYSTEM)
        {
            LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "GameCenterLoginErrorMsg", "GameCenterLoginErrorMsg"));
        }
        else if (OSSName == LIVE_SUBSYSTEM || OSSName == GDK_SUBSYSTEM)
        {
            LoginEvent_FailedClient(FText(NSLOCTEXT("RHErrorMsg", "XboxDisconnect", "XboxDisconnect")));
        }
        else if (OSSName == PS4_SUBSYSTEM || OSSName == PS5_SUBSYSTEM)
        {
            LoginEvent_FailedClient(FText(NSLOCTEXT("RHErrorMsg", "PSNDisconnect", "PSNDisconnect")));
        }
        else if (OSSName == SWITCH_SUBSYSTEM)
        {
            if (LR.Result == ERHAPI_LoginResult::Fail_OSSUserNotFound)
            {
                LoginEvent_FailedClient(FText(NSLOCTEXT("RHErrorMsg", "Disconnected", "Disconnected")));
            }
            else
            {
                LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "ConnectError", "ConnectError"));
            }
        }
        else
        {
            LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "GenericErrorMsg", "GenericErrorMsg"));
        }
        break;
    }
    case ERHAPI_LoginResult::Fail_OSSNeedsProfile:
        ShowProfileSelectionUI();
        break;
    case ERHAPI_LoginResult::Fail_OSSAgeRestriction:
        LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "AgeRestrictions", "AgeRestrictions"));
        break;
    case ERHAPI_LoginResult::Fail_OSSPrivilegeCheck:
		LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "OSSPrivilegeError", "OSSPrivilegeError"));
        break;
    case ERHAPI_LoginResult::Fail_OSSAccountTypeNotSufficient:
        LogOff(false); // The plugin prompts the user to upgrade their account, so we don't need to show an error
        break;
    case ERHAPI_LoginResult::Fail_OSSLogout:
        LogOff(true);
        break;
    case ERHAPI_LoginResult::Fail_MustAcceptAgreements:
        LoginEvent_ShowAgreements(LR.bMustAcceptEULA, LR.bMustAcceptTOS, LR.bMustAcceptPP);
        break;
    case ERHAPI_LoginResult::Fail_RHDenied:
		LoginEvent_FailedClient(FText::Format(NSLOCTEXT("RHErrorMsg", "AccessDenied", "Access Denied ({0})"), *LR.RallyHereErrorCode));
        break;
    case ERHAPI_LoginResult::Fail_RHUnknown:
		LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "UnknownError", "Unknown Error"));
        break;
	case ERHAPI_LoginResult::Fail_OSSNotSupported:
		LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "OSSNotSupported", "OSS Not Supported"));
		break;
	default:
		LoginEvent_FailedClient(NSLOCTEXT("RHErrorMsg", "ConnectError", "ConnectError"));
		break;
    }
}

// triggers the display of the profile selection UI, sets up a handler for close as well
void URHLoginDataFactory::ShowProfileSelectionUI()
{
    if (auto RH_LoginSubsystem = GetRH_LocalPlayerLoginSubsystem())
    {
        RH_LoginSubsystem->ShowLoginProfileSelectionUI(false, FRH_OnProfileSelectionUIClosed::CreateUObject(this, &URHLoginDataFactory::OnProfileSelected));
    }
}

void URHLoginDataFactory::OnProfileSelected(TSharedPtr<const FUniqueNetId> UniqueId, const FOnlineError& Error)
{
    const auto LoginOSS = GetRH_LocalPlayerLoginSubsystem()->GetLoginOSS();
    const auto OSSName = LoginOSS ? LoginOSS->GetSubsystemName() : NAME_None;
    if (OSSName != LIVE_SUBSYSTEM && OSSName != GDK_SUBSYSTEM)
    {
        LogOff(UniqueId != nullptr && UniqueId->IsValid());
    }
}

void URHLoginDataFactory::UIX_OnSignInWithGoogle(const FPlatformUserId& PlatformId)
{
    SubmitAutoLogin(true);
}

void URHLoginDataFactory::UIX_OnSignInWithApple(const FPlatformUserId& PlatformId)
{
    SubmitAutoLogin(true);
}

// triggers login process for standard pc as prompted by the user
void URHLoginDataFactory::UIX_OnSubmitLogin(FString Username, FString Password)
{
    SubmitLogin(0, FOnlineAccountCredentials(TEXT("password"), Username, Password), true);
}

// triggers login process for console as prompted by the user
void URHLoginDataFactory::UIX_OnSubmitAutoLogin(const FPlatformUserId& PlatformId)
{
    SubmitAutoLogin(true);
}

// user prompted acceptance of the EULA
void URHLoginDataFactory::UIX_OnEulaAccept()
{
    bHasAcceptedEULA = true;
	if (StoredLoginAttempt.bLoginWithCredentials)
	{
		SubmitLogin(StoredLoginAttempt.ControllerId, StoredLoginAttempt.Credentials, true);
	}
	else
	{
		SubmitAutoLogin(true);
	}
}

// user prompted rejection of the EULA
void URHLoginDataFactory::UIX_OnEulaDecline()
{
    LogOff(false);
}

// bring up the external UI to swap user
void URHLoginDataFactory::UIX_OnChangeUserAccount()
{
    if (!MyHud || LoginState != ERHLoginState::ELS_LoggedOut || !AllowUserSwitching())
    {
        return;
    }

    ShowProfileSelectionUI();
}

void URHLoginDataFactory::UIX_OnLinkDecline()
{
    LogOff(true);
}

void URHLoginDataFactory::UIX_OnLinkExistingAccount(FString Username, FString Password)
{
    LogOff(true);
}

void URHLoginDataFactory::UIX_OnCancelLogin()
{
    LogOff(false);
}

void URHLoginDataFactory::ResetAgreements()
{
    bHasAcceptedEULA = false;
    bHasAcceptedTOS = false;
    bHasAcceptedPP = false;
}

// Common procedure for exiting and re-entering the login process, often after setting new information
void URHLoginDataFactory::LogOff(bool bRetry)
{
    if (MyHud != nullptr)
    {
		if (MyHud->GetLocalPlayerSubsystem() != nullptr)
		{
			MyHud->GetLocalPlayerSubsystem()->GetLoginSubsystem()->Logout();
		}
        RecordLoginState(ERHLoginState::ELS_LoggedOut);

        if (bRetry)
        {
            UIX_OnSubmitAutoLogin(MyHud->GetPlatformUserId());
        }
    }
}

// Handle login status changes. Sometimes rebinds don't happen when accounts change if they end up in the same slot
void URHLoginDataFactory::OnlineIdentity_HandleLoginStatusChanged(const FPlatformUserId& PlatformUserId, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const FUniqueNetId& NewId)
{
	if (MyHud != nullptr && MyHud->GetPlatformUserId() == PlatformUserId)
    {
        // The local platform profile logged out
        if (OldStatus != NewStatus)
        {
            if (NewStatus == ELoginStatus::NotLoggedIn)
            {
				MyHud->GetLocalPlayerSubsystem()->GetLoginSubsystem()->Logout();
            }
        }
    }
}

/*
* EULA
*/
bool URHLoginDataFactory::LoadEULAFile(FString& SaveText)
{
    return false;
}

bool URHLoginDataFactory::GetCurrentPlayerId(FText& Id) const
{
    Id = FText::FromString(TEXT(""));
    if (MyHud != nullptr)
    {
		auto LocalUserId = MyHud->GetOSSUniqueId();
        if (LocalUserId.IsValid())
        {
            Id = FText::FromString(LocalUserId->ToString());
            return true;
        }
    }

    return false;
}

bool URHLoginDataFactory::GetCurrentPlayerName(FText& NameText)
{
    NameText = FText::FromString(TEXT(""));

    if (MyHud != nullptr)
    {
		auto LocalUserId = MyHud->GetOSSUniqueId();
		auto* LPSS = MyHud->GetLocalPlayerSubsystem();
		if (LPSS != nullptr && LocalUserId.IsValid())
		{
			auto NicknameOSS = LPSS->GetLoginSubsystem()->GetNicknameOSS();
			if (NicknameOSS != nullptr)
			{
				auto NicknameIdentity = NicknameOSS->GetIdentityInterface();
				if (NicknameIdentity.IsValid())
				{
					FString PlayerNickname = NicknameIdentity->GetPlayerNickname(*LocalUserId.GetUniqueNetId());
					if (!PlayerNickname.IsEmpty())
					{
						NameText = FText::FromString(*PlayerNickname);
					}
				}
			}
		}
    }

    return (!NameText.IsEmpty());
}

void URHLoginDataFactory::HandleControllerConnectionChange(EInputDeviceConnectionState NewConnectionState, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId)
{
    UE_LOG(RallyHereStart, Log, TEXT("URHLoginDataFactory::HandleControllerChange(IsConnection: %s, UserId: %d, ControllerId: %d)"), (NewConnectionState == EInputDeviceConnectionState::Connected ? TEXT("TRUE") : TEXT("FALSE")), PlatformUserId.GetInternalId(), InputDeviceId.GetId());

    if (MyHud != nullptr && MyHud->GetPlatformUserId() == PlatformUserId)
    {
		const bool IsConnection = NewConnectionState == EInputDeviceConnectionState::Connected;

        if (NewConnectionState != EInputDeviceConnectionState::Connected)
        {
            // If failed to find another controller, broadcast a controller disconnect event for a popup to be displayed instead of setting another controller as active
            OnControllerDisconnected.Broadcast();
        }
    }
}

// Update active controller and user data whenever the user connected to a controller has changed
void URHLoginDataFactory::HandleControllerPairingChange(FInputDeviceId InputDeviceId, FPlatformUserId NewUserPlatformId, FPlatformUserId OldUserPlatformId)
{
    UE_LOG(RallyHereStart, Log, TEXT("URHLoginDataFactory::HandleControllerPairingChange(ControllerIndex: %i, NewUserId: %d, OldUserId: %d)"), InputDeviceId.GetId(), NewUserPlatformId.GetInternalId(), OldUserPlatformId.GetInternalId());
}

bool URHLoginDataFactory::GetLastDisconnectReason(FText& ErrorMsg)
{
    return false;
}

bool URHLoginDataFactory::ShouldDisplayDisconnectError() const
{
    bool ShouldDisplayError = false;

    if (MyHud != nullptr)
    {
        if (const URHGameInstance* GameInstance = Cast<URHGameInstance>(MyHud->GetGameInstance()))
        {
            ERHNetworkError::Type LastNetworkError = GameInstance->RetrieveCurrentNetworkError();
            if (LastNetworkError == ERHNetworkError::DisconnectedXboxLive
                || LastNetworkError == ERHNetworkError::DisconnectedPlaystationNetwork
                || LastNetworkError == ERHNetworkError::DisconnectedUnknown)
            {
                ShouldDisplayError = true;
            }
        }
    }

    return ShouldDisplayError;
}

bool URHLoginDataFactory::AllowUserSwitching() const
{
	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	if (!OSS)
	{
		return false;
	}

	auto OSSName = OSS->GetSubsystemName();
	if (OSSName == LIVE_SUBSYSTEM || OSSName == GDK_SUBSYSTEM)
	{
		// Xbox requires user switching as a cert requirement
		return true;
	}

	return false;
}