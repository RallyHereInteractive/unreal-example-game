// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "GameFramework/RHGameUserSettings.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "DataFactories/RHSettingsDataFactory.h"
#include "Shared/Settings/SettingsInfo/RHSettingsInfo.h"
#include "Shared/Settings/RHSettingsWidget.h"
#include "GameFramework/RHPlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "RHUIBlueprintFunctionLibrary.h"

// URHSettingsMenuConfigAsset
#if WITH_EDITOR

#include "JsonUtilities.h"
#include "HAL/FileManagerGeneric.h"

#endif

// CHANGE TO RESET SETTINGS OR ALTER
static const int32 CURRENT_VERSION_MAJOR_CASE_SETS = 11;
static const int32 CURRENT_VERSION_MINOR_CASE_SETS = 1;
static const int32 CURRENT_VERSION_MAJOR_INI = 9;
static const int32 CURRENT_VERSION_MINOR_INI = 2;


namespace RHSettingId
{
	const FName None = FName(TEXT("None"));
	const FName Resolution = FName(TEXT("Resolution"));
	const FName ScreenMode = FName(TEXT("ScreenMode"));
	const FName GlobalQuality = FName(TEXT("GlobalQuality"));
	const FName ViewDistanceQuality = FName(TEXT("ViewDistanceQuality"));
	const FName ShadowQuality = FName(TEXT("ShadowQuality"));
	const FName TextureQuality = FName(TEXT("TextureQuality"));
	const FName VisualEffectQuality = FName(TEXT("VisualEffectQuality"));
	const FName PostProcessingQuality = FName(TEXT("PostProcessingQuality"));
	const FName FoliageQuality = FName(TEXT("FoliageQuality"));
	const FName Antialiasing = FName(TEXT("Antialiasing"));
	const FName MotionBlur = FName(TEXT("MotionBlur"));
	const FName AutoDetect = FName(TEXT("AutoDetect"));
	const FName MasterVolume = FName(TEXT("MasterVolume"));
	const FName MusicVolume = FName(TEXT("MusicVolume"));
	const FName SFXVolume = FName(TEXT("SFXVolume"));
	const FName VoiceVolume = FName(TEXT("VoiceVolume"));
	const FName SafeFrame = FName(TEXT("SafeFrame"));
	const FName BackgroundAudio = FName(TEXT("BackgroundAudio"));
	const FName MicEnabled = FName(TEXT("MicEnabled"));
	const FName TextChatEnabled = FName(TEXT("TextChatEnabled"));
	const FName PushToTalk = FName(TEXT("PushToTalk"));
}

FRHSettingsWidgetConfig::FRHSettingsWidgetConfig()
    : WidgetClass(URHSettingsWidget::StaticClass())
    , SettingInfo(URHSettingsInfoBase::StaticClass())
{}

const FText& URHSettingsContainerConfigAsset::GetSettingName() const
{
	return URHUIBlueprintFunctionLibrary::GetTextByPlatform(SettingName, SettingNameByPlatform);
}

const FText& URHSettingsContainerConfigAsset::GetSettingDescription() const
{
	return URHUIBlueprintFunctionLibrary::GetTextByPlatform(SettingDescription, SettingDescriptionByPlatform);
}

const FText& URHSettingsSectionConfigAsset::GetHeading() const
{
	return URHUIBlueprintFunctionLibrary::GetTextByPlatform(Heading, HeadingByPlatform);
}

const FText& URHSettingsPageConfigAsset::GetPageName() const
{
	return URHUIBlueprintFunctionLibrary::GetTextByPlatform(PageName, HeadingByPlatform);
}

void GeneratePropertyMap(TArray<FRHSettingPropertyId>& SettingPropertyIds, TMap<FName, int32>& SettingPropertyIdMap)
{
	for (const FRHSettingPropertyId& RHSettingPropertyId : SettingPropertyIds)
	{
		SettingPropertyIdMap.Add(RHSettingPropertyId.Name, RHSettingPropertyId.Id);
	}
}

URHSettingsDataFactory::URHSettingsDataFactory(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	FString SettingsMenuConfigAssetPathString;
	GConfig->GetString(TEXT("/Script/RallyHereStart.RHSettingsDataFactory"), TEXT("SettingsMenuConfigAssetPath"), SettingsMenuConfigAssetPathString, GGameIni);
	static ConstructorHelpers::FObjectFinder<URHSettingsMenuConfigAsset> SettingsMenuConfigAssetFinder(*SettingsMenuConfigAssetPathString);
	SettingsMenuConfigAsset = SettingsMenuConfigAssetFinder.Object;

	bNeedsSettingsFromRH = false;
}

void URHSettingsDataFactory::Initialize(class ARHHUDCommon* InHud)
{
    Super::Initialize(InHud);

    bNeedsSettingsFromRH = !IsLoggedIn();
	StoredCaseSets.Empty();

	// Register for application activated event (returning from suspension on consoles)
	AppReactivatedHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this, &URHSettingsDataFactory::AppReactivatedCallback);

	GeneratePropertyMap(BoolSettingPropertyIds, BoolSettingPropertyIdMap);
	GeneratePropertyMap(IntSettingPropertyIds, IntSettingPropertyIdMap);
	GeneratePropertyMap(FloatSettingPropertyIds, FloatSettingPropertyIdMap);

	TArray<FKey> OutKeys;
	EKeys::GetAllKeys(OutKeys);
	for (const auto& Key : OutKeys)
	{
		StringToKeyMap.Add(Key.ToString(), Key);
	}

	if (URHGameUserSettings* GameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		GameSettings->OnSettingsSaved().AddUObject(this, &URHSettingsDataFactory::OnGameUserSettingsSaved);
	}

	// perform INI upgrade if we allow generated inis
#if defined(DISABLE_GENERATED_INI_WHEN_COOKED) && !DISABLE_GENERATED_INI_WHEN_COOKED
	if (URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		const int32 CurrentIniVersionMajor = pGameSettings->GetSettingsVersionMajor();
		if (CurrentIniVersionMajor != CURRENT_VERSION_MAJOR_INI)
		{
			pGameSettings->SetSettingsVersionMajor(CURRENT_VERSION_MAJOR_INI);
			pGameSettings->SetSettingsVersionMinor(CURRENT_VERSION_MINOR_INI);

			// If we aren't at latest, we need to apply the defaults to Mcts.
			RevertPlayerPreferences();

			// If we aren't up to date, revert keybinds
			RevertKeyBindings();

			// New player test settings
			ApplySettingAsBool(TEXT("AutoShoulderSwapEnabled"), true);

			// Write the settings.
			SaveSettings();
		}
		else
		{
			const int32 CurrentIniVersionMinor = pGameSettings->GetSettingsVersionMinor();
			if (CurrentIniVersionMinor < CURRENT_VERSION_MINOR_INI)
			{
				pGameSettings->SetSettingsVersionMinor(CURRENT_VERSION_MINOR_INI);

				// Minor updates
				if (CurrentIniVersionMinor < 1)
				{
					RevertSettingToDefault(URHPlayerInput::MouseDoubleClickTime);
					RevertSettingToDefault(URHPlayerInput::MouseHoldTime);
					RevertSettingToDefault(URHPlayerInput::GamepadDoubleClickTime);
					RevertSettingToDefault(URHPlayerInput::GamepadHoldTime);
					RevertSettingToDefault(URHPlayerInput::GamepadAccelerationBoost);
					RevertSettingToDefault(URHPlayerInput::GamepadMultiplierBoost);
				}

				if (CurrentIniVersionMinor < 2)
				{
					RevertSettingToDefault(URHGameUserSettings::InputCrossPlayEnabled);
					RevertSettingToDefault(URHGameUserSettings::CrossPlayEnabled);
				}

				// Write the settings.
				SaveSettings();
			}
		}
	}
