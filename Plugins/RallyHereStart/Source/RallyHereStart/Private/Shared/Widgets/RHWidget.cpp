// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "RH_Common.h"
#include "Slate/SceneViewport.h"
#include "Shared/Widgets/RHWidget.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Framework/Application/SlateApplication.h"
#include "MovieScene.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"

URHWidget::URHWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
	IsComponent = false;
	StartsHidden = false;
#if RH_FROM_ENGINE_VERSION(5,2)
	SetIsFocusable(true);
#else
	bIsFocusable = true;
#endif
	InputComponentUsesWidgetPriority = false;
	UsesBlocker = false;
	BlockerClickToClose = false;
	EnableGameStateSetNotify = false;
	CloseOnLogout = true;
	NavigateBackIsPressed = false;
	NavigateConfirmIsPressed = false;
	ViewportEvent = FOnViewportSizeChanged();
}

void URHWidget::InitializeWidget_Implementation()
{
	if (StartsHidden)
	{
		HideWidget();
	}

	InitializeWidgetNavigation();
	InitializeWidgetButtonListeners();

	//Input component starts unregistered.  We should only register the widgets we need to (screen widgets using Show/Hide)
	UnregisterInputComponent();

	SetUsesBlocker_Internal();

	TickAnimations = NewObject<UTickAnimationManager>(this, FName("Tick Animation Manager"));
	InitializeTickAnimations();
}

void URHWidget::UninitializeWidget_Implementation()
{
	if (LoadedTexture.IsValid())
	{
		Streamable.Unload(LoadedTexture.ToSoftObjectPath());
	}

	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->UnbindParentWidget(this);
		}
	}
}

void URHWidget::SetUsesBlocker(bool bShouldUseBlocker)
{
	if (UsesBlocker != bShouldUseBlocker)
	{
		UsesBlocker = bShouldUseBlocker;
		SetUsesBlocker_Internal();
	}
}
void URHWidget::SetUsesBlocker_Internal()
{
	if (InputComponent != nullptr)
	{
		if (InputComponentUsesWidgetPriority)
		{
			//Blocking Popups should have very high priority, but should not exceed that of the InputManager, unless specifically set to higher priority
#if RH_FROM_ENGINE_VERSION(5,2)
			InputComponent->Priority = GetInputActionPriority() ? GetInputActionPriority() : (UsesBlocker ? 4500 : 0);
#else
			InputComponent->Priority = Priority ? Priority : (UsesBlocker ? 4500 : 0);
#endif
		}
		else
		{
			//Blocking Popups should have very high priority, but should not exceed that of the InputManager
			InputComponent->Priority = UsesBlocker ? 4500 : 0;
		}
	}
}

void URHWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	GeometryFromLastTick = MyGeometry;

	if (TickAnimations != nullptr)
	{
		TickAnimations->ApplyTick(InDeltaTime);
	}
}

void URHWidget::HandleViewportSizeChanged(FViewport* Viewport, uint32 SomeInt)
{
	ViewportEvent.ExecuteIfBound(Viewport->GetSizeXY());
}

class APlayerController* URHWidget::GetActivePlayerController() const
{
	return Super::GetOwningPlayer();
}

class UWorld* URHWidget::GetActiveWorld() const
{
	return Super::GetWorld();
}

class APlayerController* URHWidget::GetNormalOwningPlayer() const
{
	return Super::GetOwningPlayer();
}

class UWorld* URHWidget::GetNormalWorld() const
{
	return Super::GetWorld();
}

// =============== Animations ===============

void URHWidget::AddTickAnimation(FName AnimName, float Duration, const FTickAnimationUpdate& UpdateEvent, const FTickAnimationFinished& FinishedEvent)
{
	if (TickAnimations != nullptr)
	{
		UE_LOG(RallyHereStart, VeryVerbose, TEXT("Adding tick animation %s (duration: %f) on widget %s"), *AnimName.ToString(), Duration, *this->GetName());
		TickAnimations->AddAnimation(AnimName, Duration, UpdateEvent, FinishedEvent);
	}
}

void URHWidget::RemoveTickAnimation(FName AnimName)
{
	if (TickAnimations != nullptr)
	{
		TickAnimations->RemoveAnimation(AnimName);
	}
}

