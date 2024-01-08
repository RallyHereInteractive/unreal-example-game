// Copyright 2016-2049 Hi-Rez Studios, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "GameFramework/RHGameUserSettings.h"
#include "Online.h"
#include "Engine/LocalPlayer.h"
#include "Player/Controllers/RHPlayerController.h"
#include "GameFramework/RHHUDInterface.h"
#include "GameFramework/HUD.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DynamicRHI.h"
#include "Widgets/Layout/SSafeZone.h"
#if (defined(PLATFORM_PS4) && PLATFORM_PS4) || (defined(PLATFORM_PS5) && PLATFORM_PS5)
#include <system_service.h>
#endif
#if defined(PLATFORM_SWITCH) && PLATFORM_SWITCH
#include "SwitchApplication.h"
#endif

#if !UE_SERVER
#include "Framework/Application/SlateApplication.h"
#endif

const FName URHGameUserSettings::MasterSoundVolume(TEXT("MasterSoundVolume"));
const FName URHGameUserSettings::MusicSoundVolume(TEXT("MusicSoundVolume"));
const FName URHGameUserSettings::SFXSoundVolume(TEXT("SFXSoundVolume"));
const FName URHGameUserSettings::VoiceSoundVolume(TEXT("VoiceSoundVolume"));
const FName URHGameUserSettings::SpatialSoundEnabled(TEXT("SpatialSoundEnabled"));

const FName URHGameUserSettings::MusicCharacterSelectVolume(TEXT("MusicCharacterSelectVolume"));
const FName URHGameUserSettings::MusicMainMenuVolume(TEXT("MusicMainMenuVolume"));
const FName URHGameUserSettings::MusicRoundStartVolume(TEXT("MusicRoundStartVolume"));
const FName URHGameUserSettings::MusicMatchEndVolume(TEXT("MusicMatchEndVolume"));
const FName URHGameUserSettings::MusicRoundEndVolume(TEXT("MusicRoundEndVolume"));
const FName URHGameUserSettings::MusicTimeRemainingVolume(TEXT("MusicTimeRemainingVolume"));
const FName URHGameUserSettings::MusicCharacterVolume(TEXT("MusicCharacterVolume"));

const FName URHGameUserSettings::SFXWepWrapVolume(TEXT("SFXWepWrapVolume"));

const FName URHGameUserSettings::SafeFrameScale(TEXT("SafeFrameScale"));

const FName URHGameUserSettings::VSyncEnabled(TEXT("VSyncEnabled"));

const FName URHGameUserSettings::Brightness(TEXT("Brightness"));
const FName URHGameUserSettings::BrightnessHandheld(TEXT("BrightnessHandheld"));

const FName URHGameUserSettings::AimMode(TEXT("AimMode"));
const FName URHGameUserSettings::GyroMode(TEXT("GyroMode"));
const FName URHGameUserSettings::MuteMode(TEXT("MuteMode"));

const FName URHGameUserSettings::RadialWheelMode(TEXT("RadialWheelMode"));
const FName URHGameUserSettings::GamepadRadialWheelMode(TEXT("GamepadRadialWheelMode"));

const FName URHGameUserSettings::QuickCastEnabled(TEXT("QuickCastEnabled"));

const FName URHGameUserSettings::DamageNumbersEnabled(TEXT("DamageNumbersEnabled"));
const FName URHGameUserSettings::DamageNumbersStacked(TEXT("DamageNumbersStacked"));

const FName URHGameUserSettings::EnemyAimedAtOutlined(TEXT("EnemyAimedAtOutlined"));
const FName URHGameUserSettings::CrosshairSize(TEXT("CrosshairSize"));

const FName URHGameUserSettings::QuipsEnabled(TEXT("QuipsEnabled"));
const FName URHGameUserSettings::CommunicationsEnabled(TEXT("CommunicationsEnabled"));
const FName URHGameUserSettings::TextChatEnabled(TEXT("TextChatEnabled"));
const FName URHGameUserSettings::VoiceChatEnabled(TEXT("VoiceChatEnabled"));
const FName URHGameUserSettings::PushToTalkEnabled(TEXT("PushToTalkEnabled"));
const FName URHGameUserSettings::CrossPlayEnabled(TEXT("CrossPlayEnabled"));
const FName URHGameUserSettings::InputCrossPlayEnabled(TEXT("InputCrossPlayEnabled"));

const FName URHGameUserSettings::MotionBlurEnabled(TEXT("MotionBlurEnabled"));

const FName URHGameUserSettings::ScreenMode(TEXT("ScreenMode"));

const FName URHGameUserSettings::ForceFeedbackEnabled(TEXT("ForceFeedbackEnabled"));

const FName URHGameUserSettings::ColorCorrection(TEXT("ColorCorrection"));

const FName URHGameUserSettings::GlobalQuality(TEXT("GlobalQuality"));
const FName URHGameUserSettings::ViewDistanceQuality(TEXT("ViewDistanceQuality"));
const FName URHGameUserSettings::AntiAliasingQuality(TEXT("AntiAliasingQuality"));
const FName URHGameUserSettings::ShadowQuality(TEXT("ShadowQuality"));
const FName URHGameUserSettings::PostProcessQuality(TEXT("PostProcessQuality"));
const FName URHGameUserSettings::TextureQuality(TEXT("TextureQuality"));
const FName URHGameUserSettings::EffectsQuality(TEXT("EffectsQuality"));
const FName URHGameUserSettings::FoliageQuality(TEXT("FoliageQuality"));

const FName URHGameUserSettings::PingEnabled(TEXT("PingEnabled"));
const FName URHGameUserSettings::PacketLossEnabled(TEXT("PacketLossEnabled"));
const FName URHGameUserSettings::FPSEnabled(TEXT("FPSEnabled"));

