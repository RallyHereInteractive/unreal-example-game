#include "RallyHereStart.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "Player/Controllers/RHPlayerController.h"
#include "DataFactories/RHQueueDataFactory.h"
#include "GameFramework/RHGameInstance.h"
#include "Lobby/Widgets/RHLobbyWidget.h"
#include "Online.h"
#include "DataFactories/RHSettingsDataFactory.h"
#include "Managers/RHStoreItemHelper.h"
#include "Managers/RHJsonDataFactory.h"
#include "Managers/RHLoadoutDataFactory.h"
#include "Managers/RHOrderManager.h"
#include "Managers/RHPopupManager.h"
#include "Managers/RHUISessionManager.h"

ARHLobbyHUD::ARHLobbyHUD(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, QueueDataFactory(nullptr)
{
    // set up a bunch of references
    PendingLoginState = ERHLoginState::ELS_Unknown;
    CurrentLoginState = ERHLoginState::ELS_Unknown;
}

void ARHLobbyHUD::OnQuit()
{
    Super::OnQuit();
}

void ARHLobbyHUD::Tick(float DeltaSeconds)
{
    if (PendingLoginState != CurrentLoginState)
    {
        HandleLoginStateChange(PendingLoginState);
    }

    Super::Tick(DeltaSeconds);
}

void ARHLobbyHUD::BeginPlay()
{
    // set rich presence
    if (URHGameInstance* GameInstance = Cast<URHGameInstance>(GetGameInstance()))
    {
        GameInstance->SetPresenceForLocalPlayers(NSLOCTEXT("RHGame", "PresenceInLobby", "In Lobby"), FVariantData(FString(TEXT("in_lobby"))));
    }

    Super::BeginPlay();

	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("Switch"))
	{
		// Disable background blur widgets on the lobby for Switch
		IConsoleVariable* AllowBackgroundBlurWidgetsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.AllowBackgroundBlurWidgets"));
		if (AllowBackgroundBlurWidgetsCVar)
		{
			AllowBackgroundBlurWidgetsCVar->Set(0);
		}

		// Disable bloom on the lobby for Switch
		IConsoleVariable* BloomQualityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BloomQuality"));
		if (BloomQualityCVar)
		{
			BloomQualityCVar->Set(0);
		}
	}

	if (UWorld* pWorld = GetWorld())
	{
		pWorld->GetTimerManager().SetTimer(MinuteTimerHandle, this, &ARHLobbyHUD::OnActiveBoostsTimer, 60.0f, true, 0.0f);
	}
}

void ARHLobbyHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // stop binding t the internal HUD Drawn event
    OnHUDDrawn().RemoveAll(this);

	// TODO: Hook up Stats Tracking system again for ELO and EOM/EMO data (including rewards)
/*
    // Clear out our old cached game stats when we transition away from the lobby
    if (CRHStatsMgr* const rhStats = CRHStatsMgr::Get())
    {
        rhStats->ClearCachedStats();
    }
*/

    Super::EndPlay(EndPlayReason);
}

void ARHLobbyHUD::InstanceDataFactories()
{
    UE_LOG(RallyHereStart, Log, TEXT(" ARHLobbyHUD::InstanceDataFactories()"));
    Super::InstanceDataFactories();

    QueueDataFactory = NewObject<URHQueueDataFactory>(this);
}

void ARHLobbyHUD::InitializeDataFactories()
{
    // handle things that hinge on Login States
    if (LoginDataFactory != nullptr)
    {
        LoginDataFactory->OnLoginStateChanged.AddDynamic(this, &ARHLobbyHUD::HandleLoginStateChange);
		LoginDataFactory->OnLoginUserChanged.AddDynamic(this, &ARHLobbyHUD::HandleLoginUserChange);
	}

    UE_LOG(RallyHereStart, Log, TEXT(" ARHLobbyHUD::InitializeDataFactories()"));
    Super::InitializeDataFactories();

    // kick off initialization of factories

    if (QueueDataFactory != nullptr)
    {
        QueueDataFactory->Initialize(this);
	}    

    // check current state of login
    if (LoginDataFactory != nullptr && LoginDataFactory->IsInitialized())
    {
        HandleLoginStateChange(LoginDataFactory->GetCurrentLoginState());
		HandleLoginUserChange();
    }
}

void ARHLobbyHUD::UninitializeDataFactories()
{
    if (LoginDataFactory != nullptr)
    {
        LoginDataFactory->OnLoginStateChanged.RemoveDynamic(this, &ARHLobbyHUD::HandleLoginStateChange);
		LoginDataFactory->OnLoginUserChanged.RemoveDynamic(this, &ARHLobbyHUD::HandleLoginUserChange);
	}

    Super::UninitializeDataFactories();

    UE_LOG(RallyHereStart, Log, TEXT("ARHLobbyHUD::UninitializeDataFactories()"));
    // kick off uninit of factories
    if (QueueDataFactory != nullptr)
    {
        QueueDataFactory->Uninitialize();
    }
}

