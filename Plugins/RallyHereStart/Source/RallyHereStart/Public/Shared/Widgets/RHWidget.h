// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/RHPlayerInput.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StreamableManager.h"
#include "TickAnimationManager.h"
#include "UnrealClient.h"
#include "Player/Controllers/RHPlayerState.h"
#include "RHWidget.generated.h"

class URHInputManager;

// TODO: See if there is a common .h way to share these, as RHWidget/RHInputManager need these, and InputManager includes Widget
UENUM(BlueprintType)
enum class EContextActionHoldReleaseState : uint8
{
	HoldReleaseState_Started,
	HoldReleaseState_Canceled,
	HoldReleaseState_Completed
};

DECLARE_DYNAMIC_DELEGATE(FOnContextActionsUpdated);
DECLARE_DYNAMIC_DELEGATE(FOnContextAction);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnContextCycleAction, bool, bNext);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnContextHoldActionUpdate, float, fPercentage);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnContextHoldAction, EContextActionHoldReleaseState, Status);
// TODO: See if there is a common .h way to share these, as RHWidget/RHInputManager need these, and InputManager includes Widget

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRHWidgetNavigatedBack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHWidgetTextureLoaded, UTexture2D*, Texture);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRHWidgetGamepadHovered, class URHWidget*, Widget, bool, bHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRHWidgetMouseEntered, class URHWidget*, Widget, bool, bEntered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRHNavigateFailed, class URHWidget*, Widget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRHFocusGroupNavigateFailed, int32, FocusGroup, class URHWidget*, Widget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHideSequenceFinished, class URHWidget*, Widget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShowSequenceFinished, class URHWidget*, Widget);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnViewportSizeChanged, FIntPoint, ViewportSize);

/**
 *
 */
UCLASS(Config = Game)
class RALLYHERESTART_API URHWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URHWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RHWidget")
	void InitializeWidget();
	virtual void InitializeWidget_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RHWidget")
	void UninitializeWidget();
	virtual void UninitializeWidget_Implementation();

	UFUNCTION(BlueprintCallable)
	void BindToViewportSizeChange(const FOnViewportSizeChanged& InViewportEvent);

	UFUNCTION(BlueprintCallable)
	void UnbindFromViewportSizeChange();

	void ProcessSoundEvent(FName SoundEvent);

	// ==========    Blueprint interface for the tick animations    ====================

	UFUNCTION(BlueprintCallable)
	void AddTickAnimation(FName AnimName, float Duration, const FTickAnimationUpdate& UpdateEvent, const FTickAnimationFinished& FinishedEvent);

	UFUNCTION(BlueprintCallable)
	void RemoveTickAnimation(FName AnimName);

	UFUNCTION(BlueprintCallable)
	void PlayTickAnimation(FName AnimName);

	UFUNCTION(BlueprintCallable)
	void StopTickAnimation(FName AnimName);

	UFUNCTION(BlueprintCallable)
	void PauseTickAnimation(FName AnimName);

	UFUNCTION(BlueprintCallable)
	void ResumeTickAnimation(FName AnimName);

	UFUNCTION(BlueprintCallable)
	void SkipToEndTickAnimation(FName AnimName);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool GetTickAnimationInfo(FName AnimName, FTickAnimationParams& OutAnimParams);

	// =================================================================================

protected:

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintImplementableEvent)
	void InitializeTickAnimations();

public:
	// Can be used by HUDs to determine if, when focused, this widget should set input to UI-and-game or just UI.
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "UI Focus")
	bool bIsUIOnlyWidget;

	// If a RHWidget has this flag and gets focused by the HUD, the HUD can hide other widgets with this flag
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "UI Focus")
	bool bIsExclusiveMenuWidget;

private:
	UPROPERTY()
	UTickAnimationManager* TickAnimations;

	UPROPERTY()
	FOnViewportSizeChanged ViewportEvent;

	void HandleViewportSizeChanged(FViewport* Viewport, uint32 SomeInt);

