// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "GameFramework/RHPlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/RHAnalogStickFilter.h"
#include "GameFramework/RHBoxDeadZoneFilter.h"
#include "Engine/LocalPlayer.h"
#include "Math/Vector2D.h"

#if PLATFORM_DESKTOP
#include "Framework/Application/SlateApplication.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(RHInput, Log, All);

const FName URHPlayerInput::MouseDoubleClickTime(TEXT("MouseDoubleClickTime"));
const FName URHPlayerInput::MouseHoldTime(TEXT("MouseHoldTime"));

const FName URHPlayerInput::GamepadDoubleClickTime(TEXT("GamepadDoubleClickTime"));
const FName URHPlayerInput::GamepadHoldTime(TEXT("GamepadHoldTime"));

const FName URHPlayerInput::MouseLookSensitivity(TEXT("MouseLookSensitivity"));
const FName URHPlayerInput::GamepadLookSensitivityX(TEXT("GamepadLookSensitivityX"));
const FName URHPlayerInput::GamepadLookSensitivityY(TEXT("GamepadLookSensitivityY"));
const FName URHPlayerInput::GamepadLeftDeadZone(TEXT("GamepadLeftDeadZone"));
const FName URHPlayerInput::GamepadRightDeadZone(TEXT("GamepadRightDeadZone"));

const FName URHPlayerInput::GyroLookSensitivityX(TEXT("GyroLookSensitivity")); //string doesn't contain the X to retain compatibility
const FName URHPlayerInput::GyroLookSensitivityY(TEXT("GyroLookSensitivityY"));
const FName URHPlayerInput::GyroLookSensitivityADS(TEXT("GyroLookSensitivityADS"));
const FName URHPlayerInput::GyroLookSensitivityScope(TEXT("GyroLookSensitivityScope"));

const FName URHPlayerInput::GyroAimAssist(TEXT("GyroAimAssist"));
const FName URHPlayerInput::GyroRawInput(TEXT("GyroRawInput"));

const FName URHPlayerInput::GamepadAccelerationBoost(TEXT("GamepadAccelerationBoost"));
const FName URHPlayerInput::GamepadMultiplierBoost(TEXT("GamepadMultiplierBoost"));

const FName URHPlayerInput::InvertX(TEXT("InvertX"));
const FName URHPlayerInput::InvertY(TEXT("InvertY"));
const FName URHPlayerInput::GyroInvertX(TEXT("GyroInvertX"));
const FName URHPlayerInput::GyroInvertY(TEXT("GyroInvertY"));

const FName URHPlayerInput::ContextualActionButtonMode(TEXT("ContextualActionButtonMode"));

RH_INPUT_STATE LastInputState = PIS_UNKNOWN;

#if !UE_SERVER
RH_INPUT_STATE FRHInputPreProcessor::LastInputState = PIS_UNKNOWN;

FRHInputPreProcessor::FRHInputPreProcessor()
{
	PlayerInput = nullptr;

	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX") || PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("Switch"))
	{
		LastInputState = PIS_GAMEPAD;
	}
}

void FRHInputPreProcessor::BindPlayerInput(URHPlayerInput* playerInput)
{
	PlayerInput = playerInput;
	if (LastInputState != PIS_UNKNOWN)
	{
		PlayerInput->PreProcessInputUpdate(LastInputState, false, 1.f);
	}
}

bool FRHInputPreProcessor::IsInputStateAllowedInGame(RH_INPUT_STATE InputState) const
{
	return true;
}

//the handle methods all return false so that the slate application still runs the actual input
bool FRHInputPreProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (PlayerInput != nullptr)
	{
		if (InKeyEvent.GetKey().IsTouch())
		{
			LastInputState = PIS_TOUCH;
		}
		else if (InKeyEvent.GetKey().IsGamepadKey())
		{
			LastInputState = PIS_GAMEPAD;
		}
		else
		{
			LastInputState = PIS_KEYMOUSE;
		}

		PlayerInput->PreProcessInputUpdate(LastInputState, false, 1.f);
	}
	const bool bShouldConsumeKey = !IsInputStateAllowedInGame(LastInputState);
	return bShouldConsumeKey;
}

bool FRHInputPreProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (PlayerInput != nullptr)
	{
		if (InKeyEvent.GetKey().IsTouch())
		{
			LastInputState = PIS_TOUCH;
		}
		else if (InKeyEvent.GetKey().IsGamepadKey())
		{
			LastInputState = PIS_GAMEPAD;
		}
		else
		{
			LastInputState = PIS_KEYMOUSE;
		}

		PlayerInput->PreProcessInputUpdate(LastInputState, false, -1.f);
	}
	const bool bShouldConsumeKey = !IsInputStateAllowedInGame(LastInputState);
	return bShouldConsumeKey;
}

bool FRHInputPreProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (PlayerInput != nullptr)
	{
		if (InAnalogInputEvent.GetKey().IsTouch())
		{
			LastInputState = PIS_TOUCH;
		}
		else if (InAnalogInputEvent.GetKey().IsGamepadKey())
		{
			LastInputState = PIS_GAMEPAD;
		}
		else
		{
			LastInputState = PIS_KEYMOUSE;
		}

		PlayerInput->PreProcessInputUpdate(LastInputState, true, InAnalogInputEvent.GetAnalogValue());
	}
	const bool bShouldConsumeKey = !IsInputStateAllowedInGame(LastInputState);
	return bShouldConsumeKey;
}

bool FRHInputPreProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (PlayerInput != nullptr)
	{
		if (FSlateApplication::IsInitialized() && IsInGameThread() &&
			FSlateApplication::Get().IsFakingTouchEvents())
		{
			LastInputState = PIS_TOUCH;
		}
		else
		{
			LastInputState = MouseEvent.IsTouchEvent() ? PIS_TOUCH : PIS_KEYMOUSE;
		}

		PlayerInput->PreProcessInputUpdate(LastInputState, true, MouseEvent.GetCursorDelta().Size());
	}
	const bool bShouldConsumeKey = !IsInputStateAllowedInGame(LastInputState);
	return bShouldConsumeKey;
}
#endif //!UE_SERVER

URHPlayerInput::URHPlayerInput()
    : Super()
{
	KeyMouseSwitchDelta = 0.5f;
	GamepadSwitchDelta = 0.5f;
	InputState = LastInputState;

    GamepadLookAcceleration = FVector2D(3.f, 3.f);
    MinMouseSenseScaling = -1.0f;
    MaxMouseSenseScaling = 1.5f;

	LeftAnalogStickFilterClass = URHBoxDeadZoneFilter::StaticClass();
	LeftAnalogStickFilter = nullptr;
	bHasProcessedLeftStick = false;
	ProcessedLeftStick = FVector2D::ZeroVector;
	RightAnalogStickFilterClass = URHBoxDeadZoneFilter::StaticClass();
	RightAnalogStickFilter = nullptr;
	bHasProcessedRightStick = false;
	ProcessedRightStick = FVector2D::ZeroVector;

	// Add Custom Apply Functions
	ApplySettingFunctionMap.Add(MouseLookSensitivity, &URHPlayerInput::ApplyMouseLookSensitivity);
	ApplySettingFunctionMap.Add(GamepadAccelerationBoost, &URHPlayerInput::ApplyGamepadAccelerationBoost);
	ApplySettingFunctionMap.Add(GamepadMultiplierBoost, &URHPlayerInput::ApplyGamepadMultiplierBoost);
	ApplySettingFunctionMap.Add(InvertX, &URHPlayerInput::ApplyInvertX);
	ApplySettingFunctionMap.Add(InvertY, &URHPlayerInput::ApplyInvertY);
	// End Add Custom Apply Functions

#if !UE_SERVER
	// sets up a missing InputDetector
	if (!IsTemplate() && FSlateApplication::IsInitialized() && IsInGameThread())
	{
		InputDetector = TSharedPtr<FRHInputPreProcessor>(new FRHInputPreProcessor());
		InputDetector->PlayerInput = this;
		FSlateApplication::Get().RegisterInputPreProcessor(InputDetector);
	}

	// grab the last input state
	InputState = FRHInputPreProcessor::LastInputState;

	// prefer gamepad when plugged in and if there is no last input state saved
	if (InputState == PIS_UNKNOWN)
	{
		if (FSlateApplication::IsInitialized() && IsInGameThread())
		{
			const FString PlatformName = UGameplayStatics::GetPlatformName();
			if (PlatformName == TEXT("IOS") || PlatformName == TEXT("Android"))
			{
				InputState = PIS_TOUCH;
			}

			if (!UE_BUILD_SHIPPING && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsFakingTouchEvents())
			{
				InputState = PIS_TOUCH;
			}

			TSharedPtr<GenericApplication> GenericApplication = FSlateApplication::Get().GetPlatformApplication();
			if (GenericApplication.IsValid() && GenericApplication->IsGamepadAttached())
			{
				InputState = PIS_GAMEPAD;
			}
		}
	}

#endif //!UE_SERVER

	// figure out the preferred input when there is none set
	if (InputState == PIS_UNKNOWN)
	{
		if (FCStringAnsi::Stricmp(FPlatformProperties::PlatformName(), "XboxOne") == 0 ||
			FCStringAnsi::Stricmp(FPlatformProperties::PlatformName(), "XSX") == 0 ||
			FCStringAnsi::Stricmp(FPlatformProperties::PlatformName(), "PS4") == 0 ||
			FCStringAnsi::Stricmp(FPlatformProperties::PlatformName(), "PS5") == 0 ||
			FCStringAnsi::Stricmp(FPlatformProperties::PlatformName(), "Switch") == 0)
		{
			InputState = PIS_GAMEPAD;
		}
		else
		{
			InputState = PIS_KEYMOUSE;
		}
	}

	// save state
	LastInputState = InputState;

	const FString PlatformName = UGameplayStatics::GetPlatformName();
	if (PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5"))
	{
		EKeys::SetConsoleForGamepadLabels(EConsoleForGamepadLabels::PS4);
	}
}

URHPlayerInput::~URHPlayerInput()
{
#if !UE_SERVER
	if (FSlateApplication::IsInitialized() && IsInGameThread() && InputDetector.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputDetector);
		InputDetector.Reset();
	}
#endif //!UE_SERVER
}

