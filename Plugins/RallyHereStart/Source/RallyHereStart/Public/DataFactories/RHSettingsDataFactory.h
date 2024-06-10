// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataFactories/RHDataFactory.h"
#include "GameFramework/RHPlayerInput.h"
#include "GameFramework/RHSettingsCallbackInterface.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RHSettingsDataFactory.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoolSettingUpdate, bool, SettingValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIntSettingUpdate, int32, SettingValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIntPointSettingUpdate, FIntPoint, SettingValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloatSettingUpdate, float, SettingValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKeyBindSettingsUpdate, FName, KeyBindName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStringSettingsUpdate, FString, StringValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRHSettingValueChanged, FName, SettingId, int32, SettingValue);

class ARHHUDCommon;
class URHPlayerInput;
class UInputSettings;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsReceivedFromPlayerAccount);

UENUM(BlueprintType)
enum class ERHSettingUIType : uint8
{
	Header,
	Slider,
	OptionStepper,
	Checkbox,
	Button,
	Dropdown,
	KeyBinding,
};


/*
	We want the list of options to be extensible while retaining BP compatibility and the enum class look.
	As such, we took a queue from the MatchState in GameMode.h, which faces the same issue.
	This prevents index collision.  For game-specific extensions, please use the project abbreviation as the start
	of the variable name.
	Note: not all of these are handled by the base implementation of OnSettingChanged.
*/
namespace RHSettingId
{
	extern RALLYHERESTART_API const FName None;
	extern RALLYHERESTART_API const FName Resolution;
	extern RALLYHERESTART_API const FName ScreenMode;
	extern RALLYHERESTART_API const FName GlobalQuality;
	extern RALLYHERESTART_API const FName ViewDistanceQuality;
	extern RALLYHERESTART_API const FName ShadowQuality;
	extern RALLYHERESTART_API const FName TextureQuality;
	extern RALLYHERESTART_API const FName VisualEffectQuality;
	extern RALLYHERESTART_API const FName PostProcessingQuality;
	extern RALLYHERESTART_API const FName FoliageQuality;
	extern RALLYHERESTART_API const FName Antialiasing;
	extern RALLYHERESTART_API const FName MotionBlur;
	extern RALLYHERESTART_API const FName AutoDetect;
	extern RALLYHERESTART_API const FName MasterVolume;
	extern RALLYHERESTART_API const FName MusicVolume;
	extern RALLYHERESTART_API const FName VoiceVolume;
	extern RALLYHERESTART_API const FName SFXVolume;
	extern RALLYHERESTART_API const FName SafeFrame;
	extern RALLYHERESTART_API const FName BackgroundAudio;
	extern RALLYHERESTART_API const FName MicEnabled;
	extern RALLYHERESTART_API const FName TextChatEnabled;
	extern RALLYHERESTART_API const FName PushToTalk;
}

namespace ConstConfigPropertyGroups
{
	const int32 PLAYER_KEYBINDS = 1;
	const int32 PLAYER_PREFERENCES = 2;
	const int32 PLAYER_TRACKING = 3;
}

namespace ConstConfigPropertyIds
{
	const int32 PLAYER_NPE_GUIDED_TUTORIAL_SCENES = 83;
	const int32 PLAYER_CURRENT_CLIENT_DATA_VERSION = 113;
	const int32 LANGUAGE_ID = 120;
	const int32 FPS_ENABLED = 121;
}

namespace ConstSettingsPropertyIds
{
	const int32 SETTINGS_VERSION_MAJOR = 73;
	const int32 SETTINGS_VERSION_MINOR = 74;
	const int32 CLIENT_DATA_VERSION = 11;
}