public:
	// returns the same value as GetOwningPlayer().
	UFUNCTION(BlueprintCallable, Category = "Player")
	virtual class APlayerController* GetActivePlayerController() const;

	// returns the same value as GetWorld().
	virtual class UWorld* GetActiveWorld() const;

	// This will always return the player controller from the game world.
	UFUNCTION(BlueprintCallable, Category = "Player")
	virtual class APlayerController* GetNormalOwningPlayer() const;

	// This will always return the game world.
	virtual class UWorld* GetNormalWorld() const;

	UFUNCTION(BlueprintCallable, Category = "RHWidget")
	virtual void HideWidget();

	UFUNCTION(BlueprintCallable, Category = "RHWidget")
	virtual void ShowWidget();

	UFUNCTION(BlueprintNativeEvent, Category = "RHWidget")
	void OnShown();
	virtual void OnShown_Implementation() { };

	UFUNCTION(BlueprintNativeEvent, Category = "RHWidget")
	void OnHide();
	virtual void OnHide_Implementation() { };
	
	UFUNCTION(BlueprintNativeEvent, Category = "RHWidget|Navigation")
	void InitializeWidgetNavigation();
	virtual void InitializeWidgetNavigation_Implementation() { };

	UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
	void InitializeWidgetButtonListeners();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RHWidget")
	bool CanCloseOnLogout();

	UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    void BindToInputManager(int32 DefaultFocusGroup);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    void RegisterWidgetToInputManager(UWidget* Widget, int32 FocusGroup, UWidget* Up, UWidget* Down, UWidget* Left, UWidget* Right);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    void UpdateRegistrationToInputManager(UWidget* Widget, int32 FocusGroup, UWidget* Up, UWidget* Down, UWidget* Left, UWidget* Right);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    void UnregisterWidgetFromInputManager(UWidget* Widget);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    void UnregisterFocusGroup(int32 FocusGroup);

	// Copies a focus group from another widget to this one
	UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
	void InheritFocusGroupFromWidget(int32 TargetFocusGroupNum, URHWidget* SourceWidget, int32 SourceFocusGroupNum);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    virtual void SetFocusToGroup(int32 FocusGroup, bool KeepLastFocus);

    // Sets the focus to a specific widget within a group
    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    void SetFocusToWidgetOfGroup(int32 FocusGroup, URHWidget* Widget);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    void SetDefaultFocusForGroup(UWidget* Widget, int32 FocusGroup);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    UWidget* SetFocusToThis();

    UFUNCTION(BlueprintCallable, Category = "RHWidget|Input Manager")
    UWidget* GetCurrentFocusForGroup(int32 FocusGroup);

	UFUNCTION(BlueprintPure, Category = "RHWidget|Input Manager")
	bool GetCurrentFocusGroup(int32& OutFocusGroup);

	UFUNCTION(BlueprintCallable, Category = "RHWidget|Navigation")
	void ClearNavigationInputThrottle();

	UFUNCTION(BlueprintCallable, Category = "RHWidget|Navigation")
	bool ExplicitNavigateUp();

	UFUNCTION(BlueprintCallable, Category = "RHWidget|Navigation")
	bool ExplicitNavigateDown();

	UFUNCTION(BlueprintCallable, Category = "RHWidget|Navigation")
	bool ExplicitNavigateLeft();

	UFUNCTION(BlueprintCallable, Category = "RHWidget|Navigation")
	bool ExplicitNavigateRight();

	// Clears a specific context for this widget
	UFUNCTION(BlueprintCallable, Category="Context Action")
	void ClearContextAction(FName ContextName);

	// Clears all of the context for this widget
	UFUNCTION(BlueprintCallable, Category="Context Action")
	void ClearAllContextActions();

	// Adds a given context for this widget
	UFUNCTION(BlueprintCallable, Category="Context Action")
	void AddContextAction(FName ContextName, FText FormatAdditive = FText());

	// Adds multiple contexts for this widget
	UFUNCTION(BlueprintCallable, Category="Context Action")
	void AddContextActions(TArray<FName> ContextNames);

	// Sets an event callback for a given context being activated
	UFUNCTION(BlueprintCallable, Category="Context Action")
	void SetContextAction(FName ContextName, const FOnContextAction& EventCallback);

	// Sets an event callback for a given cycle context being activated
	UFUNCTION(BlueprintCallable, Category="Context Action")
	void SetContextCycleAction(FName ContextName, const FOnContextCycleAction& EventCallback);

	// Sets an event callback for a given hold and release context being activated
	UFUNCTION(BlueprintCallable, Category="Context Action")
	void SetContextHoldReleaseAction(FName ContextName, const FOnContextHoldActionUpdate& UpdateCallback, const FOnContextHoldAction& EventCallback);

    UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "User Interface|Animation")
    void SetAllAnimationsPlaybackSpeed(float PlaybackSpeed);

	UFUNCTION(BlueprintCallable)
	void TriggerGlobalInvalidate();

	UFUNCTION(BlueprintCallable, Category = "RHWidget")
	void DisplayGenericPopup(const FString& sTitle, const FString& sDesc);

	UFUNCTION(BlueprintCallable, Category = "RHWidget")
	void DisplayGenericError(const FString& sDesc);

	// Sets/Clears pressed state for these actions and pushes values on to Blueprint if needed
	bool NavigateBackPressedOnWidget();
	bool NavigateBackOnWidget();
	void NavigateBackCancelledOnWidget();

	bool NavigateConfirmPressedOnWidget();
	bool NavigateConfirmOnWidget();
	void NavigateConfirmCancelledOnWidget();

	// Clears pressed Cancel/Confirm presses
	void ClearPressedStates();

	// Allow blueprints to simulate hover/unhover in weird cases
    UFUNCTION(BlueprintCallable, Category = "RHWidget|Navigation")
    void OnGamepadHover();

	virtual void NativeGamepadHover();
    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void GamepadHover();

    // Allow blueprints to simulate hover/unhover in weird cases
    UFUNCTION(BlueprintCallable, Category = "RHWidget|Navigation")
    void OnGamepadUnhover();

	virtual void NativeGamepadUnhover();
    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void GamepadUnhover();

    UFUNCTION(BlueprintNativeEvent, Category = "RHWidget")
    FEventReply GamepadButtonDown(FKey Button);

    UFUNCTION(BlueprintNativeEvent, Category = "RHWidget")
    FEventReply GamepadButtonUp(FKey Button);

    UFUNCTION(BlueprintCallable, Category = "RHWidget")
    FGeometry GetGeometryFromLastTick();

    UFUNCTION(BlueprintCallable, Category = "RHWidget")
    void AsyncLoadTexture2D(TSoftObjectPtr<UTexture2D> Texture2DRef);

    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget")
    FRHWidgetNavigatedBack OnNavigateBack;

    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget")
    FRHWidgetTextureLoaded OnTextureLoadComplete;

    UFUNCTION(BlueprintNativeEvent, Category = "RHWidget|View Manager")
    void StartHideSequence(FName FromRoute, FName ToRoute);

    UFUNCTION(BlueprintNativeEvent, Category = "RHWidget|View Manager")
    void StartShowSequence(FName FromRoute, FName ToRoute);

    UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
    void CallOnHideSequenceFinished();

    UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
    void CallOnShowSequenceFinished();

    //This will add a new route to the top of the view route stack, with an param to clear the current stack first.
    UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
    bool AddViewRoute(FName RouteName, bool ClearRouteStack = false, bool ForceTransition = false, UObject* Data = nullptr);

    //This will remove the view route from the stack, if it is the top view, it will swap the view to the next top view
    UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
    bool RemoveViewRoute(FName RouteName, bool ForceTransition = false);

    //This will swap a new route with another given view route.  If SwapTargetRoute is empty, it will swap the top of the view route stack.
    UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
    bool SwapViewRoute(FName RouteName, FName SwapTargetRoute = NAME_None, bool ForceTransition = false);

    //This will remove the top of the view route stack, and go to the next view route under the top.
    UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
    bool RemoveTopViewRoute(bool ForceTransition = false);

	// Returns if the current screen is the top view route or not
	UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
	bool IsTopViewRoute();

	UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
	void SetRouteName(FName RouteName) { MyRouteName = RouteName; }

    // Grabs any stored data for a given route
    UFUNCTION(BlueprintPure, Category = "RHWidget|View Manager")
    bool GetPendingRouteData(FName RouteName, UObject*& Data) const;

    // Sets the pending route data for a route, used when transitioning back and not using a AddViewRoute call
    UFUNCTION(BlueprintCallable, Category = "RHWidget|View Manager")
    void SetPendingRouteData(FName RouteName, UObject* Data);

    UFUNCTION(BlueprintPure, Category = "RHWidget|View Manager")
    URHViewManager* GetViewManager() const;

    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void NavigateUpFailure();
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget")
    FRHNavigateFailed OnNavigateUpFailed;

    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void NavigateDownFailure();
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
    FRHNavigateFailed OnNavigateDownFailed;

    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void NavigateLeftFailure();
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
    FRHNavigateFailed OnNavigateLeftFailed;

    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void NavigateRightFailure();
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
    FRHNavigateFailed OnNavigateRightFailed;

    UFUNCTION()
    virtual void NativeFocusGroupNavigateUpFailure(int32 FocusGroup, URHWidget* Widget);
    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void FocusGroupNavigateUpFailure(int32 FocusGroup);
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
    FRHFocusGroupNavigateFailed OnFocusGroupNavigateUpFailed;

    UFUNCTION()
    virtual void NativeFocusGroupNavigateDownFailure(int32 FocusGroup, URHWidget* Widget);
    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void FocusGroupNavigateDownFailure(int32 FocusGroup);
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
    FRHFocusGroupNavigateFailed OnFocusGroupNavigateDownFailed;

    UFUNCTION()
    virtual void NativeFocusGroupNavigateLeftFailure(int32 FocusGroup, URHWidget* Widget);
    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void FocusGroupNavigateLeftFailure(int32 FocusGroup);
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
    FRHFocusGroupNavigateFailed OnFocusGroupNavigateLeftFailed;

    UFUNCTION()
    virtual void NativeFocusGroupNavigateRightFailure(int32 FocusGroup, URHWidget* Widget);
    UFUNCTION(BlueprintImplementableEvent, Category = "RHWidget|Navigation")
    void FocusGroupNavigateRightFailure(int32 FocusGroup);
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
    FRHFocusGroupNavigateFailed OnFocusGroupNavigateRightFailed;

	void SetUsesBlocker(bool bShouldUseBlocker);

    UPROPERTY()
    FOnHideSequenceFinished OnHideSequenceFinished;
    UPROPERTY()
    FOnShowSequenceFinished OnShowSequenceFinished;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
	FRHWidgetGamepadHovered OnGamepadHovered;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "RHWidget|Navigation")
	FRHWidgetMouseEntered OnMouseEntered;

	// focus support
    /**
    * Returns whether or not the RHWidget is ready to receive navigation focus.
    * By default, this is based on IsVisible and IsFocusable.
    */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Focus")
    bool IsFocusEnabled();

	UFUNCTION(BlueprintPure, Category = "Focus")
	bool GetIsComponent() { return IsComponent; }

	UFUNCTION(BlueprintPure, Category = "Focus")
	bool GetUsesBlocker() { return UsesBlocker; }

	UFUNCTION(BlueprintPure, Category = "Focus")
	bool GetBlockerClickToClose() { return BlockerClickToClose; }