#endif
}

void URHSettingsDataFactory::Uninitialize()
{

}

void URHSettingsDataFactory::AppReactivatedCallback()
{
	UE_LOG(RallyHereStart, Log, TEXT("URHSettingsDataFactory::AppReactivatedCallback()"));
	DECLARE_CYCLE_STAT(TEXT("URHSettingsDataFactory::AppReactivatedCallback"), STAT_URHSettingsDataFactory_AppReactivatedCallback, STATGROUP_TaskGraphTasks);

	const FGraphEventRef Task = FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &URHSettingsDataFactory::AppReactivatedCallbackGameThread),
		GET_STATID(STAT_URHSettingsDataFactory_AppReactivatedCallback),
		nullptr,
		ENamedThreads::GameThread);
}

void URHSettingsDataFactory::AppReactivatedCallbackGameThread()
{
	if (GEngine != nullptr && GEngine->GameUserSettings != nullptr)
	{
		if (URHGameUserSettings* pRHGameUserSettings = Cast<URHGameUserSettings>(GEngine->GameUserSettings))
		{
			if (pRHGameUserSettings->SetSafeFrameScaleToSystemOverride())
			{
				ApplySettingAsFloat(URHGameUserSettings::SafeFrameScale, pRHGameUserSettings->GetSafeFrameScale());
			}
		}
	}
}

void URHSettingsDataFactory::OnGameUserSettingsSaved()
{
	if (URHGameUserSettings* GameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bool BoolValue;
		if (GameSettings->GetSettingAsBool(URHGameUserSettings::FPSEnabled, BoolValue))
		{
			SaveToProfileConfig(ConstConfigPropertyIds::FPS_ENABLED, BoolValue, ConstConfigPropertyGroups::PLAYER_TRACKING);
		}

		SaveToProfileConfig(ConstConfigPropertyIds::LANGUAGE_ID, GameSettings->GetCurrentLanguage(), ConstConfigPropertyGroups::PLAYER_TRACKING);
	}
}

void URHSettingsDataFactory::PostLogin()
{
    Super::PostLogin();

    UE_LOG(RallyHereStart, Verbose, TEXT("PostLogin()"));

	InitSettingsForPlayer();
}

void URHSettingsDataFactory::PostLogoff()
{
    Super::PostLogoff();

    bNeedsSettingsFromRH = true;
}