USTRUCT(BlueprintType)
struct FRHAllowedPlatformTypes
{
    GENERATED_USTRUCT_BODY()
public:
    FRHAllowedPlatformTypes()
        : XBoxOne(true)
        , PS4(true)
        , Switch(true)
        , Windows(true)
        , Mac(true)
        , Linux(true)
        , IOS(true)
        , Android(true)
		, XSX(true)
		, PS5(true)
    {}

public:
	bool IsPlatformAllowed(const FString& PlatformName) const
	{
		if (PlatformName == TEXT("XboxOne")) return XBoxOne;
		if (PlatformName == TEXT("XB1")) return XBoxOne;
		if (PlatformName == TEXT("XSX")) return XSX;
		if (PlatformName == TEXT("PS4")) return PS4;
		if (PlatformName == TEXT("PS5")) return PS5;
		if (PlatformName == TEXT("Linux")) return Linux;
		if (PlatformName == TEXT("Mac")) return Mac;
		if (PlatformName == TEXT("Windows")) return Windows;
		if (PlatformName == TEXT("WinGDK")) return Windows;
		if (PlatformName == TEXT("Switch")) return Switch;
		if (PlatformName == TEXT("IOS")) return IOS;
		if (PlatformName == TEXT("Android")) return Android;

		return false;
	}

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool XBoxOne;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool PS4;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool Switch;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool Windows;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool Mac;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool Linux;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool IOS;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool Android;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	bool XSX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	bool PS5;
};


USTRUCT(BlueprintType)
struct FRHSettingsState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bIsGamepadAttached;
	UPROPERTY()
	bool bIsMouseAttached;
	UPROPERTY()
	bool bIsDockedMode;
	UPROPERTY()
	bool bIsHandheldMode;
	UPROPERTY()
	bool bIsTouchMode;

	FORCEINLINE bool operator==(const FRHSettingsState& Other) const
	{
		return bIsGamepadAttached == Other.bIsGamepadAttached
			&& bIsMouseAttached == Other.bIsMouseAttached
			&& bIsDockedMode == Other.bIsDockedMode
			&& bIsHandheldMode == Other.bIsHandheldMode
			&& bIsTouchMode == Other.bIsTouchMode;
	}
	FORCEINLINE bool operator!=(const FRHSettingsState& Other) const
	{
		return !(*this == Other);
	}

	FRHSettingsState() :
		bIsGamepadAttached(false),
		bIsMouseAttached(false),
		bIsDockedMode(false),
		bIsHandheldMode(false),
		bIsTouchMode(false)
	{ }
};

USTRUCT(BlueprintType)
struct FRHRequiredInputTypes
{
    GENERATED_USTRUCT_BODY()
public:
    FRHRequiredInputTypes()
        : Gamepad(false)
        , Mouse(false)
		, Touch(false)
    {}

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool Gamepad;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    bool Mouse;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	bool Touch;
};

USTRUCT(BlueprintType)
struct FRHSettingsWidgetConfig
{
    GENERATED_USTRUCT_BODY()

public:
	FRHSettingsWidgetConfig();

	// Widget class used to control the setting
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    TSubclassOf<class URHSettingsWidget> WidgetClass;

	// Settings Info class used for overriding
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    TSubclassOf<class URHSettingsInfoBase> SettingInfo;
};

USTRUCT(BlueprintType)
struct FColorOptions
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair Color")
		FText OptionName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair Color")
		FLinearColor OptionColor;

	FColorOptions()
		: OptionName(FText::FromString(TEXT("")))
		, OptionColor(FLinearColor::White)
	{}
};

UCLASS(BlueprintType)
class URHSettingsColorOptionsAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	URHSettingsColorOptionsAsset()
		: ColorOptions()
	{}

	// Custom Color Options
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Color Options")
	TArray<struct FColorOptions> ColorOptions;
};