const FName URHGameUserSettings::MobilePerformanceMode(TEXT("MobilePerformanceMode"));

const FName URHGameUserSettings::TouchFireMode(TEXT("TouchFireMode"));
const FName URHGameUserSettings::TouchModeAutoVaultEnabled(TEXT("TouchModeAutoVaultEnabled"));

FCachedSettingValue::FCachedSettingValue(const FString& InValue)
{
	StringValue = InValue;
	IntValue = FCString::Atoi(*InValue);
	FloatValue = FCString::Atof(*InValue);
	BoolValue = InValue.ToBool();
}

FCachedSettingValue::FCachedSettingValue(int32 InValue)
{
	StringValue = FString::FromInt(InValue);
	IntValue = InValue;
	FloatValue = InValue;
	BoolValue = InValue != 0 ? true : false;
}

FCachedSettingValue::FCachedSettingValue(float InValue)
{
	StringValue = FString::SanitizeFloat(InValue);
	IntValue = FMath::Sign(InValue) * FMath::CeilToInt(FMath::Abs(InValue)); // round away from zero, so that we do not make a value 0 and thus "false"
	FloatValue = InValue;
	BoolValue = InValue != 0.f ? true : false;
}

FCachedSettingValue::FCachedSettingValue(bool InValue)
{
	StringValue = InValue ? TEXT("true") : TEXT("false");
	IntValue = InValue ? 1 : 0;
	FloatValue = InValue ? 1.f : 0.f;
	BoolValue = InValue;
}

