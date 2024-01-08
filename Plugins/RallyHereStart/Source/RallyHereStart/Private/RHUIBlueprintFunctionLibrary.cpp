#include "RallyHereStart.h"
#include "GameFramework/RHGameInstance.h"
#include "GameFramework/RHGameUserSettings.h"
#include "Player/Controllers/RHPlayerController.h"
#include "Managers/RHEventManager.h"
#include "Managers/RHStoreItemHelper.h"
#include "Runtime/Engine/Classes/Engine/UserInterfaceSettings.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Shared/Settings/RHSettingsWidget.h"
#include "Shared/Settings/RHSettingsPreview.h"
#include "GameFramework/RHGameState.h"
#include "Player/Controllers/RHPlayerState.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "Lobby/Widgets/RHMediaPlayerWidget.h"
#include "RH_OnlineSubsystemNames.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_LocalPlayerSessionSubsystem.h"
#include "EnhancedInputSubsystems.h"

#include "JsonUtilities.h"

void URHUIBlueprintFunctionLibrary::RegisterGridNavigation(URHWidget* ParentWidget, int32 FocusGroup, const TArray<UWidget*> NavWidgets, int32 GridWidth, bool NavToLastElementOnDown)
{
    if (ParentWidget == nullptr || GridWidth <= 0)
    {
        return;
    }

    for (int32 i = 0; i < NavWidgets.Num(); i++)
    {
        UWidget* NavWidget = NavWidgets[i];

        UWidget* LeftWidget = nullptr;
        int32 LeftIndex = i - 1;
        if (NavWidgets.IsValidIndex(LeftIndex) && LeftIndex % GridWidth != GridWidth - 1)
        {
            LeftWidget = NavWidgets[LeftIndex];
        }

        UWidget* RightWidget = nullptr;
        int32 RightIndex = i + 1;
        if (NavWidgets.IsValidIndex(RightIndex) && RightIndex % GridWidth != 0)
        {
            RightWidget = NavWidgets[RightIndex];
        }

        UWidget* UpWidget = nullptr;
        int32 UpIndex = i - GridWidth;
        if (NavWidgets.IsValidIndex(UpIndex))
        {
            UpWidget = NavWidgets[UpIndex];
        }

        UWidget* DownWidget = nullptr;
        int32 DownIndex = i + GridWidth;
        if (NavWidgets.IsValidIndex(DownIndex))
        {
            DownWidget = NavWidgets[DownIndex];
        }
        else if (NavToLastElementOnDown)
        {
            const int32 LastRowStart = NavWidgets.Num() - (NavWidgets.Num() % GridWidth);
            if (i < LastRowStart)
            {
                DownWidget = NavWidgets[NavWidgets.Num() - 1];
            }
        }

        ParentWidget->RegisterWidgetToInputManager(NavWidget, FocusGroup, UpWidget, DownWidget, LeftWidget, RightWidget);
    }

}

bool URHUIBlueprintFunctionLibrary::IsPlatformType(bool IsConsole, bool IsPC, bool IsMobile)
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("IOS") || PlatformName == TEXT("Android"))
	{
		return IsMobile;
	}
	else if (PLATFORM_DESKTOP)
	{
		return IsPC;
	}
	else
	{
		return IsConsole;
	}
}

bool URHUIBlueprintFunctionLibrary::IsAnonymousLogin(ARHHUDCommon* pHUD)
{
	FName OSSName = NAME_None;

	if (pHUD != nullptr)
	{
		auto* LPSS = pHUD->GetLocalPlayerSubsystem();
		if (LPSS != nullptr)
		{
			auto* OSS = LPSS->GetLoginSubsystem()->GetLoginOSS();
			if (OSS != nullptr)
			{
				OSSName = OSS->GetSubsystemName();
			}
		}
	}

	return OSSName == ANON_SUBSYSTEM;
}

void URHUIBlueprintFunctionLibrary::RegisterLinearNavigation(URHWidget* ParentWidget, const TArray<URHWidget*> NavWidgets, int32 FocusGroup, bool bHorizontal, bool bLooping)
{
	int32 NumWidgets = NavWidgets.Num();

	if (!ParentWidget || NumWidgets == 0)
	{
		return;
	}

	for (int32 i = 0; i < NumWidgets; ++i)
	{
		URHWidget* PrevWidget = (i > 0) ? NavWidgets[i - 1] : bLooping ? NavWidgets.Last() : nullptr;
		URHWidget* NextWidget = ((i + 1) < NumWidgets) ? NavWidgets[i + 1] : bLooping ? NavWidgets[0] : nullptr;

		if (bHorizontal)
		{
			ParentWidget->RegisterWidgetToInputManager(NavWidgets[i], FocusGroup, nullptr, nullptr, PrevWidget, NextWidget);
		}
		else
		{
			ParentWidget->RegisterWidgetToInputManager(NavWidgets[i], FocusGroup, PrevWidget, NextWidget, nullptr, nullptr);
		}
	}
}