void URHSettingsDataFactory::GetSettingsFromPlayerAccount()
{
	if (IsLoggedIn() && bNeedsSettingsFromRH)
	{
        if (URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
        {
            pGameSettings->LoadSettings();
        }

		if (URH_PlayerInfo* LocalPlayerInfo = MyHud->GetLocalPlayerInfo())
		{
			LocalPlayerInfo->GetPlayerSettings("case", FTimespan(), true, FRH_PlayerInfoGetPlayerSettingsDelegate::CreateUObject(this, &URHSettingsDataFactory::OnGetPlayerCaseSetSettingsResponse));
		}
	}
    OnSettingsReceivedFromPlayerAccount.Broadcast();
}

void URHSettingsDataFactory::OnGetPlayerCaseSetSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& Response)
{
	TArray<FRHSettingConfigSet> UnpackagedCaseSettings;
	UnpackageCaseSettings(Response.Content, UnpackagedCaseSettings);
	UpdateStoredCaseSets(UnpackagedCaseSettings);

	// Create the config for the user so we can at least match settings version numbers to patch them if needed later on.
	TArray<FRHSettingConfigSet> PlayerPreferencesCaseSet;
	if (FindCaseFromSetsById(UnpackagedCaseSettings, ConstConfigPropertyGroups::PLAYER_PREFERENCES, 1, PlayerPreferencesCaseSet, true))
	{
		FRHSettingPropertyValue FoundVersionMajor;
		if (FindCasePropertyFromSetsById(PlayerPreferencesCaseSet, FString::FromInt(ConstSettingsPropertyIds::SETTINGS_VERSION_MAJOR), FoundVersionMajor))
		{
			if (FoundVersionMajor.IntVal != CURRENT_VERSION_MAJOR_CASE_SETS)
			{
				// Catch us up to the current version
				SaveToProfileConfig(ConstSettingsPropertyIds::SETTINGS_VERSION_MAJOR, CURRENT_VERSION_MAJOR_CASE_SETS);
				SaveToProfileConfig(ConstSettingsPropertyIds::SETTINGS_VERSION_MINOR, CURRENT_VERSION_MINOR_CASE_SETS);

				// Force revert to apply to local machine in-case another user played on machine.
				RevertPlayerPreferences();

				// If we aren't up to date, revert keybinds
				RevertKeyBindings();

				// New player test settings
				ApplySettingAsBool(TEXT("AutoShoulderSwapEnabled"), true);

				// Save these reverts locally (shouldn't save to mcts because it blocks defaults from saving).
				SaveSettings();
				bNeedsSettingsFromRH = false;
				OnSettingsReceivedFromPlayerAccount.Broadcast();
				return;
			}
			else
			{
				FRHSettingPropertyValue FoundVersionMinor;
				if (FindCasePropertyFromSetsById(PlayerPreferencesCaseSet, FString::FromInt(ConstSettingsPropertyIds::SETTINGS_VERSION_MINOR), FoundVersionMinor))
				{
					if (FoundVersionMinor.IntVal != CURRENT_VERSION_MINOR_CASE_SETS)
					{
						SaveToProfileConfig(ConstSettingsPropertyIds::SETTINGS_VERSION_MINOR, CURRENT_VERSION_MINOR_CASE_SETS);
						if (FoundVersionMajor.IntVal == 11)
						{
							if (FoundVersionMinor.IntVal < 1)
							{
								RevertSettingToDefault(URHPlayerInput::MouseDoubleClickTime);
								RevertSettingToDefault(URHPlayerInput::MouseHoldTime);
								RevertSettingToDefault(URHPlayerInput::GamepadDoubleClickTime);
								RevertSettingToDefault(URHPlayerInput::GamepadHoldTime);
								RevertSettingToDefault(URHPlayerInput::GamepadAccelerationBoost);
								RevertSettingToDefault(URHPlayerInput::GamepadMultiplierBoost);
							}
						}
					}
				}
			}
		}

		// Load all the settings in from mcts, if the user has no setting, revert it to default
		for (const TPair<FName, int32>& KeyValuePair : BoolSettingPropertyIdMap)
		{
			FRHSettingPropertyValue FoundProperty;
			if (FindCasePropertyFromSetsById(PlayerPreferencesCaseSet, FString::FromInt(KeyValuePair.Value), FoundProperty))
			{
				ApplySettingAsBool(KeyValuePair.Key, FoundProperty.IntVal > 0);
				SaveSettingAsBool(KeyValuePair.Key);
			}
			else
			{
				RevertSettingToDefault(KeyValuePair.Key);
			}
		}

		for (const TPair<FName, int32>& KeyValuePair : IntSettingPropertyIdMap)
		{
			FRHSettingPropertyValue FoundProperty;
			if (FindCasePropertyFromSetsById(PlayerPreferencesCaseSet, FString::FromInt(KeyValuePair.Value), FoundProperty))
			{
				ApplySettingAsInt(KeyValuePair.Key, FoundProperty.IntVal);
				SaveSettingAsInt(KeyValuePair.Key);
			}
			else
			{
				RevertSettingToDefault(KeyValuePair.Key);
			}
		}

		for (const TPair<FName, int32>& KeyValuePair : FloatSettingPropertyIdMap)
		{
			FRHSettingPropertyValue FoundProperty;
			if (FindCasePropertyFromSetsById(PlayerPreferencesCaseSet, FString::FromInt(KeyValuePair.Value), FoundProperty))
			{
				ApplySettingAsFloat(KeyValuePair.Key, FoundProperty.FloatVal);
				SaveSettingAsFloat(KeyValuePair.Key);
			}
			else
			{
				RevertSettingToDefault(KeyValuePair.Key);
			}
		}
	}

	if (URHPlayerInput* const PlayerInput = GetRHPlayerInput())
	{
		// Key bindings
		TArray<FRHSettingConfigSet> PlayerKeybindsCaseSet;
		if (FindCaseFromSetsById(UnpackagedCaseSettings, ConstConfigPropertyGroups::PLAYER_KEYBINDS, 1, PlayerKeybindsCaseSet, true))
		{
			FRHSettingPropertyValue FoundProperty;
			if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
			{
				for (const auto& CustomInputActionKey : DefaultUserSettings->CustomInputActionKeys)
				{
					bool bUseDefaults = true;
					if (FindCasePropertyFromSetsById(PlayerKeybindsCaseSet, FString::FromInt(CustomInputActionKey.PropId), FoundProperty) && !FoundProperty.StringVal.IsEmpty() && FoundProperty.StringVal.Len() > 0)
					{
						TArray<FRHInputActionKey> OutKBMKeys;
						TArray<FRHInputActionKey> OutGPKeys;

						StringToInputActionKeyBind(FoundProperty.StringVal, OutKBMKeys, OutGPKeys);

						if (OutKBMKeys.Num() > 0 || OutGPKeys.Num() > 0)
						{
							SetCustomInputActionKeyMapping(CustomInputActionKey.KeyboardName, OutKBMKeys, EInputType::KBM);
							SetCustomInputActionKeyMapping(CustomInputActionKey.GamepadName, OutGPKeys, EInputType::GP);
							bUseDefaults = false;
						}
					}

					if (bUseDefaults)
					{
						TArray<FRHInputActionKey> OutDefaultKBMKeys;
						TArray<FRHInputActionKey> OutDefaultGPKeys;
						GetDefaultRHInputActionKeys(CustomInputActionKey.KeyboardName, EInputType::KBM, OutDefaultKBMKeys);
						GetDefaultRHInputActionKeys(CustomInputActionKey.GamepadName, EInputType::GP, OutDefaultGPKeys);

						SetCustomInputActionKeyMapping(CustomInputActionKey.KeyboardName, OutDefaultKBMKeys, EInputType::KBM);
						SetCustomInputActionKeyMapping(CustomInputActionKey.GamepadName, OutDefaultGPKeys, EInputType::GP);
					}
				}

				for (const auto& CustomInputAxisKey : DefaultUserSettings->CustomInputAxisKeys)
				{
					bool bUseDefaults = true;
					if (FindCasePropertyFromSetsById(PlayerKeybindsCaseSet, FString::FromInt(CustomInputAxisKey.PropId), FoundProperty) && !FoundProperty.StringVal.IsEmpty() && FoundProperty.StringVal.Len() > 0)
					{
						TArray<FKey> OutKBMKeys;
						TArray<FKey> OutGPKeys;

						StringToInputAxisKeyBind(FoundProperty.StringVal, OutKBMKeys, OutGPKeys);

						if (OutKBMKeys.Num() > 0 || OutGPKeys.Num() > 0)
						{
							SetCustomInputAxisKeyMapping(CustomInputAxisKey.KeyboardName, OutKBMKeys, EInputType::KBM, CustomInputAxisKey.KeyboardScale);
							SetCustomInputAxisKeyMapping(CustomInputAxisKey.GamepadName, OutGPKeys, EInputType::GP, CustomInputAxisKey.GamepadScale);
							bUseDefaults = false;
						}
					}

					if (bUseDefaults)
					{
						TArray<FKey> OutDefaultKBMKeys;
						TArray<FKey> OutDefaultGPKeys;
						GetDefaultInputAxisKeys(CustomInputAxisKey.KeyboardName, EInputType::KBM, CustomInputAxisKey.KeyboardScale, OutDefaultKBMKeys);
						GetDefaultInputAxisKeys(CustomInputAxisKey.GamepadName, EInputType::GP, CustomInputAxisKey.GamepadScale, OutDefaultGPKeys);

						SetCustomInputAxisKeyMapping(CustomInputAxisKey.KeyboardName, OutDefaultKBMKeys, EInputType::KBM, CustomInputAxisKey.KeyboardScale);
						SetCustomInputAxisKeyMapping(CustomInputAxisKey.GamepadName, OutDefaultGPKeys, EInputType::GP, CustomInputAxisKey.GamepadScale);
					}
				}
			}
			SaveKeyBindings();
		}
		else
		{
			// This user has never saved keybindings, so clear it out incase another local user adjusted theirs.
			RevertKeyBindings();
		}
	}

	// When we get an update from MCTS, make sure to force save the new changes
	SaveSettings();
	bNeedsSettingsFromRH = false;
	OnSettingsReceivedFromPlayerAccount.Broadcast();
}

void URHSettingsDataFactory::SaveSettings()
{
    if (URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
    {
		pGameSettings->SaveSettings();
    }

    if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
    {
		SaveKeyBindings();
        RHPlayerInput->SaveConfig(CPF_Config, *GInputIni);
        GConfig->Flush(false, GInputIni);
    }
	
	// Push StoredCaseSets
	if (IsLoggedIn())
	{
		if (URH_PlayerInfo* LocalPlayerInfo = MyHud->GetLocalPlayerInfo())
		{
			TMap<FString, FSettingData> OutgoingSettingsContent;
			if (PackageCaseSettings(StoredCaseSets, OutgoingSettingsContent))
			{
				FRH_PlayerSettingsDataWrapper OutgoingData;
				OutgoingData.Content = OutgoingSettingsContent;
				LocalPlayerInfo->SetPlayerSettings("case", OutgoingData, FRH_PlayerInfoSetPlayerSettingsDelegate::CreateUObject(this, &URHSettingsDataFactory::OnSetPlayerCaseSetSettingsResponse));
			}
		}
	}
}

void URHSettingsDataFactory::OnSetPlayerCaseSetSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& ResponseData)
{
	if (bSuccess)
	{
		TArray<FRHSettingConfigSet> UnpackagedCaseSettings;
		UnpackageCaseSettings(ResponseData.Content, UnpackagedCaseSettings);
		UpdateStoredCaseSets(UnpackagedCaseSettings);
	}
}

bool URHSettingsDataFactory::GetSettingAsBool(FName Name, bool& OutBool) const
{
	bool bFoundSetting = false;
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->GetSettingAsBool(Name, OutBool);
	}
	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->GetSettingAsBool(Name, OutBool);
	}
	return bFoundSetting;
}

bool URHSettingsDataFactory::GetSettingAsInt(FName Name, int32& OutInt) const
{
	bool bFoundSetting = false;
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->GetSettingAsInt(Name, OutInt);
	}
	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->GetSettingAsInt(Name, OutInt);
	}
	return bFoundSetting;
}