void URHPlayerInput::RevertSettingsToDefault()
{
	SavedSettingsConfig.Empty();
	ApplySavedSettings();
}

void URHPlayerInput::ApplySavedSettings()
{
	// Remove any setting that is stale, or what is default
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

	// Copy over our saved settings, use default as base
	AppliedSettingsConfig = DefaultSettingsConfig;
	for (const TPair<FName, FString>& KeyPair : SavedSettingsConfig)
	{
		AppliedSettingsConfig.Add(KeyPair.Key, FCachedSettingValue(KeyPair.Value));
	}

	// Do anything needed in order to apply the setting externally
	for (TPair<FName, FApplyInputSettingFunctionPtr>& ApplySettingPair : ApplySettingFunctionMap)
	{
		const FApplyInputSettingFunctionPtr ApplySettingFunctionPtr = ApplySettingPair.Value;
		const FName ApplySettingName = ApplySettingPair.Key;
		if (ApplySettingFunctionPtr != nullptr)
		{
			(this->*(ApplySettingFunctionPtr))(ApplySettingName);
		}
	}
}

void URHPlayerInput::ApplySavedKeyMappings()
{
	RemoveCustomDefaultKeyMappings();

	if (const UInputSettings* const pInputSettings = GetDefault<UInputSettings>())
	{
		TArray<FName> CustomActionKeyBindNames;
		GetCustomActionKeyMappingNames(CustomActionKeyBindNames);

		TArray<FName> OutActionNames;
		pInputSettings->GetActionNames(OutActionNames);
		for (const FName& ActionMappingName : OutActionNames)
		{
			if (CustomActionKeyBindNames.Contains(ActionMappingName))
			{
				FRHCustomInputActionKeyMappings OutCustomInputActionKeyMappings;
				GetCustomActionMappingForDefaultMapping(ActionMappingName, OutCustomInputActionKeyMappings);
				AppliedActionKeyMappings.Emplace(ActionMappingName, OutCustomInputActionKeyMappings);
			}
		}

		TArray<FName> CustomAxisKeyBindNames;
		GetCustomAxisKeyMappingNames(CustomAxisKeyBindNames);

		TArray<FName> OutAxisNames;
		pInputSettings->GetAxisNames(OutAxisNames);
		for (const FName& AxisMappingName : OutAxisNames)
		{
			if (CustomAxisKeyBindNames.Contains(AxisMappingName))
			{
				FRHCustomInputAxisKeyMappings OutCustomInputAxisKeyMappings;
				GetCustomAxisMappingForDefaultMapping(AxisMappingName, OutCustomInputAxisKeyMappings);
				AppliedAxisKeyMappings.Emplace(AxisMappingName, OutCustomInputAxisKeyMappings);
			}
		}
	}

	for (const TPair<FName, FRHCustomInputActionKeyMappings>& SavedPair : CustomActionKeyMappings)
	{
		AppliedActionKeyMappings.Add(SavedPair.Key, SavedPair.Value);
	}

	for (const TPair<FName, FRHCustomInputAxisKeyMappings>& SavedPair : CustomAxisKeyMappings)
	{
		AppliedAxisKeyMappings.Add(SavedPair.Key, SavedPair.Value);
	}

	RemoveStaleCustomBindings();
	ApplyCustomMappings();
}

void URHPlayerInput::PostInitProperties()
{
    Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	// Load the default settings from config
	if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
	{
		for (const FSettingConfigPair& Pair : DefaultUserSettings->SettingsConfig)
		{
			DefaultSettingsConfig.Add(Pair.Name, Pair.Value);
		}
	}

	ApplySavedSettings();
	ApplySavedKeyMappings();

	if (LeftAnalogStickFilterClass != nullptr)
	{
		LeftAnalogStickFilter = NewObject<URHAnalogStickFilter>(this, LeftAnalogStickFilterClass);
		LeftAnalogStickFilter->StickType = EAnalogStickType::Left;
		LeftAnalogStickFilter->PlayerInput = this;
	}

	if (RightAnalogStickFilterClass != nullptr)
	{
		RightAnalogStickFilter = NewObject<URHAnalogStickFilter>(this, RightAnalogStickFilterClass);
		RightAnalogStickFilter->StickType = EAnalogStickType::Right;
		RightAnalogStickFilter->PlayerInput = this;
	}
}


bool URHPlayerInput::GetDefaultSettingAsBool(const FName& Name, bool& OutBool) const
{
	const FCachedSettingValue* ValuePtr = DefaultSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutBool = ValuePtr->BoolValue;
		return true;
	}

	return false;
}

bool URHPlayerInput::GetDefaultSettingAsInt(const FName& Name, int32& OutInt) const
{
	const FCachedSettingValue* ValuePtr = DefaultSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutInt = ValuePtr->IntValue;
		return true;
	}

	return false;
}

bool URHPlayerInput::GetDefaultSettingAsFloat(const FName& Name, float& OutFloat) const
{
	const FCachedSettingValue* ValuePtr = DefaultSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutFloat = ValuePtr->FloatValue;
		return true;
	}

	return false;
}

bool URHPlayerInput::GetSettingAsBool(const FName& Name, bool& OutBool) const
{
	const FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutBool = ValuePtr->BoolValue;
		return true;
	}

	return false;
}

bool URHPlayerInput::GetSettingAsInt(const FName& Name, int32& OutInt) const
{
	const FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutInt = ValuePtr->IntValue;
		return true;
	}

	return false;
}

bool URHPlayerInput::GetSettingAsFloat(const FName& Name, float& OutFloat) const
{
	const FCachedSettingValue* ValuePtr = AppliedSettingsConfig.Find(Name);
	if (ValuePtr != nullptr)
	{
		OutFloat = ValuePtr->FloatValue;
		return true;
	}

	return false;
}

bool URHPlayerInput::ApplySettingAsBool(const FName& Name, bool Value)
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

bool URHPlayerInput::ApplySettingAsInt(const FName& Name, int32 Value)
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

bool URHPlayerInput::ApplySettingAsFloat(const FName& Name, float Value)
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

bool URHPlayerInput::SaveSetting(const FName& Name)
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

bool URHPlayerInput::RevertSettingToDefault(const FName& Name)
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

void URHPlayerInput::CallApplySettingFunction(const FName& Name)
{
	const FApplyInputSettingFunctionPtr* ApplySettingFunctionPtr = ApplySettingFunctionMap.Find(Name);
	if (ApplySettingFunctionPtr != nullptr)
	{
		(this->*(*ApplySettingFunctionPtr))(Name);
	}
}

// CUSTOM APPLY FUNCTIONS
void URHPlayerInput::ApplyMouseLookSensitivity(const FName& Name)
{
	float OutFloat;
	if (GetSettingAsFloat(Name, OutFloat))
	{
		SetMouseSensitivity(OutFloat);
	}
}

void URHPlayerInput::ApplyGamepadAccelerationBoost(const FName& Name)
{
	float OutFloat;
	if (GetSettingAsFloat(Name, OutFloat))
	{
		// TODO: Apply gamepad acceleration boost
	}
}

void URHPlayerInput::ApplyGamepadMultiplierBoost(const FName& Name)
{
	float OutFloat;
	if (GetSettingAsFloat(Name, OutFloat))
	{
		// TODO: Apply gamepad multiplier boost
	}
}

void URHPlayerInput::ApplyInvertX(const FName& Name)
{
	bool OutBool;
	if (GetSettingAsBool(Name, OutBool))
	{
		FInputAxisProperties GamepadXAxis;
		FInputAxisProperties MouseXAxis;
		GetAxisProperties("Gamepad_RightX", GamepadXAxis);
		GetAxisProperties("MouseX", MouseXAxis);
		GamepadXAxis.bInvert = OutBool;
		MouseXAxis.bInvert = OutBool;
		SetAxisProperties("Gamepad_RightX", GamepadXAxis);
		SetAxisProperties("MouseX", MouseXAxis);
	}
}

void URHPlayerInput::ApplyInvertY(const FName& Name)
{
	bool OutBool;
	if (GetSettingAsBool(Name, OutBool))
	{
		FInputAxisProperties GamepadYAxis;
		FInputAxisProperties MouseYAxis;
		GetAxisProperties("Gamepad_RightY", GamepadYAxis);
		GetAxisProperties("MouseY", MouseYAxis);
		GamepadYAxis.bInvert = OutBool;
		MouseYAxis.bInvert = OutBool;
		SetAxisProperties("Gamepad_RightY", GamepadYAxis);
		SetAxisProperties("MouseY", MouseYAxis);
	}
}



// END CUSTOM APPLY FUNCTIONS

// Old Getter function passthroughs

bool URHPlayerInput::GetInvertX() const
{
	bool OutBool;
	if (GetSettingAsBool(InvertX, OutBool))
	{
		return OutBool;
	}
	return false;
}

bool URHPlayerInput::GetInvertY() const
{
	bool OutBool;
	if (GetSettingAsBool(InvertY, OutBool))
	{
		return OutBool;
	}
	return false;
}

FVector2D URHPlayerInput::GetGamepadLookSensitivity() const
{
	FVector2D toReturn(1.f, 1.f);

	float OutGamepadLookSensitivityX;
	if (GetSettingAsFloat(GamepadLookSensitivityX, OutGamepadLookSensitivityX))
	{
		toReturn.X = OutGamepadLookSensitivityX;
	}

	float OutGamepadLookSensitivityY;
	if (GetSettingAsFloat(GamepadLookSensitivityY, OutGamepadLookSensitivityY))
	{
		toReturn.Y = OutGamepadLookSensitivityY;
	}

	return toReturn;
}

//End old Getter function passthroughs

float URHPlayerInput::ConvertGyroLookSensitivity(float value, bool isY) const
{
	float OutValue = value;

	float OutGyroLookSensitivity;
	if (GetSettingAsFloat(isY ? GyroLookSensitivityY : GyroLookSensitivityX, OutGyroLookSensitivity))
	{
		OutValue *= OutGyroLookSensitivity;
	}

	bool OutGyroInvertX = false;
	GetSettingAsBool(GyroInvertX, OutGyroInvertX);

	bool OutGyroInvertY = false;
	GetSettingAsBool(GyroInvertY, OutGyroInvertY);

	if ((isY && OutGyroInvertX) || (!isY && OutGyroInvertY))
	{
		OutValue *= -1.f;
	}

	return OutValue;
}