void URHWidget::PlayTickAnimation(FName AnimName)
{
	if (TickAnimations != nullptr)
	{
		UE_LOG(RallyHereStart, VeryVerbose, TEXT("PlayTickAnimation, playing tick animation %s on widget %s"), *AnimName.ToString(), *this->GetName());
		TickAnimations->PlayAnimation(AnimName);
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("PlayTickAnimation, cannot play animation '%s', TickAnimations is a nullptr on widget %s"), *AnimName.ToString(), *this->GetName());
	}
}

void URHWidget::StopTickAnimation(FName AnimName)
{
	if (TickAnimations != nullptr)
	{
		TickAnimations->StopAnimation(AnimName);
	}
}

void URHWidget::PauseTickAnimation(FName AnimName)
{
	if (TickAnimations != nullptr)
	{
		TickAnimations->PauseAnimation(AnimName);
	}
}

void URHWidget::ResumeTickAnimation(FName AnimName)
{
	if (TickAnimations != nullptr)
	{
		TickAnimations->ResumeAnimation(AnimName);
	}
}

void URHWidget::SkipToEndTickAnimation(FName AnimName)
{
	if (TickAnimations != nullptr)
	{
		TickAnimations->SkipToEndAnimation(AnimName);
	}
}

bool URHWidget::GetTickAnimationInfo(FName AnimName, FTickAnimationParams& OutAnimParams)
{
	if (TickAnimations != nullptr)
	{
		return TickAnimations->GetAnimationInfo(AnimName, OutAnimParams);
	}
	return false;
}

// ==========================================

void URHWidget::BindToViewportSizeChange(const FOnViewportSizeChanged& InViewportEvent)
{
	ViewportEvent = InViewportEvent;

	if (GetWorld() != nullptr)
	{
		if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
		{
			if (FSceneViewport* SceneViewport = ViewportClient->GetGameViewport())
			{
				if (!SceneViewport->ViewportResizedEvent.IsBoundToObject(this))
				{
					SceneViewport->ViewportResizedEvent.AddUObject(this, &URHWidget::HandleViewportSizeChanged);
				}
			}
		}
	}
}

void URHWidget::UnbindFromViewportSizeChange()
{
	ViewportEvent = FOnViewportSizeChanged();

	if (GetWorld() != nullptr)
	{
		if (UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
		{
			if (FSceneViewport* SceneViewport = ViewportClient->GetGameViewport())
			{
				SceneViewport->ViewportResizedEvent.RemoveAll(this);
			}
		}
	}
}

void URHWidget::SetAllAnimationsPlaybackSpeed(float PlaybackSpeed)
{
    for (int32 i = 0; i < ActiveSequencePlayers.Num(); i++)
    {
        if (ActiveSequencePlayers[i] != nullptr)
        {
            ActiveSequencePlayers[i]->SetPlaybackSpeed(PlaybackSpeed);
        }
    }
}

void URHWidget::TriggerGlobalInvalidate()
{
	UE_LOG(RallyHereStart, Log, TEXT("Triggering invalidate from URHWidget."));
	//$$ TJS: Workaround for some global invalidation issues during gameplay.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}
}

void URHWidget::ProcessSoundEvent(FName SoundEvent)
{
	if (MyHud.IsValid())
	{
		MyHud->ProcessSoundEvent(SoundEvent);
	}
}

URHInputManager* URHWidget::GetInputManager()
{
	if (MyHud.IsValid())
	{
		return MyHud->GetInputManager();
	}

	return nullptr;
}

void URHWidget::BindToInputManager(int32 DefaultFocusGroup)
{
	if (!bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->BindParentWidget(this, DefaultFocusGroup);
		}
	}

	bBoundToInputManager = true;
}

void URHWidget::RegisterWidgetToInputManager(UWidget* Widget, int32 FocusGroup, UWidget* Up, UWidget* Down, UWidget* Left, UWidget* Right)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->RegisterWidget(this, Widget, FocusGroup, Up, Down, Left, Right);
		}
	}
}

