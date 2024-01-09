// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Shared/Settings/RHSettingsMenu.h"
#include "Shared/Settings/RHSettingsPage.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Managers/RHPopupManager.h"
#if defined(PLATFORM_SWITCH) && PLATFORM_SWITCH
#include "SwitchApplication.h"
#endif

URHSettingsMenu::URHSettingsMenu(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SettingsPageClass = URHSettingsPage::StaticClass();
	MenuConfigAsset = nullptr;
}

void URHSettingsMenu::InitializeWidget_Implementation()
{
    Super::InitializeWidget_Implementation();
}

void URHSettingsMenu::ShowWidget()
{
	if (SettingsPages.Num() == 0)
	{
		if (MyHud.IsValid())
		{
			if (class URHSettingsDataFactory* pSettingsDataFactory = MyHud->GetSettingsDataFactory())
			{
				if (const URHSettingsMenuConfigAsset* SettingsMenuConfig = pSettingsDataFactory->GetSettingsMenuConfigAsset())
				{
					SetMenuConfig(SettingsMenuConfig);
				}
			}
		}
	}
	Super::ShowWidget();
}

void URHSettingsMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

	if (FSlateApplication::IsInitialized() && IsInGameThread())
	{
		const FRHSettingsState OldSettingsState = SettingsState;

		ARHHUDCommon* Hud = MyHud.Get();

		// There aren't any clear-cut callbacks for both mouse and gamepad connect/disconnect for all platforms.
		// I'm doing this on tick so it only does the check when the settings menu is open.
		const TSharedPtr<GenericApplication> GenericApplication = FSlateApplication::Get().GetPlatformApplication();
		if (GenericApplication.IsValid())
		{
			// For most platforms, we want to just assume input is attached, because it would be weird to hide the primary bindings.
			const FString PlatformName = UGameplayStatics::GetPlatformName();
			if (PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5") || PlatformName == TEXT("XboxOne") || PlatformName == TEXT("XSX"))
			{
				SettingsState.bIsGamepadAttached = true;
				SettingsState.bIsMouseAttached = false; // GenericApplication->IsMouseAttached(); // We currently are not supporting Mouse/Keyboard on consoles.
				SettingsState.bIsTouchMode = false;
			}
			else if (PlatformName == TEXT("Switch"))
			{
				SettingsState.bIsGamepadAttached = true;
				SettingsState.bIsMouseAttached = false;
				SettingsState.bIsTouchMode = false;
			}
			else if (PLATFORM_DESKTOP)
			{
				SettingsState.bIsGamepadAttached = GenericApplication->IsGamepadAttached();
				SettingsState.bIsMouseAttached = true;
				SettingsState.bIsTouchMode = Hud != nullptr && Hud->GetCurrentInputState() == RH_INPUT_STATE::PIS_TOUCH;
			}
			else
			{
				SettingsState.bIsGamepadAttached = GenericApplication->IsGamepadAttached();
				SettingsState.bIsMouseAttached = GenericApplication->IsMouseAttached();
				SettingsState.bIsTouchMode = Hud != nullptr && Hud->GetCurrentInputState() == RH_INPUT_STATE::PIS_TOUCH;
			}
		}

#if defined(PLATFORM_SWITCH) && PLATFORM_SWITCH
		SettingsState.bIsDockedMode = FSwitchApplication::IsBoostModeActive();
		SettingsState.bIsHandheldMode = !SettingsState.bIsDockedMode;
#else
		SettingsState.bIsDockedMode = true;
		SettingsState.bIsHandheldMode = false;
#endif

		if (OldSettingsState != SettingsState)
		{
			OnSettingsStateChanged();
			RebuildNavigation();
		}
    }
}

void URHSettingsMenu::OnSettingsStateChanged()
{
	for (const auto& Page : SettingsPages)
	{
		if (Page->OnSettingsStateChanged(SettingsState))
		{
			ShowPage(Page);
		}
		else
		{
			HidePage(Page);
		}
	}
}