void URHPlayerInput::SaveKeyMappings()
{
	CustomActionKeyMappings.Empty();
	CustomAxisKeyMappings.Empty();

	for (const TPair<FName, FRHCustomInputActionKeyMappings>& CustomInputActionKeyMapping : AppliedActionKeyMappings)
	{
		FRHCustomInputActionKeyMappings OutCustomInputActionKeyMappings;
		GetCustomActionMappingForDefaultMapping(CustomInputActionKeyMapping.Key, OutCustomInputActionKeyMappings);
		if (CustomInputActionKeyMapping.Value != OutCustomInputActionKeyMappings)
		{
			CustomActionKeyMappings.Add(CustomInputActionKeyMapping.Key, CustomInputActionKeyMapping.Value);
		}
	}

	for (const TPair<FName, FRHCustomInputAxisKeyMappings>& CustomInputAxisKeyMapping : AppliedAxisKeyMappings)
	{
		FRHCustomInputAxisKeyMappings OutCustomInputAxisKeyMappings;
		GetCustomAxisMappingForDefaultMapping(CustomInputAxisKeyMapping.Key, OutCustomInputAxisKeyMappings);
		if (CustomInputAxisKeyMapping.Value != OutCustomInputAxisKeyMappings)
		{
			CustomAxisKeyMappings.Add(CustomInputAxisKeyMapping.Key, CustomInputAxisKeyMapping.Value);
		}
	}
}

void URHPlayerInput::RemoveCustomDefaultKeyMappings()
{
    if (const UInputSettings* const pInputSettings = GetDefault<UInputSettings>())
    {
		TArray<FName> CustomActionKeyBindNames;
		GetCustomActionKeyMappingNames(CustomActionKeyBindNames);

        TArray<FName> OutActionNames;
        pInputSettings->GetActionNames(OutActionNames);
        for (const FName& ActionMappingName : OutActionNames)
        {
			if (CustomActionKeyBindNames.Contains(ActionMappingName))
			{
				TArray<FInputActionKeyMapping> DefaultActionKeyMappings;
				pInputSettings->GetActionMappingByName(ActionMappingName, DefaultActionKeyMappings);
				for (const FInputActionKeyMapping& DefaultActionKeyMapping : DefaultActionKeyMappings)
				{
					RemoveActionMapping(DefaultActionKeyMapping);
				}
			}
        }

		TArray<FName> CustomAxisKeyBindNames;
		GetCustomAxisKeyMappingNames(CustomAxisKeyBindNames);

        TArray<FName> OutAxisNames;
        pInputSettings->GetAxisNames(OutAxisNames);
        for (const FName& AxisMappingName : OutAxisNames)
        {
			if (CustomAxisKeyBindNames.Contains(AxisMappingName))
			{
				TArray<FInputAxisKeyMapping> DefaultAxisKeyMappings;
				pInputSettings->GetAxisMappingByName(AxisMappingName, DefaultAxisKeyMappings);
				for (const FInputAxisKeyMapping& DefaultAxisKeyMapping : DefaultAxisKeyMappings)
				{
					RemoveAxisMapping(DefaultAxisKeyMapping);
				}
			}
        }
    }
}

void URHPlayerInput::GetCustomActionMappingForDefaultMapping(const FName& ActionMappingName, FRHCustomInputActionKeyMappings& OutCustomInputActionKeyMappings) const
{
    if (const UInputSettings* const pInputSettings = GetDefault<UInputSettings>())
    {
        TArray<FInputActionKeyMapping> DefaultActionKeys;
        pInputSettings->GetActionMappingByName(ActionMappingName, DefaultActionKeys);

		EInputActionType DefaultInputActionType = EInputActionType::Press;
		if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
		{
			for (const FRHInputActionNameTypePair& InputActionNameTypePair : DefaultUserSettings->InputActionNameTypePairs)
			{
				if (InputActionNameTypePair.Name == ActionMappingName)
				{
					DefaultInputActionType = InputActionNameTypePair.Type;
					break;
				}
			}
		}

        for (const FInputActionKeyMapping& DefaultKey : DefaultActionKeys)
        {
            if (DefaultKey.Key.IsValid() && !DefaultKey.Key.IsGamepadKey() && !DefaultKey.Key.IsTouch())
            {
                OutCustomInputActionKeyMappings.KBM_Mappings.EmplaceAt(0, FRHInputActionKeyMapping(FInputActionKeyMapping(ActionMappingName, DefaultKey.Key), DefaultInputActionType));
            }
            else if (DefaultKey.Key.IsValid() && DefaultKey.Key.IsGamepadKey() && !DefaultKey.Key.IsTouch())
            {
                OutCustomInputActionKeyMappings.GP_Mappings.EmplaceAt(0, FRHInputActionKeyMapping(FInputActionKeyMapping(ActionMappingName, DefaultKey.Key), DefaultInputActionType));
            }
            else if (DefaultKey.Key.IsValid() && DefaultKey.Key.IsTouch() && !DefaultKey.Key.IsGamepadKey())
            {
                OutCustomInputActionKeyMappings.Touch_Mappings.EmplaceAt(0, FRHInputActionKeyMapping(FInputActionKeyMapping(ActionMappingName, DefaultKey.Key), DefaultInputActionType));
            }
        }
    }
}

void URHPlayerInput::GetCustomAxisMappingForDefaultMapping(const FName& AxisMappingName, FRHCustomInputAxisKeyMappings& OutCustomInputAxisKeyMappings) const
{
    if (const UInputSettings* const pInputSettings = GetDefault<UInputSettings>())
    {
        TArray<FInputAxisKeyMapping> DefaultAxisKeys;
        pInputSettings->GetAxisMappingByName(AxisMappingName, DefaultAxisKeys);

        for (const FInputAxisKeyMapping& DefaultKey : DefaultAxisKeys)
        {
            if (DefaultKey.Key.IsValid() && !DefaultKey.Key.IsGamepadKey() && !DefaultKey.Key.IsTouch())
            {
                if (!OutCustomInputAxisKeyMappings.KBM_Mappings.Contains(DefaultKey.Scale))
                {
                    OutCustomInputAxisKeyMappings.KBM_Mappings.Add(DefaultKey.Scale, FRHInputAxisKeyMappings());
                }
                OutCustomInputAxisKeyMappings.KBM_Mappings[DefaultKey.Scale].InputAxisKeyMappings.EmplaceAt(0, AxisMappingName, DefaultKey.Key, DefaultKey.Scale);
            }
            else if (DefaultKey.Key.IsValid() && DefaultKey.Key.IsGamepadKey() && !DefaultKey.Key.IsTouch())
            {
                if (!OutCustomInputAxisKeyMappings.GP_Mappings.Contains(DefaultKey.Scale))
                {
                    OutCustomInputAxisKeyMappings.GP_Mappings.Add(DefaultKey.Scale, FRHInputAxisKeyMappings());
                }
                OutCustomInputAxisKeyMappings.GP_Mappings[DefaultKey.Scale].InputAxisKeyMappings.EmplaceAt(0, AxisMappingName, DefaultKey.Key, DefaultKey.Scale);
            }
            else if (DefaultKey.Key.IsValid() && DefaultKey.Key.IsTouch() && !DefaultKey.Key.IsGamepadKey())
            {
                if (!OutCustomInputAxisKeyMappings.Touch_Mappings.Contains(DefaultKey.Scale))
                {
                    OutCustomInputAxisKeyMappings.Touch_Mappings.Add(DefaultKey.Scale, FRHInputAxisKeyMappings());
                }
                OutCustomInputAxisKeyMappings.Touch_Mappings[DefaultKey.Scale].InputAxisKeyMappings.EmplaceAt(0, AxisMappingName, DefaultKey.Key, DefaultKey.Scale);
            }
        }
    }
}

void URHPlayerInput::SetCustomKeyMappingsToDefault()
{
    for (const TPair<FName, FRHCustomInputActionKeyMappings>& ActionMappingPair : AppliedActionKeyMappings)
    {
        for (const FRHInputActionKeyMapping& KBM_Mapping : ActionMappingPair.Value.KBM_Mappings)
        {
            RemoveActionMapping(KBM_Mapping.Mapping);
        }
        for (const FRHInputActionKeyMapping& GP_Mapping : ActionMappingPair.Value.GP_Mappings)
        {
            RemoveActionMapping(GP_Mapping.Mapping);
        }
        for (const FRHInputActionKeyMapping& Touch_Mapping : ActionMappingPair.Value.Touch_Mappings)
        {
            RemoveActionMapping(Touch_Mapping.Mapping);
        }
    }
	AppliedActionKeyMappings.Empty();

    for (const TPair<FName, FRHCustomInputAxisKeyMappings>& AxisMappingPair : AppliedAxisKeyMappings)
    {
        for (const TPair<float, FRHInputAxisKeyMappings>& KBM_MappingList : AxisMappingPair.Value.KBM_Mappings)
        {
            for (const FInputAxisKeyMapping& KBM_Mapping : KBM_MappingList.Value.InputAxisKeyMappings)
            {
                RemoveAxisMapping(KBM_Mapping);
            }
        }
        for (const TPair<float, FRHInputAxisKeyMappings>& GP_MappingList : AxisMappingPair.Value.GP_Mappings)
        {
            for (const FInputAxisKeyMapping& GP_Mapping : GP_MappingList.Value.InputAxisKeyMappings)
            {
                RemoveAxisMapping(GP_Mapping);
            }
        }
        for (const TPair<float, FRHInputAxisKeyMappings>& Touch_MappingList : AxisMappingPair.Value.Touch_Mappings)
        {
            for (const FInputAxisKeyMapping& Touch_Mapping : Touch_MappingList.Value.InputAxisKeyMappings)
            {
                RemoveAxisMapping(Touch_Mapping);
            }
        }
    }
    AppliedAxisKeyMappings.Empty();

    if (const UInputSettings* const pInputSettings = GetDefault<UInputSettings>())
    {
		TArray<FName> CustomActionKeyBindNames;
		GetCustomActionKeyMappingNames(CustomActionKeyBindNames);

        TArray<FName> OutActionNames;
        pInputSettings->GetActionNames(OutActionNames);
        for (const FName& ActionMappingName : OutActionNames)
        {
			if (CustomActionKeyBindNames.Contains(ActionMappingName))
			{
				FRHCustomInputActionKeyMappings OutCustomInputActionKeyMappings;
				GetCustomActionMappingForDefaultMapping(ActionMappingName, OutCustomInputActionKeyMappings);
				AppliedActionKeyMappings.Emplace(ActionMappingName, OutCustomInputActionKeyMappings);
			}
        }

		TArray<FName> CustomAxisKeyBindNames;
		GetCustomAxisKeyMappingNames(CustomAxisKeyBindNames);

        TArray<FName> OutAxisNames;
        pInputSettings->GetAxisNames(OutAxisNames);
        for (const FName& AxisMappingName : OutAxisNames)
        {
			if (CustomAxisKeyBindNames.Contains(AxisMappingName))
			{
				FRHCustomInputAxisKeyMappings OutCustomInputAxisKeyMappings;
				GetCustomAxisMappingForDefaultMapping(AxisMappingName, OutCustomInputAxisKeyMappings);
				AppliedAxisKeyMappings.Emplace(AxisMappingName, OutCustomInputAxisKeyMappings);
			}
        }
    }

	CustomActionKeyMappings.Empty();
	CustomAxisKeyMappings.Empty();

    ApplyCustomMappings();

    OnKeyMappingsUpdated.Broadcast();
}