void ARHLobbyHUD::HandleLoginStateChange(ERHLoginState LoginState)
{
    UE_LOG(RallyHereStart, Log, TEXT("ARHLobbyHUD::HandleLoginStateChange()"));
    if (LoginState == CurrentLoginState) return;

    if (CheckDataFactoryInitialization())
    {
        if (LoginState == ERHLoginState::ELS_LoggedIn)
        {
            UE_LOG(RallyHereStart, Log, TEXT("ARHLobbyHUD::HandleLoginStateChange() user is logged in."));

			if (QueueDataFactory)
			{
				// queue data factory match status binding
				QueueDataFactory->OnQueueStatusChange.AddDynamic(this, &ARHLobbyHUD::HandleMatchStatusUpdated);
				QueueDataFactory->PostLogin();
			}

            if (URHStoreItemHelper* StoreItemHelper = GetItemHelper())
            {
                StoreItemHelper->OnNotEnoughCurrency.AddDynamic(this, &ARHLobbyHUD::OnNotEnoughCurrency);
                StoreItemHelper->GetUpdatedStoreContents();
            }

            if (ViewManager)
            {
                ViewManager->PostLogin();
            }

            PartyManager->PostLogin();
            SettingsFactory->PostLogin();

			LoggedInPlayerInfo = GetLocalPlayerInfo();

			if (LoggedInPlayerInfo != nullptr)
			{
				if (URHOrderManager* OrderManager = GetOrderManager())
				{
					if (URHUISessionManager* SessionManager = GetUISessionManager())
					{
						OrderManager->SetOrderWatchForPlayer(LoggedInPlayerInfo, SessionManager->GetPlayerLoggedInTime(LoggedInPlayerInfo));
					}
				}
			}

            // set the party invite mode to all members
            PartyManager->SetPartyInviteMode(ERH_PartyInviteRightsMode::ERH_PIRM_AllMembers);
        }

        if (LoginState == ERHLoginState::ELS_LoggedOut && CurrentLoginState == ERHLoginState::ELS_LoggedIn)
        {
            UE_LOG(RallyHereStart, Log, TEXT("ARHLobbyHUD::HandleLoginStateChange() user is logged out."));

			if (QueueDataFactory)
			{
				QueueDataFactory->OnQueueStatusChange.RemoveDynamic(this, &ARHLobbyHUD::HandleMatchStatusUpdated);
				QueueDataFactory->PostLogoff();
			}

            if (URHStoreItemHelper* StoreItemHelper = GetItemHelper())
            {
                StoreItemHelper->OnNotEnoughCurrency.RemoveDynamic(this, &ARHLobbyHUD::OnNotEnoughCurrency);
            }

            if (ViewManager)
            {
                ViewManager->PostLogoff();
				ViewManager->ClearRoutes();
            }

            PartyManager->PostLogoff();
            SettingsFactory->PostLogoff();

			if (LoggedInPlayerInfo != nullptr)
			{
				if (URHOrderManager* OrderManager = GetOrderManager())
				{
					OrderManager->ClearOrderWatchForPlayer(LoggedInPlayerInfo);
				}

				LoggedInPlayerInfo = nullptr;
			}
        }

        CurrentLoginState = PendingLoginState = LoginState;
    }
    else
    {
        // set a pending login state change to be handled on tick until we the data factories are initialized
        PendingLoginState = LoginState;
    }
}