URHGameUserSettings::URHGameUserSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsLoading = false;

	bSettingGlobalQuality = false;

	SavedScreenResolution = FIntPoint(0,0);
	SavedSelectedRegion = FString();
	LastWhatsNewVersion = 0;

	// Add Custom Apply Functions
	ApplySettingFunctionMap.Add(MasterSoundVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(MusicSoundVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(SFXSoundVolume, &URHGameUserSettings::ApplySoundVolume);

	ApplySettingFunctionMap.Add(MusicCharacterSelectVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(MusicMainMenuVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(MusicRoundStartVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(MusicMatchEndVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(MusicRoundEndVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(MusicTimeRemainingVolume, &URHGameUserSettings::ApplySoundVolume);
	ApplySettingFunctionMap.Add(MusicCharacterVolume, &URHGameUserSettings::ApplySoundVolume);

	ApplySettingFunctionMap.Add(SFXWepWrapVolume, &URHGameUserSettings::ApplySoundVolume);

	ApplySettingFunctionMap.Add(VoiceSoundVolume, &URHGameUserSettings::ApplyVoiceSoundVolume);
	ApplySettingFunctionMap.Add(SpatialSoundEnabled, &URHGameUserSettings::ApplySpatialSoundEnabled);
	ApplySettingFunctionMap.Add(SafeFrameScale, &URHGameUserSettings::ApplySafeFrameScale);
	ApplySettingFunctionMap.Add(VSyncEnabled, &URHGameUserSettings::ApplyVSyncEnabled);
	ApplySettingFunctionMap.Add(Brightness, &URHGameUserSettings::ApplyBrightness);
	ApplySettingFunctionMap.Add(BrightnessHandheld, &URHGameUserSettings::ApplyBrightnessHandheld);
	ApplySettingFunctionMap.Add(VoiceChatEnabled, &URHGameUserSettings::ApplyVoiceChatEnabled);
	ApplySettingFunctionMap.Add(PushToTalkEnabled, &URHGameUserSettings::ApplyPushToTalk);
	ApplySettingFunctionMap.Add(CrossPlayEnabled, &URHGameUserSettings::ApplyCrossPlay);
	ApplySettingFunctionMap.Add(InputCrossPlayEnabled, &URHGameUserSettings::ApplyInputCrossPlay);
	ApplySettingFunctionMap.Add(MotionBlurEnabled, &URHGameUserSettings::ApplyMotionBlur);
	ApplySettingFunctionMap.Add(ScreenMode, &URHGameUserSettings::ApplyScreenMode);
	ApplySettingFunctionMap.Add(ForceFeedbackEnabled, &URHGameUserSettings::ApplyForceFeedback);
	ApplySettingFunctionMap.Add(ColorCorrection, &URHGameUserSettings::ApplyColorCorrection);
	ApplySettingFunctionMap.Add(GlobalQuality, &URHGameUserSettings::ApplyGlobalQuality);
	ApplySettingFunctionMap.Add(ViewDistanceQuality, &URHGameUserSettings::ApplyQuality);
	ApplySettingFunctionMap.Add(AntiAliasingQuality, &URHGameUserSettings::ApplyQuality);
	ApplySettingFunctionMap.Add(ShadowQuality, &URHGameUserSettings::ApplyQuality);
	ApplySettingFunctionMap.Add(PostProcessQuality, &URHGameUserSettings::ApplyQuality);
	ApplySettingFunctionMap.Add(TextureQuality, &URHGameUserSettings::ApplyQuality);
	ApplySettingFunctionMap.Add(EffectsQuality, &URHGameUserSettings::ApplyQuality);
	ApplySettingFunctionMap.Add(FoliageQuality, &URHGameUserSettings::ApplyQuality);
	ApplySettingFunctionMap.Add(PingEnabled, &URHGameUserSettings::ApplyTelemetrySettings);
	ApplySettingFunctionMap.Add(PacketLossEnabled, &URHGameUserSettings::ApplyTelemetrySettings);
	ApplySettingFunctionMap.Add(FPSEnabled, &URHGameUserSettings::ApplyTelemetrySettings);
	// End Add Custom Apply Functions
}

void URHGameUserSettings::RevertSettingsToDefault()
{
	SavedSettingsConfig.Empty();

	ApplySavedSettings();
}

void URHGameUserSettings::ApplySavedSettings()
{
	LoadSaveGameConfig();

#if PLATFORM_DESKTOP
	// Grab our last confirmed screen mode as what is saved.
	SavedSettingsConfig.Add(URHGameUserSettings::ScreenMode, FString::FromInt(GetScreenMode()));
#endif

	// Remove any setting that is stale, or is default
	TArray<FName> KeysToRemove;
	for (const TPair<FName, FString>& KeyPair : SavedSettingsConfig)
	{
		if (!DefaultSettingsConfig.Contains(KeyPair.Key) || KeyPair.Value == DefaultSettingsConfig[KeyPair.Key].StringValue)
		{
			KeysToRemove.Add(KeyPair.Key);
		}
	}
	for (const FName& ToRemove : KeysToRemove)
	{
		SavedSettingsConfig.Remove(ToRemove);
	}

	// Copy over our saved settings, use default at base
	AppliedSettingsConfig = DefaultSettingsConfig;
	for (const TPair<FName, FString>& KeyPair : SavedSettingsConfig)
	{
		AppliedSettingsConfig.Add(KeyPair.Key, FCachedSettingValue(KeyPair.Value));
	}

	// Do anything needed in order to apply the setting externally
	for (TPair<FName, FApplySettingFunctionPtr>& ApplySettingPair : ApplySettingFunctionMap)
	{
		const FApplySettingFunctionPtr ApplySettingFunctionPtr = ApplySettingPair.Value;
		const FName ApplySettingName = ApplySettingPair.Key;

		if (ApplySettingName != URHGameUserSettings::GlobalQuality)
		{
			if (ApplySettingFunctionPtr != nullptr)
			{
				(this->*(ApplySettingFunctionPtr))(ApplySettingName);
			}
		}
	}

	SetCurrentLanguage(SavedDisplayLanguage);
#if PLATFORM_DESKTOP
	if (SavedScreenResolution.X == 0 || SavedScreenResolution.Y == 0)
	{
		SavedScreenResolution = GetScreenResolution();
	}
	// Don't apply screen resolution if loading, unreal handles this for us.
	if (!bIsLoading)
	{
		SetScreenResolution(SavedScreenResolution);
		ApplyResolutionSettings(true);
	}
#endif

	OnSettingsSavedNativeDel.Broadcast();
}

void URHGameUserSettings::LoadSettings(bool bForceReload /* = false*/)
{
	bIsLoading = true;

    Super::LoadSettings(bForceReload);
	LoadSaveGameConfig();   // these act as overrides for the base load, since the base load is static data on some platforms

	// Load the default settings from config
	auto DefaultUserSettings = Cast<URHGameUserSettingsDefault>(URHGameUserSettingsDefault::StaticClass()->GetDefaultObject());
	for (const FSettingConfigPair& Pair : DefaultUserSettings->SettingsConfig)
	{
		DefaultSettingsConfig.Add(Pair.Name, FCachedSettingValue(Pair.Value));
	}

	ApplySavedSettings();

	SetGamepadIconSet(GetGamepadIconSet());

	bIsLoading = false;
}

void URHGameUserSettings::SaveSettings()
{
	Super::SaveSettings();
	SaveSaveGameConfig();
}

bool URHGameUserSettings::LoadSaveGameConfig()
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX") || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("Switch"))
	{
		FString SaveGameSlotName = GetClass()->GetName();
		int32 SaveGameUserIndex = 0;

		auto Identity = Online::GetIdentityInterface();
		if (UWorld* const World = GWorld)
		{
			if (ULocalPlayer* const LocalPlayer = World->GetFirstLocalPlayerFromController())
			{
				const auto PlayerId = LocalPlayer->GetPreferredUniqueNetId();
				if (Identity.IsValid() && PlayerId.IsValid())
				{
					SaveGameUserIndex = Identity->GetPlatformUserIdFromUniqueNetId(*PlayerId);
				}
			}
		}
		
		if (UGameplayStatics::DoesSaveGameExist(SaveGameSlotName, SaveGameUserIndex))
		{
			if (URHSettingsSaveGame* const pSaveGame = Cast<URHSettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveGameSlotName, SaveGameUserIndex)))
			{
				// Only load the allowed settings from the profile
				if (URHGameUserSettingsDefault* const DefaultUserSettings = Cast<URHGameUserSettingsDefault>(URHGameUserSettingsDefault::StaticClass()->GetDefaultObject()))
				{
					SavedSettingsConfig.Empty();
					for (const TPair<FName, FString>& SavedSettingConfig : pSaveGame->SavedSettingsConfig)
					{
						SavedSettingsConfig.Add(SavedSettingConfig.Key, SavedSettingConfig.Value);
					}
				}
				SavedDisplayLanguage = pSaveGame->SavedDisplayLanguage;
				SavedLocalActions = pSaveGame->SavedLocalActions;
				SavedSelectedRegion = pSaveGame->SavedSelectedRegion;
				LastWhatsNewVersion = pSaveGame->LastWhatsNewVersion;
				SavedTransientOrderIds = pSaveGame->SavedTransientOrderIds;
				SavedViewedNewsPanelIds = pSaveGame->SavedViewedNewsPanelIds;
				SavedRecentlySeenStoreItemLootIds = pSaveGame->SavedRecentlySeenStoreItemLootIds;
				SavedSeenAcquiredItemIds = pSaveGame->SavedSeenAcquiredItemIds;
			}
		}

		return true;
	}
	
	return false;
}

bool URHGameUserSettings::SaveSaveGameConfig()
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX") || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("Switch"))
	{
		const FString SaveGameSlotName = GetClass()->GetName();
		int32 SaveGameUserIndex = 0;

		auto Identity = Online::GetIdentityInterface();
		if (UWorld* const World = GWorld)
		{
			if (ULocalPlayer* const LocalPlayer = World->GetFirstLocalPlayerFromController())
			{
				const auto PlayerId = LocalPlayer->GetPreferredUniqueNetId();
				if (Identity.IsValid() && PlayerId.IsValid())
				{
					SaveGameUserIndex = Identity->GetPlatformUserIdFromUniqueNetId(*PlayerId);
				}
			}
		}
		if (URHSettingsSaveGame* const pSaveGame = Cast<URHSettingsSaveGame>(UGameplayStatics::CreateSaveGameObject(URHSettingsSaveGame::StaticClass())))
		{
			// Only write the allowed settings to the profile
			if (URHGameUserSettingsDefault* const DefaultUserSettings = Cast<URHGameUserSettingsDefault>(URHGameUserSettingsDefault::StaticClass()->GetDefaultObject()))
			{
				pSaveGame->SavedSettingsConfig.Empty();
				for (const TPair<FName, FString>& SavedSettingConfig : SavedSettingsConfig)
				{
					pSaveGame->SavedSettingsConfig.Add(SavedSettingConfig.Key, SavedSettingConfig.Value);
				}
			}

			pSaveGame->SavedDisplayLanguage = SavedDisplayLanguage;
			pSaveGame->SavedLocalActions = SavedLocalActions;
			pSaveGame->SavedSelectedRegion = SavedSelectedRegion;
			pSaveGame->LastWhatsNewVersion = LastWhatsNewVersion;
			pSaveGame->SavedTransientOrderIds = SavedTransientOrderIds;
			pSaveGame->SavedViewedNewsPanelIds = SavedViewedNewsPanelIds;
			pSaveGame->SavedRecentlySeenStoreItemLootIds = SavedRecentlySeenStoreItemLootIds;
			pSaveGame->SavedSeenAcquiredItemIds = SavedSeenAcquiredItemIds;

			return UGameplayStatics::SaveGameToSlot(pSaveGame, SaveGameSlotName, SaveGameUserIndex);
		}
	}
	else
	{
		SaveConfig();
	}
	
    return false;
}

bool URHGameUserSettings::GetDefaultSettingAsBool(const FName& Name, bool& OutBool) const
{
	const FCachedSettingValue* ValuePtr = DefaultSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutBool = ValuePtr->BoolValue;
		return true;
	}

	return false;
}

bool URHGameUserSettings::GetDefaultSettingAsInt(const FName& Name, int32& OutInt) const
{
	const FCachedSettingValue* ValuePtr = DefaultSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutInt = ValuePtr->IntValue;
		return true;
	}

	return false;
}