void URHWidget::UpdateRegistrationToInputManager(UWidget* Widget, int32 FocusGroup, UWidget* Up, UWidget* Down, UWidget* Left, UWidget* Right)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->UpdateWidgetRegistration(this, Widget, FocusGroup, Up, Down, Left, Right);
		}
	}
}

void URHWidget::UnregisterWidgetFromInputManager(UWidget* Widget)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->UnregisterWidget(this, Widget);
		}
	}
}

void URHWidget::UnregisterFocusGroup(int32 FocusGroup)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->UnregisterFocusGroup(this, FocusGroup);
		}
	}
}

void URHWidget::InheritFocusGroupFromWidget(int32 TargetFocusGroupNum, URHWidget* SourceWidget, int32 SourceFocusGroupNum)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->InheritFocusGroupFromWidget(this, TargetFocusGroupNum, SourceWidget, SourceFocusGroupNum);
		}
	}
}

void URHWidget::SetFocusToGroup(int32 FocusGroup, bool KeepLastFocus)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->SetFocusToGroup(this, FocusGroup, KeepLastFocus);
		}
	}
}

void URHWidget::SetFocusToWidgetOfGroup(int32 FocusGroup, URHWidget* Widget)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->SetFocusToWidgetOfGroup(this, FocusGroup, Widget);
		}
	}
}

void URHWidget::SetDefaultFocusForGroup(UWidget* Widget, int32 FocusGroup)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->SetDefaultFocusForGroup(this, Widget, FocusGroup);
		}
	}
}

UWidget* URHWidget::SetFocusToThis()
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			return InputManager->SetFocusToThis(this);
		}
	}

	return nullptr;
}

UWidget* URHWidget::GetCurrentFocusForGroup(int32 FocusGroup)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			return InputManager->GetCurrentFocusForGroup(this, FocusGroup);
		}
	}

	return nullptr;
}

bool URHWidget::GetCurrentFocusGroup(int32& OutFocusGroup)
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			return InputManager->GetCurrentFocusGroup(this, OutFocusGroup);
		}
	}

	return false;
}

void URHWidget::ClearNavigationInputThrottle()
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->ClearNavInputThrottled();
		}
	}
}

bool URHWidget::ExplicitNavigateUp()
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->NavigateUp();
			return true;
		}
	}

	return false;
}

bool URHWidget::ExplicitNavigateDown()
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->NavigateDown();
			return true;
		}
	}

	return false;
}

bool URHWidget::ExplicitNavigateLeft()
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->NavigateLeft();
			return true;
		}
	}

	return false;
}

bool URHWidget::ExplicitNavigateRight()
{
	if (bBoundToInputManager)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->NavigateRight();
			return true;
		}
	}

	return false;
}

void URHWidget::ClearContextAction(FName ContextName)
{
	if (MyRouteName != NAME_None)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->ClearContextAction(MyRouteName, ContextName);
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHWidget::ClearContextAction called without a Route Set"));
	}
}

void URHWidget::ClearAllContextActions()
{
	if (MyRouteName != NAME_None)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->ClearAllContextActions(MyRouteName);
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHWidget::ClearAllContextActions called without a Route Set"));
	}
}

void URHWidget::AddContextAction(FName ContextName, FText FormatAdditive /*= FText()*/)
{
	if (MyRouteName != NAME_None)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->AddContextAction(MyRouteName, ContextName, FormatAdditive);
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHWidget::AddContextAction called without a Route Set"));
	}
}

void URHWidget::AddContextActions(TArray<FName> ContextNames)
{
	if (MyRouteName != NAME_None)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->AddContextActions(MyRouteName, ContextNames);
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHWidget::AddContextActions called without a Route Set"));
	}
}

void URHWidget::SetContextAction(FName ContextName, const FOnContextAction& EventCallback)
{
	if (MyRouteName != NAME_None)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->SetContextAction(MyRouteName, ContextName, EventCallback);
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHWidget::CleaSetContextActionrContextAction called without a Route Set"));
	}
}

void URHWidget::SetContextCycleAction(FName ContextName, const FOnContextCycleAction& EventCallback)
{
	if (MyRouteName != NAME_None)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->SetContextCycleAction(MyRouteName, ContextName, EventCallback);
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHWidget::SetContextCycleAction called without a Route Set"));
	}
}