void ARHLobbyHUD::HandleLoginUserChange()
{
	// On Consoles, we don't save to locally generated inis, load them now so they don't need to wait for login
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX") || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("Switch"))
	{
		if (URHGameUserSettings* pRHGameUserSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
		{
			pRHGameUserSettings->LoadSaveGameConfig();
			pRHGameUserSettings->ApplySavedSettings();
		}
	}
}

void ARHLobbyHUD::OnNotEnoughCurrency(URHStorePurchaseRequest* PurchaseRequest)
{
    if (PurchaseRequest && PurchaseRequest->CurrencyType)
    {
        if (URHPopupManager* PopupManager = GetPopupManager())
        {
            FRHPopupConfig NotEnoughCurrencyPopup;
            NotEnoughCurrencyPopup.Header = NSLOCTEXT("General", "Error", "Error");
            NotEnoughCurrencyPopup.Description = FText::Format(NSLOCTEXT("RHPurchaseRequest", "NotEnoughCurrencyDesc", "You do not have enough {0} to complete this purchase"), PurchaseRequest->CurrencyType->GetItemName());
            NotEnoughCurrencyPopup.CancelAction.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

            FRHPopupButtonConfig& ConfirmButton = (NotEnoughCurrencyPopup.Buttons[NotEnoughCurrencyPopup.Buttons.Add(FRHPopupButtonConfig())]);
            ConfirmButton.Label = NSLOCTEXT("General", "Confirm", "Confirm");
            ConfirmButton.Type = ERHPopupButtonType::Confirm;
            ConfirmButton.Action.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

            PopupManager->AddPopup(NotEnoughCurrencyPopup);
        }
    }
}

bool ARHLobbyHUD::CheckDataFactoryInitialization()
{
    UE_LOG(RallyHereStart, VeryVerbose, TEXT(""));
    UE_LOG(RallyHereStart, VeryVerbose, TEXT("ARHLobbyHUD::CheckDataFactoryInitialization()"));
    UE_LOG(RallyHereStart, VeryVerbose, TEXT("LoginDataFactory->IsInitialized is %s"), (LoginDataFactory->IsInitialized() ? TEXT("True") : TEXT("False")));
    UE_LOG(RallyHereStart, VeryVerbose, TEXT("QueueDataFactory->IsInitialized is %s"), (QueueDataFactory->IsInitialized() ? TEXT("True") : TEXT("False")));
    return (
        LoginDataFactory->IsInitialized() &&
        QueueDataFactory->IsInitialized()
        );
}

// evaluating focus for the UI based on children of an assigned container
void ARHLobbyHUD::SetUIFocus_Implementation()
{
    UE_LOG(RallyHereStart, Verbose, TEXT("============================================================"));
    UE_LOG(RallyHereStart, Verbose, TEXT("ARHLobbyHUD::SetUIFocus_Implementation() invoked"));

    if (ViewManager)
    {
		URHWidget* TargetFocusWidget = nullptr;

        URHPopupManager* PopupManager = GetPopupManager();
        if (PopupManager != nullptr && PopupManager->IsFocusEnabled() && PopupManager->IsVisible())
        {
            TargetFocusWidget = PopupManager;
        }
        else
        {
			TargetFocusWidget = ViewManager->GetTopViewRouteWidget();
        }

        // if it's not null, then we've found our focusable widget
        if (TargetFocusWidget)
        {
            UE_LOG(RallyHereStart, Verbose, TEXT("Found TargetFocusWidget. Settings focus to : %s"), *TargetFocusWidget->GetName());
            UWidget* FocusedWidget = TargetFocusWidget->SetFocusToThis();

            // pPC -> set input mode game and ui (passing in found target)
            if (APlayerController* PlayerController = GetPlayerControllerOwner())
            {
                FInputModeGameAndUI InputMode;
                InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
                InputMode.SetWidgetToFocus(FocusedWidget ? FocusedWidget->TakeWidget() : TargetFocusWidget->TakeWidget());
                PlayerController->SetInputMode(InputMode);
            }

            // toggle blocker if this widget uses it or not (only valid on popups)
            if (TargetFocusWidget->GetUsesBlocker())
            {
                // if it does broadcast true
                // this includes displaying blocker, binding to the focused widget, and placing the blocker widget under it
                OnTriggerBlockerChange.Broadcast(true, TargetFocusWidget);
            }
            else
            {
                // otherwise, hide the blocker if it does not use it
                OnTriggerBlockerChange.Broadcast(false, nullptr);
            }
        }
    }

    UE_LOG(RallyHereStart, Verbose, TEXT("ARHLobbyHUD::SetUIFocus_Implementation() finished"));
    UE_LOG(RallyHereStart, Verbose, TEXT("============================================================"));
}

URHJsonDataFactory* ARHLobbyHUD::GetJsonDataFactory() const
{
    if (URHGameInstance* gameInstance = Cast<URHGameInstance>(GetGameInstance()))
    {
        return gameInstance->GetJsonDataFactory();
    }

    return nullptr;
}

/*
* Social Message Popups
*/
void ARHLobbyHUD::ShowPopupConfirmation(FText Message, ESocialMessageType MessageType)
{
	URHPopupManager* popup = GetPopupManager();

    if (popup == nullptr)
    {
        UE_LOG(RallyHereStart, Error, TEXT("Need blueprint implementation for GetPopupManager()!"));
        return;
    }

    switch (MessageType)
    {
        case ESocialMessageType::EInvite:
        {
            FRHPopupConfig invitePopupParams;
            invitePopupParams.Description = Message;
            invitePopupParams.IsImportant = true;
            invitePopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            FRHPopupButtonConfig& confirmInviteBtn = (invitePopupParams.Buttons[invitePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
            confirmInviteBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
            confirmInviteBtn.Type = ERHPopupButtonType::Confirm;
            confirmInviteBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            popup->AddPopup(invitePopupParams);
        }
            break;
        case ESocialMessageType::EInviteResponse:
        {
            FRHPopupConfig responsePopupParams;
            responsePopupParams.Description = FText::Format(NSLOCTEXT("RHFriendlist", "Invitation", "{0} has invited you to a party."), Message);
            responsePopupParams.IsImportant = true;
            responsePopupParams.CancelAction.AddDynamic(this, &ARHLobbyHUD::HandleDenyPartyInvitation);

            FRHPopupButtonConfig& confirmResponseBtn = (responsePopupParams.Buttons[responsePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
            confirmResponseBtn.Label = NSLOCTEXT("General", "Accept", "Accept");
            confirmResponseBtn.Type = ERHPopupButtonType::Confirm;
            confirmResponseBtn.Action.AddDynamic(this, &ARHLobbyHUD::HandleAcceptPartyInvitation);

            FRHPopupButtonConfig& cancelResponseBtn = (responsePopupParams.Buttons[responsePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
            cancelResponseBtn.Label = NSLOCTEXT("General", "Decline", "Decline");
            cancelResponseBtn.Type = ERHPopupButtonType::Cancel;
            cancelResponseBtn.Action.AddDynamic(this, &ARHLobbyHUD::HandleDenyPartyInvitation);

            popup->AddPopup(responsePopupParams);
        }
            break;
        case ESocialMessageType::EInviteError:
        {
            FRHPopupConfig errorPopupParams;
            errorPopupParams.Description = Message;
            errorPopupParams.IsImportant = true;
            errorPopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            FRHPopupButtonConfig& confirmErrorBtn = (errorPopupParams.Buttons[errorPopupParams.Buttons.Add(FRHPopupButtonConfig())]);
            confirmErrorBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
            confirmErrorBtn.Type = ERHPopupButtonType::Confirm;
            confirmErrorBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            popup->AddPopup(errorPopupParams);
        }
            break;
        case ESocialMessageType::EInviteExpired:
        {
            FRHPopupConfig expiredPopupParams;
            expiredPopupParams.Description = FText::Format(NSLOCTEXT("RHFriendlist", "InvitationExpired", "An invitation from {0} has expired."), Message);
            expiredPopupParams.IsImportant = true;
            expiredPopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            FRHPopupButtonConfig& confirmExpiredBtn = (expiredPopupParams.Buttons[expiredPopupParams.Buttons.Add(FRHPopupButtonConfig())]);
            confirmExpiredBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
            confirmExpiredBtn.Type = ERHPopupButtonType::Confirm;
            confirmExpiredBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            popup->AddPopup(expiredPopupParams);
        }
            break;
        case ESocialMessageType::EGenericMsg:
        {
            FRHPopupConfig genericPopupParams;
            genericPopupParams.Description = Message;
            genericPopupParams.IsImportant = true;
            genericPopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            FRHPopupButtonConfig& confirmGenericBtn = (genericPopupParams.Buttons[genericPopupParams.Buttons.Add(FRHPopupButtonConfig())]);
            confirmGenericBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
            confirmGenericBtn.Type = ERHPopupButtonType::Confirm;
            confirmGenericBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

            popup->AddPopup(genericPopupParams);
        }
            break;
    }
}


void ARHLobbyHUD::HandleAcceptPartyInvitation()
{
    if (PartyManager != nullptr)
    {
        PartyManager->UIX_AcceptPartyInvitation();
    }
}

void ARHLobbyHUD::HandleDenyPartyInvitation()
{
    if (PartyManager != nullptr)
    {
        PartyManager->UIX_DenyPartyInvitation();
    }
}

void ARHLobbyHUD::ForceEulaAccept()
{
    if (LoginDataFactory != nullptr)
    {
        LoginDataFactory->UIX_OnEulaAccept();
    }
}

void ARHLobbyHUD::GMMF()
{
	if (QueueDataFactory)
	{
		QueueDataFactory->CreateCustomMatchSession(true);	
	}
}


URHLobbyWidget* ARHLobbyHUD::GetLobbyWidget_Implementation()
{
	UE_LOG(RallyHereStart, Warning, TEXT("ARHLobbyHUD::GetLobbyWidget_Implementation called. Please override in BP and return the lobby widget instance."));
    return nullptr;
}

void ARHLobbyHUD::SetSafeFrameScale_Implementation(float SafeFrameScale)
{
	ApplySafeFrameScale(SafeFrameScale);
}

/*
* Player Muting
*/
bool ARHLobbyHUD::IsPlayerMuted(URH_PlayerInfo* PlayerData)
{
	// #RHTODO - Voice

    //if (PlayerData != nullptr && pcomGetVoice() != nullptr && pcomGetVoice()->IsInitialized())
    //{
    //    return pcomGetVoice()->IsPlayerMuted(CTgNetId(PlayerData->GetPlayerId(), TNIT_PLAYER));
    //}
    return false;
}