bool URHGameUserSettings::GetDefaultSettingAsFloat(const FName& Name, float& OutFloat) const
{
	const FCachedSettingValue* ValuePtr = DefaultSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutFloat = ValuePtr->FloatValue;
		return true;
	}

	return false;
}

bool URHGameUserSettings::GetSettingAsBool(const FName& Name, bool& OutBool) const
{
	const FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutBool = ValuePtr->BoolValue;
		return true;
	}

	return false;
}

bool URHGameUserSettings::GetSettingAsInt(const FName& Name, int32& OutInt) const
{
	const FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutInt = ValuePtr->IntValue;
		return true;
	}

	return false;
}

bool URHGameUserSettings::GetSettingAsFloat(const FName& Name, float& OutFloat) const
{
	const FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutFloat = ValuePtr->FloatValue;
		return true;
	}

	return false;
}

bool URHGameUserSettings::ApplySettingAsBool(const FName& Name, bool Value)
{
	FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		(*ValuePtr) = FCachedSettingValue(Value);

		CallApplySettingFunction(Name);

		return true;
	}
	return false;
}

bool URHGameUserSettings::ApplySettingAsInt(const FName& Name, int32 Value)
{
	FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		(*ValuePtr) = FCachedSettingValue(Value);

		CallApplySettingFunction(Name);

		return true;
	}
	return false;
}

bool URHGameUserSettings::ApplySettingAsFloat(const FName& Name, float Value)
{
	FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		(*ValuePtr) = FCachedSettingValue(Value);

		CallApplySettingFunction(Name);

		return true;
	}
	return false;
}

bool URHGameUserSettings::SaveSetting(const FName& Name)
{
	FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		FCachedSettingValue* DefaultValuePtr = DefaultSettingsConfig.Find(Name);
		if (DefaultValuePtr != nullptr && DefaultValuePtr->StringValue == ValuePtr->StringValue)
		{
			SavedSettingsConfig.Remove(Name);
		}
		else
		{
			SavedSettingsConfig.Add(Name, ValuePtr->StringValue);
		}

		return true;
	}
	return false;
}

void URHGameUserSettings::SaveLocalAction(const FName& Name)
{
	SavedLocalActions.Add(Name);
	OnLocalSettingSaved.Broadcast(Name);
	SaveSettings();
}

bool URHGameUserSettings::RevertSettingToDefault(const FName& Name)
{
	FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	FCachedSettingValue* DefaultValuePtr = DefaultSettingsConfig.Find(Name);
	if (ValuePtr != nullptr && DefaultValuePtr != nullptr)
	{
		(*ValuePtr) = *(DefaultValuePtr);

		CallApplySettingFunction(Name);

		return true;
	}
	return false;
}

void URHGameUserSettings::CallApplySettingFunction(const FName& Name)
{
	const FApplySettingFunctionPtr* ApplySettingFunctionPtr = ApplySettingFunctionMap.Find(Name);
	if (ApplySettingFunctionPtr != nullptr && (*ApplySettingFunctionPtr) != nullptr)
	{
		(this->*(*ApplySettingFunctionPtr))(Name);

		OnSettingApplied.Broadcast(Name);
	}
}