void URHWidget::SetContextHoldReleaseAction(FName ContextName, const FOnContextHoldActionUpdate& UpdateCallback, const FOnContextHoldAction& EventCallback)
{
	if (MyRouteName != NAME_None)
	{
		if (URHInputManager* InputManager = GetInputManager())
		{
			InputManager->SetContextHoldReleaseAction(MyRouteName, ContextName, UpdateCallback, EventCallback);
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHWidget::SetContextHoldReleaseAction called without a Route Set"));
	}
}

void URHWidget::DisplayGenericPopup(const FString& sTitle, const FString& sDesc)
{
	if (MyHud.IsValid())
	{
		MyHud->DisplayGenericPopup(sTitle, sDesc);
	}
}

void URHWidget::DisplayGenericError(const FString& sDesc)
{
	DisplayGenericPopup(NSLOCTEXT("General", "Error", "Error").ToString(), sDesc);
}


bool URHWidget::NavigateBackPressed_Implementation()
{
	return false;
}

bool URHWidget::NavigateConfirmPressed_Implementation()
{
	return false;
}

bool URHWidget::NavigateBack_Implementation()
{
	return false;
}

void URHWidget::NavigateBackCancelled_Implementation()
{
}

bool URHWidget::NavigateConfirm_Implementation()
{
	return false;
}

void URHWidget::NavigateConfirmCancelled_Implementation()
{
}

void URHWidget::GameStateSet_Implementation(class AGameStateBase* GameState)
{
}

bool URHWidget::NavigateBackPressedOnWidget()
{
	if (!NavigateBackIsPressed)
	{
		NavigateBackIsPressed = true;

		return NavigateBackPressed();
	}

	return false;
}

bool URHWidget::NavigateBackOnWidget()
{
	if (NavigateBackIsPressed)
	{
		return NavigateBack();
	}

	return false;
}

void URHWidget::NavigateBackCancelledOnWidget()
{
	if (NavigateBackIsPressed)
	{
		NavigateBackIsPressed = false;
		NavigateBackCancelled();
	}
}

bool URHWidget::NavigateConfirmPressedOnWidget()
{
	if (!NavigateConfirmIsPressed)
	{
		NavigateConfirmIsPressed = true;

		return NavigateConfirmPressed();
	}

	return false;
}

bool URHWidget::NavigateConfirmOnWidget()
{
	if (NavigateConfirmIsPressed)
	{
		NavigateConfirmIsPressed = false;
		return NavigateConfirm();
	}

	return false;
}

void URHWidget::NavigateConfirmCancelledOnWidget()
{
	if (NavigateConfirmIsPressed)
	{
		NavigateConfirmIsPressed = false;
		NavigateConfirmCancelled();
	}
}

void URHWidget::ClearPressedStates()
{
	NavigateBackIsPressed = false;
	NavigateConfirmIsPressed = false;
}

void URHWidget::HideWidget()
{
	if (GetVisibility() == ESlateVisibility::Collapsed) return;

	SetVisibility(ESlateVisibility::Collapsed);

	if (MyHud.IsValid())
	{
		MyHud->SetUIFocus();
	}

	UnregisterInputComponent();

	OnHide();
}

void URHWidget::ShowWidget()
{
	if (GetVisibility() == ESlateVisibility::SelfHitTestInvisible) return;

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (MyHud.IsValid())
	{
		MyHud->SetUIFocus();
	}

	RegisterInputComponent();

	OnShown();
}

void URHWidget::OnGamepadHover()
{
	RegisterInputComponent();
	NativeGamepadHover();
	GamepadHover();
}

void URHWidget::NativeGamepadHover()
{

}

void URHWidget::OnGamepadUnhover()
{
	UnregisterInputComponent();
	NativeGamepadUnhover();
	GamepadUnhover();
}

void URHWidget::NativeGamepadUnhover()
{

}

FEventReply URHWidget::GamepadButtonDown_Implementation(FKey Button)
{
	return FEventReply(false);
}

FEventReply URHWidget::GamepadButtonUp_Implementation(FKey Button)
{
	return FEventReply(false);
}

void URHWidget::NativeOnInitialized()
{
	if (auto* Player = GetOwningPlayer())
	{
		MyHud = Cast<ARHHUDCommon>(Player->GetHUD());

		if (MyHud.IsValid())
		{
			ToggleMobileLayout(MyHud->GetCurrentInputState());
		}
	}
	Super::NativeOnInitialized(); // Has to happen last so MyHud is ready for the BP event
	if (EnableGameStateSetNotify)
	{
		if (UWorld* MyWorld = GetWorld())
		{
			AGameStateBase* pGameState = MyWorld->GetGameState();
			if (pGameState != nullptr)
			{
				GameStateSet(pGameState);
			}
			else
			{
				MyWorld->GameStateSetEvent.AddUObject(this, &URHWidget::GameStateSet);
			}
		}
	}
}

FGeometry URHWidget::GetGeometryFromLastTick()
{
	return GeometryFromLastTick;
}

bool URHWidget::CanCloseOnLogout_Implementation()
{
	return CloseOnLogout;
}

void URHWidget::AsyncLoadTexture2D(TSoftObjectPtr<UTexture2D> Texture2DRef)
{
	if (LoadedTexture != Texture2DRef)
	{
		if (LoadedTexture.IsValid())
		{
			Streamable.Unload(LoadedTexture.ToSoftObjectPath());
		}

		LoadedTexture = Texture2DRef;
	}

	if (LoadedTexture.IsValid())
	{
		OnTextureLoaded();
	}
	else
	{
		FSoftObjectPath TexturePath(LoadedTexture.ToSoftObjectPath());
		if (TexturePath.IsValid())
		{
			Streamable.RequestAsyncLoad(TexturePath, FStreamableDelegate::CreateUObject(this, &URHWidget::OnTextureLoaded));
		}
	}
}

void URHWidget::OnTextureLoaded()
{
	if (LoadedTexture.IsValid())
	{
		OnTextureLoadComplete.Broadcast(LoadedTexture.Get());
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("Async texture load failure."));
	}
}