void URHPlayerInput::ApplyCustomMappings()
{
    for (const TPair<FName, FRHCustomInputActionKeyMappings>& ActionMappingPair : AppliedActionKeyMappings)
    {
        for (const FRHInputActionKeyMapping& KBM_Mapping : ActionMappingPair.Value.KBM_Mappings)
        {
			AddActionMapping(KBM_Mapping.Mapping);
        }
        for (const FRHInputActionKeyMapping& GP_Mapping : ActionMappingPair.Value.GP_Mappings)
        {
			AddActionMapping(GP_Mapping.Mapping);
        }
        for (const FRHInputActionKeyMapping& Touch_Mapping : ActionMappingPair.Value.Touch_Mappings)
        {
			AddActionMapping(Touch_Mapping.Mapping);
        }
    }

    for (const TPair<FName, FRHCustomInputAxisKeyMappings>& AxisMappingPair : AppliedAxisKeyMappings)
    {
        for (const TPair<float, FRHInputAxisKeyMappings>& KBM_MappingList : AxisMappingPair.Value.KBM_Mappings)
        {
            for (const FInputAxisKeyMapping& KBM_Mapping : KBM_MappingList.Value.InputAxisKeyMappings)
            {
                AddAxisMapping(KBM_Mapping);
            }
        }
        for (const TPair<float, FRHInputAxisKeyMappings>& GP_MappingList : AxisMappingPair.Value.GP_Mappings)
        {
            for (const FInputAxisKeyMapping& GP_Mapping : GP_MappingList.Value.InputAxisKeyMappings)
            {
                AddAxisMapping(GP_Mapping);
            }
        }
        for (const TPair<float, FRHInputAxisKeyMappings>& Touch_MappingList : AxisMappingPair.Value.Touch_Mappings)
        {
            for (const FInputAxisKeyMapping& Touch_Mapping : Touch_MappingList.Value.InputAxisKeyMappings)
            {
                AddAxisMapping(Touch_Mapping);
            }
        }
    }
}

void URHPlayerInput::RemoveStaleCustomBindings()
{
	TArray<FName> CustomActionKeyBindNames;
	GetCustomActionKeyMappingNames(CustomActionKeyBindNames);

	TArray<FName> ActionKeyMappingKeys;
	AppliedActionKeyMappings.GenerateKeyArray(ActionKeyMappingKeys);
	for (const FName& ActionKeyMappingKey : ActionKeyMappingKeys)
	{
		if (!CustomActionKeyBindNames.Contains(ActionKeyMappingKey))
		{
			AppliedActionKeyMappings.Remove(ActionKeyMappingKey);
		}
	}

	TArray<FName> CustomAxisKeyBindNames;
	GetCustomAxisKeyMappingNames(CustomAxisKeyBindNames);

	TArray<FName> AxisKeyMappingKeys;
	AppliedAxisKeyMappings.GenerateKeyArray(AxisKeyMappingKeys);
	for (const FName& AxisKeyMappingKey : AxisKeyMappingKeys)
	{
		if (!CustomAxisKeyBindNames.Contains(AxisKeyMappingKey))
		{
			AppliedAxisKeyMappings.Remove(AxisKeyMappingKey);
		}
	}
}

void URHPlayerInput::GetCustomActionKeyMappingNames(TArray<FName>& OutNames) const
{
	if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
	{
		for (const FRHCustomInputActionKey& CustomInputActionKey : DefaultUserSettings->CustomInputActionKeys)
		{
			OutNames.AddUnique(CustomInputActionKey.KeyboardName);
			OutNames.AddUnique(CustomInputActionKey.GamepadName);
		}
		for (const FRHInputActionTiedNames& InputActionTiedName : DefaultUserSettings->InputActionTiedNames)
		{
			OutNames.AddUnique(InputActionTiedName.Press);
			OutNames.AddUnique(InputActionTiedName.Hold);
			OutNames.AddUnique(InputActionTiedName.Repeat);
		}
	}
}

void URHPlayerInput::GetCustomAxisKeyMappingNames(TArray<FName>& OutNames) const
{
	if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
	{
		for (const FRHCustomInputAxisKey& CustomInputAxisKey : DefaultUserSettings->CustomInputAxisKeys)
		{
			OutNames.AddUnique(CustomInputAxisKey.KeyboardName);
			OutNames.AddUnique(CustomInputAxisKey.GamepadName);
		}
	}
}

TArray<FRHInputActionKeyMapping>* URHPlayerInput::GetCustomInputActionKeyMappingsFor(const FName& Name, EInputType InputType)
{
    TArray<FRHInputActionKeyMapping>* Mappings = nullptr;
    if (AppliedActionKeyMappings.Contains(Name))
    {
        switch (InputType)
        {
        case EInputType::KBM:
			Mappings = &(AppliedActionKeyMappings[Name].KBM_Mappings);
            break;
        case EInputType::GP:
			Mappings = &(AppliedActionKeyMappings[Name].GP_Mappings);
            break;
        case EInputType::Touch:
			Mappings = &(AppliedActionKeyMappings[Name].Touch_Mappings);
            break;
        }
    }
    return Mappings;
}

void URHPlayerInput::GetCustomInputActionKeyMappingsCopyFor(const FName& Name, EInputType InputType, TArray<FRHInputActionKeyMapping>& OutInputActionKeyMapping) const
{
    if (AppliedActionKeyMappings.Contains(Name))
    {
        switch (InputType)
        {
        case EInputType::KBM:
			OutInputActionKeyMapping.Append(AppliedActionKeyMappings[Name].KBM_Mappings);
            break;
        case EInputType::GP:
			OutInputActionKeyMapping.Append(AppliedActionKeyMappings[Name].GP_Mappings);
            break;
        case EInputType::Touch:
			OutInputActionKeyMapping.Append(AppliedActionKeyMappings[Name].Touch_Mappings);
            break;
        }
    }
}

TArray<FInputAxisKeyMapping>* URHPlayerInput::GetCustomInputAxisKeyMappingsFor(const FName& Name, EInputType InputType, float Scale)
{
    TArray<FInputAxisKeyMapping>* Mappings = nullptr;
    if (AppliedAxisKeyMappings.Contains(Name))
    {
        switch (InputType)
        {
        case EInputType::KBM:
            if (AppliedAxisKeyMappings[Name].KBM_Mappings.Contains(Scale))
            {
                Mappings = &(AppliedAxisKeyMappings[Name].KBM_Mappings[Scale].InputAxisKeyMappings);
            }
            break;
        case EInputType::GP:
            if (AppliedAxisKeyMappings[Name].GP_Mappings.Contains(Scale))
            {
                Mappings = &(AppliedAxisKeyMappings[Name].GP_Mappings[Scale].InputAxisKeyMappings);
            }
            break;
        case EInputType::Touch:
            if (AppliedAxisKeyMappings[Name].Touch_Mappings.Contains(Scale))
            {
                Mappings = &(AppliedAxisKeyMappings[Name].Touch_Mappings[Scale].InputAxisKeyMappings);
            }
            break;
        }
    }
    return Mappings;
}

void URHPlayerInput::GetCustomInputAxisKeyMappingsCopyFor(const FName& Name, EInputType InputType, float Scale, TArray<FInputAxisKeyMapping>& OutInputAxisKeyMapping) const
{
    if (AppliedAxisKeyMappings.Contains(Name))
    {
        switch (InputType)
        {
        case EInputType::KBM:
            if (AppliedAxisKeyMappings[Name].KBM_Mappings.Contains(Scale))
            {
                OutInputAxisKeyMapping.Append(AppliedAxisKeyMappings[Name].KBM_Mappings[Scale].InputAxisKeyMappings);
            }
            break;
        case EInputType::GP:
            if (AppliedAxisKeyMappings[Name].GP_Mappings.Contains(Scale))
            {
                OutInputAxisKeyMapping.Append(AppliedAxisKeyMappings[Name].GP_Mappings[Scale].InputAxisKeyMappings);
            }
            break;
        case EInputType::Touch:
            if (AppliedAxisKeyMappings[Name].Touch_Mappings.Contains(Scale))
            {
                OutInputAxisKeyMapping.Append(AppliedAxisKeyMappings[Name].Touch_Mappings[Scale].InputAxisKeyMappings);
            }
            break;
        }
    }
}