// CUSTOM APPLY FUNCTIONS
void URHGameUserSettings::ApplySoundVolume(const FName& Name)
{
	float OutFloat;
	if (GetSettingAsFloat(Name, OutFloat))
	{
		// TODO: Hook up various sound volumes
/*		if (FAkAudioDevice* AkAudioDevice = FAkAudioDevice::Get())
		{
			AkAudioDevice->SetRTPCValue(*(RTPCName->ToString()), OutFloat * 100.f, 0, nullptr);
		}
*/
	}

	if (Name == MasterSoundVolume)
	{
		// Reapply voice sound volume to take master volume into account
		ApplyVoiceSoundVolume(VoiceSoundVolume);
	}
}
void URHGameUserSettings::ApplyVoiceSoundVolume(const FName& Name)
{
	/* #RHTODO - Voice
	if (IPComVoiceInterface* const pVoice = pcomGetVoice())
	{
		float OutMasterVolume = 1.f;
		GetSettingAsFloat(MasterSoundVolume, OutMasterVolume);

		float OutFloat;
		if (GetSettingAsFloat(Name, OutFloat))
		{
			UE_LOG(RallyHereStart, Log, TEXT("URHGameUserSettings::ApplyVoiceSoundVolume"));
			OutFloat *= OutMasterVolume;
			OutFloat = FMath::Clamp(OutFloat, 0.f, 1.f);
			const bool bMute = !GetVoiceChatEnabled() || OutFloat <= 0.f;
			pVoice->SetSpeakerVolume(OutFloat, bMute);
		}
	}
	*/
}

void URHGameUserSettings::ApplySpatialSoundEnabled(const FName& Name)
{
	bool IsSpatialSoundEnabled;
	if (GetSettingAsBool(Name, IsSpatialSoundEnabled))
	{
		// TODO: Hook up spatial sound toggle
/*		if (FAkAudioDevice* AkAudioDevice = FAkAudioDevice::Get())
		{
			AkAudioDevice->SetRTPCValue(*(RTPCName.ToString()), IsSpatialSoundEnabled ? 1.f : 0.f, 0, nullptr);
		}
*/
	}
}

void URHGameUserSettings::ApplySafeFrameScale(const FName& Name)
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (IsRunningGame() && PlatformName != TEXT("IOS") && PlatformName != TEXT("Android"))
	{
		float SlateSafeFrameScale = FMath::GetMappedRangeValueClamped(FVector2D(0.9f, 1.0f), FVector2D(1.0f, 0.0f), GetSafeFrameScale());
		SSafeZone::SetGlobalSafeZoneScale(SlateSafeFrameScale);
	}
}

void URHGameUserSettings::ApplyVSyncEnabled(const FName& Name)
{
#if PLATFORM_DESKTOP
	if (GEngine != nullptr)
	{
		bool OutBool;
		if (GetSettingAsBool(Name, OutBool))
		{
			if (OutBool)
			{
				GEngine->Exec(nullptr, TEXT("r.vsync 1"));
			}
			else
			{
				GEngine->Exec(nullptr, TEXT("r.vsync 0"));
			}
		}
	}
#endif
}

void URHGameUserSettings::ApplyVoiceChatEnabled(const FName& Name)
{
	/* #RHTODO - Voice
	if (IPComVoiceInterface* const pVoice = pcomGetVoice())
	{
		UE_LOG(RallyHereStart, Log, TEXT("URHGameUserSettings::ApplyVoiceChatEnabled"));
		pVoice->SetEnableMic(GetVoiceChatEnabled());
		pVoice->SetEnableSpeaker(GetVoiceChatEnabled());
	}
	*/
}

void URHGameUserSettings::ApplyPushToTalk(const FName& Name)
{
	/* #RHTODO - Voice
	if (IPComVoiceInterface* const pVoice = pcomGetVoice())
	{
		UE_LOG(RallyHereStart, Log, TEXT("URHGameUserSettings::ApplyPushToTalk"));
		pVoice->SetPushToTalk(GetPushToTalk());
	}
	*/
}

void URHGameUserSettings::ApplyCrossPlay(const FName& Name)
{
	/* #RHTODO - CrossPlay
	if (FPComClient* const MctsClient = pcomGetClient(0))
	{
		bool OutBool = false;
		if (GetSettingAsBool(Name, OutBool))
		{
			MctsClient->SetPlatformCrossplayEnabled(OutBool);
		}
	}
	*/
}

void URHGameUserSettings::ApplyInputCrossPlay(const FName& Name)
{
	/* #RHTODO - CrossPlay
	if (FPComClient* const MctsClient = pcomGetClient(0))
	{
		bool OutBool = false;
		if (GetSettingAsBool(Name, OutBool))
		{
			MctsClient->SetInputCrossplayEnabled(OutBool);
		}
	}
	*/
}

void URHGameUserSettings::ApplyMotionBlur(const FName& Name)
{
	static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlur.Max"));
	if (CVar)
	{
		bool OutBool;
		if (GetSettingAsBool(Name, OutBool))
		{
			const float MotionBlurAsFloat = OutBool ? -1.0f : 0.0f;
			CVar->Set(MotionBlurAsFloat, ECVF_SetByGameSetting);
		}
	}
}