// focus support
bool URHWidget::IsFocusEnabled_Implementation()
{
#if RH_FROM_ENGINE_VERSION(5,2)
	return IsFocusable() && IsVisible();
#else
	return bIsFocusable && IsVisible();
#endif
}

void URHWidget::StartHideSequence_Implementation(FName FromRoute, FName ToRoute)
{
	HideWidget();
	CallOnHideSequenceFinished();
}

void URHWidget::StartShowSequence_Implementation(FName FromRoute, FName ToRoute)
{
	ShowWidget();
	CallOnShowSequenceFinished();
}

void URHWidget::CallOnHideSequenceFinished()
{
	OnHideSequenceFinished.Broadcast(this);
}

void URHWidget::CallOnShowSequenceFinished()
{
	OnShowSequenceFinished.Broadcast(this);
}

bool URHWidget::AddViewRoute(FName RouteName, bool ClearRouteStack, bool ForceTransition, UObject* Data)
{
	if (URHViewManager* ViewManager = GetViewManager())
	{
		if (ClearRouteStack)
		{
			return ViewManager->ReplaceRoute(RouteName, ForceTransition, Data);
		}
		else
		{
			return ViewManager->PushRoute(RouteName, ForceTransition, Data);
		}
	}

	return false;
}

bool URHWidget::RemoveViewRoute(FName RouteName, bool ForceTransition/* = false*/)
{
	if (URHViewManager* ViewManager = GetViewManager())
	{
		return ViewManager->RemoveRoute(RouteName, ForceTransition);
	}

	return false;
}

bool URHWidget::SwapViewRoute(FName RouteName, FName SwapTargetRoute, bool ForceTransition)
{
	if (URHViewManager* ViewManager = GetViewManager())
	{
		return ViewManager->SwapRoute(RouteName, SwapTargetRoute, ForceTransition);
	}

	return false;
}

bool URHWidget::RemoveTopViewRoute(bool ForceTransition)
{
	if (URHViewManager* ViewManager = GetViewManager())
	{
		return ViewManager->PopRoute(ForceTransition);
	}

	return false;
}