bool URHSettingsDataFactory::GetSettingAsFloat(FName Name, float& OutFloat) const
{
	bool bFoundSetting = false;
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->GetSettingAsFloat(Name, OutFloat);
	}
	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->GetSettingAsFloat(Name, OutFloat);
	}
	return bFoundSetting;
}

void URHSettingsDataFactory::ApplySettingAsBool(FName Name, bool Value)
{
	bool bFoundSetting = false;
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->ApplySettingAsBool(Name, Value);
	}
	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->ApplySettingAsBool(Name, Value);
	}

	if (bFoundSetting)
	{
		if(FOnSettingUpdateMulticast* Del = SettingAppliedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
	}
}

void URHSettingsDataFactory::ApplySettingAsInt(FName Name, int32 Value)
{
	bool bFoundSetting = false;
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->ApplySettingAsInt(Name, Value);
	}
	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->ApplySettingAsInt(Name, Value);
	}

	if (bFoundSetting)
	{
		if (FOnSettingUpdateMulticast* Del = SettingAppliedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
	}
}

void URHSettingsDataFactory::ApplySettingAsFloat(FName Name, float Value)
{
	bool bFoundSetting = false;
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->ApplySettingAsFloat(Name, Value);
	}
	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->ApplySettingAsFloat(Name, Value);
	}

	if (bFoundSetting)
	{
		if (FOnSettingUpdateMulticast* Del = SettingAppliedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
	}
}

void URHSettingsDataFactory::SaveSettingAsBool(FName Name)
{
	bool bFoundSetting = false;
	bool OutValue = false;
	bool OutDefaultValue = false;

	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->SaveSetting(Name) && pRHGameSettings->GetSettingAsBool(Name, OutValue);
		pRHGameSettings->GetDefaultSettingAsBool(Name, OutDefaultValue);
	}

	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->SaveSetting(Name) && pRHPlayerInput->GetSettingAsBool(Name, OutValue);
		pRHPlayerInput->GetDefaultSettingAsBool(Name, OutDefaultValue);
	}

	if (bFoundSetting)
	{
		if (FOnSettingUpdateMulticast* Del = SettingSavedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
		if (BoolSettingPropertyIdMap.Contains(Name))
		{
			if (HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_PREFERENCES, FString::FromInt(BoolSettingPropertyIdMap[Name])) || OutValue != OutDefaultValue)
			{
				SaveToProfileConfig(BoolSettingPropertyIdMap[Name], OutValue);
			}
		}
	}
}

void URHSettingsDataFactory::SaveSettingAsInt(FName Name)
{
	bool bFoundSetting = false;
	int32 OutValue = 0;
	int32 OutDefaultValue = 0;

	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->SaveSetting(Name) && pRHGameSettings->GetSettingAsInt(Name, OutValue);
		pRHGameSettings->GetDefaultSettingAsInt(Name, OutDefaultValue);
	}

	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->SaveSetting(Name) && pRHPlayerInput->GetSettingAsInt(Name, OutValue);
		pRHPlayerInput->GetDefaultSettingAsInt(Name, OutDefaultValue);
	}

	if (bFoundSetting)
	{
		if (FOnSettingUpdateMulticast* Del = SettingSavedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
		if (IntSettingPropertyIdMap.Contains(Name))
		{
			if (HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_PREFERENCES, FString::FromInt(IntSettingPropertyIdMap[Name])) || OutValue != OutDefaultValue)
			{
				SaveToProfileConfig(IntSettingPropertyIdMap[Name], OutValue);
			}
		}
	}
}

void URHSettingsDataFactory::SaveSettingAsFloat(FName Name)
{
	bool bFoundSetting = false;
	float OutValue = 0.f;
	float OutDefaultValue = 0.f;

	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->SaveSetting(Name) && pRHGameSettings->GetSettingAsFloat(Name, OutValue);
		pRHGameSettings->GetDefaultSettingAsFloat(Name, OutDefaultValue);
	}

	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->SaveSetting(Name) && pRHPlayerInput->GetSettingAsFloat(Name, OutValue);
		pRHPlayerInput->GetDefaultSettingAsFloat(Name, OutDefaultValue);
	}

	if (bFoundSetting)
	{
		if (FOnSettingUpdateMulticast* Del = SettingSavedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
		if (FloatSettingPropertyIdMap.Contains(Name))
		{
			if (HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_PREFERENCES, FString::FromInt(FloatSettingPropertyIdMap[Name])) || OutValue != OutDefaultValue)
			{
				SaveToProfileConfig(FloatSettingPropertyIdMap[Name], OutValue);
			}
		}
	}
}

void URHSettingsDataFactory::RevertSettingToDefault(FName Name)
{
	bool bFoundSetting = false;
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		bFoundSetting |= pRHGameSettings->RevertSettingToDefault(Name);
		pRHGameSettings->SaveSetting(Name);
	}

	if (URHPlayerInput* const pRHPlayerInput = GetRHPlayerInput())
	{
		bFoundSetting |= pRHPlayerInput->RevertSettingToDefault(Name);
		pRHPlayerInput->SaveSetting(Name);
	}

	if (bFoundSetting)
	{
		if (FOnSettingUpdateMulticast* Del = SettingAppliedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
		if (FOnSettingUpdateMulticast* Del = SettingSavedDelMap.Find(Name))
		{
			Del->Broadcast();
		}
		if (BoolSettingPropertyIdMap.Contains(Name) && HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_PREFERENCES, FString::FromInt(BoolSettingPropertyIdMap[Name])))
		{
			bool OutBool = false;
			if (GetSettingAsBool(Name, OutBool))
			{
				SaveToProfileConfig(BoolSettingPropertyIdMap[Name], OutBool);
			}
		}
		if (IntSettingPropertyIdMap.Contains(Name) && HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_PREFERENCES, FString::FromInt(IntSettingPropertyIdMap[Name])))
		{
			int32 OutInt = 0;
			if (GetSettingAsInt(Name, OutInt))
			{
				SaveToProfileConfig(IntSettingPropertyIdMap[Name], OutInt);
			}
		}
		if (FloatSettingPropertyIdMap.Contains(Name) && HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_PREFERENCES, FString::FromInt(FloatSettingPropertyIdMap[Name])))
		{
			float OutFloat = 0.f;
			if (GetSettingAsFloat(Name, OutFloat))
			{
				SaveToProfileConfig(FloatSettingPropertyIdMap[Name], OutFloat);
			}
		}
	}
}

void URHSettingsDataFactory::BindSettingCallbacks(FName Name, const FSettingDelegateStruct& SettingDelegateStruct)
{
	if(SettingDelegateStruct.SettingApplied.IsBound())
	{
		SettingAppliedDelMap.FindOrAdd(Name).AddUnique(SettingDelegateStruct.SettingApplied);
	}

	if(SettingDelegateStruct.SettingSaved.IsBound())
	{
		SettingSavedDelMap.FindOrAdd(Name).AddUnique(SettingDelegateStruct.SettingSaved);
	}
}