protected:
    // UMG implementations of each of these events that happens once an event is verified and let through
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RHWidget|Navigation")
    bool NavigateBackPressed();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RHWidget|Navigation")
    bool NavigateBack();
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RHWidget|Navigation")
	void NavigateBackCancelled();

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RHWidget|Navigation")
    bool NavigateConfirmPressed();
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RHWidget|Navigation")
    bool NavigateConfirm();
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RHWidget|Navigation")
	void NavigateConfirmCancelled();

	UFUNCTION(BlueprintNativeEvent, Category = "RHWidget")
	void GameStateSet(class AGameStateBase* GameState);

	UPROPERTY(BlueprintReadOnly, Category = "RHWidget")
    TWeakObjectPtr<class ARHHUDCommon> MyHud;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RHWidget")
	FName MyRouteName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RHWidget")
    bool CloseOnLogout;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RHWidget")
    bool IsComponent;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RHWidget")
    bool StartsHidden;

    UPROPERTY(GlobalConfig)
    bool InputComponentUsesWidgetPriority;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RHWidget")
    bool UsesBlocker;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RHWidget")
    bool BlockerClickToClose;

	UPROPERTY(EditAnywhere, Category = "RHWidget")
	bool EnableGameStateSetNotify;

	bool NavigateBackIsPressed;
	bool NavigateConfirmIsPressed;

	UPROPERTY(BlueprintReadOnly, Category = "RHWidget")
	TSoftObjectPtr<UTexture2D> LoadedTexture;

	virtual void NativeOnInitialized() override;

	bool bMctsBindingsRegistered;

	FStreamableManager Streamable;

private:
	URHInputManager* GetInputManager();

	void SetUsesBlocker_Internal();

	void OnTextureLoaded();

	bool bBoundToInputManager;

	FGeometry GeometryFromLastTick;

	//Mobile layout support
public:
    UFUNCTION(BlueprintPure, Category = "Mobile")
    bool ShouldUseMobileLayout() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Mobile")
	void OnEnterMobileLayout();

	UFUNCTION(BlueprintImplementableEvent, Category = "Mobile")
	void OnExitMobileLayout();
private:
	//If in touch mode, activate mobile layout
	UFUNCTION()
	void ToggleMobileLayout(RH_INPUT_STATE InputState);

	//Find MobileLayout animation and play if found
	void FindAndPlayMobileLayoutAnim();

	//Find MobileLayout animation
	UWidgetAnimation* FindMobileLayoutAnim();

	//Restore layout from before playing MobileLayout animation
	void RestorePreMobileAnimLayout();

	UPROPERTY(Transient)
	UWidgetAnimation* MobileLayoutAnim;

	UPROPERTY(Transient)
	bool bMobileLayoutActive;
};