void URHPlayerInput::GetCustomInputAxisKeyMappingsFor(const FName& Name, EInputType InputType, TMap<float, TArray<FInputAxisKeyMapping>*>& OutCustomInputAxisKeyMappingsList)
{
    if (AppliedAxisKeyMappings.Contains(Name))
    {
        switch (InputType)
        {
        case EInputType::KBM:
            for(const TPair<float, FRHInputAxisKeyMappings>& Mappings : AppliedAxisKeyMappings[Name].KBM_Mappings)
            {
                OutCustomInputAxisKeyMappingsList.Add(Mappings.Key, GetCustomInputAxisKeyMappingsFor(Name, InputType, Mappings.Key));
            }
            break;
        case EInputType::GP:
            for (const TPair<float, FRHInputAxisKeyMappings>& Mappings : AppliedAxisKeyMappings[Name].GP_Mappings)
            {
                OutCustomInputAxisKeyMappingsList.Add(Mappings.Key, GetCustomInputAxisKeyMappingsFor(Name, InputType, Mappings.Key));
            }
            break;
        case EInputType::Touch:
            for (const TPair<float, FRHInputAxisKeyMappings>& Mappings : AppliedAxisKeyMappings[Name].Touch_Mappings)
            {
                OutCustomInputAxisKeyMappingsList.Add(Mappings.Key, GetCustomInputAxisKeyMappingsFor(Name, InputType, Mappings.Key));
            }
            break;
        }
    }
}

void URHPlayerInput::GetCustomInputAxisKeyMappingsCopyFor(const FName& Name, EInputType InputType, TMap<float, TArray<FInputAxisKeyMapping>>& OutCustomInputAxisKeyMappingsList) const
{
	if (AppliedAxisKeyMappings.Contains(Name))
	{
		switch (InputType)
		{
		case EInputType::KBM:
			for (const TPair<float, FRHInputAxisKeyMappings>& Mappings : AppliedAxisKeyMappings[Name].KBM_Mappings)
			{
				TArray<FInputAxisKeyMapping> OutInputAxisKeyMapping;
				GetCustomInputAxisKeyMappingsCopyFor(Name, InputType, Mappings.Key, OutInputAxisKeyMapping);
				OutCustomInputAxisKeyMappingsList.Add(Mappings.Key, OutInputAxisKeyMapping);
			}
			break;
		case EInputType::GP:
			for (const TPair<float, FRHInputAxisKeyMappings>& Mappings : AppliedAxisKeyMappings[Name].GP_Mappings)
			{
				TArray<FInputAxisKeyMapping> OutInputAxisKeyMapping;
				GetCustomInputAxisKeyMappingsCopyFor(Name, InputType, Mappings.Key, OutInputAxisKeyMapping);
				OutCustomInputAxisKeyMappingsList.Add(Mappings.Key, OutInputAxisKeyMapping);
			}
			break;
		case EInputType::Touch:
			for (const TPair<float, FRHInputAxisKeyMappings>& Mappings : AppliedAxisKeyMappings[Name].Touch_Mappings)
			{
				TArray<FInputAxisKeyMapping> OutInputAxisKeyMapping;
				GetCustomInputAxisKeyMappingsCopyFor(Name, InputType, Mappings.Key, OutInputAxisKeyMapping);
				OutCustomInputAxisKeyMappingsList.Add(Mappings.Key, OutInputAxisKeyMapping);
			}
			break;
		}
	}
}

void URHPlayerInput::GetDefaultInputActionKeys(const FName& Name, EInputType InputType, TArray<FRHInputActionKey>& OutKeys) const
{
	FRHCustomInputActionKeyMappings OutCustomInputActionKeyMappings;
	GetCustomActionMappingForDefaultMapping(Name, OutCustomInputActionKeyMappings);

	TArray<FRHInputActionKeyMapping>* InputActionKeyMappings = nullptr;
	switch (InputType)
	{
	case EInputType::KBM:
		InputActionKeyMappings = &(OutCustomInputActionKeyMappings.KBM_Mappings);
		break;
	case EInputType::GP:
		InputActionKeyMappings = &(OutCustomInputActionKeyMappings.GP_Mappings);
		break;
	case EInputType::Touch:
		InputActionKeyMappings = &(OutCustomInputActionKeyMappings.Touch_Mappings);
		break;
	}

	if (InputActionKeyMappings != nullptr)
	{
		for (const FRHInputActionKeyMapping& InputActionKeyMapping : *InputActionKeyMappings)
		{
			OutKeys.Emplace(InputActionKeyMapping.Mapping.Key, InputActionKeyMapping.Type);
		}
	}
}

void URHPlayerInput::GetCustomInputActionKeys(const FName& Name, EInputType InputType, TArray<FRHInputActionKey>& OutKeys) const
{
    TArray<FRHInputActionKeyMapping> OutInputActionKeyMappings;
    GetCustomInputActionKeyMappingsCopyFor(Name, InputType, OutInputActionKeyMappings);
    for (const FRHInputActionKeyMapping& InputActionKeyMapping : OutInputActionKeyMappings)
    {
        OutKeys.Emplace(InputActionKeyMapping.Mapping.Key, InputActionKeyMapping.Type);
    }
}

void URHPlayerInput::GetDefaultInputAxisKeys(const FName& Name, EInputType InputType, float Scale, TArray<FKey>& OutKeys) const
{
	FRHCustomInputAxisKeyMappings OutCustomInputAxisKeyMappings;
	GetCustomAxisMappingForDefaultMapping(Name, OutCustomInputAxisKeyMappings);

	TMap<float, FRHInputAxisKeyMappings>* InputAxisKeyMappings = nullptr;
	switch (InputType)
	{
	case EInputType::KBM:
		InputAxisKeyMappings = &(OutCustomInputAxisKeyMappings.KBM_Mappings);
		break;
	case EInputType::GP:
		InputAxisKeyMappings = &(OutCustomInputAxisKeyMappings.GP_Mappings);
		break;
	case EInputType::Touch:
		InputAxisKeyMappings = &(OutCustomInputAxisKeyMappings.Touch_Mappings);
		break;
	}

	if (InputAxisKeyMappings != nullptr && InputAxisKeyMappings->Contains(Scale))
	{
		for (const FInputAxisKeyMapping& InputAxisKeyMapping : (*InputAxisKeyMappings)[Scale].InputAxisKeyMappings)
		{
			OutKeys.Add(InputAxisKeyMapping.Key);
		}
	}
}

void URHPlayerInput::GetCustomInputAxisKeys(const FName& Name, EInputType InputType, float Scale, TArray<FKey>& OutKeys) const
{
    TArray<FInputAxisKeyMapping> OutInputAxisKeyMappings;
    GetCustomInputAxisKeyMappingsCopyFor(Name, InputType, Scale, OutInputAxisKeyMappings);
    for (const FInputAxisKeyMapping& InputAxisKeyMapping : OutInputAxisKeyMappings)
    {
        OutKeys.Add(InputAxisKeyMapping.Key);
    }
}

FName URHPlayerInput::GetCustomInputActionWithKeys(const TArray<FRHInputActionKey>& Keys, EInputType InputType) const
{
	for (const TPair<FName, FRHCustomInputActionKeyMappings>& CustomActionKeyMapping : AppliedActionKeyMappings)
	{
		TArray<FRHInputActionKeyMapping> OutInputActionKeyMappings;
		GetCustomInputActionKeyMappingsCopyFor(CustomActionKeyMapping.Key, InputType, OutInputActionKeyMappings);
		if (InputType == EInputType::KBM || InputType == EInputType::Touch)
		{
			for (FRHInputActionKeyMapping& InputActionMapping : OutInputActionKeyMappings)
			{
				FRHInputActionKey InputActionKey(InputActionMapping.Mapping.Key, InputActionMapping.Type);
				if (Keys.Contains(InputActionKey))
				{
					return CustomActionKeyMapping.Key;
				}
			}
		}
		else if (InputType == EInputType::GP)
		{
			bool bContainsAllKeys = Keys.Num() > 0 && Keys.Num() == OutInputActionKeyMappings.Num();
			if (bContainsAllKeys)
			{
				for (FRHInputActionKeyMapping& InputActionMapping : OutInputActionKeyMappings)
				{
					FRHInputActionKey InputActionKey(InputActionMapping.Mapping.Key, InputActionMapping.Type);
					if (!Keys.Contains(InputActionKey))
					{
						bContainsAllKeys = false;
					}
				}
			}
			if (bContainsAllKeys)
			{
				return CustomActionKeyMapping.Key;
			}
		}
	}

	return FName();
}

TPair<FName, float> URHPlayerInput::GetCustomInputAxisWithKeys(const TArray<FKey>& Keys, EInputType InputType) const
{
	for (const TPair<FName, FRHCustomInputAxisKeyMappings>& CustomInputAxisKeyMappings : AppliedAxisKeyMappings)
	{
		TMap<float, TArray<FInputAxisKeyMapping>> OutMappingsList;
		GetCustomInputAxisKeyMappingsCopyFor(CustomInputAxisKeyMappings.Key, InputType, OutMappingsList);
		for (const TPair<float, TArray<FInputAxisKeyMapping>>& Mappings : OutMappingsList)
		{
			if (InputType == EInputType::KBM || InputType == EInputType::Touch)
			{
				TArray<FInputAxisKeyMapping> AxisKeyBindingsToRemove;
				for (const FInputAxisKeyMapping& Mapping : Mappings.Value)
				{
					if (Keys.Contains(Mapping.Key))
					{
						return TPair<FName,float>(CustomInputAxisKeyMappings.Key, Mappings.Key);
					}
				}
			}
			else if (InputType == EInputType::GP)
			{
				bool bContainsAllKeys = Keys.Num() > 0 && Keys.Num() == Mappings.Value.Num();
				if (bContainsAllKeys)
				{
					for (const FInputAxisKeyMapping& Mapping : Mappings.Value)
					{
						if (!Keys.Contains(Mapping.Key))
						{
							bContainsAllKeys = false;
						}
					}
				}
				if (bContainsAllKeys)
				{
					return TPair<FName, float>(CustomInputAxisKeyMappings.Key, Mappings.Key);
				}
			}
		}
	}
	return TPair<FName, float>(FName(), 0.f);
}