void URHSettingsDataFactory::SaveKeyBindings()
{
    SaveKeybindsToProfileConfig();

    if(URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
    {
		RHPlayerInput->SaveKeyMappings();
        for (const auto& EditableKeybindName : EditableKeybindNames)
        {
            OnKeyBindSettingsSaved.Broadcast(EditableKeybindName);
        }
    }
}

void URHSettingsDataFactory::RevertKeyBindings()
{
	if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
	{
		RHPlayerInput->SetCustomKeyMappingsToDefault();
	}
	SaveKeyBindings();
}

void URHSettingsDataFactory::AddEditableKeybindName(FName EditableKeybindName)
{
    EditableKeybindNames.AddUnique(EditableKeybindName);
}

void URHSettingsDataFactory::GetDefaultRHInputActionKeys(const FName& Name, EInputType InputType, TArray<FRHInputActionKey>& OutKeys) const
{
	if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
	{
		RHPlayerInput->GetDefaultInputActionKeys(Name, InputType, OutKeys);
	}
}

void URHSettingsDataFactory::GetDefaultInputActionKeys(const FName& Name, EInputType InputType, TArray<FKey>& OutKeys) const
{
	if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
	{
		TArray<FRHInputActionKey> OutRHKeys;
		RHPlayerInput->GetDefaultInputActionKeys(Name, InputType, OutRHKeys);
		for (const FRHInputActionKey& InputActionKey : OutRHKeys)
		{
			OutKeys.Add(InputActionKey.Key);
		}
	}
}

void URHSettingsDataFactory::GetCustomRHInputActionKeys(FName Name, EInputType InputType, TArray<FRHInputActionKey>& OutKeys) const
{
	if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
	{
		RHPlayerInput->GetCustomInputActionKeys(Name, InputType, OutKeys);
	}
}

void URHSettingsDataFactory::GetCustomInputActionKeys(FName Name, EInputType InputType, TArray<FKey>& OutKeys) const
{
    if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
    {
		TArray<FRHInputActionKey> OutRHKeys;
        RHPlayerInput->GetCustomInputActionKeys(Name, InputType, OutRHKeys);
		for (const FRHInputActionKey& InputActionKey : OutRHKeys)
		{
			OutKeys.Add(InputActionKey.Key);
		}
    }
}

void URHSettingsDataFactory::GetDefaultInputAxisKeys(const FName& Name, EInputType InputType, float Scale, TArray<FKey>& OutKeys) const
{
	if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
	{
		RHPlayerInput->GetDefaultInputAxisKeys(Name, InputType, Scale, OutKeys);
	}
}

void URHSettingsDataFactory::GetCustomInputAxisKeys(FName Name, EInputType InputType, float Scale, TArray<FKey>& OutKeys) const
{
    if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
    {
        RHPlayerInput->GetCustomInputAxisKeys(Name, InputType, Scale, OutKeys);
    }
}

void URHSettingsDataFactory::SetCustomInputActionKeyMapping(FName Name, const TArray<FRHInputActionKey>& Keys, EInputType InputType)
{
    if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
    {
        TArray<FName> OutModifiedKeybindNames;
        RHPlayerInput->SetCustomInputActionKeyMapping(Name, Keys, InputType, OutModifiedKeybindNames);
        for (const auto& ModifiedKeybindName : OutModifiedKeybindNames)
        {
            OnKeyBindSettingsApplied.Broadcast(ModifiedKeybindName);
        }
    }
}

void URHSettingsDataFactory::SetCustomInputAxisKeyMapping(FName Name, const TArray<FKey>& Keys, EInputType InputType, float Scale)
{
    if (URHPlayerInput* const RHPlayerInput = GetRHPlayerInput())
    {
        TArray<FName> OutModifiedKeybindNames;
        RHPlayerInput->SetCustomInputAxisKeyMapping(Name, Keys, InputType, Scale, OutModifiedKeybindNames);
        for (const auto& ModifiedKeybindName : OutModifiedKeybindNames)
        {
            OnKeyBindSettingsApplied.Broadcast(ModifiedKeybindName);
        }
    }
}

URHPlayerInput* URHSettingsDataFactory::GetRHPlayerInput() const
{
    if (const ARHHUDCommon* const pRHHUDCommon = Cast<ARHHUDCommon>(MyHud))
    {
        if (const APlayerController* const pPlayerController = pRHHUDCommon->GetOwningPlayerController())
        {
            if (URHPlayerInput* const pRHPlayerInput = Cast<URHPlayerInput>(pPlayerController->PlayerInput))
            {
                return pRHPlayerInput;
            }
        }
    }
    return nullptr;
}

bool URHSettingsDataFactory::IsUserLoggedIn()
{
	return IsLoggedIn();
}

void URHSettingsDataFactory::RevertPlayerPreferences()
{
	for (const TPair<FName, int32>& KeyValuePair : BoolSettingPropertyIdMap)
	{
		RevertSettingToDefault(KeyValuePair.Key);
		SaveSettingAsBool(KeyValuePair.Key);
	}
	for (const TPair<FName, int32>& KeyValuePair : IntSettingPropertyIdMap)
	{
		RevertSettingToDefault(KeyValuePair.Key);
		SaveSettingAsInt(KeyValuePair.Key);
	}
	for (const TPair<FName, int32>& KeyValuePair : FloatSettingPropertyIdMap)
	{
		RevertSettingToDefault(KeyValuePair.Key);
		SaveSettingAsFloat(KeyValuePair.Key);
	}
}

void URHSettingsDataFactory::InitSettingsForPlayer()
{
    GetSettingsFromPlayerAccount();
}

void URHSettingsDataFactory::SaveToProfileConfig(int32 PropId, int32 SettingValue, int32 PropGroup /*= ConstConfigPropertyGroups::PLAYER_PREFERENCES*/)
{
	FRHSettingConfigSet NewConfigSet;
	NewConfigSet.ConfigSetId = PropGroup;
	NewConfigSet.V = 1;

	FRHSettingPropertyValue NewProperty;
	NewProperty.IntVal = SettingValue;
	NewProperty.bIsSet = true;

	NewConfigSet.PropertiesById.Add(FString::FromInt(PropId), NewProperty);

	TArray<FRHSettingConfigSet> ConfigToAdd;
	ConfigToAdd.Add(NewConfigSet);
	UpdateStoredCaseSets(ConfigToAdd);
}

void URHSettingsDataFactory::SaveToProfileConfig(int32 PropId, float SettingValue, int32 PropGroup /*= ConstConfigPropertyGroups::PLAYER_PREFERENCES*/)
{
	FRHSettingConfigSet NewConfigSet;
	NewConfigSet.ConfigSetId = PropGroup;
	NewConfigSet.V = 1;

	FRHSettingPropertyValue NewProperty;
	NewProperty.FloatVal = SettingValue;
	NewProperty.bIsSet = true;

	NewConfigSet.PropertiesById.Add(FString::FromInt(PropId), NewProperty);

	TArray<FRHSettingConfigSet> ConfigToAdd;
	ConfigToAdd.Add(NewConfigSet);
	UpdateStoredCaseSets(ConfigToAdd);
}

void URHSettingsDataFactory::SaveToProfileConfig(int32 PropId, FString SettingValue, int32 PropGroup /*= ConstConfigPropertyGroups::PLAYER_PREFERENCES*/)
{
	FRHSettingConfigSet NewConfigSet;
	NewConfigSet.ConfigSetId = PropGroup;
	NewConfigSet.V = 1;

	FRHSettingPropertyValue NewProperty;
	NewProperty.StringVal = SettingValue;
	NewProperty.bIsSet = true;

	NewConfigSet.PropertiesById.Add(FString::FromInt(PropId), NewProperty);

	TArray<FRHSettingConfigSet> ConfigToAdd;
	ConfigToAdd.Add(NewConfigSet);
	UpdateStoredCaseSets(ConfigToAdd);
}

void URHSettingsDataFactory::SaveKeybindsToProfileConfig()
{
	if (IsLoggedIn())
	{
		if (URHPlayerInput* pRHPlayerInput = GetRHPlayerInput())
		{
			if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
			{
				for (const auto& CustomInputActionKey : DefaultUserSettings->CustomInputActionKeys)
				{
					TArray<FRHInputActionKey> OutKBMKeys;
					TArray<FRHInputActionKey> OutGPKeys;
					GetCustomRHInputActionKeys(CustomInputActionKey.KeyboardName, EInputType::KBM, OutKBMKeys);
					GetCustomRHInputActionKeys(CustomInputActionKey.GamepadName, EInputType::GP, OutGPKeys);
					const FString KeybindingString = InputActionKeyBindToString(OutKBMKeys, OutGPKeys);

					TArray<FRHInputActionKey> OutDefaultKBMKeys;
					TArray<FRHInputActionKey> OutDefaultGPKeys;
					GetDefaultRHInputActionKeys(CustomInputActionKey.KeyboardName, EInputType::KBM, OutDefaultKBMKeys);
					GetDefaultRHInputActionKeys(CustomInputActionKey.GamepadName, EInputType::GP, OutDefaultGPKeys);
					const FString DefaultKeybindingString = InputActionKeyBindToString(OutDefaultKBMKeys, OutDefaultGPKeys);

					if (HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_KEYBINDS, FString::FromInt(CustomInputActionKey.PropId)) || KeybindingString != DefaultKeybindingString)
					{
						SaveToProfileConfig(CustomInputActionKey.PropId, KeybindingString, ConstConfigPropertyGroups::PLAYER_KEYBINDS);
					}
				}

				for (const auto& CustomInputAxisKey : DefaultUserSettings->CustomInputAxisKeys)
				{
					TArray<FKey> OutKBMKeys;
					TArray<FKey> OutGPKeys;
					GetCustomInputAxisKeys(CustomInputAxisKey.KeyboardName, EInputType::KBM, CustomInputAxisKey.KeyboardScale, OutKBMKeys);
					GetCustomInputAxisKeys(CustomInputAxisKey.GamepadName, EInputType::GP, CustomInputAxisKey.GamepadScale, OutGPKeys);
					const FString KeybindingString = InputAxisKeyBindToString(OutKBMKeys, OutGPKeys);

					TArray<FKey> OutDefaultKBMKeys;
					TArray<FKey> OutDefaultGPKeys;
					GetDefaultInputAxisKeys(CustomInputAxisKey.KeyboardName, EInputType::KBM, CustomInputAxisKey.KeyboardScale, OutDefaultKBMKeys);
					GetDefaultInputAxisKeys(CustomInputAxisKey.GamepadName, EInputType::GP, CustomInputAxisKey.GamepadScale, OutDefaultGPKeys);
					const FString DefaultKeybindingString = InputAxisKeyBindToString(OutDefaultKBMKeys, OutDefaultGPKeys);

					if (HasStoredPropertyById(ConstConfigPropertyGroups::PLAYER_KEYBINDS, FString::FromInt(CustomInputAxisKey.PropId)) || KeybindingString != DefaultKeybindingString)
					{
						SaveToProfileConfig(CustomInputAxisKey.PropId, KeybindingString, ConstConfigPropertyGroups::PLAYER_KEYBINDS);
					}
				}
			}
		}
	}
}

