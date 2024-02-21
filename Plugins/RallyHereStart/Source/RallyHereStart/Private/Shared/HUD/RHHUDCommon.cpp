#include "RallyHereStart.h"
#include "DataFactories/RHLoginDataFactory.h"
#include "DataFactories/RHSettingsDataFactory.h"
#include "Managers/RHInputManager.h"
#include "Managers/RHPartyManager.h"
#include "Managers/RHPopupManager.h"
#include "Managers/RHStoreItemHelper.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "HAL/PlatformApplicationMisc.h"
#include "GameFramework/RHGameInstance.h"
#include "GameFramework/RHGameUserSettings.h"
#include "Player/Controllers/RHPlayerState.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_MatchmakingBrowser.h"

ARHHUDCommon::ARHHUDCommon(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , LoginDataFactory(nullptr)
    , SettingsFactory(nullptr)
    , PartyManager(nullptr)
	, HasHUDBeenDrawn(false)
{
    InputManagerClass = URHInputManager::StaticClass();

	ColorPaletteDT = nullptr;
	FontPaletteDT = nullptr;
}

void ARHHUDCommon::BeginPlay()
{
	InitializeSoundTheme();
	InstanceDataFactories();
	InitializeDataFactories();

	PlayerInput = Cast<URHPlayerInput>(GetOwningPlayerController()->PlayerInput);
	if (PlayerInput.IsValid())
	{
		PlayerInput->OnInputStateChanged().AddUObject(this, &ARHHUDCommon::InputStateChangePassthrough);

		//in case something's already managed to bind, be sure to alert them
		InputStateChangePassthrough(PlayerInput->GetInputState());
	}

	CreateInputManager();

	Super::BeginPlay();

	float OutFloat;
	if (SettingsFactory != nullptr && SettingsFactory->GetSettingAsFloat(URHGameUserSettings::SafeFrameScale, OutFloat))
	{
		ApplySafeFrameScale(OutFloat);
	}
	else
	{
		ApplySafeFrameScale(1.f);
	}

	if (UGameInstance* pGameInstance = GetGameInstance())
	{
		if (URH_GameInstanceSubsystem* pGISS = pGameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
		{
			if (URH_MatchmakingBrowserCache* pMMCache = pGISS->GetMatchmakingCache())
			{
				pMMCache->OnRegionsUpdated.AddDynamic(this, &ARHHUDCommon::OnRegionsUpdated);
			}
		}
	}
}

void ARHHUDCommon::CreateInputManager()
{
	//override if you need to use a game-specific PlayerInput class, or set InputManagerClass
	InputManager = NewObject<URHInputManager>(this, InputManagerClass);
	InputManager->Initialize(this);
}

void ARHHUDCommon::SetNavigationEnabled(bool Enabled)
{
	if (InputManager != nullptr)
	{
		InputManager->SetNavigationEnabled(Enabled);
	}
}

void ARHHUDCommon::InstanceDataFactories()
{
    LoginDataFactory = NewObject<URHLoginDataFactory>(this);
    SettingsFactory = NewObject<URHSettingsDataFactory>(this);
    PartyManager = NewObject<URHPartyManager>(this);
}

void ARHHUDCommon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if WITH_EDITOR
    if (UWorld* pWorld = GetWorld())
    {
        if (pWorld->WorldType == EWorldType::PIE)
        {
            if (URHLoginDataFactory* pLoginDataFactory = GetLoginDataFactory())
            {
                pLoginDataFactory->LogOff(false);
            }
        }
    }
#endif

	UninitializeDataFactories();

	if (PlayerInput.IsValid())
	{
		PlayerInput->OnInputStateChanged().RemoveAll(this);
	}

	if (ViewManager)
	{
		ViewManager->Uninitialize();
	}

	if (UGameInstance* pGameInstance = GetGameInstance())
	{
		if (URH_GameInstanceSubsystem* pGISS = pGameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
		{
			if (URH_MatchmakingBrowserCache* pMMCache = pGISS->GetMatchmakingCache())
			{
				pMMCache->OnRegionsUpdated.RemoveAll(this);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ARHHUDCommon::InitializeDataFactories()
{
    if (LoginDataFactory != nullptr)
    {
        LoginDataFactory->Initialize(this);
    }
	if (SettingsFactory != nullptr)
    {
        SettingsFactory->Initialize(this);
    }
    if (PartyManager != nullptr)
    {
		PartyManager->Initialize(this);
    }
    
    if (URHOrderManager* acqMgr = GetOrderManager())
    {
        if (IsLobbyHUD())
        {
            acqMgr->OnOrderFailed.AddDynamic(this, &ARHHUDCommon::ShowErrorPopup);
        }
        else
        {
            acqMgr->OnOrderFailed.AddDynamic(this, &ARHHUDCommon::LogErrorMessage);
        }

		acqMgr->OnInvalidVoucherObtained.AddDynamic(this, &ARHHUDCommon::OnInvalidVoucherOrder);
    }

	// handle things that hinge on Login States
    if (LoginDataFactory != nullptr)
    {
        LoginDataFactory->OnControllerDisconnected.AddDynamic(this, &ARHHUDCommon::HandleControllerDisconnect);
    }
}

void ARHHUDCommon::InitializeSoundTheme()
{
	UE_LOG(RallyHereStart, Verbose, TEXT("Override InitializeSoundTheme() with your game's sound theme."));
}

void ARHHUDCommon::UninitializeDataFactories()
{
    if (LoginDataFactory != nullptr)
    {
        LoginDataFactory->Uninitialize();
    }
	if (SettingsFactory != nullptr)
    {
        SettingsFactory->Uninitialize();
    }
    if (PartyManager != nullptr)
    {
		PartyManager->Uninitialize();
    }
    
    if (URHOrderManager* acqMgr = GetOrderManager())
    {
        acqMgr->OnOrderFailed.RemoveAll(this);
		acqMgr->OnInvalidVoucherObtained.RemoveDynamic(this, &ARHHUDCommon::OnInvalidVoucherOrder);
    }

    if (LoginDataFactory != nullptr)
    {
        LoginDataFactory->OnControllerDisconnected.RemoveDynamic(this, &ARHHUDCommon::HandleControllerDisconnect);
    }
}

void ARHHUDCommon::SetUIFocus_Implementation()
{
	UE_LOG(RallyHereStart, Verbose, TEXT("Override in your blueprint HUD class for focus resolution."));
}

bool ARHHUDCommon::IsSamePlatformAsLocalPlayer(const FGuid& PlayerId) const
{
	// #RHTODO
    return false;
}

bool ARHHUDCommon::ShouldShowCrossplayIconForPlayer(const FGuid& PlayerId) const
{
	// #RHTODO - Crossplay
    return false;
}

bool ARHHUDCommon::ShouldShowCrossplayIconForPlayerState(ARHPlayerState* PlayerState) const
{
	if (PlayerState != nullptr)
	{
		return ShouldShowCrossplayIconForPlayer(PlayerState->GetRHPlayerUuid());
	}
	return false;
}

void ARHHUDCommon::OnRegionsUpdated(URH_MatchmakingBrowserCache* MatchingBrowserCache)
{
	OnPreferredRegionUpdated.Broadcast();
}

bool ARHHUDCommon::GetPreferredRegionId(FString& OutRegionId) const
{
	if (class URHSettingsDataFactory* const pRHSettingsDataFactory = GetSettingsDataFactory())
	{
		OutRegionId = pRHSettingsDataFactory->GetSelectedRegion();
		return !OutRegionId.IsEmpty();
	}
	return false;
}

void ARHHUDCommon::SetPreferredRegionId(const FString& RegionId)
{
	if (class URHSettingsDataFactory* const pRHSettingsDataFactory = GetSettingsDataFactory())
	{
		pRHSettingsDataFactory->SetSelectedRegion(RegionId);
		pRHSettingsDataFactory->SaveSettings();
		OnPreferredRegionUpdated.Broadcast();
	}
}

void ARHHUDCommon::GetRegionList(TMap<FString, FText>& OutRegionIdToNameMap) const
{
	// Fixed mapping of known regions here - uses translated region names instead of non translated off the region data from API
	TMap<FString, FText> ReferenceRegionsAndNames;
	ReferenceRegionsAndNames.Emplace(TEXT("1"), NSLOCTEXT("RHRegionSelect", "NorthAmerica", "N. America - East"));
	ReferenceRegionsAndNames.Emplace(TEXT("9"), NSLOCTEXT("RHRegionSelect", "NorthAmericaWest", "N. America - West"));
	ReferenceRegionsAndNames.Emplace(TEXT("2"), NSLOCTEXT("RHRegionSelect", "Europe", "Europe"));
	ReferenceRegionsAndNames.Emplace(TEXT("8"), NSLOCTEXT("RHRegionSelect", "Russia", "Russia"));
	ReferenceRegionsAndNames.Emplace(TEXT("4"), NSLOCTEXT("RHRegionSelect", "Brazil", "Brazil"));
	ReferenceRegionsAndNames.Emplace(TEXT("5"), NSLOCTEXT("RHRegionSelect", "LatinAmericaNorth", "Latin Amer North"));
	ReferenceRegionsAndNames.Emplace(TEXT("6"), NSLOCTEXT("RHRegionSelect", "LatinAmericaSouth", "Latin Amer South"));
	ReferenceRegionsAndNames.Emplace(TEXT("10"), NSLOCTEXT("RHRegionSelect", "Japan", "Asia"));
	ReferenceRegionsAndNames.Emplace(TEXT("7"), NSLOCTEXT("RHRegionSelect", "SoutheastAsia", "SE Asia"));
	ReferenceRegionsAndNames.Emplace(TEXT("3"), NSLOCTEXT("RHRegionSelect", "Oceania", "Oceania"));

	if (UGameInstance* pGameInstance = GetGameInstance())
	{
		if (URH_GameInstanceSubsystem* pGISS = pGameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
		{
			if (URH_MatchmakingBrowserCache* pMMCache = pGISS->GetMatchmakingCache())
			{
				auto Regions = pMMCache->GetAllRegions();

				TArray<FRHAPI_Region> RegionList;
				RegionList.Append(Regions);

				RegionList.Sort([](const FRHAPI_Region& A, const FRHAPI_Region& B) -> bool
				{
					return A.GetSortOrder() < B.GetSortOrder();
				});

				if (RegionList.Num() > 0)
				{
					// prepare for return
					OutRegionIdToNameMap.Empty();
					OutRegionIdToNameMap.Reserve(RegionList.Num());

					for (const auto& Region : RegionList)
					{
						const FString RegionId = Region.GetRegionId();
						FString RegionName;
						if (ReferenceRegionsAndNames.Contains(RegionId))
						{
							OutRegionIdToNameMap.Add(RegionId, ReferenceRegionsAndNames[RegionId]);
						}
						else
						{
							OutRegionIdToNameMap.Add(RegionId, FText::FromString(RegionId));
						}
					}
				}
			}
		}
	}
}

void ARHHUDCommon::OnQuit()
{
	URHPopupManager* popup = GetPopupManager();

    if (popup == nullptr)
    {
        UE_LOG(RallyHereStart, Error, TEXT("Need blueprint implementation for GetPopupManager()!"));
        return;
    }

    FRHPopupConfig popupParams;
    popupParams.Header = NSLOCTEXT("RHMenu", "QuitHeader", "Quit Game");
    popupParams.Description = NSLOCTEXT("RHMenu", "QuitDesc", "Are you sure you want to quit the game?");

    popupParams.IsImportant = true;
    popupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

    FRHPopupButtonConfig& confirmBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
    confirmBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
    confirmBtn.Type = ERHPopupButtonType::Confirm;
    confirmBtn.Action.AddDynamic(this, &ARHHUDCommon::OnConfirmQuit);

    FRHPopupButtonConfig& cancelBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
    cancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
    cancelBtn.Type = ERHPopupButtonType::Cancel;
    cancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

    popup->AddPopup(popupParams);
}

void ARHHUDCommon::OnConfirmQuit()
{
	if (GetOwningPlayerController() != nullptr)
	{
		GetOwningPlayerController()->ConsoleCommand("Exit");
	}
}

void ARHHUDCommon::OnLogout()
{
	URHPopupManager* popup = GetPopupManager();

	if (popup == nullptr)
	{
		UE_LOG(RallyHereStart, Error, TEXT("Need blueprint implementation for GetPopupManager()!"));
		return;
	}

	FRHPopupConfig popupParams;
	popupParams.Header = NSLOCTEXT("RHMenu", "LogoutHeader", "Logout");
	popupParams.Description = NSLOCTEXT("RHMenu", "LogoutDesc", "Are you sure you want to log out?");

	popupParams.IsImportant = true;
	popupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

	FRHPopupButtonConfig& confirmBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
	confirmBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
	confirmBtn.Type = ERHPopupButtonType::Confirm;
	confirmBtn.Action.AddDynamic(this, &ARHHUDCommon::OnConfirmLogout);

	FRHPopupButtonConfig& cancelBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
	cancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
	cancelBtn.Type = ERHPopupButtonType::Cancel;
	cancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

	popup->AddPopup(popupParams);
}

void ARHHUDCommon::OnConfirmLogout()
{
	if (GetLocalPlayerSubsystem() != nullptr && GetLocalPlayerSubsystem()->GetLoginSubsystem() != nullptr)
	{
		GetLocalPlayerSubsystem()->GetLoginSubsystem()->Logout();
	}
}

void ARHHUDCommon::UIX_ReportServer()
{
    if (URHPopupManager* popup = GetPopupManager())
    {
        FRHPopupConfig popupParams;
        popupParams.Header = NSLOCTEXT("RHMenu", "ReportServerHeader", "Report Server");
        popupParams.Description = NSLOCTEXT("RHMenu", "ReportServerDesc", "Are you sure you want to report this server as having technical difficulties?");
        popupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

        FRHPopupButtonConfig& confirmBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
        confirmBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
        confirmBtn.Type = ERHPopupButtonType::Confirm;
        confirmBtn.Action.AddDynamic(this, &ARHHUDCommon::ConfirmReportServer);

        FRHPopupButtonConfig& cancelBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
        cancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
        cancelBtn.Type = ERHPopupButtonType::Cancel;
        cancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

        popup->AddPopup(popupParams);
    }
}

void ARHHUDCommon::ConfirmReportServer()
{
	// #RHTODO - Report Server
}

void ARHHUDCommon::ShowErrorPopup(FText ErrorMsg)
{
    if (URHPopupManager* popup = GetPopupManager())
    {
        FRHPopupConfig popupParams;
        popupParams.Header = NSLOCTEXT("General", "Error", "Error");
        popupParams.Description = ErrorMsg;
        popupParams.IsImportant = true;
        popupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

        FRHPopupButtonConfig& cancelBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
        cancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
        cancelBtn.Type = ERHPopupButtonType::Cancel;
        cancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

        popup->AddPopup(popupParams);
    }

    LogErrorMessage(ErrorMsg);
}

void ARHHUDCommon::LogErrorMessage(FText ErrorMsg)
{
    FString FromText = ErrorMsg.ToString();
    UE_LOG(RallyHereStart, Error, TEXT("HUD LogErrorMessage: %s"), *FromText);
}

void ARHHUDCommon::PrintToLog(FText InText)
{
    FString FromText = InText.ToString();
    UE_LOG(RallyHereStart, Log, TEXT("HUD PrintToLog: %s"), *FromText);
}

void ARHHUDCommon::OnInvalidVoucherOrder(URHStoreItem* StoreItem)
{

}

URHOrderManager* ARHHUDCommon::GetOrderManager() const
{
    if (URHGameInstance* gameInstance = Cast<URHGameInstance>(GetGameInstance()))
    {
        return gameInstance->GetOrderManager();
    }

    return nullptr;
}

class URHLoadoutDataFactory* ARHHUDCommon::GetLoadoutDataFactory() const
{
	if (URHGameInstance* gameInstance = Cast<URHGameInstance>(GetGameInstance()))
	{
		return gameInstance->GetLoadoutDataFactory();
	}

	return nullptr;
}

URHStoreItemHelper* ARHHUDCommon::GetItemHelper() const
{
    if (URHGameInstance* gameInstance = Cast<URHGameInstance>(GetGameInstance()))
    {
        if (URHStoreItemHelper* pStoreHelper = gameInstance->GetStoreItemHelper())
        {
            return Cast<URHStoreItemHelper>(pStoreHelper);
        }
    }

    return nullptr;
}

URHUISessionManager* ARHHUDCommon::GetUISessionManager() const
{
    if (URHGameInstance* gameInstance = Cast<URHGameInstance>(GetGameInstance()))
    {
        return gameInstance->GetUISessionManager();
    }

    return nullptr;
}

/**
* Console
*/
void ARHHUDCommon::HandleControllerDisconnect()
{
	URHPopupManager* PopupManager = GetPopupManager();
	if (PopupManager != nullptr)
	{
		FRHPopupConfig ControllerDisconnectPopupParams;

		const FString PlatformName = UGameplayStatics::GetPlatformName();
		if (PlatformName == TEXT("PS5"))
		{
			const bool bUseAlternateTranslations = FPlatformMisc::GetDefaultLocale() == TEXT("es");
			if (bUseAlternateTranslations)
			{
				ControllerDisconnectPopupParams.SubHeading = NSLOCTEXT("RHMenu", "ControllerDisconnectTitle_PS5_Alt", "Controller Disconnected");
				ControllerDisconnectPopupParams.Description = NSLOCTEXT("RHMenu", "ControllerDisconnectDescription_PS5_Alt", "The active controller has been disconnected! Please reconnect to continue playing.");
			}
			else
			{
				ControllerDisconnectPopupParams.SubHeading = NSLOCTEXT("RHMenu", "ControllerDisconnectTitle_PS5", "Controller Disconnected");
				ControllerDisconnectPopupParams.Description = NSLOCTEXT("RHMenu", "ControllerDisconnectDescription_PS5", "The active controller has been disconnected! Please reconnect to continue playing.");
			}
		}
		else if (PlatformName == TEXT("PS4"))
		{
			const bool bUseAlternateTranslations = FPlatformMisc::GetDefaultLocale() == TEXT("es");
			if (bUseAlternateTranslations)
			{
				ControllerDisconnectPopupParams.SubHeading = NSLOCTEXT("RHMenu", "ControllerDisconnectTitle_PS4_Alt", "DUALSHOCK®4 Wireless Controller Disconnected");
				ControllerDisconnectPopupParams.Description = NSLOCTEXT("RHMenu", "ControllerDisconnectDescription_PS4_Alt", "The active controller has been disconnected! Please reconnect to continue playing.");
			}
			else
			{
				ControllerDisconnectPopupParams.SubHeading = NSLOCTEXT("RHMenu", "ControllerDisconnectTitle_PS4", "DUALSHOCK®4 Wireless Controller Disconnected");
				ControllerDisconnectPopupParams.Description = NSLOCTEXT("RHMenu", "ControllerDisconnectDescription_PS4", "The active controller has been disconnected! Please reconnect to continue playing.");
			}
		}
		else if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX"))
		{
			ControllerDisconnectPopupParams.SubHeading = NSLOCTEXT("RHMenu", "ControllerDisconnectTitle_XboxOne", "Controller Disconnected");
			ControllerDisconnectPopupParams.Description = NSLOCTEXT("RHMenu", "ControllerDisconnectDescription_XboxOne", "The active controller has been disconnected! Please reconnect to continue playing.");
		}
		else
		{
			ControllerDisconnectPopupParams.SubHeading = NSLOCTEXT("RHMenu", "ControllerDisconnectTitle", "Controller Disconnected");
			ControllerDisconnectPopupParams.Description = NSLOCTEXT("RHMenu", "ControllerDisconnectDescription", "The active controller has been disconnected! Please reconnect to continue playing.");
		}

		ControllerDisconnectPopupParams.IsImportant = true;
		ControllerDisconnectPopupParams.CancelAction.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);
		ControllerDisconnectPopupParams.TextAlignment = ETextJustify::Center;

		FRHPopupButtonConfig& ContinueButton = (ControllerDisconnectPopupParams.Buttons[ControllerDisconnectPopupParams.Buttons.Add(FRHPopupButtonConfig())]);
		ContinueButton.Label = NSLOCTEXT("General", "Continue", "Continue");
		ContinueButton.Type = ERHPopupButtonType::Confirm;
		ContinueButton.Action.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

		PopupManager->AddPopup(ControllerDisconnectPopupParams);
	}
}

/**
* Color Palette
*/
bool ARHHUDCommon::GetColor(FName ColorName, FLinearColor& ReturnColor) const
{
	if(ColorPaletteDT != nullptr)
	{
		if(FColorPaletteInfo* RowColor = ColorPaletteDT->FindRow<FColorPaletteInfo>(ColorName, TEXT("Color Info Lookup"), false))
		{
			ReturnColor = RowColor->LinearColor;
			return true;
		}
	}
	return false;
};

/**
* Font Palette
*/
bool ARHHUDCommon::GetFont(FName FontName, FSlateFontInfo& ReturnFont) const
{
	if(FontPaletteDT != nullptr)
	{
		if(FFontPaletteInfo* RowFont = FontPaletteDT->FindRow<FFontPaletteInfo>(FontName, TEXT("Font Info Lookup"), false))
		{
			ReturnFont = RowFont->FontInfo;
			return true;
		}
	}
	return false;
};

bool ARHHUDCommon::IsMuted(const FGuid& PlayerUuid)
{
	/* #RHTODO - Voice
	if (pcomGetVoice() != nullptr && pcomGetVoice()->IsInitialized())
	{
		CTgNetId PlayerNetId(PlayerId, TNIT_PLAYER);
		return pcomGetVoice()->IsPlayerMuted(PlayerNetId);
	}
	*/
	return false;
}

bool ARHHUDCommon::MutePlayer(const FGuid& PlayerUuid, bool Mute)
{
	/* #RHTODO - Voice
	if (pcomGetVoice() != nullptr && pcomGetVoice()->IsInitialized())
	{
		CTgNetId PlayerNetId(PlayerId, TNIT_PLAYER);
		pcomGetVoice()->MutePlayer(PlayerNetId, Mute);

		return pcomGetVoice()->IsPlayerMuted(PlayerNetId);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("pcomGetVoice not found!"));
	}
	*/
	return false;
}

void ARHHUDCommon::ProcessSoundEvent(FName SoundEventName)
{
	APlayerController* PC = GetOwningPlayerController();

	if (SoundTheme != nullptr && PC != nullptr)
	{
		SoundTheme->ProcessSoundEvent(SoundEventName, PC);
	}
}

void ARHHUDCommon::DisplayGenericPopup(const FString& sTitle, const FString& sDesc)
{
	if (URHPopupManager* popupManager = GetPopupManager())
	{
		FRHPopupConfig popupData;
		popupData.Header = FText::FromString(sTitle);
		popupData.Description = FText::FromString(sDesc);
		popupData.CancelAction.AddDynamic(popupManager, &URHPopupManager::OnPopupCanceled);

		popupData.Buttons.AddDefaulted();
		FRHPopupButtonConfig& buttonConfirm = popupData.Buttons.Last();
		buttonConfirm.Label = NSLOCTEXT("General", "Okay", "Okay");
		buttonConfirm.Action.AddDynamic(popupManager, &URHPopupManager::OnPopupCanceled);

		popupManager->AddPopup(popupData);
	}
}

void ARHHUDCommon::DisplayGenericError(const FString& sDesc)
{
	DisplayGenericPopup(NSLOCTEXT("General", "Error", "Error").ToString(), sDesc);
}


RH_INPUT_STATE ARHHUDCommon::GetCurrentInputState()
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX") || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("Switch"))
	{
		return RH_INPUT_STATE::PIS_GAMEPAD;
	}

	if (PlayerInput.IsValid())
	{
		return PlayerInput->GetInputState();
	}
	else
	{
		UE_LOG(RallyHereStart, Verbose, TEXT("RH_INPUT_STATE ARHHUDCommon::GetCurrentInputState() cannot retrieve valid PlayerInput"));
		// prefer GAMEPAD when the controller is plugged in for the application, this will not override the last input state
		if (FSlateApplication::IsInitialized() && IsInGameThread())
		{
			TSharedPtr<GenericApplication> GenericApplication = FSlateApplication::Get().GetPlatformApplication();
			if (GenericApplication.IsValid() && GenericApplication->IsGamepadAttached())
			{
				UE_LOG(RallyHereStart, Verbose, TEXT("Gamepad detected, returning current input state as RH_INPUT_STATE::PIS_GAMEPAD"));
				return RH_INPUT_STATE::PIS_GAMEPAD;
			}
		}

		UE_LOG(RallyHereStart, Verbose, TEXT("returning current input state as RH_INPUT_STATE::PIS_UNKNOWN"));
		return RH_INPUT_STATE::PIS_UNKNOWN;
	}
}

void ARHHUDCommon::InputStateChangePassthrough(RH_INPUT_STATE inputState)
{
	if (inputState == RH_INPUT_STATE::PIS_GAMEPAD)
	{
		SetUIFocus();
	}

	OnInputStateChanged.Broadcast(inputState);
}

void ARHHUDCommon::DrawHUD()
{
	if (!HasHUDBeenDrawn)
	{
		HasHUDBeenDrawn = true;
		HUDDrawn.Broadcast();
	}
	Super::DrawHUD();
}

FPlatformUserId ARHHUDCommon::GetPlatformUserId() const
{
	if (GetOwningPlayerController() != nullptr)
	{
		return GetOwningPlayerController()->GetPlatformUserId();
	}
	return FPlatformUserId();
}

FUniqueNetIdWrapper ARHHUDCommon::GetOSSUniqueId() const
{
	auto LPSS = GetLocalPlayerSubsystem();
	if (LPSS != nullptr)
	{
		return LPSS->GetOSSUniqueId();
	}
	return FUniqueNetIdWrapper();
}

URH_LocalPlayerSubsystem* ARHHUDCommon::GetLocalPlayerSubsystem() const
{
	auto* PC = GetOwningPlayerController();
	if (PC != nullptr)
	{
		auto* LP = PC->GetLocalPlayer();
		if (LP != nullptr)
		{
			auto* RHLP = LP->GetSubsystem<URH_LocalPlayerSubsystem>();
			return RHLP;
		}
	}

	return nullptr;
}

URH_GameInstanceSubsystem* ARHHUDCommon::GetGameInstanceSubsystem() const
{
	auto* World = GetWorld();
	if (World != nullptr)
	{
		auto* GI = World->GetGameInstance();
		if (GI != nullptr)
		{
			return GI->GetSubsystem<URH_GameInstanceSubsystem>();
		}
	}
	return nullptr;
}

URH_PlayerInfoSubsystem* ARHHUDCommon::GetPlayerInfoSubsystem() const
{
	auto* RHLP = GetLocalPlayerSubsystem();
	if (RHLP != nullptr)
	{
		return RHLP->GetPlayerInfoSubsystem();
	}
	return nullptr;
}

URH_PlayerInfo* ARHHUDCommon::GetOrCreatePlayerInfo(const FGuid& PlayerUuid)
{
	if (!PlayerUuid.IsValid())
	{
		return nullptr;
	}

	auto* pSubsystem = GetPlayerInfoSubsystem();

	if (!pSubsystem)
	{
		return nullptr;
	}

	return pSubsystem->GetOrCreatePlayerInfo(PlayerUuid);
}

URH_PlayerPlatformInfo* ARHHUDCommon::GetOrCreatePlayerPlatformInfo(const FRH_PlayerPlatformId& Identity)
{
	if (!Identity.IsValid())
	{
		return nullptr;
	}

	auto* pSubsystem = GetPlayerInfoSubsystem();

	if (!pSubsystem)
	{
		return nullptr;
	}

	return pSubsystem->GetOrCreatePlayerPlatformInfo(Identity);
}

URH_PlayerInfo* ARHHUDCommon::GetPlayerInfo(const FGuid& PlayerUuid)
{
	if (PlayerUuid.IsValid())
	{
		return nullptr;
	}

	auto* pSubsystem = GetPlayerInfoSubsystem();

	if (!pSubsystem)
	{
		return nullptr;
	}

	return pSubsystem->GetPlayerInfo(PlayerUuid);
}

URH_PlayerPlatformInfo* ARHHUDCommon::GetPlayerPlatformInfo(const FRH_PlayerPlatformId& Identity)
{
	if (Identity.IsValid())
	{
		return nullptr;
	}

	auto* pSubsystem = GetPlayerInfoSubsystem();

	if (!pSubsystem)
	{
		return nullptr;
	}

	return pSubsystem->GetPlayerPlatformInfo(Identity);
}

URH_PlayerInfo* ARHHUDCommon::GetLocalPlayerInfo()
{
	auto* pLocalPlayerSubsystem = GetLocalPlayerSubsystem();
	if (!pLocalPlayerSubsystem)
	{
		return nullptr;
	}

	return pLocalPlayerSubsystem->GetLocalPlayerInfo();
}

FGuid ARHHUDCommon::GetLocalPlayerUuid()
{
	auto* pLocalPlayerSubsystem = GetLocalPlayerSubsystem();
	if (!pLocalPlayerSubsystem)
	{
		return FGuid();
	}

	return pLocalPlayerSubsystem->GetPlayerUuid();
}

FRH_PlayerPlatformId ARHHUDCommon::GetLocalPlayerPlatformId()
{
	auto* pLocalPlayerSubsystem = GetLocalPlayerSubsystem();
	if (!pLocalPlayerSubsystem)
	{
		return FRH_PlayerPlatformId();
	}
	return pLocalPlayerSubsystem->GetPlayerPlatformId();
}

bool ARHHUDCommon::IsCrossplayEnabled() const
{
	if (URH_LocalPlayerSubsystem* LPSS = GetLocalPlayerSubsystem())
	{
		if (URH_LocalPlayerLoginSubsystem* LoginSS = LPSS->GetLoginSubsystem())
		{
			return LoginSS->IsCrossplayEnabled();
		}
	}
	return false;
}