void URHPlayerInput::SetCustomInputActionKeyMapping(const FName& Name, const TArray<FRHInputActionKey>& Keys, EInputType InputType, TArray<FName>& OutModifiedKeybindNames)
{
	// If a Tied Input Action is being modified, switch all tied actions for the keys.
	if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
	{
		for (const FRHInputActionTiedNames& InputActionTiedName : DefaultUserSettings->InputActionTiedNames)
		{
			if (InputActionTiedName.Press == Name || InputActionTiedName.Hold == Name || InputActionTiedName.Repeat == Name)
			{
				TArray<FRHInputActionKey> KeysCopy = Keys;
				if (!InputActionTiedName.Press.IsNone())
				{
					for (FRHInputActionKey& InputActionKey : KeysCopy)
					{
						InputActionKey.Type = EInputActionType::Press;
					}
					SetCustomInputActionKeyMapping_Internal(InputActionTiedName.Press, KeysCopy, InputType, OutModifiedKeybindNames);
				}
				if (!InputActionTiedName.Hold.IsNone())
				{
					for (FRHInputActionKey& InputActionKey : KeysCopy)
					{
						InputActionKey.Type = EInputActionType::Hold;
					}
					SetCustomInputActionKeyMapping_Internal(InputActionTiedName.Hold, KeysCopy, InputType, OutModifiedKeybindNames);
				}
				if (!InputActionTiedName.Repeat.IsNone())
				{
					for (FRHInputActionKey& InputActionKey : KeysCopy)
					{
						InputActionKey.Type = EInputActionType::Repeat;
					}
					SetCustomInputActionKeyMapping_Internal(InputActionTiedName.Repeat, KeysCopy, InputType, OutModifiedKeybindNames);

					OnKeyMappingsUpdated.Broadcast();
				}

				return;
			}
		}
	}

	// If the keys to be resolved are an axis, switch all keys.
	TArray<FKey> NormalKeys;
	for (const FRHInputActionKey& InputActionKey : Keys)
	{
		NormalKeys.Add(InputActionKey.Key);
	}
	const TPair<FName, float> AxisNameScale = GetCustomInputAxisWithKeys(NormalKeys, InputType);
	if (!AxisNameScale.Key.IsNone())
	{
		TArray<FRHInputActionKey> OutCurrentKeys;
		GetCustomInputActionKeys(Name, InputType, OutCurrentKeys);

		TArray<FKey> NormalCurrentKeys;
		for (FRHInputActionKey& InputActionKey : OutCurrentKeys)
		{
			NormalCurrentKeys.Add(InputActionKey.Key);
		}

		SetCustomInputAxisKeyMapping(AxisNameScale.Key, NormalCurrentKeys, InputType, AxisNameScale.Value, OutModifiedKeybindNames);
		return;
	}

	// If the keys to be resolved are a Tied Input Action, switch all keys.
	TArray<FRHInputActionKey> PressReplaceKeys = Keys;
	for (FRHInputActionKey& InputActionKey : PressReplaceKeys)
	{
		InputActionKey.Type = EInputActionType::Press;
	}
	const FName PressReplaceName = GetCustomInputActionWithKeys(PressReplaceKeys, InputType);

	TArray<FRHInputActionKey> HoldReplaceKeys = Keys;
	for (FRHInputActionKey& InputActionKey : HoldReplaceKeys)
	{
		InputActionKey.Type = EInputActionType::Hold;
	}
	const FName HoldReplaceName = GetCustomInputActionWithKeys(HoldReplaceKeys, InputType);

	TArray<FRHInputActionKey> RepeatReplaceKeys = Keys;
	for (FRHInputActionKey& InputActionKey : RepeatReplaceKeys)
	{
		InputActionKey.Type = EInputActionType::Repeat;
	}
	const FName RepeatReplaceName = GetCustomInputActionWithKeys(RepeatReplaceKeys, InputType);

	if (URHPlayerInputDefault* DefaultUserSettings = Cast<URHPlayerInputDefault>(URHPlayerInputDefault::StaticClass()->GetDefaultObject()))
	{
		for (const FRHInputActionTiedNames& InputActionTiedName : DefaultUserSettings->InputActionTiedNames)
		{
			if (InputActionTiedName.Press == PressReplaceName && InputActionTiedName.Hold == HoldReplaceName && InputActionTiedName.Repeat == RepeatReplaceName)
			{
				TArray<FRHInputActionKey> OutCurrentKeys;
				GetCustomInputActionKeys(Name, InputType, OutCurrentKeys);

				for (FRHInputActionKey& InputActionKey : OutCurrentKeys)
				{
					InputActionKey.Type = EInputActionType::Press;
				}

				const FName CurrentPressName = GetCustomInputActionWithKeys(OutCurrentKeys, InputType);
				if (!CurrentPressName.IsNone())
				{
					SetCustomInputActionKeyMapping_Internal(CurrentPressName, PressReplaceKeys, InputType, OutModifiedKeybindNames);
				}
				else if (!PressReplaceName.IsNone())
				{
					ResolveCustomInputActionKeyMappingConflicts(PressReplaceKeys, OutCurrentKeys, InputType, OutModifiedKeybindNames);
				}

				for (FRHInputActionKey& InputActionKey : OutCurrentKeys)
				{
					InputActionKey.Type = EInputActionType::Hold;
				}

				const FName CurrentHoldName = GetCustomInputActionWithKeys(OutCurrentKeys, InputType);
				if (!CurrentHoldName.IsNone())
				{
					SetCustomInputActionKeyMapping_Internal(CurrentHoldName, HoldReplaceKeys, InputType, OutModifiedKeybindNames);
				}
				else if (!HoldReplaceName.IsNone())
				{
					ResolveCustomInputActionKeyMappingConflicts(HoldReplaceKeys, OutCurrentKeys, InputType, OutModifiedKeybindNames);
				}

				for (FRHInputActionKey& InputActionKey : OutCurrentKeys)
				{
					InputActionKey.Type = EInputActionType::Repeat;
				}

				const FName CurrentRepeatName = GetCustomInputActionWithKeys(OutCurrentKeys, InputType);
				if (!CurrentRepeatName.IsNone())
				{
					SetCustomInputActionKeyMapping_Internal(CurrentRepeatName, RepeatReplaceKeys, InputType, OutModifiedKeybindNames);
				}
				else if (!RepeatReplaceName.IsNone())
				{
					ResolveCustomInputActionKeyMappingConflicts(RepeatReplaceKeys, OutCurrentKeys, InputType, OutModifiedKeybindNames);
				}

				// We made room for the action to be bound by resolving the replace conflicts, now we can set safely without breaking any tied bindings
				if (CurrentPressName.IsNone() && CurrentHoldName.IsNone() && CurrentRepeatName.IsNone())
				{
					SetCustomInputActionKeyMapping_Internal(Name, Keys, InputType, OutModifiedKeybindNames);
				}

				OnKeyMappingsUpdated.Broadcast();
				return;
			}
		}
	}

	// Nothing special is blocking this new binding.
	SetCustomInputActionKeyMapping_Internal(Name, Keys, InputType, OutModifiedKeybindNames);
}

void URHPlayerInput::SetCustomInputActionKeyMapping_Internal(const FName& Name, const TArray<FRHInputActionKey>& Keys, EInputType InputType, TArray<FName>& OutModifiedKeybindNames)
{
    if (TArray<FRHInputActionKeyMapping>* const InputActionKeyMappings = GetCustomInputActionKeyMappingsFor(Name, InputType))
    {
		TArray<FRHInputActionKey> ReplacementKeys;
        TArray<FKey> NormalReplacementKeys;
        ReplacementKeys.Reserve(InputActionKeyMappings->Num());
        for (const FRHInputActionKeyMapping& InputActionMapping : *InputActionKeyMappings)
        {
			const FRHInputActionKey InputActionKey(InputActionMapping.Mapping.Key, InputActionMapping.Type);
			ReplacementKeys.Add(InputActionKey);
            NormalReplacementKeys.Add(InputActionMapping.Mapping.Key);
            RemoveActionMapping(InputActionMapping.Mapping);
        }
		InputActionKeyMappings->Empty();

        if (Keys.Num() > 0)
        {
            ResolveCustomInputActionKeyMappingConflicts(Keys, ReplacementKeys, InputType, OutModifiedKeybindNames);

			TArray<FKey> NormalKeys;
			for (const FRHInputActionKey& Key : Keys)
			{
				NormalKeys.Add(Key.Key);
			}
            ResolveCustomInputAxisKeyMappingConflicts(NormalKeys, NormalReplacementKeys, InputType, OutModifiedKeybindNames);

            for (const FRHInputActionKey& ActionKey : Keys)
            {
				const FInputActionKeyMapping Mapping(Name, ActionKey.Key);
				InputActionKeyMappings->Emplace(Mapping, ActionKey.Type);
                AddActionMapping(InputActionKeyMappings->Last().Mapping);
            }
        }

        OutModifiedKeybindNames.Add(Name);
    }
	
	OnKeyMappingsUpdated.Broadcast();
}

void URHPlayerInput::SetCustomInputAxisKeyMapping(const FName& Name, const TArray<FKey>& Keys, EInputType InputType, float Scale, TArray<FName>& OutModifiedKeybindNames)
{
    if (TArray<FInputAxisKeyMapping>* const Mappings = GetCustomInputAxisKeyMappingsFor(Name, InputType, Scale))
    {
        TArray<FKey> ReplacementKeys;
        ReplacementKeys.Reserve(Mappings->Num());
        for (const FInputAxisKeyMapping& Mapping : *Mappings)
        {
            ReplacementKeys.Add(Mapping.Key);
            RemoveAxisMapping(Mapping);
        }
        Mappings->Empty();

        if (Keys.Num() > 0)
        {
			// Make sure to swap InputAction Mappings with the old axis key for all types of InputAction
			TArray<FRHInputActionKey> ActionKeys;
			for (const FKey& Key : Keys)
			{
				ActionKeys.Emplace(Key, EInputActionType::Press);
			}
			TArray<FRHInputActionKey> ActionReplacementKeys;
			for (const FKey& ReplacementKey : ReplacementKeys)
			{
				ActionReplacementKeys.Emplace(ReplacementKey, EInputActionType::Press);
			}
            ResolveCustomInputActionKeyMappingConflicts(ActionKeys, ActionReplacementKeys, InputType, OutModifiedKeybindNames);
			
			for (FRHInputActionKey& ActionKey : ActionKeys)
			{
				ActionKey.Type = EInputActionType::Hold;
			}
			for (FRHInputActionKey& ActionReplacementKey : ActionReplacementKeys)
			{
				ActionReplacementKey.Type = EInputActionType::Hold;
			}
			ResolveCustomInputActionKeyMappingConflicts(ActionKeys, ActionReplacementKeys, InputType, OutModifiedKeybindNames);

			for (FRHInputActionKey& ActionKey : ActionKeys)
			{
				ActionKey.Type = EInputActionType::Repeat;
			}
			for (FRHInputActionKey& ActionReplacementKey : ActionReplacementKeys)
			{
				ActionReplacementKey.Type = EInputActionType::Repeat;
			}
			ResolveCustomInputActionKeyMappingConflicts(ActionKeys, ActionReplacementKeys, InputType, OutModifiedKeybindNames);

            ResolveCustomInputAxisKeyMappingConflicts(Keys, ReplacementKeys, InputType, OutModifiedKeybindNames);
			
            for (const FKey& Key : Keys)
            {
                Mappings->Emplace(Name, Key, Scale);
                AddAxisMapping(Mappings->Last());
            }
        }

        OutModifiedKeybindNames.Add(Name);
    }

    OnKeyMappingsUpdated.Broadcast();
}