FString URHSettingsDataFactory::InputActionKeyBindToString(const TArray<FRHInputActionKey>& KBMKeys, const TArray<FRHInputActionKey>& GPKeys) const
{
	const FRHInputActionKey KBM1Key = KBMKeys.IsValidIndex(0) ? KBMKeys[0] : FRHInputActionKey();
	const FRHInputActionKey KBM2Key = KBMKeys.IsValidIndex(1) ? KBMKeys[1] : FRHInputActionKey();
	const FRHInputActionKey GPKey = GPKeys.IsValidIndex(0) ? GPKeys[0] : FRHInputActionKey();
	const FRHInputActionKey ComboKey = GPKeys.IsValidIndex(1) ? GPKeys[1] : FRHInputActionKey();
	FString ToReturn = FString::Format(TEXT("{0},{1},{2},{3}"), { KBM1Key.Key.ToString(), KBM2Key.Key.ToString(), GPKey.Key.ToString(), ComboKey.Key.ToString() });
	if (KBM1Key.Type != EInputActionType::Press || KBM2Key.Type != EInputActionType::Press || GPKey.Type != EInputActionType::Press)
	{
		ToReturn.Append(FString::Format(TEXT(",{0},{1},{2}"),{
			FString::FromInt((int32)KBM1Key.Type),
			FString::FromInt((int32)KBM2Key.Type),
			FString::FromInt((int32)GPKey.Type)
		}));
	}
	return ToReturn;
}

void URHSettingsDataFactory::StringToInputActionKeyBind(const FString& KeybindString, TArray<FRHInputActionKey>& OutKBMKeys, TArray<FRHInputActionKey>& OutGPKeys) const
{
	TArray<FString> KeybindStrings;
	KeybindString.ParseIntoArray(KeybindStrings, TEXT(","), true);

	if (KeybindStrings.Num() >= 4)
	{
		if (StringToKeyMap.Contains(KeybindStrings[0]))
		{
			OutKBMKeys.Emplace(StringToKeyMap[KeybindStrings[0]], EInputActionType::Press);
		}
		if (StringToKeyMap.Contains(KeybindStrings[1]))
		{
			OutKBMKeys.Emplace(StringToKeyMap[KeybindStrings[1]], EInputActionType::Press);
		}
		if (StringToKeyMap.Contains(KeybindStrings[2]))
		{
			OutGPKeys.Emplace(StringToKeyMap[KeybindStrings[2]], EInputActionType::Press);
		}
		if (StringToKeyMap.Contains(KeybindStrings[3]))
		{
			OutGPKeys.Emplace(StringToKeyMap[KeybindStrings[3]], EInputActionType::Press);
		}
	}
	// If there are 7, we have press/hold/double specified, so read in for KBM primary, KBM secondary, and GP combo.
	if (KeybindStrings.Num() == 7)
	{
		TMap<FString, EInputActionType> StringToInputActionEnumMap;
		StringToInputActionEnumMap.Add(FString::FromInt((int32)(EInputActionType::Press)), EInputActionType::Press);
		StringToInputActionEnumMap.Add(FString::FromInt((int32)(EInputActionType::Hold)), EInputActionType::Hold);
		StringToInputActionEnumMap.Add(FString::FromInt((int32)(EInputActionType::Repeat)), EInputActionType::Repeat);

		if (OutKBMKeys.Num() > 0 && StringToInputActionEnumMap.Contains(KeybindStrings[4]))
		{
			OutKBMKeys[0].Type = StringToInputActionEnumMap[KeybindStrings[4]];
		}
		if (OutKBMKeys.Num() > 1 && StringToInputActionEnumMap.Contains(KeybindStrings[5]))
		{
			OutKBMKeys[1].Type = StringToInputActionEnumMap[KeybindStrings[5]];
		}
		if (StringToInputActionEnumMap.Contains(KeybindStrings[6]))
		{
			for (FRHInputActionKey& InputActionKey : OutGPKeys)
			{
				InputActionKey.Type = StringToInputActionEnumMap[KeybindStrings[6]];
			}
		}
	}
}