UCLASS()
class RALLYHERESTART_API URHSettingsContainerConfigAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	URHSettingsContainerConfigAsset()
		: bIsAvailableOffline(true)
		, AllowedPlatformTypes()
		, bUsePreview(false)
		, PreviewWidget(nullptr)
		, WidgetConfigs()
		, SettingName()
		, SettingDescription()
	{}

	// Can this setting be altered while not logged in? Settings related to device
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	bool bIsAvailableOffline;

	// Platforms that allow the setting to be configured
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FRHAllowedPlatformTypes AllowedPlatformTypes;

	// Required input type for setting to be configured
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FRHRequiredInputTypes RequiredInputTypes;

	// Required experiment to be enabled for setting to be configured
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	FString RequiredExperiment;

	// Shows the settings preview
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	bool bUsePreview;

	// Settings Info class used for overriding
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings", meta = (EditCondition = "bUsePreview"))
	TSubclassOf<class URHSettingsPreview> PreviewWidget;

	// How the setting will be represented
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	TArray<struct FRHSettingsWidgetConfig> WidgetConfigs;

	// Grabs the current Setting Name based on our current Platform and configuration
	UFUNCTION(BlueprintPure)
	const FText& GetSettingName() const;

	// Grabs the current Setting Description based on our current Platform and configuration
	UFUNCTION(BlueprintPure)
	const FText& GetSettingDescription() const;

protected:
	// Name of the setting
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	FText SettingName;

	// Allows for overriding of name based on platform
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	TMap<FString, FText> SettingNameByPlatform;

	// Description of the setting
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	FText SettingDescription;

	// Allows for overriding of description based on platform
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	TMap<FString, FText> SettingDescriptionByPlatform;
};

USTRUCT(BlueprintType)
struct FRHSettingsGroupConfig
{
    GENERATED_USTRUCT_BODY()

public:
	FRHSettingsGroupConfig()
		: MainSettingContainerAsset(nullptr)
		, SubSettingContainerAssets()
	{
		SubSettingContainerAssets.Reset();
	}

	// Primary setting for the setting group
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
    class URHSettingsContainerConfigAsset* MainSettingContainerAsset;

	// Settings that relate to the main setting of the group
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	TArray<class URHSettingsContainerConfigAsset*> SubSettingContainerAssets;
};

UCLASS()
class URHSettingsSectionConfigAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	// Groups of settings that fall under the heading
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	TArray<struct FRHSettingsGroupConfig> SettingsGroups;

	// Grabs the current Heading based on our current Platform and configuration
	UFUNCTION(BlueprintPure)
	const FText& GetHeading() const;

protected:
	// Heading for the section of settings
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	FText Heading;

	// Allows for overriding of heading based on platform
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	TMap<FString, FText> HeadingByPlatform;
};

UCLASS()
class URHSettingsPageConfigAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	TArray<class URHSettingsSectionConfigAsset*> SettingsSectionConfigs;

	// Grabs the current Page Name based on our current Platform and configuration
	UFUNCTION(BlueprintPure)
	const FText& GetPageName() const;

protected:
	// Name of the page that contains settings
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	FText PageName;

	// Allows for overriding of page based on platform
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	TMap<FString, FText> HeadingByPlatform;
};

USTRUCT()
struct FRHSettingPropertyId
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY()
	FName Name;

	UPROPERTY()
	int32 Id;

	FRHSettingPropertyId() :
		Id(0)
	{ }
};

USTRUCT()
struct FRHSettingPropertyValue
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY()
	int32 IntVal;

	UPROPERTY()
	FString StringVal;

	UPROPERTY()
	float FloatVal;
	
	UPROPERTY()
	bool bIsSet;

	FRHSettingPropertyValue() :
		IntVal(-1), StringVal(""), FloatVal(-1.f), bIsSet(false)
	{ }
};

USTRUCT()
struct FRHSettingConfigSet
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY()
	FString CaseId;

	UPROPERTY()
	int32 V;

	UPROPERTY()
	int32 ConfigSetId;

	UPROPERTY()
	TMap<FString, FRHSettingPropertyValue> PropertiesById;

	FRHSettingConfigSet() :
		CaseId(""), V(0), ConfigSetId(0)
	{
		PropertiesById.Empty();
	}
};