URHStoreItemHelper* URHUIBlueprintFunctionLibrary::GetStoreItemHelper(const UObject* WorldContextObject)
{
    if (WorldContextObject)
    {
        if (UWorld* const World = WorldContextObject->GetWorld())
        {
            if (URHGameInstance* gameInstance = Cast<URHGameInstance>(World->GetGameInstance()))
            {
                return Cast<URHStoreItemHelper>(gameInstance->GetStoreItemHelper());
            }
        }
    }

    return nullptr;
}

URH_PlayerInfo* URHUIBlueprintFunctionLibrary::GetLocalPlayerInfo(ARHHUDCommon* HUD)
{
	if (HUD != nullptr)
	{
		auto* LPSS = HUD->GetLocalPlayerSubsystem();
		if (LPSS != nullptr)
		{
			return LPSS->GetLocalPlayerInfo();
		}
	}
	return nullptr;
}

int URHUIBlueprintFunctionLibrary::CompareStrings(const FString& LeftString, const FString& RightString)
{
	return LeftString.Compare(RightString);
}

FText URHUIBlueprintFunctionLibrary::Key_GetShortDisplayName(const FKey& Key)
{
    return Key.GetDisplayName(false);
}

class URHSettingsWidget* URHUIBlueprintFunctionLibrary::CreateSettingsWidget(ARHHUDCommon* HUD, const TSubclassOf<class URHSettingsWidget>& SettingsWidgetClass)
{
	if (HUD != nullptr)
	{
		return CreateWidget<URHSettingsWidget>(HUD->GetWorld(), SettingsWidgetClass);
	}

	return nullptr;
}

class URHSettingsWidget* URHUIBlueprintFunctionLibrary::CreateSettingsWidgetWithConfig(ARHHUDCommon* HUD, FRHSettingsWidgetConfig SettingsWidgetConfig)
{
	if (HUD != nullptr)
	{
		if (URHSettingsWidget* SettingsWidget = CreateSettingsWidget(HUD, SettingsWidgetConfig.WidgetClass))
		{
            SettingsWidget->InitializeFromWidgetConfig(HUD, SettingsWidgetConfig);
            return SettingsWidget;
		}
	}

	return nullptr;
}

class URHSettingsPreview* URHUIBlueprintFunctionLibrary::CreateSettingsPreview(ARHHUDCommon* HUD, const TSubclassOf<class URHSettingsPreview>& SettingsPreviewClass)
{
	if (HUD != nullptr)
	{
		return CreateWidget<URHSettingsPreview>(HUD->GetWorld(), SettingsPreviewClass);
	}

	return nullptr;
}

FReportPlayerParams URHUIBlueprintFunctionLibrary::SetupReportPlayerFromGameState(int64 PlayerId, const ARHGameState* State)
{
	FReportPlayerParams Params = FReportPlayerParams();

	// #RHTODO - Report Player
	
	return Params;
}

bool URHUIBlueprintFunctionLibrary::UIX_ReportPlayer(const UObject* WorldContextObject, const FReportPlayerParams& Params)
{
	// #RHTODO - Report Player
	return false;
}

bool URHUIBlueprintFunctionLibrary::CanReportServer(const UObject* WorldContextObject)
{
	// #RHTODO - Report Server	
	return false;
}