void URHSettingsMenu::SetMenuConfig(const class URHSettingsMenuConfigAsset* InMenuConfigAsset)
{
	if (InMenuConfigAsset == nullptr)
	{
		return;
	}

    MenuConfigAsset = InMenuConfigAsset;

    if (MyHud.IsValid())
    {
		if (UWorld* const pWorld = GetWorld())
		{
			for (const class URHSettingsPageConfigAsset* const PageConfigAsset : MenuConfigAsset->SettingsPageConfigs)
			{
				if (URHSettingsMenu::IsPageConfigAllowed(PageConfigAsset))
				{
					if (class URHSettingsPage* SettingsPage = CreateWidget<URHSettingsPage>(pWorld, SettingsPageClass))
					{
						SettingsPage->InitializeWidget();
						SettingsPage->SetPageConfig(PageConfigAsset);
						AddSettingsPageWidget(SettingsPage);
						SettingsPages.Add(SettingsPage);
					}
				}
			}
		}
	}

    OnMenuConfigSet();
}

void URHSettingsMenu::ShowPage(class URHSettingsPage* SettingsPage)
{
    if (SettingsPage != nullptr)
    {
        SettingsPage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        OnShowPage(SettingsPage);
    }
}

void URHSettingsMenu::HidePage(class URHSettingsPage* SettingsPage)
{
    if (SettingsPage != nullptr)
    {
        SettingsPage->SetVisibility(ESlateVisibility::Collapsed);
        OnHidePage(SettingsPage);
    }
}

bool URHSettingsMenu::IsPageConfigAllowed(const class URHSettingsPageConfigAsset* PageConfigAsset)
{
	if (PageConfigAsset == nullptr)
	{
		return false;
	}

    for (const URHSettingsSectionConfigAsset* const SectionConfig : PageConfigAsset->SettingsSectionConfigs)
    {
        if (URHSettingsPage::IsSectionConfigAllowed(SectionConfig))
        {
            return true;
        }
    }
    return false;
}

void URHSettingsMenu::CheckSavePendingChanges()
{
    if (MyHud.IsValid()) 
    {
        if (URHPopupManager* popup = MyHud->GetPopupManager())
        {
            FRHPopupConfig popupParams;
            popupParams.Header = NSLOCTEXT("RHSettings", "SaveSettings", "Save Settings?");
            popupParams.CancelAction.AddDynamic(this, &URHSettingsMenu::OnRevertSettings);

            FRHPopupButtonConfig& confirmBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
            confirmBtn.Label = NSLOCTEXT("General", "Save", "SAVE");
            confirmBtn.Type = ERHPopupButtonType::Confirm;
            confirmBtn.Action.AddDynamic(this, &URHSettingsMenu::OnSaveSettings);

            FRHPopupButtonConfig& cancelBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
            cancelBtn.Label = NSLOCTEXT("RHSettings", "Revert", "REVERT");
            cancelBtn.Type = ERHPopupButtonType::Cancel;
            cancelBtn.Action.AddDynamic(this, &URHSettingsMenu::OnRevertSettings);

            popup->AddPopup(popupParams);
        }
        else
        {
            // If we can't pop the popup for any reason just save the settings
            OnSaveSettings();
        }
    }
    else
    {
        // If we can't pop the popup for any reason just save the settings
        OnSaveSettings();
    }
}

void URHSettingsMenu::ConfirmRevertSettings()
{
	if (MyHud.IsValid())
	{
		if (URHPopupManager* PopupManager = MyHud->GetPopupManager())
		{
			FRHPopupConfig PopupData = FRHPopupConfig();

			PopupData.Header = NSLOCTEXT("RHSettings", "RevertConfirmTitle", "REVERT SETTINGS?");
			PopupData.Description = NSLOCTEXT("RHSettings", "RevertConfirmDesc", "Would you like to revert the settings on this page?");
			PopupData.IsImportant = true;
			PopupData.CancelAction.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

			FRHPopupButtonConfig& AcceptButton = PopupData.Buttons[PopupData.Buttons.AddDefaulted()];
			AcceptButton.Type = ERHPopupButtonType::Confirm;
			AcceptButton.Label = NSLOCTEXT("General", "Okay", "Okay");
			AcceptButton.Action.AddDynamic(this, &URHSettingsMenu::RevertSettings);

			FRHPopupButtonConfig& CancelButton = PopupData.Buttons[PopupData.Buttons.AddDefaulted()];
			CancelButton.Type = ERHPopupButtonType::Cancel;
			CancelButton.Label = NSLOCTEXT("General", "Cancel", "Cancel");
			CancelButton.Action.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

			PopupManager->AddPopup(PopupData);
		}
	}
}

void URHSettingsMenu::OnRevertSettings()
{
    OnConfirmExit(false);
}

void URHSettingsMenu::OnSaveSettings()
{
    OnConfirmExit(true);
}