void URHPlayerInput::ResolveCustomInputActionKeyMappingConflicts(const TArray<FRHInputActionKey>& ConflictingKeys, const TArray<FRHInputActionKey>& ReplacementKeys, EInputType InputType, TArray<FName>& OutModifiedKeybindNames)
{
    for (const TPair<FName, FRHCustomInputActionKeyMappings>& CustomInputActionKeyMappings : AppliedActionKeyMappings)
    {
		TArray<FName> KeybindNamesToResolve;
		GetCustomActionKeyMappingNames(KeybindNamesToResolve);
		GetCustomAxisKeyMappingNames(KeybindNamesToResolve);
        if (!KeybindNamesToResolve.Contains(CustomInputActionKeyMappings.Key))
        {
            continue;
        }

        if (TArray<FRHInputActionKeyMapping>* const InputActionKeyMappings = GetCustomInputActionKeyMappingsFor(CustomInputActionKeyMappings.Key, InputType))
        {
			if (DoesInputTypeSupportChords(InputType))
			{
				bool bContainsAllKeys = ConflictingKeys.Num() > 0 && ConflictingKeys.Num() == InputActionKeyMappings->Num();
				if (bContainsAllKeys)
				{
					for (FRHInputActionKeyMapping& InputActionMapping : *InputActionKeyMappings)
					{
						FRHInputActionKey InputActionKey(InputActionMapping.Mapping.Key, InputActionMapping.Type);
						if (!ConflictingKeys.Contains(InputActionKey))
						{
							bContainsAllKeys = false;
						}
					}
				}
				if (bContainsAllKeys)
				{
					for (const FRHInputActionKeyMapping& InputActionMapping : *InputActionKeyMappings)
					{
						RemoveActionMapping(InputActionMapping.Mapping);
					}
					InputActionKeyMappings->Empty();

					for (const FRHInputActionKey& InputActionKey : ReplacementKeys)
					{
						FInputActionKeyMapping Mapping(CustomInputActionKeyMappings.Key, InputActionKey.Key);
						InputActionKeyMappings->Emplace(Mapping, InputActionKey.Type);
						AddActionMapping(InputActionKeyMappings->Last().Mapping);
					}

					OutModifiedKeybindNames.Add(CustomInputActionKeyMappings.Key);
				}
			}
			else
            {
                TArray<FRHInputActionKeyMapping> ActionKeyBindingsToRemove;
                for (FRHInputActionKeyMapping& InputActionMapping : *InputActionKeyMappings)
                {
					FRHInputActionKey InputActionKey(InputActionMapping.Mapping.Key, InputActionMapping.Type);
                    if (ConflictingKeys.Contains(InputActionKey))
                    {
						RemoveActionMapping(InputActionMapping.Mapping);

                        const int32 ConflictingKeyIndex = ConflictingKeys.Find(InputActionKey);
                        if (ReplacementKeys.IsValidIndex(ConflictingKeyIndex))
                        {
							const FRHInputActionKey Replacement = ReplacementKeys[ConflictingKeyIndex];
							InputActionMapping.Mapping.Key = Replacement.Key;
							InputActionMapping.Type = Replacement.Type;
                            AddActionMapping(InputActionMapping.Mapping);
                        }
                        else
                        {
                            ActionKeyBindingsToRemove.AddUnique(InputActionMapping);
                        }
                        OutModifiedKeybindNames.Add(CustomInputActionKeyMappings.Key);
                    }
                }
                for (const FRHInputActionKeyMapping& ActionKeyBindingToRemove : ActionKeyBindingsToRemove)
                {
					InputActionKeyMappings->Remove(ActionKeyBindingToRemove);
                }
            }
        }
    }
}

void URHPlayerInput::ResolveCustomInputAxisKeyMappingConflicts(const TArray<FKey>& ConflictingKeys, const TArray<FKey>& ReplacementKeys, EInputType InputType, TArray<FName>& OutModifiedKeybindNames)
{
    for (const TPair<FName, FRHCustomInputAxisKeyMappings>& CustomInputAxisKeyMappings : AppliedAxisKeyMappings)
    {
		TArray<FName> KeybindNamesToResolve;
		GetCustomActionKeyMappingNames(KeybindNamesToResolve);
		GetCustomAxisKeyMappingNames(KeybindNamesToResolve);
        if (!KeybindNamesToResolve.Contains(CustomInputAxisKeyMappings.Key))
        {
            continue;
        }

        TMap<float, TArray<FInputAxisKeyMapping>*> OutMappingsList;
        GetCustomInputAxisKeyMappingsFor(CustomInputAxisKeyMappings.Key, InputType, OutMappingsList);
        for(const TPair<float, TArray<FInputAxisKeyMapping>*>& Mappings : OutMappingsList)
        {
            if (Mappings.Value == nullptr)
            {
                continue;
            }

			if (DoesInputTypeSupportChords(InputType))
			{
				bool bContainsAllKeys = ConflictingKeys.Num() > 0 && ConflictingKeys.Num() == Mappings.Value->Num();
				if (bContainsAllKeys)
				{
					for (FInputAxisKeyMapping& Mapping : *Mappings.Value)
					{
						if (!ConflictingKeys.Contains(Mapping.Key))
						{
							bContainsAllKeys = false;
						}
					}
				}
				if (bContainsAllKeys)
				{
					for (const FInputAxisKeyMapping& Mapping : *Mappings.Value)
					{
						RemoveAxisMapping(Mapping);
					}
					Mappings.Value->Empty();

					for (const FKey& Key : ReplacementKeys)
					{
						Mappings.Value->Emplace(CustomInputAxisKeyMappings.Key, Key, Mappings.Key);
						AddAxisMapping(Mappings.Value->Last());
					}

					OutModifiedKeybindNames.Add(CustomInputAxisKeyMappings.Key);
				}
			}
			else
            {
                TArray<FInputAxisKeyMapping> AxisKeyBindingsToRemove;
                for (FInputAxisKeyMapping& Mapping : *Mappings.Value)
                {
                    if (ConflictingKeys.Contains(Mapping.Key))
                    {
                        const int32 ConflictingKeyIndex = ConflictingKeys.Find(Mapping.Key);
                        const FKey Replacement = ReplacementKeys.IsValidIndex(ConflictingKeyIndex) ? ReplacementKeys[ConflictingKeyIndex] : EKeys::Invalid;

                        RemoveAxisMapping(Mapping);
                        if (Replacement != EKeys::Invalid)
                        {
                            Mapping.Key = Replacement;
                            AddAxisMapping(Mapping);
                        }
                        else
                        {
                            AxisKeyBindingsToRemove.AddUnique(Mapping);
                        }
                        OutModifiedKeybindNames.Add(CustomInputAxisKeyMappings.Key);
                    }
                }
                for (const FInputAxisKeyMapping& AxisKeyBindingToRemove : AxisKeyBindingsToRemove)
                {
                    Mappings.Value->Remove(AxisKeyBindingToRemove);
                }
            }
        }
    }
}

void URHPlayerInput::GetSoloInputActionKeyMappings(const TArray<FName>& MappingNames, TArray<TPair<FName, FRHInputActionKey>>& OutSoloMappings)
{
    TArray<FRHInputActionKey> OutKeys;
    for (const FName& MappingName : MappingNames)
    {
		GetCustomInputActionKeys(MappingName, EInputType::KBM, OutKeys);
		if (!DoesInputTypeSupportChords(EInputType::KBM) || OutKeys.Num() == 1)
		{
			for (FRHInputActionKey& InputActionKey : OutKeys)
			{
				OutSoloMappings.Add(TPair<FName, FRHInputActionKey>(MappingName, InputActionKey));
			}
		}
		OutKeys.Empty();
        GetCustomInputActionKeys(MappingName, EInputType::GP, OutKeys);
		if (!DoesInputTypeSupportChords(EInputType::GP) || OutKeys.Num() == 1)
		{
			for (FRHInputActionKey& InputActionKey : OutKeys)
			{
				OutSoloMappings.Add(TPair<FName, FRHInputActionKey>(MappingName, InputActionKey));
			}
		}
		OutKeys.Empty();
		GetCustomInputActionKeys(MappingName, EInputType::Touch, OutKeys);
		if (!DoesInputTypeSupportChords(EInputType::Touch) || OutKeys.Num() == 1)
		{
			for (FRHInputActionKey& InputActionKey : OutKeys)
			{
				OutSoloMappings.Add(TPair<FName, FRHInputActionKey>(MappingName, InputActionKey));
			}
		}
		OutKeys.Empty();
    }
}