FKey URHUIBlueprintFunctionLibrary::GetKeyForBinding(class APlayerController* PlayerController, FName Binding, bool SecondaryKey, bool FallbackToDefault, bool IsGamepadDoubleTap)
{
	UE_LOG(RallyHereStart, Verbose, TEXT("URHUIBlueprintFunctionLibrary::GetKeyForBinding Binding: %s, Secondary: %d, Fallback: %d, Doubletap: %d"), *Binding.ToString(), SecondaryKey, FallbackToDefault, IsGamepadDoubleTap);
	if (Binding == "NavigateConfirm" || Binding == "Confirm")
	{
		if (!SecondaryKey)
		{
			return URHPlayerInput::GetGamepadConfirmButton();
		}
		else
		{
			return FKey();
		}
	}
	else if (Binding == "NavigateBack" || Binding == "Back")
	{
		if (!SecondaryKey)
		{
			return URHPlayerInput::GetGamepadCancelButton();
		}
		else
		{
			return FKey();
		}
	}
	else if (PlayerController)
	{
		if (URHPlayerInput* const PlayerInput = Cast<URHPlayerInput>(PlayerController->PlayerInput))
		{
			EInputType InputType = EInputType::KBM;

			const FString PlatformName = UGameplayStatics::GetPlatformName();
			if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX") || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("Switch"))
			{
				InputType = EInputType::GP;
			}
			else
			{
				switch (PlayerInput->GetInputState())
				{
				case RH_INPUT_STATE::PIS_GAMEPAD:
					InputType = EInputType::GP;
					break;
				case RH_INPUT_STATE::PIS_UNKNOWN:
				case RH_INPUT_STATE::PIS_KEYMOUSE:
					InputType = EInputType::KBM;
					break;
				case RH_INPUT_STATE::PIS_TOUCH:
					InputType = EInputType::Touch;
					break;
				}
			}

			TArray<FRHInputActionKey> ActionKeys;
			PlayerInput->GetCustomInputActionKeys(Binding, InputType, ActionKeys);

			UE_LOG(RallyHereStart, Verbose, TEXT("URHUIBlueprintFunctionLibrary::GetKeyForBinding Num Action Keys Found: %d"), ActionKeys.Num());

			if (ActionKeys.Num())
			{
				if (!SecondaryKey || (IsGamepadDoubleTap && InputType == EInputType::GP))
				{
					UE_LOG(RallyHereStart, Verbose, TEXT("URHUIBlueprintFunctionLibrary::GetKeyForBinding Returning Primary Key: %s"), *ActionKeys[0].Key.ToString());
					return ActionKeys[0].Key;
				}
				else if (ActionKeys.Num() > 1 && ActionKeys[1].Key.IsGamepadKey())
				{
					UE_LOG(RallyHereStart, Verbose, TEXT("URHUIBlueprintFunctionLibrary::GetKeyForBinding Returning Secondary Key: %s"), *ActionKeys[1].Key.ToString());
					return ActionKeys[1].Key;
				}
			}

			if (!FallbackToDefault)
			{
				return FKey();
			}

			PlayerInput->GetDefaultInputActionKeys(Binding, InputType, ActionKeys);

			UE_LOG(RallyHereStart, Verbose, TEXT("URHUIBlueprintFunctionLibrary::GetKeyForBinding Num Default Action Keys Found: %d"), ActionKeys.Num());

			if (ActionKeys.Num())
			{
				if (!SecondaryKey || (IsGamepadDoubleTap && InputType == EInputType::GP))
				{
					UE_LOG(RallyHereStart, Verbose, TEXT("URHUIBlueprintFunctionLibrary::GetKeyForBinding Returning Primary Key: %s"), *ActionKeys[0].Key.ToString());
					return ActionKeys[0].Key;
				}
				else if (ActionKeys.Num() > 1 && ActionKeys[1].Key.IsGamepadKey())
				{
					UE_LOG(RallyHereStart, Verbose, TEXT("URHUIBlueprintFunctionLibrary::GetKeyForBinding Returning Secondary Key: %s"), *ActionKeys[1].Key.ToString());
					return ActionKeys[1].Key;
				}
			}
		}
	}

	return FKey();
}

const FText& URHUIBlueprintFunctionLibrary::GetTextByPlatform(const FText& DefaultText, const TMap<FString, FText>& PlatformTexts)
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();

	if (PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5"))
	{
		const bool bUseAlternateTranslations = FPlatformMisc::GetDefaultLocale() == TEXT("es");
		if (bUseAlternateTranslations)
		{
			if (const FText* PlatformText = PlatformTexts.Find(PlatformName + TEXT("_Alt")))
			{
				return *PlatformText;
			}
		}
	}

	if (const FText* PlatformText = PlatformTexts.Find(PlatformName))
	{
		return *PlatformText;
	}

	return DefaultText;
}