FString URHSettingsDataFactory::InputAxisKeyBindToString(const TArray<FKey>& KBMKeys, const TArray<FKey>& GPKeys) const
{
	const FKey KBM1Key = KBMKeys.IsValidIndex(0) ? KBMKeys[0] : EKeys::Invalid;
	const FKey KBM2Key = KBMKeys.IsValidIndex(1) ? KBMKeys[1] : EKeys::Invalid;
	const FKey GPKey = GPKeys.IsValidIndex(0) ? GPKeys[0] : EKeys::Invalid;
	const FKey ComboKey = GPKeys.IsValidIndex(1) ? GPKeys[1] : EKeys::Invalid;
	return FString::Format(TEXT("{0},{1},{2},{3}"), { KBM1Key.ToString(), KBM2Key.ToString(), GPKey.ToString(), ComboKey.ToString() });
}

void URHSettingsDataFactory::StringToInputAxisKeyBind(const FString& KeybindString, TArray<FKey>& OutKBMKeys, TArray<FKey>& OutGPKeys) const
{
	TArray<FString> KeybindStrings;
	KeybindString.ParseIntoArray(KeybindStrings, TEXT(","), true);

	if (KeybindStrings.Num() == 4)
	{
		if (StringToKeyMap.Contains(KeybindStrings[0]))
		{
			OutKBMKeys.Add(StringToKeyMap[KeybindStrings[0]]);
		}
		if (StringToKeyMap.Contains(KeybindStrings[1]))
		{
			OutKBMKeys.Add(StringToKeyMap[KeybindStrings[1]]);
		}
		if (StringToKeyMap.Contains(KeybindStrings[2]))
		{
			OutGPKeys.Add(StringToKeyMap[KeybindStrings[2]]);
		}
		if (StringToKeyMap.Contains(KeybindStrings[3]))
		{
			OutGPKeys.Add(StringToKeyMap[KeybindStrings[3]]);
		}
	}
}

TArray<FString> URHSettingsDataFactory::GetAvailableLanguages() const
{
    return FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Game);
}

FString URHSettingsDataFactory::GetCurrentLanguage() const
{
    if (URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
    {
        return pGameSettings->GetCurrentLanguage();
    }
    return FString();
}

void URHSettingsDataFactory::ApplyLanguage(FString LanguageCulture)
{
    if (URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
    {
        pGameSettings->SetCurrentLanguage(LanguageCulture);
        OnDisplayLanguageApplied.Broadcast(LanguageCulture);
    }
}

void URHSettingsDataFactory::SaveLanguage()
{
    if (URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
    {
        pGameSettings->SaveCurrentLanguage();
        SaveToProfileConfig(ConstConfigPropertyIds::LANGUAGE_ID, pGameSettings->GetCurrentLanguage(), ConstConfigPropertyGroups::PLAYER_TRACKING);
        OnDisplayLanguageSaved.Broadcast(pGameSettings->GetCurrentLanguage());
    }
}

void URHSettingsDataFactory::RevertLanguageToDefault()
{
	if (URHGameUserSettings* const pGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		FString UserLocale = FPlatformMisc::GetDefaultLocale();
		TArray<FString> AvailableLangs = GetAvailableLanguages();
		if (AvailableLangs.Contains(UserLocale))
		{
			pGameSettings->SetCurrentLanguage(UserLocale);
		}
		else
		{
			// If UserLocale = "en-US" then Main = "en" and Sub = "US"
			FString Main, Sub;
			if (UserLocale.Split("-", &Main, &Sub) && AvailableLangs.Contains(Main))
			{
				pGameSettings->SetCurrentLanguage(Main);
			}
			else
			{
				pGameSettings->SetCurrentLanguage(TEXT("en"));
			}
		}

		ApplyLanguage(pGameSettings->GetCurrentLanguage());
		SaveLanguage();
	}
}

FIntPoint URHSettingsDataFactory::GetScreenResolution() const
{
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		return pRHGameSettings->GetScreenResolution();
	}

	return FIntPoint();
}

void URHSettingsDataFactory::ApplyScreenResolution(FIntPoint ScreenResolution)
{
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		pRHGameSettings->SetScreenResolution(ScreenResolution);
		pRHGameSettings->ApplyResolutionSettings(true);
		OnScreenResolutionApplied.Broadcast(ScreenResolution);
	}
}

void URHSettingsDataFactory::SaveScreenResolution()
{
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		const FIntPoint SettingValue = pRHGameSettings->GetScreenResolution();
		pRHGameSettings->SaveScreenResolution();
		OnScreenResolutionSaved.Broadcast(SettingValue);
	}
}

void URHSettingsDataFactory::RevertScreenResolution()
{
	if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
	{
		pRHGameSettings->RevertScreenResolution();
		const FIntPoint DefaultScreenResolution = GetScreenResolution();
		ApplyScreenResolution(DefaultScreenResolution);
		SaveScreenResolution();
	}
}

FString URHSettingsDataFactory::GetSelectedRegion() const
{
    if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
    {
        return pRHGameSettings->GetSelectedRegion();
    }

    // empty is by default an invalid region
    return FString();
}

bool URHSettingsDataFactory::SetSelectedRegion(const FString& RegionId)
{
    if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
    {
        pRHGameSettings->SaveSelectedRegion(RegionId);
        return true;
    }

    return false;
}

bool URHSettingsDataFactory::PackageCaseSettings(const TMap<int32, FRHSettingConfigSet>& InCaseSets, TMap<FString, FSettingData>& OutSettingsContent)
{
	OutSettingsContent.Empty();

	if (InCaseSets.Num() > 0)
	{
		for (auto pair : InCaseSets)
		{
			FSettingData PackagedData;
			if (PackageCaseSetting(pair.Value, PackagedData))
			{
				OutSettingsContent.Add(pair.Value.CaseId, PackagedData);
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHSettingsDataFactory::PackageCaseSettings failed to Package Case Set with Id=%s"), *(pair.Value.CaseId));
			}
		}
	}

	return OutSettingsContent.Num() > 0;
}

bool URHSettingsDataFactory::PackageCaseSetting(const FRHSettingConfigSet& InCaseSet, FSettingData& OutSettingData)
{
	TSharedPtr<FJsonObject> NewObject = MakeShared<FJsonObject>();
	TSharedPtr<FJsonValueObject> NewValueObject = MakeShared<FJsonValueObject>(NewObject);

	if (FJsonObject* ConfigSetObject = NewObject.Get())
	{
		ConfigSetObject->SetNumberField("config_set_id", InCaseSet.ConfigSetId);
		TSharedPtr<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();

		for (const auto& PropertyPair : InCaseSet.PropertiesById)
		{
			if (!PropertyPair.Key.IsEmpty() && PropertyPair.Value.bIsSet)
			{
				TSharedPtr<FJsonObject> NewPropertyObject = MakeShared<FJsonObject>();
				TSharedPtr<FJsonValueObject> NewPropertyValueObject = MakeShared<FJsonValueObject>(NewPropertyObject);
				if (FJsonObject* PropertyObject = NewPropertyObject.Get())
				{
					if (PropertyPair.Value.IntVal >= 0)
					{
						PropertyObject->SetNumberField("int_value", PropertyPair.Value.IntVal);

					}
					if (!PropertyPair.Value.StringVal.IsEmpty())
					{
						PropertyObject->SetStringField("string_value", PropertyPair.Value.StringVal);

					}
					if (PropertyPair.Value.FloatVal >= 0)
					{
						PropertyObject->SetNumberField("float_value", PropertyPair.Value.FloatVal);

					}

					PropertiesObject->Values.Add(PropertyPair.Key, NewPropertyValueObject);
				}
			}
		}

		ConfigSetObject->SetObjectField("props", PropertiesObject);
	}

	OutSettingData.V = InCaseSet.V;
	OutSettingData.SetValue(FRHAPI_JsonValue::CreateFromUnrealValue(NewValueObject));
	const auto NewValue = OutSettingData.GetValueOrNull();
	return NewValue && NewValue->GetValue().IsValid();
}