void URHGameUserSettings::ApplyScreenMode(const FName& Name)
{
	// Don't apply screen mode if settings are being loaded, unreal handles this for us
	if (bIsLoading)
	{
		return;
	}
	// Only apply screen mode on desktop
#if PLATFORM_DESKTOP
	int32 OutInt;
	if (GetSettingAsInt(Name, OutInt))
	{
		EWindowMode::Type NewWindowMode = EWindowMode::Type(OutInt);
		RequestResolutionChange(ResolutionSizeX, ResolutionSizeY, NewWindowMode, true);
		SetFullscreenMode(NewWindowMode);
		ApplyResolutionSettings(true);
	}
#endif
}

void URHGameUserSettings::ApplyForceFeedback(const FName& Name)
{
	if (UWorld* const pWorld = GWorld)
	{
		if (ULocalPlayer* const pLocalPlayer = pWorld->GetFirstLocalPlayerFromController())
		{
			if (ARHPlayerController* const pPlayerController = Cast<ARHPlayerController>(pLocalPlayer->GetPlayerController(pWorld)))
			{
				pPlayerController->SetUseForceFeedback(GetUseForceFeedback());
			}
		}
	}
}

void URHGameUserSettings::ApplyColorCorrection(const FName& Name)
{
#if !UE_SERVER
	if (!FSlateApplicationBase::IsInitialized())
	{
		return;
	}

	int32 OutInt;
	if (GetSettingAsInt(Name, OutInt))
	{
		EColorVisionDeficiency Type = EColorVisionDeficiency::NormalVision;

		switch (OutInt)
		{
		case 1 :
			Type = EColorVisionDeficiency::Deuteranope;
			break;
		case 2 :
			Type = EColorVisionDeficiency::Protanope;
			break;
		case 3 :
			Type = EColorVisionDeficiency::Tritanope;
			break;
		default:
			break;
		}

		if (FSlateApplicationBase::Get().GetRenderer())
		{
			// Set to 10 as it is full intensity for shifting the colors to the selected vision deficiency
			FSlateApplicationBase::Get().GetRenderer()->SetColorVisionDeficiencyType(Type, 10, true, false);
		}
	}
#endif
}

void URHGameUserSettings::ApplyGlobalQuality(const FName& Name)
{
#if PLATFORM_DESKTOP
	int32 OutInt;
	if (GetSettingAsInt(Name, OutInt))
	{
		bSettingGlobalQuality = true;
		if (OutInt >= 1 && OutInt <= 4)
		{
			ApplySettingAsInt(ViewDistanceQuality, OutInt - 1);
			ApplySettingAsInt(AntiAliasingQuality, OutInt - 1);
			ApplySettingAsInt(ShadowQuality, OutInt - 1);
			ApplySettingAsInt(PostProcessQuality, OutInt - 1);
			ApplySettingAsInt(TextureQuality, OutInt - 1);
			ApplySettingAsInt(EffectsQuality, OutInt - 1);
			ApplySettingAsInt(FoliageQuality, OutInt - 1);
		}
		else if (OutInt == 0)
		{
			ApplySettingAsInt(ViewDistanceQuality, AutoQualitySettings.ViewDistanceQuality);
			ApplySettingAsInt(AntiAliasingQuality, AutoQualitySettings.AntiAliasingQuality);
			ApplySettingAsInt(ShadowQuality, AutoQualitySettings.ShadowQuality);
			ApplySettingAsInt(PostProcessQuality, AutoQualitySettings.PostProcessQuality);
			ApplySettingAsInt(TextureQuality, AutoQualitySettings.TextureQuality);
			ApplySettingAsInt(EffectsQuality, AutoQualitySettings.EffectsQuality);
			ApplySettingAsInt(FoliageQuality, AutoQualitySettings.FoliageQuality);
		}
		bSettingGlobalQuality = false;

		Scalability::SetQualityLevels(ScalabilityQuality);
	}
#endif
}

void URHGameUserSettings::ApplyQuality(const FName& Name)
{
#if PLATFORM_DESKTOP
	int32 OutInt;
	if (GetSettingAsInt(Name, OutInt))
	{
		if (Name == ViewDistanceQuality)
		{
			SetViewDistanceQuality(OutInt);
		}
		else if (Name == AntiAliasingQuality)
		{
			SetAntiAliasingQuality(OutInt);
		}
		else if (Name == ShadowQuality)
		{
			SetShadowQuality(OutInt);
		}
		else if (Name == PostProcessQuality)
		{
			SetPostProcessingQuality(OutInt);
		}
		else if (Name == TextureQuality)
		{
			SetTextureQuality(OutInt);
		}
		else if (Name == EffectsQuality)
		{
			SetVisualEffectQuality(OutInt);
		}
		else if (Name == FoliageQuality)
		{
			SetFoliageQuality(OutInt);
		}

		if (!bSettingGlobalQuality)
		{
			// Set Global Quality if it aligns
			if (GetViewDistanceQuality() == OutInt
				&& GetAntiAliasingQuality() == OutInt
				&& GetShadowQuality() == OutInt
				&& GetPostProcessingQuality() == OutInt
				&& GetTextureQuality() == OutInt
				&& GetVisualEffectQuality() == OutInt
				&& GetFoliageQuality() == OutInt)
			{
				AppliedSettingsConfig.Add(GlobalQuality, FCachedSettingValue(OutInt + 1));
			}
			else if (GetViewDistanceQuality() == AutoQualitySettings.ViewDistanceQuality
				&& GetAntiAliasingQuality() == AutoQualitySettings.AntiAliasingQuality
				&& GetShadowQuality() == AutoQualitySettings.ShadowQuality
				&& GetPostProcessingQuality() == AutoQualitySettings.PostProcessQuality
				&& GetTextureQuality() == AutoQualitySettings.TextureQuality
				&& GetVisualEffectQuality() == AutoQualitySettings.EffectsQuality
				&& GetFoliageQuality() == AutoQualitySettings.FoliageQuality)
			{
				AppliedSettingsConfig.Add(GlobalQuality, FCachedSettingValue(0));
			}
			else
			{
				AppliedSettingsConfig.Add(GlobalQuality, FCachedSettingValue(-1));
			}

			Scalability::SetQualityLevels(ScalabilityQuality);
		}

	}
#endif
}