class APlayerController* URHUIBlueprintFunctionLibrary::GetLocalPlayerController(const class UObject* WorldContextObject, int32 PlayerIndex)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		uint32 Index = 0;
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = Iterator->Get();
			check(PlayerController != nullptr);
			if (PlayerController->IsLocalPlayerController())
			{
				if (Index == PlayerIndex)
				{
					return PlayerController;
				}
				Index++;
			}
		}
	}
	return nullptr;
}

class ARHGameState* URHUIBlueprintFunctionLibrary::GetGameState(const class UObject* WorldContextObject)
{
	UWorld* const World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	return World ? World->GetGameState<ARHGameState>() : nullptr;
}

EGamepadIcons URHUIBlueprintFunctionLibrary::GetGamepadIconSet()
{
	URHGameUserSettings* RHSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings());
	return RHSettings != nullptr ? RHSettings->GetGamepadIconSet() : EGamepadIcons::XboxOne;
}

URHBattlepass* URHUIBlueprintFunctionLibrary::GetActiveBattlepass(const class UObject* WorldContextObject)
{
	if (WorldContextObject)
	{
		if (UWorld* const World = WorldContextObject->GetWorld())
		{
			if (URHGameInstance* GameInstance = Cast<URHGameInstance>(World->GetGameInstance()))
			{
				if (URHEventManager* EventManager = GameInstance->GetEventManager())
				{
					return EventManager->GetActiveEvent<URHBattlepass>();
				}
			}
		}
	}

	return nullptr;
}

bool URHUIBlueprintFunctionLibrary::HasCinematicToPlay(UDataTable* CinematicDataTable)
{
	if (!CinematicDataTable)
	{
		if (URHMediaPlayerWidget* MediaPlayerWidgetDefault = Cast<URHMediaPlayerWidget>(URHMediaPlayerWidget::StaticClass()->GetDefaultObject()))
		{
			if (MediaPlayerWidgetDefault->PlaylistDataTableClassName.ToString().Len() > 0)
			{
				CinematicDataTable = LoadObject<UDataTable>(nullptr, *MediaPlayerWidgetDefault->PlaylistDataTableClassName.ToString(), nullptr, LOAD_None, nullptr);
			}
		}
	}

	if (CinematicDataTable != nullptr)
	{
		URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings());

		TArray<FName> RowNames = CinematicDataTable->GetRowNames();
		for (auto& RowName : RowNames)
		{
			FRHMediaPlayerWidgetPlaylistEntry* Entry = CinematicDataTable->FindRow<FRHMediaPlayerWidgetPlaylistEntry>(RowName, "Get playList entry.");
			if (!Entry->bOnlyWatchOnce || (pGameSettings != nullptr && !pGameSettings->IsLocalActionSaved(Entry->LocalActionName)))
			{
				return true;
			}
		}
	}

	return false;
}

ERHAPI_Platform URHUIBlueprintFunctionLibrary::GetLoggedInPlatformId(ARHHUDCommon* pHUD)
{
	auto LPSS = pHUD != nullptr ? pHUD->GetLocalPlayerSubsystem() : nullptr;
	if (LPSS != nullptr)
	{
		return LPSS->GetLoggedInPlatform();
	}

	return ERHAPI_Platform::Anon;
}

ERHAPI_Platform URHUIBlueprintFunctionLibrary::GetPlatformIdByOSSName(FName OSSName)
{
	if (OSSName == STEAM_SUBSYSTEM || OSSName == STEAMV2_SUBSYSTEM)
	{
		return ERHAPI_Platform::Steam;
	}
	else if (OSSName == EOS_SUBSYSTEM || OSSName == EOSPLUS_SUBSYSTEM)
	{
		return ERHAPI_Platform::Epic;
	}
	else if (OSSName == PS4_SUBSYSTEM || OSSName == PS5_SUBSYSTEM)
	{
		return ERHAPI_Platform::Psn;
	}
	else if (OSSName == LIVE_SUBSYSTEM || OSSName == GDK_SUBSYSTEM)
	{
		return ERHAPI_Platform::XboxLive;
	}
	else if (OSSName == SWITCH_SUBSYSTEM)
	{
		return ERHAPI_Platform::NintendoSwitch;
	}
	else if (OSSName == APPLE_SUBSYSTEM)
	{
		return ERHAPI_Platform::Apple;
	}
	else if (OSSName == GOOGLE_SUBSYSTEM)
	{
		return ERHAPI_Platform::Google;
	}
	else if (OSSName == ANON_SUBSYSTEM)
	{
		return ERHAPI_Platform::Anon;
	}
	else
	{
		return ERHAPI_Platform::Anon;
	}
}