UCLASS(BlueprintType)
class RALLYHERESTART_API URHSettingsMenuConfigAsset : public UDataAsset
{
    GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	TArray<class URHSettingsPageConfigAsset*> SettingsPageConfigs;
};

/**
 * Settings Service
 */
UCLASS(config=Game, defaultconfig)
class RALLYHERESTART_API URHSettingsDataFactory 
	: public URHDataFactory
	, public IRHSettingsCallbackInterface
{
	GENERATED_UCLASS_BODY()

public:
    virtual void Initialize(class ARHHUDCommon* InHud) override;
    virtual void Uninitialize() override;
    virtual void InitSettingsForPlayer();
    virtual void PostLogin() override;
    virtual void PostLogoff() override;
    virtual void GetSettingsFromPlayerAccount();
	void OnGetPlayerCaseSetSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& Response);

    void OnGameUserSettingsSaved();

    UPROPERTY(BlueprintAssignable)
    FOnSettingsReceivedFromPlayerAccount OnSettingsReceivedFromPlayerAccount;

// Settings
public:
    UFUNCTION(BlueprintCallable, Category = "Settings")
    void SaveSettings();
	void OnSetPlayerCaseSetSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& ResponseData, const FRH_ErrorInfo& ErrorInfo);

	virtual bool GetSettingAsBool(FName Name, bool& OutBool) const override;
	virtual bool GetSettingAsInt(FName Name, int32& OutInt) const override;
	virtual bool GetSettingAsFloat(FName Name, float& OutFloat) const override;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplySettingAsBool(FName Name, bool Value);
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplySettingAsInt(FName Name, int32 Value);
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ApplySettingAsFloat(FName Name, float Value);

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SaveSettingAsBool(FName Name);
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SaveSettingAsInt(FName Name);
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SaveSettingAsFloat(FName Name);

	void RevertSettingToDefault(FName Name);

	virtual void BindSettingCallbacks(FName Name, const FSettingDelegateStruct& SettingDelegateStruct) override;

	TMap<FName, FOnSettingUpdateMulticast> SettingAppliedDelMap;
	TMap<FName, FOnSettingUpdateMulticast> SettingSavedDelMap;

public:
	//Returns the default RH InputAction keys. Are you sure you don't want to use GetCustomRHInputActionKeys?
	UFUNCTION(BlueprintPure, Category = "Settings | Keybindings")
	void GetDefaultRHInputActionKeys(const FName& Name, EInputType InputType, TArray<FRHInputActionKey>& OutKeys) const;
	//Returns the default InputAction keys. Are you sure you don't want to use GetCustomInputActionKeys?
	UFUNCTION(BlueprintPure, Category = "Settings | Keybindings")
	void GetDefaultInputActionKeys(const FName& Name, EInputType InputType, TArray<FKey>& OutKeys) const;
	UFUNCTION(BlueprintPure, Category = "Settings | Keybindings")
	void GetCustomRHInputActionKeys(FName Name, EInputType InputType, TArray<FRHInputActionKey>& OutKeys) const;
    UFUNCTION(BlueprintPure, Category = "Settings | Keybindings")
    void GetCustomInputActionKeys(FName Name, EInputType InputType, TArray<FKey>& OutKeys) const;

	//Returns the default axis keys. Are you sure you don't want to use GetCustomInputAxisKeys?
	UFUNCTION(BlueprintPure, Category = "Settings | Keybindings")
	void GetDefaultInputAxisKeys(const FName& Name, EInputType InputType, float Scale, TArray<FKey>& OutKeys) const;
    UFUNCTION(BlueprintPure, Category = "Settings | Keybindings")
    void GetCustomInputAxisKeys(FName Name, EInputType InputType, float Scale, TArray<FKey>& OutKeys) const;

    void SetCustomInputActionKeyMapping(FName Name, const TArray<FRHInputActionKey>& Keys, EInputType InputType);
    void SetCustomInputAxisKeyMapping(FName Name, const TArray<FKey>& Keys, EInputType InputType, float Scale);

    UPROPERTY(BlueprintAssignable, Category = "Settings | KeyBindings")
    FOnKeyBindSettingsUpdate OnKeyBindSettingsApplied;

    UPROPERTY(BlueprintAssignable, Category = "Settings | KeyBindings")
    FOnKeyBindSettingsUpdate OnKeyBindSettingsSaved;

    UFUNCTION(BlueprintCallable, Category = "Settings | Keybinds")
    void SaveKeyBindings();

	UFUNCTION(BlueprintCallable, Category = "Settings | Keybinds")
	void RevertKeyBindings();

    void AddEditableKeybindName(FName EditableKeybindName);

	//return value indicates success.  Check against Super's return value when overriding
	UFUNCTION(BlueprintCallable, Category = "Settings")
	virtual bool OnSettingChanged(FName SettingId, int32 SettingValue);

