// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Inventory/RHLoadoutTypes.h"
#include "Lobby/Widgets/RHPlayerCosmeticWidget.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"

bool URHPlayerCosmeticWidget::GetItemsForSlot(FOnGetCosmeticItems Event, ERHLoadoutSlotTypes SlotType)
{
	if (!Event.IsBound())
	{
		return false;
	}

	switch (SlotType)
	{
	case ERHLoadoutSlotTypes::AVATAR_SLOT:
		return GetItemsByTag(Event, SlotType, CollectionNames::AvatarCollectionName);
		break;
	case ERHLoadoutSlotTypes::BANNER_SLOT:
		return GetItemsByTag(Event, SlotType, CollectionNames::BannerCollectionName);
		break;
	case ERHLoadoutSlotTypes::BORDER_SLOT:
		return GetItemsByTag(Event, SlotType, CollectionNames::BorderCollectionName);
		break;
	case ERHLoadoutSlotTypes::TITLE_SLOT:
		return GetItemsByTag(Event, SlotType, CollectionNames::TitleCollectionName);
		break;
	default:
		break;
	}

	return false;
}

bool URHPlayerCosmeticWidget::GetItemsByTag(FOnGetCosmeticItems Event, ERHLoadoutSlotTypes SlotType, FName TagName)
{
	UPInv_AssetManager* AssMan = Cast<UPInv_AssetManager>(UPInv_AssetManager::GetIfInitialized());

	if (AssMan != nullptr)
	{
		TArray<TSoftObjectPtr<UPlatformInventoryItem>> Items = AssMan->GetAllItemsForTag<UPlatformInventoryItem>(FGameplayTag::RequestGameplayTag(TagName));

		TArray<FSoftObjectPath> AssetsToLoad;
		TArray<TSoftObjectPtr<UPlatformInventoryItem>> ValidItems;

		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i].IsValid())
			{
				ValidItems.Add(Items[i]);
			}
			else
			{
				AssetsToLoad.Add(Items[i].ToSoftObjectPath());
			}
		}

		if (AssetsToLoad.Num() == 0)
		{
			AdditionalAssetsAsyncLoaded(Event, SlotType, Items);
			return true;
		}
		else
		{
			Streamable.RequestAsyncLoad(AssetsToLoad, FStreamableDelegate::CreateUObject(this, &URHPlayerCosmeticWidget::AdditionalAssetsAsyncLoaded, Event, SlotType, Items));
		}
	}

	return false;
}

void URHPlayerCosmeticWidget::AdditionalAssetsAsyncLoaded(FOnGetCosmeticItems Event, ERHLoadoutSlotTypes SlotType, TArray<TSoftObjectPtr<UPlatformInventoryItem>> Items)
{
	// Verify we are still bound by the time we get fully loaded assets.
	if (!Event.IsBound() || MyHud->GetLocalPlayerInfo() == nullptr)
	{
		return;
	}

	URH_PlayerCosmeticOwnershipHelper* Helper = NewObject<URH_PlayerCosmeticOwnershipHelper>();
	Helper->PlayerInfo = MyHud->GetLocalPlayerInfo();
	Helper->Event = Event;

	// Pre cull unneeded items
	for (auto& PlatformInventoryItem : Items)
	{
		if (UPlatformInventoryItem* Item = PlatformInventoryItem.Get())
		{
			if (Item->GetItemId().IsValid() && !Item->IsItemDisabled())
			{
				Helper->ItemsToCheck.Add(Item);
			}
		}
	}

	Helper->OnCompleteDelegate = FRH_OnOwnershipCheckCompleteDelegate::CreateLambda([this, Helper]()
		{
			// Instead of having unique idenifiers for these, just remove any completed
			for (int32 i = PendingOwnershipRequests.Num() - 1; i >= 0; i--)
			{
				if (PendingOwnershipRequests[i]->IsComplete)
				{
					PendingOwnershipRequests.RemoveAt(i);
				}
			}
		});

	PendingOwnershipRequests.Add(Helper);
	Helper->StartCheck();
}

void URH_PlayerCosmeticOwnershipHelper::StartCheck()
{
	if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
	{
		for (auto& PlatformInventoryItem : ItemsToCheck)
		{
			PlayerInventory->IsInventoryItemOwned(PlatformInventoryItem->GetItemId(), FRH_GetInventoryStateDelegate::CreateUObject(this, &URH_PlayerCosmeticOwnershipHelper::OnIsOwnedResponse, PlatformInventoryItem));
		}
	}
}

void URH_PlayerCosmeticOwnershipHelper::OnIsOwnedResponse(bool bSuccess, UPlatformInventoryItem* Item)
{
	Results++;

	if (bSuccess)
	{
		ResultList.Items.Add(Item);
	}

	if (Results == ItemsToCheck.Num())
	{
		Event.Execute(ResultList);
		IsComplete = true;
		OnCompleteDelegate.ExecuteIfBound();
	}
}