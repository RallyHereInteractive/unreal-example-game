#pragma once

#include "CoreMinimal.h"
#include "Shared/Widgets/RHWidget.h"
#include "RHHomeScreenWidget.generated.h"

UCLASS()
class RALLYHERESTART_API URHHomeScreenWidget : public URHWidget
{
    GENERATED_BODY()

public:
	virtual void InitializeWidget_Implementation() override;

protected:
	UFUNCTION(BlueprintCallable)
	void CheckForOnShownEvents();

	bool CheckForVoucherRedemption();

	bool CheckForWhatsNewModal();

	UFUNCTION(BlueprintImplementableEvent)
	void OnCrossplaySettingChanged();
};