public:
	const class URHSettingsMenuConfigAsset* GetSettingsMenuConfigAsset() const { return SettingsMenuConfigAsset; }

    UFUNCTION(BlueprintPure, Category = "Settings | PlayerInput")
    URHPlayerInput* GetRHPlayerInput() const;

	UFUNCTION(BlueprintCallable, Category = "Settings | Keybinds")
	void RevertPlayerPreferences();

protected:
	UPROPERTY(Config)
	TArray<FRHSettingPropertyId> BoolSettingPropertyIds;
	TMap<FName, int32> BoolSettingPropertyIdMap;

	UPROPERTY(Config)
	TArray<FRHSettingPropertyId> IntSettingPropertyIds;
	TMap<FName, int32> IntSettingPropertyIdMap;

	UPROPERTY(Config)
	TArray<FRHSettingPropertyId> FloatSettingPropertyIds;
	TMap<FName, int32> FloatSettingPropertyIdMap;

protected:
	UFUNCTION(BlueprintPure, Category = "Platformgorm UMG | IsUserLoggedIn")
	bool IsUserLoggedIn();
	void SaveToProfileConfig(int32 PropId, int32 SettingValue, int32 PropGroup = ConstConfigPropertyGroups::PLAYER_PREFERENCES);
	void SaveToProfileConfig(int32 PropId, float SettingValue, int32 PropGroup = ConstConfigPropertyGroups::PLAYER_PREFERENCES);
	void SaveToProfileConfig(int32 PropId, FString SettingValue, int32 PropGroup = ConstConfigPropertyGroups::PLAYER_PREFERENCES);
	bool HasSetPlayerPreferenceById(int32 PropId) const;
    void SaveKeybindsToProfileConfig();
	FString InputActionKeyBindToString(const TArray<FRHInputActionKey>& KBMKeys, const TArray<FRHInputActionKey>& GPKeys) const;
	void StringToInputActionKeyBind(const FString& KeybindString, TArray<FRHInputActionKey>& OutKBMKeys, TArray<FRHInputActionKey>& OutGPKeys) const;
	FString InputAxisKeyBindToString(const TArray<FKey>& KBMKeys, const TArray<FKey>& GPKeys) const;
	void StringToInputAxisKeyBind(const FString& KeybindString, TArray<FKey>& OutKBMKeys, TArray<FKey>& OutGPKeys) const;
	bool HasSetPlayerKeybindById(int32 PropId) const;

	// Handle application resuming from suspension (console)
	void AppReactivatedCallback();
	void AppReactivatedCallbackGameThread();
	FDelegateHandle AppReactivatedHandle;

protected:
    bool bNeedsSettingsFromRH;

private:
	TMap<FString, FKey> StringToKeyMap;

	////////////////
	// Custom Key Binding
	/////////////
public:
	UPROPERTY(config, EditDefaultsOnly, Category = "Settings")
	FName SettingsMenuConfigAssetPath;

	UPROPERTY()
	URHSettingsMenuConfigAsset* SettingsMenuConfigAsset;

    // Holds a list of names that cannot share keybinds with one another.
    TArray<FName> EditableKeybindNames;

