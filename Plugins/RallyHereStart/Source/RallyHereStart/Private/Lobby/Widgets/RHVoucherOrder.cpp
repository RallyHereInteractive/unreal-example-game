// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Managers/RHPopupManager.h"
#include "Managers/RHStoreItemHelper.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "Lobby/Widgets/RHVoucherOrder.h"

void URHVoucherOrder::DisplayVoucherRedemptionFailed()
{
    if (MyHud != nullptr)
    {
		if (ARHLobbyHUD* LobbyHUD = Cast<ARHLobbyHUD>(MyHud))
		{
			if (URHPopupManager* PopupManager = LobbyHUD->GetPopupManager())
			{
				FRHPopupConfig VoucherFailedPopup;
				VoucherFailedPopup.Header = NSLOCTEXT("General", "Error", "Error");
				VoucherFailedPopup.Description = NSLOCTEXT("RHVoucherOrder", "FailedDesc", "Failed to Redeem Voucher, please restart the game and try again");
				VoucherFailedPopup.CancelAction.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

				FRHPopupButtonConfig& ConfirmButton = (VoucherFailedPopup.Buttons[VoucherFailedPopup.Buttons.Add(FRHPopupButtonConfig())]);
				ConfirmButton.Label = NSLOCTEXT("General", "Confirm", "Confirm");
				ConfirmButton.Type = ERHPopupButtonType::Confirm;
				ConfirmButton.Action.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

				PopupManager->AddPopup(VoucherFailedPopup);
			}
		}
    }
}

void URHVoucherOrder::GetVoucherOrders(const FRH_GetAffordableItemsInVendorDynamicDelegate& Delegate)
{
    if (ARHLobbyHUD* LobbyHUD = Cast<ARHLobbyHUD>(MyHud))
    {
        if (URHStoreItemHelper* StoreItemHelper = LobbyHUD->GetItemHelper())
        {
			StoreItemHelper->GetAffordableVoucherItems(MyHud->GetLocalPlayerInfo(), Delegate);
        }
    }
}

void URHVoucherOrder::RedeemVouchers(TArray<URHStoreItem*> VoucherItems, const FRH_CatalogCallDynamicDelegate& Delegate)
{
	if (VoucherItems.Num() == 0 || RedeemVouchersHelper != nullptr)
	{
		Delegate.ExecuteIfBound(false);
		return;
	}

	TArray<URHStoreItem*> PossibleVoucherItems;

	for (int32 i = 0; i < VoucherItems.Num(); i++)
	{
		if (VoucherItems[i] != nullptr)
		{
			if (VoucherItems[i]->GetPrices().Num())
			{
				PossibleVoucherItems.Add(VoucherItems[i]);
			}
		}
	}

	RedeemVouchersHelper = NewObject<URH_RedeemVouchersAsyncTaskHelper>();
	RedeemVouchersHelper->VoucherItems = PossibleVoucherItems;
	RedeemVouchersHelper->PlayerInfo = MyHud->GetLocalPlayerInfo();
	RedeemVouchersHelper->RequestsCompleted = 0;
	RedeemVouchersHelper->OnCompleteDelegate = OnRedeemVouchersAsyncTaskComplete::CreateWeakLambda(this, [this, Delegate]()
		{
			if (RedeemVouchersHelper->PurchaseRequests.Num())
			{
				if (ARHLobbyHUD* LobbyHUD = Cast<ARHLobbyHUD>(MyHud))
				{
					if (URHStoreItemHelper* StoreItemHelper = LobbyHUD->GetItemHelper())
					{
						StoreItemHelper->SubmitFinalPurchase(RedeemVouchersHelper->PurchaseRequests, Delegate);
						RedeemVouchersHelper = nullptr;
						return;
					}
				}
			}

			Delegate.ExecuteIfBound(false);
			RedeemVouchersHelper = nullptr;
		});

	RedeemVouchersHelper->Start();
}

void URH_RedeemVouchersAsyncTaskHelper::Start()
{
    for (int32 i = 0; i < VoucherItems.Num(); i++)
    {
		TArray<URHStoreItemPrice*> Prices = VoucherItems[i]->GetPrices();

    	// only check the first price if it exists
        //for (int32 j = 0; j < Prices.Num(); ++j)
    	int32 j = 0;
    	if (Prices.IsValidIndex(j))
        {
			VoucherItems[i]->CanAfford(PlayerInfo, Prices[j], 1, FRH_GetInventoryStateDelegate::CreateUObject(this, &URH_RedeemVouchersAsyncTaskHelper::CanAffordCheck, VoucherItems[i], Prices[j]));
			break;
        }
	    else
	    {
		    CanAffordCheck(false, VoucherItems[i], nullptr);
	    }
    }
}

void URH_RedeemVouchersAsyncTaskHelper::CanAffordCheck(bool bOwned, URHStoreItem* VoucherItem, URHStoreItemPrice* Price)
{
	RequestsCompleted++;

	if (bOwned && VoucherItem != nullptr && Price != nullptr)
	{
		if (URHStorePurchaseRequest* PurchaseRequest = VoucherItem->GetPurchaseRequest())
		{
			PurchaseRequest->RequestingPlayerInfo = PlayerInfo;
			PurchaseRequest->PriceInUI = Price->Price;
			PurchaseRequest->CurrencyType = Price->CurrencyType.Get();
			PurchaseRequest->ExternalTransactionId = "Redeem DLC Voucher";
			PurchaseRequests.Add(PurchaseRequest);
		}
	}

	if (IsCompleted())
	{
		OnCompleteDelegate.ExecuteIfBound();
	}
}