void URHGameUserSettings::ApplyTelemetrySettings(const FName& Name)
{
	OnTelemetrySettingsAppliedNativeDel.Broadcast();
}

// END CUSTOM APPLY FUNCTIONS

float URHGameUserSettings::GetSafeFrameScale() const
{
	float OutFloat;
	if (GetSettingAsFloat(SafeFrameScale, OutFloat))
	{
		return OutFloat;
	}
	return 1.f;
}

bool URHGameUserSettings::SetSafeFrameScaleToSystemOverride()
{
#if (defined(PLATFORM_PS4) && PLATFORM_PS4) || (defined(PLATFORM_PS5) && PLATFORM_PS5)
	SceSystemServiceDisplaySafeAreaInfo SafeAreaInfo;
	if (sceSystemServiceGetDisplaySafeAreaInfo(&SafeAreaInfo) >= 0)
	{
		const bool bDifferent = (GetSafeFrameScale() != SafeAreaInfo.ratio);
		AppliedSettingsConfig.Add(SafeFrameScale, FCachedSettingValue(SafeAreaInfo.ratio));
		return bDifferent;
	}
#endif

	return false;
}

FORCENOINLINE void ApplyBrightnessHelper(float InBrightness, float DefaultValue, float HalfRange)
{
	static auto CVarOutputGamma = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TonemapperGamma"));

	FVector2D InputRange(0.0f, 100.0f);
	FVector2D OutputRange(DefaultValue - HalfRange, DefaultValue + HalfRange);
	float Gamma = FMath::GetMappedRangeValueClamped(InputRange, OutputRange, InBrightness);
	CVarOutputGamma->Set(Gamma, ECVF_SetByGameSetting);
}

void URHGameUserSettings::ApplyBrightness(const FName& Name)
{
#if defined(PLATFORM_SWITCH) && PLATFORM_SWITCH
	if (!FSwitchApplication::IsBoostModeActive())
	{
		return;
	}
#endif

	float CurrentBrightness = 50.0f;
	GetSettingAsFloat(Name, CurrentBrightness);

	// Special override to zero for default value
	if (FMath::IsNearlyEqual(CurrentBrightness, 50.0f))
	{
		static auto CVarOutputGamma = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TonemapperGamma"));
		CVarOutputGamma->Set(0.0f, ECVF_SetByGameSetting);
	}
	else
	{
		ApplyBrightnessHelper(CurrentBrightness, 2.2f, 2.0f);
	}
}

void URHGameUserSettings::ApplyBrightnessHandheld(const FName& Name)
{
#if defined(PLATFORM_SWITCH) && PLATFORM_SWITCH
	if (FSwitchApplication::IsBoostModeActive())
	{
		return;
	}
#endif

	float CurrentBrightness = 50.0f;
	GetSettingAsFloat(Name, CurrentBrightness);

	ApplyBrightnessHelper(CurrentBrightness, 2.6f, 2.0f);
}

int32 URHGameUserSettings::GetADSMode() const
{
	int32 OutInt;
	if (GetSettingAsInt(AimMode, OutInt))
	{
		return OutInt;
	}
    return 0;
}