bool URHWidget::IsTopViewRoute()
{
	if (URHViewManager* ViewManager = GetViewManager())
	{
		return (ViewManager->GetTopViewRouteWidget() == this);
	}

	return false;
}

bool URHWidget::GetPendingRouteData(FName RouteName, UObject*& Data) const
{
	if (URHViewManager* ViewManager = GetViewManager())
	{
		return ViewManager->GetPendingRouteData(RouteName, Data);
	}

	return false;
}

void URHWidget::SetPendingRouteData(FName RouteName, UObject* Data)
{
	if (URHViewManager* ViewManager = GetViewManager())
	{
		ViewManager->SetPendingRouteData(RouteName, Data);
	}
}

URHViewManager* URHWidget::GetViewManager() const
{
	if (MyHud.IsValid())
	{
		return MyHud->GetViewManager();
	}

	return nullptr;
}

bool URHWidget::ShouldUseMobileLayout() const
{
	if (PLATFORM_ANDROID || PLATFORM_IOS)
	{
		return true;
	}

	// PC pretending to be mobile for development purposes
	if (!UE_BUILD_SHIPPING && FParse::Param(FCommandLine::Get(), TEXT("mobileUX")))
	{
		return true;
	}

	if (MyHud.IsValid())
	{
		RH_INPUT_STATE InputState = MyHud->GetCurrentInputState();
		return InputState == RH_INPUT_STATE::PIS_TOUCH && !UE_BUILD_SHIPPING && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsFakingTouchEvents();
	}

	return false;
}

void URHWidget::ToggleMobileLayout(RH_INPUT_STATE InputState)
{
	if (ShouldUseMobileLayout())
	{
		if (!bMobileLayoutActive)
		{
			bMobileLayoutActive = true;
			OnEnterMobileLayout();
			FindAndPlayMobileLayoutAnim();
		}
	}
	else
	{
		if (bMobileLayoutActive)
		{
			bMobileLayoutActive = false;
			OnExitMobileLayout();
			RestorePreMobileAnimLayout();
		}
	}
}

void URHWidget::FindAndPlayMobileLayoutAnim()
{
}

UWidgetAnimation* URHWidget::FindMobileLayoutAnim()
{
	if (!MobileLayoutAnim)
	{
		if (UWidgetBlueprintGeneratedClass* OwningClass = GetWidgetTreeOwningClass())
		{
			for (UWidgetAnimation* Animation : OwningClass->Animations)
			{
				static const FName NAME_MobileLayout(TEXT("MobileLayout"));
				if (Animation && Animation->GetMovieScene() &&
					Animation->GetMovieScene()->GetFName().IsEqual(NAME_MobileLayout, ENameCase::IgnoreCase, false))
				{
					if (MyHud.IsValid())
					{
						MyHud->OnInputStateChanged.AddUniqueDynamic(this, &URHWidget::ToggleMobileLayout);
					}
					MobileLayoutAnim = Animation;
					return MobileLayoutAnim;
				}
			}
		}
	}
	return MobileLayoutAnim;
}

void URHWidget::RestorePreMobileAnimLayout()
{
}

void URHWidget::NativeFocusGroupNavigateUpFailure(int32 FocusGroup, URHWidget* Widget)
{
	FocusGroupNavigateUpFailure(FocusGroup);
	OnFocusGroupNavigateUpFailed.Broadcast(FocusGroup, Widget);
}

void URHWidget::NativeFocusGroupNavigateDownFailure(int32 FocusGroup, URHWidget* Widget)
{
	FocusGroupNavigateDownFailure(FocusGroup);
	OnFocusGroupNavigateDownFailed.Broadcast(FocusGroup, Widget);
}

void URHWidget::NativeFocusGroupNavigateLeftFailure(int32 FocusGroup, URHWidget* Widget)
{
	FocusGroupNavigateLeftFailure(FocusGroup);
	OnFocusGroupNavigateLeftFailed.Broadcast(FocusGroup, Widget);
}

void URHWidget::NativeFocusGroupNavigateRightFailure(int32 FocusGroup, URHWidget* Widget)
{
	FocusGroupNavigateRightFailure(FocusGroup);
	OnFocusGroupNavigateRightFailed.Broadcast(FocusGroup, Widget);
}