ERHPlatformDisplayType URHUIBlueprintFunctionLibrary::ConvertPlatformTypeToDisplayType(ARHHUDCommon* pHUD, ERHAPI_Platform PlatformType)
{
	const ERHAPI_Platform LocalPlatform = URHUIBlueprintFunctionLibrary::GetLoggedInPlatformId(pHUD);

	const FString PlatformName = UGameplayStatics::GetPlatformName();
	const bool PlatformConsoleGeneric = PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX") || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("Switch");

	switch (PlatformType)
	{
	case ERHAPI_Platform::Psn:
		if (!PlatformConsoleGeneric || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5"))
		{
			return ERHPlatformDisplayType::Playstation;
		}
		else
		{
			return ERHPlatformDisplayType::ConsoleGeneric;
		}
	case ERHAPI_Platform::XboxLive:
		if (!PlatformConsoleGeneric || PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX"))
		{
			return ERHPlatformDisplayType::Xbox;
		}
		else
		{
			return ERHPlatformDisplayType::ConsoleGeneric;
		}
	case ERHAPI_Platform::NintendoSwitch:
	case ERHAPI_Platform::NintendoNaid:
	case ERHAPI_Platform::NintendoPpid:
		return !PlatformConsoleGeneric ? ERHPlatformDisplayType::Switch : ERHPlatformDisplayType::ConsoleGeneric;
	case ERHAPI_Platform::Anon:
	case ERHAPI_Platform::Google:
	case ERHAPI_Platform::Twitch:
		break;
	case ERHAPI_Platform::Steam:
		if (LocalPlatform == PlatformType || LocalPlatform == ERHAPI_Platform::Anon)
		{
			return ERHPlatformDisplayType::Steam;
		}
		break;
	case ERHAPI_Platform::Epic:
		if (LocalPlatform == PlatformType || LocalPlatform == ERHAPI_Platform::Anon)
		{
			return ERHPlatformDisplayType::Epic;
		}
		break;
	default:
		break;

	}

	return ERHPlatformDisplayType::PC;
}

ERHPlayerOnlineStatus URHUIBlueprintFunctionLibrary::GetPlayerOnlineStatus(const class UObject* WorldContextObject, URH_PlayerInfo* PlayerInfo, const URH_LocalPlayerSubsystem* LocalPlayerSS, bool bAllowPartyStatus /*= true*/, bool bAllowFriendRequestStatus /*= true*/)
{
	if (PlayerInfo != nullptr)
	{
		// local player status
		if (LocalPlayerSS != nullptr)
		{
			if (LocalPlayerSS->GetPlayerUuid() == PlayerInfo->GetRHPlayerUuid())
			{
				//OnlineStatus = ERHPlayerOnlineStatus::FGS_DND;
				//OnlineStatus = ERHPlayerOnlineStatus::FGS_InQueue;
				//OnlineStatus = ERHPlayerOnlineStatus::FGS_InMatch;
					
				if (bAllowPartyStatus)
				{
					if (const auto RHSS = LocalPlayerSS->GetSessionSubsystem())
					{
						const auto Sessions = RHSS->GetSessionsByType(GetDefault<URHPartyManager>()->GetRHSessionType());
						for (const auto& Session : Sessions)
						{
							if (Session != nullptr && Session->GetSessionPlayerCount() > 1)
							{
								return ERHPlayerOnlineStatus::FGS_InParty;
							}
						}
					}
				}

				return ERHPlayerOnlineStatus::FGS_InGame;
			}

			if (bAllowPartyStatus)
			{
				const auto RHSS = LocalPlayerSS->GetSessionSubsystem();
				if (RHSS != nullptr)
				{
					const auto Sessions = RHSS->GetSessionsByType(GetDefault<URHPartyManager>()->GetRHSessionType());
					for (const auto& Session : Sessions)
					{
						if (Session != nullptr)
						{
							auto* pPartyMember = Session->GetSessionPlayer(PlayerInfo->GetRHPlayerUuid());
							if (pPartyMember != nullptr)
							{
								return pPartyMember->Status == ERHAPI_SessionPlayerStatus::Invited ? ERHPlayerOnlineStatus::FGS_PendingParty : ERHPlayerOnlineStatus::FGS_InParty;
							}
						}
					}
				}
			}

			if (bAllowFriendRequestStatus)
			{
				if (const auto FriendSubsystem = LocalPlayerSS->GetFriendSubsystem())
				{
					if (const auto Friend = FriendSubsystem->GetFriendByPlayerInfo(PlayerInfo))
					{
						if (Friend->RHFriendRequestSent())
						{
							return ERHPlayerOnlineStatus::FGS_PendingInvite;
						}
						else if (Friend->RhPendingFriendRequest())
						{
							return ERHPlayerOnlineStatus::FGS_FriendRequest;
						}
					}
				}
			}
		}

		// basic presence status
		auto* Presence = PlayerInfo->GetPresence();
		if (PlayerInfo->GetRHPlayerUuid().IsValid() && Presence != nullptr)
		{
			switch (Presence->Status)
			{
			case ERHAPI_OnlineStatus::Away:
				return ERHPlayerOnlineStatus::FGS_Away;
			case ERHAPI_OnlineStatus::Invisible: // should be DND?
			case ERHAPI_OnlineStatus::Offline:
				return ERHPlayerOnlineStatus::FGS_Offline;
			case ERHAPI_OnlineStatus::Online:
				return ERHPlayerOnlineStatus::FGS_Online;
			}
		}

		// No presence, use friend data instead
		if (LocalPlayerSS != nullptr)
		{
			if (const auto FriendSubsystem = LocalPlayerSS->GetFriendSubsystem())
			{
				if (const auto* Friend = FriendSubsystem->GetFriendByPlayerInfo(PlayerInfo))
				{
					if (Friend->IsOnline())
					{
						return ERHPlayerOnlineStatus::FGS_Online;
					}
					else
					{
						return ERHPlayerOnlineStatus::FGS_Offline;
					}
				}
			}
		}
	}

	return ERHPlayerOnlineStatus::FGS_Offline;
}

FText URHUIBlueprintFunctionLibrary::GetPlayerStatusMessage(const class UObject* WorldContextObject, URH_PlayerInfo* PlayerInfo, const URH_LocalPlayerSubsystem* LocalPlayerSS)
{
	return GetStatusMessage(GetPlayerOnlineStatus(WorldContextObject, PlayerInfo, LocalPlayerSS, false));
}

ERHPlayerOnlineStatus URHUIBlueprintFunctionLibrary::GetFriendOnlineStatus(const class UObject* WorldContextObject, const URH_RHFriendAndPlatformFriend* Friend, const URH_LocalPlayerSubsystem* LocalPlayerSS, bool bAllowPartyStatus /*= true*/, bool bAllowFriendRequestStatus /*= true*/)
{
	if (Friend != nullptr)
	{
		auto* FriendSS = Friend->GetFriendSubsystem();

		if (FriendSS != nullptr)
		{
			const auto PlayerInfoSubsystem = FriendSS->GetRH_PlayerInfoSubsystem();
			
			if (PlayerInfoSubsystem != nullptr)
			{
				if (URH_PlayerInfo* PlayerInfo = PlayerInfoSubsystem->GetPlayerInfo(Friend->GetPlayerAndPlatformInfo().PlayerUuid))
				{
					return GetPlayerOnlineStatus(WorldContextObject, PlayerInfo, LocalPlayerSS, bAllowPartyStatus, bAllowFriendRequestStatus);
				}
				else if (LocalPlayerSS)
				{
					if (const URH_PlatformFriend* PortalFriend = Friend->GetPlatformFriend(LocalPlayerSS->GetLoggedInPlatform()))
					{
						if (PortalFriend->IsFriend())
						{
							if (PortalFriend->IsPlayingThisGame())
							{
								return ERHPlayerOnlineStatus::FGS_InGame;
							}
							else if (PortalFriend->IsOnline())
							{
								return ERHPlayerOnlineStatus::FGS_Online;
							}
						}
					}

					if (const URH_PlayerPlatformInfo* PlayerPlatformInfo = PlayerInfoSubsystem->GetPlayerPlatformInfo(Friend->GetPlayerAndPlatformInfo().PlayerPlatformId))
					{
						return PlayerPlatformInfo->GetPlatform() == LocalPlayerSS->GetLoggedInPlatform() ? ERHPlayerOnlineStatus::FGS_Online : ERHPlayerOnlineStatus::FGS_Offline;
					}
				}
			}
		}
	}
	
	return ERHPlayerOnlineStatus::FGS_Offline;
}

FText URHUIBlueprintFunctionLibrary::GetFriendStatusMessage(const class UObject* WorldContextObject, const URH_RHFriendAndPlatformFriend* Friend, const URH_LocalPlayerSubsystem* LocalPlayerSS)
{
	return GetStatusMessage(GetFriendOnlineStatus(WorldContextObject, Friend, LocalPlayerSS));
}

FText URHUIBlueprintFunctionLibrary::GetStatusMessage(ERHPlayerOnlineStatus PlayerStatus)
{
	switch (PlayerStatus)
	{
	case ERHPlayerOnlineStatus::FGS_InGame:
		return NSLOCTEXT("RHFriendlist", "InGame", "IN LOBBY");
	case ERHPlayerOnlineStatus::FGS_Online:
		return NSLOCTEXT("RHFriendlist", "IsAvailable", "AVAILABLE");
	case ERHPlayerOnlineStatus::FGS_InParty:
		return NSLOCTEXT("RHFriendlist", "InParty", "IN PARTY");
	case ERHPlayerOnlineStatus::FGS_PendingParty:
		return NSLOCTEXT("RHPartyInvite", "Pending", "INVITE PENDING");
	case ERHPlayerOnlineStatus::FGS_DND:
		return NSLOCTEXT("RHFriendlist", "DoNotDisturb", "DO NOT DISTURB");
	case ERHPlayerOnlineStatus::FGS_Away:
		return NSLOCTEXT("RHFriendlist", "DoNotDisturb", "DO NOT DISTURB");
	case ERHPlayerOnlineStatus::FGS_InQueue:
		return NSLOCTEXT("RHSocial", "InQueue", "In Queue");
	case ERHPlayerOnlineStatus::FGS_InMatch:
		return NSLOCTEXT("RHSocial", "InMatch", "In Match");
	case ERHPlayerOnlineStatus::FGS_FriendRequest:
		return NSLOCTEXT("RHFriendlist", "IncomingRequest", "INCOMING REQUEST");
	case ERHPlayerOnlineStatus::FGS_PendingInvite:
		return NSLOCTEXT("RHSocial", "RequestedHeader", "REQUEST SENT");
	case ERHPlayerOnlineStatus::FGS_Offline:
	default:
		return NSLOCTEXT("RHFriendlist", "IsOffline", "OFFLINE");
	}
}

URHCurrency* URHUIBlueprintFunctionLibrary::GetCurrencyItemByItemId(const FRH_ItemId& CurrencyItemId)
{
	URHCurrency* CurrencyItem = nullptr;
	if (UPInv_AssetManager* AssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfValid()))
	{
		CurrencyItem = AssetManager->GetSoftPrimaryAssetByItemId<URHCurrency>(CurrencyItemId).Get();
	}

	return CurrencyItem;
}

FName URHUIBlueprintFunctionLibrary::GetKeyName(FKey Key)
{
	return Key.GetFName();
}

FKey URHUIBlueprintFunctionLibrary::GetGamepadConfirmButton()
{
	return URHPlayerInput::GetGamepadConfirmButton();
}

FKey URHUIBlueprintFunctionLibrary::GetGamepadCancelButton()
{
	return URHPlayerInput::GetGamepadCancelButton();
}

float URHUIBlueprintFunctionLibrary::GetUMG_DPI_Scaling()
{
	FVector2D viewportSize;
	GEngine->GameViewport->GetViewportSize(viewportSize);

	int32 X = FGenericPlatformMath::FloorToInt(viewportSize.X);
	int32 Y = FGenericPlatformMath::FloorToInt(viewportSize.Y);

	return GetDefault<UUserInterfaceSettings>(UUserInterfaceSettings::StaticClass())->GetDPIScaleBasedOnSize(FIntPoint(X, Y));
}

bool URHUIBlueprintFunctionLibrary::IsWithEditor()
{
#if WITH_EDITOR
	return true;
#else
	return false;
#endif
}

int32 URHUIBlueprintFunctionLibrary::GetPlayerCohortGroup(URH_PlayerInfo* PlayerInfo, int32 NumberOfGroups)
{
	if (PlayerInfo != nullptr)
	{
		int32 MashedGuid = PlayerInfo->GetRHPlayerUuid().A + PlayerInfo->GetRHPlayerUuid().B + PlayerInfo->GetRHPlayerUuid().C + PlayerInfo->GetRHPlayerUuid().D;
		return MashedGuid % NumberOfGroups;
	}

	return 0;
}