bool URHGameUserSettings::GetUseQuickCast() const
{
	bool OutBool;
	if (GetSettingAsBool(QuickCastEnabled, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHGameUserSettings::GetDamageNumbersDisplayEnabled() const
{
	bool OutBool;
	if (GetSettingAsBool(DamageNumbersEnabled, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHGameUserSettings::GetDamageNumbersDisplayStacking() const
{
	bool OutBool;
	if (GetSettingAsBool(DamageNumbersStacked, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHGameUserSettings::GetEnemyAimedAtOutlined() const
{
	bool OutBool;
	if (GetSettingAsBool(EnemyAimedAtOutlined, OutBool))
	{
		return OutBool;
	}
	return false;
}

ECrosshairSize URHGameUserSettings::GetCrosshairSize() const
{
	int32 OutInt;
	if (GetSettingAsInt(CrosshairSize, OutInt))
	{
		switch (OutInt)
		{
		case 1:
			return ECrosshairSize::Medium;
			break;
		case 2:
			return ECrosshairSize::Large;
			break;
		}
	}
	return ECrosshairSize::Standard;
}

EGyroMode URHGameUserSettings::GetGyroMode() const
{
	int32 OutInt;
	if (GetSettingAsInt(GyroMode, OutInt))
	{
		switch (OutInt)
		{
		case 1:
			return EGyroMode::AimOnly;
			break;
		case 2:
			return EGyroMode::Always;
			break;
		}
	}
	return EGyroMode::None;
}

EMuteMode URHGameUserSettings::GetMuteMode() const
{
	int32 OutInt;
	if (GetSettingAsInt(MuteMode, OutInt))
	{
		switch (OutInt)
		{
		case 0:
			return EMuteMode::VoicechatOnly;
		case 1:
			return EMuteMode::VoicechatAndQuips;
		case 2:
			return EMuteMode::VoicechatAndCommunications;
		case 3:
			return EMuteMode::VoicechatQuipsCommunications;
		default:
			return EMuteMode::VoicechatOnly;
		}
	}
	return EMuteMode::VoicechatOnly;
}

bool URHGameUserSettings::GetQuipsEnabled() const
{
	bool OutBool;
	if (GetSettingAsBool(QuipsEnabled, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHGameUserSettings::GetCommunicationsEnabled() const
{
	bool OutBool;
	if (GetSettingAsBool(CommunicationsEnabled, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHGameUserSettings::GetVoiceChatEnabled() const
{
	bool OutBool;
	if (GetSettingAsBool(VoiceChatEnabled, OutBool))
	{
		return OutBool;
	}
	return false;
}

EColorVisionDeficiency URHGameUserSettings::GetColorCorrection() const
{
	int32 OutInt;
	if (GetSettingAsInt(ColorCorrection, OutInt))
	{
		return EColorVisionDeficiency(OutInt);
	}
	return EColorVisionDeficiency::NormalVision;
}

bool URHGameUserSettings::GetTextChatEnabled() const
{
	return false;
}

bool URHGameUserSettings::GetPushToTalk() const
{
	bool OutBool;
	if (GetSettingAsBool(PushToTalkEnabled, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHGameUserSettings::GetUseForceFeedback() const
{
	bool OutBool;
	if (GetSettingAsBool(ForceFeedbackEnabled, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHGameUserSettings::GetPlatformCrossPlay() const
{
	/* #RHTODO - CrossPlay
	if (FPComClient* MctsClient = pcomGetClient(0))
	{
		return MctsClient->IsPlatformCrossplayEnabled();
	}
	*/
	return false;
}

bool URHGameUserSettings::GetInputCrossPlay() const
{
	/* #RHTODO - CrossPlay
	if (FPComClient* MctsClient = pcomGetClient(0))
	{
		return MctsClient->IsInputCrossplayEnabled();
	}
	*/
	return false;
}

EGamepadIcons URHGameUserSettings::GetGamepadIconSet() const
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();

	if (PLATFORM_DESKTOP)
	{
		return GamepadIconSet;
	}
	else if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX"))
	{
		//On the gamepads' owning platforms, never return anything other than that icon set
		return EGamepadIcons::XboxOne;
	}
	else if (PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5"))
	{
		return EGamepadIcons::PlayStation4;
	}
	else if (PlatformName == TEXT("Switch"))
	{
		return EGamepadIcons::NintendoSwitch;
	}

    //Defaulting to Xbox icons for any other platforms for now.  Would probably want to change in the future.
    return EGamepadIcons::XboxOne;
}

void URHGameUserSettings::SetGamepadIconSet(EGamepadIcons IconSet)
{
	if (GamepadIconSet != IconSet)
	{
		GamepadIconSet = IconSet;
		OnGamepadIconSetSettingsApplied.Broadcast();
	}
}

int32 URHGameUserSettings::GetScreenMode() const
{
    return FullscreenMode;
}

void URHGameUserSettings::RevertScreenResolution()
{
#if PLATFORM_DESKTOP
	const FIntPoint CurrentDesktopResolution = GetDesktopResolution();
	if (CurrentDesktopResolution != FIntPoint(0, 0))
	{
		SetScreenResolution(CurrentDesktopResolution);
	}
	else
#endif
	{
		SetScreenResolution(DefaultScreenResolution);
	}
	DesiredScreenWidth = ResolutionSizeX;
	DesiredScreenHeight = ResolutionSizeY;
	LastUserConfirmedDesiredScreenWidth = DesiredScreenWidth;
	LastUserConfirmedDesiredScreenHeight = DesiredScreenHeight;
	ApplyResolutionSettings(false);
}

void URHGameUserSettings::SetCurrentLanguage(FString Language)
{
    // NOTE: Currently, while we're display this to the user as "Language" selection, we are actually
    // treating this as a Culture-swap, which controls language, locale and all asset groups -CS
    // More Info: https://docs.unrealengine.com/en-US/Gameplay/Localization/ManageActiveCultureRuntime/index.html

    if (!Language.IsEmpty() && FInternationalization::Get().SetCurrentCulture(Language))
    {
        if (!GIsEditor)
        {
            GConfig->SetString(TEXT("Internationalization"), TEXT("Culture"), *Language, GGameUserSettingsIni);
            GConfig->EmptySection(TEXT("Internationalization.AssetGroupCultures"), GGameUserSettingsIni);

            // set proxy config value
            CurrentDisplayLanguage = Language;
        }
    }
    else
    {
        CurrentDisplayLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();
    }
}

bool URHGameUserSettings::SaveTransientOrderId(const FRH_LootId& LootId)
{
	if (!SavedTransientOrderIds.Contains(LootId))
	{
		SavedTransientOrderIds.Add(LootId);
		return true;
	}

	return false;
}

bool URHGameUserSettings::SaveViewedNewPanelId(FName UniqueName)
{
	if(!SavedViewedNewsPanelIds.Contains(UniqueName))
	{
		SavedViewedNewsPanelIds.Add(UniqueName);
		return true;
	}

	return false;
}

void URHGameUserSettings::SaveRecentlySeenStoreItemLootIds(const TSet<FRH_LootId>& LootIds)
{
	SavedRecentlySeenStoreItemLootIds = LootIds;
}

bool URHGameUserSettings::SaveRecentlySeenStoreItemLootId(const FRH_LootId& LootIds)
{
	if (!SavedRecentlySeenStoreItemLootIds.Contains(LootIds))
	{
		SavedRecentlySeenStoreItemLootIds.Add(LootIds);
		return true;
	}

	return false;
}

void URHGameUserSettings::SaveSeenAcquiredItemIds(const TSet<FRH_ItemId>& ItemIds)
{
	SavedSeenAcquiredItemIds = ItemIds;
}

bool URHGameUserSettings::SaveSeenAcquiredItemId(const FRH_ItemId& ItemId)
{
	if (!SavedSeenAcquiredItemIds.Contains(ItemId))
	{
		SavedSeenAcquiredItemIds.Add(ItemId);
		return true;
	}

	return false;
}