// NON-GENERIC SETTINGS
// Most settings fit into the RHSettingsInfo_Generic flow
// If there is a werid setting that can't it can live here

public:
    UFUNCTION(BlueprintPure, Category = "Settings | Language")
    TArray<FString> GetAvailableLanguages() const;

    UFUNCTION(BlueprintPure, Category = "Settings | Language")
    FString GetCurrentLanguage() const;

    UFUNCTION(BlueprintCallable, Category = "Settings | Language")
    void ApplyLanguage(FString LanguageCulture);

    UFUNCTION(BlueprintCallable, Category = "Settings | Language")
    void SaveLanguage();

	UFUNCTION(BlueprintCallable, Category = "Settings | Language")
	void RevertLanguageToDefault();

    UPROPERTY(BlueprintAssignable, Category = "Settings | Language")
    FOnStringSettingsUpdate OnDisplayLanguageApplied;

    UPROPERTY(BlueprintAssignable, Category = "Settings | Language")
    FOnStringSettingsUpdate OnDisplayLanguageSaved;

public:
	UFUNCTION(BlueprintPure, Category = "Settings | Display")
	FIntPoint GetScreenResolution() const;

	UFUNCTION(BlueprintCallable, Category = "Settings | Display")
	void ApplyScreenResolution(FIntPoint ScreenResolution);

	UFUNCTION(BlueprintCallable, Category = "Settings | Display")
	void SaveScreenResolution();

	UFUNCTION(BlueprintCallable, Category = "Settings | Display")
	void RevertScreenResolution();

	UPROPERTY(BlueprintAssignable, Category = "Settings | Display")
	FOnIntPointSettingUpdate OnScreenResolutionApplied;

	UPROPERTY(BlueprintAssignable, Category = "Settings | Display")
	FOnIntPointSettingUpdate OnScreenResolutionSaved;

    UFUNCTION(BlueprintPure, Category = "Settings | Region")
    FString GetSelectedRegion() const;
     
    UFUNCTION(BlueprintCallable, Category = "Settings | Region")
    bool SetSelectedRegion(const FString& RegionId);

	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FRHSettingValueChanged OnSettingValueChanged;

// Case Settings
protected:
	bool PackageCaseSettings(const TMap<int32, FRHSettingConfigSet>& InCaseSets, TMap<FString, FRHAPI_SetSinglePlayerSettingRequest>& OutSettingsContent);
	bool PackageCaseSetting(const FRHSettingConfigSet& InCaseSet, FRHAPI_SetSinglePlayerSettingRequest& OutSettingData);

	bool UnpackageCaseSettings(const TMap<FString, FRHAPI_SettingData>& InSettingsContent, TArray<FRHSettingConfigSet>& OutCaseSets);
	bool UnpackageCaseSetting(const FString& InCaseId, const FRHAPI_SettingData& InSettingData, FRHSettingConfigSet& OutCaseSet);

	bool FindCaseFromSetsById(const TArray<FRHSettingConfigSet>& InCaseSets, const int32& InConfigSetId, const int32& InV, TArray<FRHSettingConfigSet>& OutFoundCaseSets, bool bCreateIfNeeded);
	bool FindCasePropertyFromSetsById(const TArray<FRHSettingConfigSet>& InCaseSets, const FString& InPropertyId, FRHSettingPropertyValue& OutFoundProperty);

	bool HasStoredCaseSetById(const int32& SetId);
	bool HasStoredPropertyById(const int32& SetId, const FString& PropId);

	// Updates StoredCaseSets by merging with input; keeps unchanged values
	void UpdateStoredCaseSets(const TArray<FRHSettingConfigSet>& NewCaseSets);

	UPROPERTY()
	TMap<int32, FRHSettingConfigSet> StoredCaseSets;

	TArray<FIntPoint> ResolutionSettings;
};