bool URHSettingsDataFactory::UnpackageCaseSettings(const TMap<FString, FSettingData>& InSettingsContent, TArray<FRHSettingConfigSet>& OutCaseSets)
{
	OutCaseSets.Empty();

	if (InSettingsContent.Num() > 0)
	{
		for (const auto& tuple : InSettingsContent)
		{
			FRHSettingConfigSet UnpackagedCaseSet;
			if (UnpackageCaseSetting(tuple.Key, tuple.Value, UnpackagedCaseSet))
			{
				OutCaseSets.Add(UnpackagedCaseSet);
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHSettingsDataFactory::UnpackageCaseSettings failed to Unpackage Case Set with Id=%s"), *(tuple.Key));
			}
		}
	}

	return OutCaseSets.Num() > 0;
}

bool URHSettingsDataFactory::UnpackageCaseSetting(const FString& InCaseId, const FSettingData& InSettingData, FRHSettingConfigSet& OutCaseSet)
{
	const auto Value = InSettingData.GetValueOrNull();
	if (Value == nullptr)
	{
		return false;
	}
	
	const TSharedPtr<FJsonObject>* CaseSetData = nullptr;
	if (Value->GetValue().Get()->TryGetObject(CaseSetData))
	{
		if (const FJsonObject* CaseSetObject = CaseSetData->Get())
		{
			OutCaseSet.CaseId = InCaseId;
			OutCaseSet.V = InSettingData.V;
			CaseSetObject->TryGetNumberField("config_set_id", OutCaseSet.ConfigSetId);
			
			const TSharedPtr<FJsonObject>* PropertiesData = nullptr;
			if (CaseSetObject->TryGetObjectField("props", PropertiesData))
			{
				if (const FJsonObject* PropertiesObject = PropertiesData->Get())
				{
					for (const auto& pair : PropertiesObject->Values)
					{
						const TSharedPtr<FJsonObject>* PropertyData = nullptr;
						if (pair.Value.Get()->TryGetObject(PropertyData))
						{
							if (const FJsonObject* PropertyObject = PropertyData->Get())
							{
								FRHSettingPropertyValue NewPropertyValue;

								if (PropertyObject->TryGetNumberField("int_value", NewPropertyValue.IntVal))
								{
									NewPropertyValue.bIsSet = true;
								}
								if (PropertyObject->TryGetStringField("string_value", NewPropertyValue.StringVal))
								{
									NewPropertyValue.bIsSet = true;
								}
								if (PropertyObject->TryGetNumberField("float_value", NewPropertyValue.FloatVal))
								{
									NewPropertyValue.bIsSet = true;
								}

								OutCaseSet.PropertiesById.Add(pair.Key, NewPropertyValue);
							}
						}
					}
				}
			}
		}
	}

	return OutCaseSet.CaseId == InCaseId;
}

bool URHSettingsDataFactory::FindCaseFromSetsById(const TArray<FRHSettingConfigSet>& InCaseSets, const int32& InConfigSetId, const int32& InV, TArray<FRHSettingConfigSet>& OutFoundCaseSets, bool bCreateIfNeeded)
{
	OutFoundCaseSets.Empty();

	for (const FRHSettingConfigSet& CaseSet : InCaseSets)
	{
		if (CaseSet.ConfigSetId == InConfigSetId && CaseSet.V == InV)
		{
			OutFoundCaseSets.Add(CaseSet);
		}
	}

	if (OutFoundCaseSets.Num() <= 0 && bCreateIfNeeded)
	{
		FRHSettingConfigSet NewConfigSet;
		NewConfigSet.CaseId = "create";
		NewConfigSet.V = InV;
		NewConfigSet.ConfigSetId = InConfigSetId;
		OutFoundCaseSets.Add(NewConfigSet);
	}

	return OutFoundCaseSets.Num() > 0;
}

bool URHSettingsDataFactory::FindCasePropertyFromSetsById(const TArray<FRHSettingConfigSet>& InCaseSets, const FString& InPropertyId, FRHSettingPropertyValue& OutFoundProperty)
{
	for (const FRHSettingConfigSet& CaseSet : InCaseSets)
	{
		for (const auto& pair : CaseSet.PropertiesById)
		{
			if (pair.Key == InPropertyId && pair.Value.bIsSet)
			{
				OutFoundProperty = pair.Value;
				return true;
			}
		}
	}

	return false;
}

bool URHSettingsDataFactory::HasStoredCaseSetById(const int32& SetId)
{
	return StoredCaseSets.Contains(SetId);
}

bool URHSettingsDataFactory::HasStoredPropertyById(const int32& SetId, const FString& PropId)
{
	if (auto FoundCaseSet = StoredCaseSets.Find(SetId))
	{
		return FoundCaseSet->PropertiesById.Contains(PropId);
	}

	return false;
}

void URHSettingsDataFactory::UpdateStoredCaseSets(const TArray<FRHSettingConfigSet>& NewCaseSets)
{
	for (FRHSettingConfigSet NewCaseSet : NewCaseSets)
	{
		if (!StoredCaseSets.Contains(NewCaseSet.ConfigSetId))
		{
			NewCaseSet.CaseId = NewCaseSet.CaseId.IsEmpty() ? "create" : NewCaseSet.CaseId;
			StoredCaseSets.Add(NewCaseSet.ConfigSetId, NewCaseSet);
			continue;
		}

		if (auto StoredCaseSet = StoredCaseSets.Find(NewCaseSet.ConfigSetId))
		{
			// Sync CaseIds
			const bool bStoredCaseIdMissing = StoredCaseSet->CaseId.IsEmpty() || StoredCaseSet->CaseId == "create";
			const bool bNewCaseIdMissing = NewCaseSet.CaseId.IsEmpty() || NewCaseSet.CaseId == "create";
			StoredCaseSet->CaseId = bStoredCaseIdMissing && !bNewCaseIdMissing ? NewCaseSet.CaseId : StoredCaseSet->CaseId;

			for (const auto& pair : NewCaseSet.PropertiesById)
			{
				StoredCaseSet->PropertiesById.Add(pair);
			}
		}
	}
}


bool URHSettingsDataFactory::OnSettingChanged(FName SettingId, int32 SettingValue)
{
	UGameUserSettings* GameSettings = GEngine->GetGameUserSettings();
	if (GameSettings == nullptr)
	{
		return false;
	}

	bool success = false;

	//loss of enum means loss of switch statement :(
	if (SettingId == RHSettingId::Resolution)
	{
		if (SettingValue < ResolutionSettings.Num())
		{
			GameSettings->SetScreenResolution(ResolutionSettings[SettingValue]);
			success = true;
		}
	}
	else if (SettingId == RHSettingId::ScreenMode)
	{
		if (SettingValue < EWindowMode::NumWindowModes)
		{
			GameSettings->SetFullscreenMode(EWindowMode::Type(SettingValue));
			success = true;
		}
	}

	if (success)
	{
		GameSettings->ApplyResolutionSettings(false);
		GameSettings->SaveSettings();
		GameSettings->ApplySettings(false);
	}
	if (success)
	{
		// broadcast a delegate for anyone interested
		OnSettingValueChanged.Broadcast(SettingId, SettingValue);
	}
	return success;
}