void URHPlayerInput::GetChordInputActionKeyMappings(const TArray<FName>& MappingNames, TArray<TPair<FName, TArray<FRHInputActionKey>>>& OutChordMappings)
{
    TArray<FRHInputActionKey> OutKeys;
    for (const FName& MappingName : MappingNames)
    {
		if (DoesInputTypeSupportChords(EInputType::KBM))
		{
			GetCustomInputActionKeys(MappingName, EInputType::KBM, OutKeys);
			if (OutKeys.Num() > 1)
			{
				OutChordMappings.Add(TPair<FName, TArray<FRHInputActionKey>>(MappingName, OutKeys));
			}
			OutKeys.Empty();
		}
		if (DoesInputTypeSupportChords(EInputType::GP))
		{
			GetCustomInputActionKeys(MappingName, EInputType::GP, OutKeys);
			if (OutKeys.Num() > 1)
			{
				OutChordMappings.Add(TPair<FName, TArray<FRHInputActionKey>>(MappingName, OutKeys));
			}
			OutKeys.Empty();
		}
		if (DoesInputTypeSupportChords(EInputType::Touch))
		{
			GetCustomInputActionKeys(MappingName, EInputType::Touch, OutKeys);
			if (OutKeys.Num() > 1)
			{
				OutChordMappings.Add(TPair<FName, TArray<FRHInputActionKey>>(MappingName, OutKeys));
			}
			OutKeys.Empty();
		}
    }
}

float URHPlayerInput::MassageAxisInput(FKey Key, float RawValue)
{
	if (Key == EKeys::Gamepad_LeftX)
	{
		if (LeftAnalogStickFilter != nullptr)
		{
			if (!bHasProcessedLeftStick)
			{
				FKeyState* GamepadLeftY = GetKeyState(EKeys::Gamepad_LeftY);
				FVector2D RawInput(RawValue, GamepadLeftY != nullptr ? GamepadLeftY->RawValue.X : 0.f);
				ProcessedLeftStick = LeftAnalogStickFilter->FilterStickInput(RawInput);
				bHasProcessedLeftStick = true;
				if (GamepadLeftY != nullptr)
				{
					GamepadLeftY->Value.X = ProcessedLeftStick.Y;
				}
				return ProcessedLeftStick.X;
			}
			else
			{
				return ProcessedLeftStick.X;
			}
		}
	}
	else if (Key == EKeys::Gamepad_LeftY)
	{
		if (LeftAnalogStickFilter != nullptr)
		{
			if (!bHasProcessedLeftStick)
			{
				FKeyState* GamepadLeftX = GetKeyState(EKeys::Gamepad_LeftX);
				FVector2D RawInput(GamepadLeftX != nullptr ? GamepadLeftX->RawValue.X : 0.0f, RawValue);
				ProcessedLeftStick = LeftAnalogStickFilter->FilterStickInput(RawInput);
				bHasProcessedLeftStick = true;
				if (GamepadLeftX != nullptr)
				{
					GamepadLeftX->Value.X = ProcessedLeftStick.X;
				}
				return ProcessedLeftStick.Y;
			}
			else
			{
				return ProcessedLeftStick.Y;
			}
		}
	}
	else if (Key == EKeys::Gamepad_RightX)
	{
		if (RightAnalogStickFilter != nullptr)
		{
			if (!bHasProcessedRightStick)
			{
				FKeyState* GamepadRightY = GetKeyState(EKeys::Gamepad_RightY);
				FVector2D RawInput(RawValue, GamepadRightY != nullptr ? GamepadRightY->RawValue.X : 0.0f);
				ProcessedRightStick = RightAnalogStickFilter->FilterStickInput(RawInput);
				bHasProcessedRightStick = true;
				if (GamepadRightY != nullptr)
				{
					GamepadRightY->Value.X = ProcessedRightStick.Y;
				}
				return ProcessedRightStick.X;
			}
			else
			{
				return ProcessedRightStick.X;
			}
		}
	}
	else if (Key == EKeys::Gamepad_RightY)
	{
		if (RightAnalogStickFilter != nullptr)
		{
			if (!bHasProcessedRightStick)
			{
				FKeyState* GamepadRightX = GetKeyState(EKeys::Gamepad_RightX);
				FVector2D RawInput(GamepadRightX != nullptr ? GamepadRightX->RawValue.X : 0.0f, RawValue);
				ProcessedRightStick = RightAnalogStickFilter->FilterStickInput(RawInput);
				bHasProcessedRightStick = true;
				if (GamepadRightX != nullptr)
				{
					GamepadRightX->Value.X = ProcessedRightStick.X;
				}
				return ProcessedRightStick.Y;
			}
			else
			{
				return ProcessedRightStick.Y;
			}
		}
	}
#if PLATFORM_DESKTOP
    else if (FSlateApplication::Get().IsFakingTouchEvents())
    {
        if (Key == EKeys::MouseX)
        {
            FKeyState* MouseX = GetKeyState(EKeys::MouseX);
            if (MouseX != nullptr)
            {
                MouseX->Value.X = 0.0f;
            }
            return 0.0f;
        }
        else if (Key == EKeys::MouseY)
        {
            FKeyState* MouseY = GetKeyState(EKeys::MouseY);
            if (MouseY != nullptr)
            {
                MouseY->Value.X = 0.0f;
            }
            return 0.0f;
        }
    }        
#endif

	return Super::MassageAxisInput(Key, RawValue);
}

void URHPlayerInput::SaveStickFilterConfig()
{
    if (LeftAnalogStickFilter != nullptr)
    {
        LeftAnalogStickFilter->SaveConfig();
    }

    if (RightAnalogStickFilter != nullptr)
    {
        RightAnalogStickFilter->SaveConfig();
    }
}

bool URHPlayerInput::InputKey(const FInputKeyParams& Params)
{
	bool RetVal = Super::InputKey(Params);

	if (Params.Key.IsTouch())
	{
		UpdateInputState(PIS_TOUCH, Params.Delta.Y != 0 || Params.Delta.Z != 0, Params.Get1DAxisDelta());
	}
	else if (Params.IsGamepad())
	{
		UpdateInputState(PIS_GAMEPAD, Params.Delta.Y != 0 || Params.Delta.Z != 0, Params.Get1DAxisDelta());
	}
	else
	{
		UpdateInputState(PIS_KEYMOUSE, Params.Delta.Y != 0 || Params.Delta.Z != 0, Params.Get1DAxisDelta());
	}

	return RetVal;
}

bool URHPlayerInput::InputTouch(uint32 Handle, ETouchType::Type Type, const FVector2D& TouchLocation, float Force, FDateTime DeviceTimestamp, uint32 TouchpadIndex)
{
	bool RetVal = Super::InputTouch(Handle, Type, TouchLocation, Force, DeviceTimestamp, TouchpadIndex);

	UpdateInputState(PIS_TOUCH, false, 0.f);

	return RetVal;
}

bool URHPlayerInput::InputMotion(const FVector& Tilt, const FVector& RotationRate, const FVector& Gravity, const FVector& Acceleration)
{
	bool RetVal = Super::InputMotion(Tilt, RotationRate, Gravity, Acceleration);

	UpdateInputState(PIS_TOUCH, false, 0.f);

	return RetVal;
}

bool URHPlayerInput::InputGesture(const FKey Gesture, const EInputEvent Event, const float Value)
{
	bool RetVal = Super::InputGesture(Gesture, Event, Value);

	UpdateInputState(PIS_TOUCH, false, 0.f);

	return RetVal;
}

void URHPlayerInput::ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused)
{
	Super::ProcessInputStack(InputComponentStack, DeltaTime, bGamePaused);

	//Base input system was giving weird results based on viewport capture states when directly looking at
	//      number of mouse samples over a period of time.  Since that was inconsistent, directly use the
	//      mouse cursor and query changes to the position of the mouse.
	if (InputState != PIS_KEYMOUSE &&
		InputState != PIS_TOUCH)
	{
		APlayerController* PlayerController = GetOuterAPlayerController();
		ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(PlayerController->Player);
		if (LocalPlayer && LocalPlayer->ViewportClient)
		{
			static FVector2D PreviousMousePos, MousePos;
			LocalPlayer->ViewportClient->GetMousePosition(MousePos);
			if (MousePos != PreviousMousePos)
			{
				CurrentDelta += DeltaTime;
			}
			else
			{
				CurrentDelta = 0.f;
			}
			PreviousMousePos = MousePos;
			UpdateInputState(PIS_KEYMOUSE, true, CurrentDelta);
		}
	}

	bHasProcessedLeftStick = false;
	bHasProcessedRightStick = false;
}

bool URHPlayerInput::CanChangeToInputState(RH_INPUT_STATE NewState, bool bAxis, float Delta)
{
	if (NewState == InputState)
	{
		return false;
	}

	switch (NewState)
	{
	case PIS_KEYMOUSE:
	{
		//Ignore 0 axis inputs
		if (InputState == PIS_GAMEPAD)
		{
			//If we're an axis, directly use delta.  Else assume mouse
			return bAxis ? FMath::Abs(Delta) > KeyMouseSwitchDelta : true;
		}

#if !UE_SERVER
		// Don't allow switching from touch to keymouse if we're faking touch events
		if (InputState == PIS_TOUCH && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsFakingTouchEvents())
		{
			return false;
		}
#endif

		// A bit of a hack, but IOS can't determine if keyboard events are from hardware or virtual keyboard, so just don't allow PIS_KEYMOUSE for now
		const FString PlatformName = UGameplayStatics::GetPlatformName();
		if (PlatformName == TEXT("IOS"))
		{
			return false;
		}

		return true;
		break;
	}
	case PIS_GAMEPAD:
		//Ignore 0 button inputs
		if (!bAxis && Delta == 0.f)
		{
			return false;
		}
		//Prevent spam hopping if a player has a gamepad plugged in, but not in active use
		if (FMath::Abs(Delta - PrevGamepadDelta) < GamepadSwitchDelta)
		{
			return false;
		}
		PrevGamepadDelta = Delta;
		return true;
		break;
	case PIS_TOUCH:
		return false;
	default:
		return true;
	}
}

void URHPlayerInput::UpdateInputState(RH_INPUT_STATE NewState, bool bAxis, float Delta)
{
	if (CanChangeToInputState(NewState, bAxis, Delta))
	{
		CurrentDelta = 0.f;
		InputState = NewState;
		LastInputState = NewState;
		InputStateChangedEvent.Broadcast(InputState);
	}
}

FKey URHPlayerInput::GetGamepadConfirmButton()
{
	return EKeys::Virtual_Accept;
}

FKey URHPlayerInput::GetGamepadCancelButton()
{
	return EKeys::Virtual_Back;
}

