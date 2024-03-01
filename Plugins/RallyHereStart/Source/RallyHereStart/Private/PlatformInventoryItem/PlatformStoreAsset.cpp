// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "GameplayTagContainer.h"
#include "PlatformInventoryItem/PlatformStoreAsset.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "PlatformInventoryItem/PInv_Delegates.h"
#include "GameFramework/RHGameInstance.h"
#include "Managers/RHStoreItemHelper.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_CatalogSubsystem.h"

const FName UPlatformStoreAsset::StoreAssetCollectionName(TEXT("ItemCollection.StoreAsset"));

UPlatformStoreAsset::UPlatformStoreAsset(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
    : Super(ObjectInitializer)
{
    static const FGameplayTag StoreAssetCollectionTag = FGameplayTag::RequestGameplayTag(StoreAssetCollectionName);
    CollectionContainer.AddTag(StoreAssetCollectionTag);

    IsOwnableInventoryItem = false;
    LootId = FRH_LootId();
}

FPrimaryAssetId UPlatformStoreAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(UPInv_AssetManager::PlatformStoreAssetType, GetFName());
}

void UPlatformStoreAsset::OverridePerObjectConfigSection(FString& SectionName)
{
	SectionName = GetName() + TEXT(" ") + UPlatformStoreAsset::StaticClass()->GetName();
}

#if WITH_EDITORONLY_DATA
void UPlatformStoreAsset::UpdateAssetBundleData()
{
	Super::UpdateAssetBundleData();

	if (!UAssetManager::IsInitialized())
	{
		if (!ReadyForBundleDataHandle.IsValid())
		{
			ReadyForBundleDataHandle = PInv_Delegates::OnReadyForBundleData.AddUObject(this, &UPlatformStoreAsset::UpdateAssetBundleData);
		}

		return;
	}
}
#endif

void UPlatformStoreAsset::IsOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(false);
		return;
	}

	URHStoreItemHelper* StoreItemHelper = nullptr;
	URH_CatalogSubsystem* CatalogSubsystem = nullptr;

	if (UWorld* const World = PlayerInfo->GetWorld())
	{
		if (URHGameInstance* GameInstance = Cast<URHGameInstance>(World->GetGameInstance()))
		{
			StoreItemHelper = Cast<URHStoreItemHelper>(GameInstance->GetStoreItemHelper());

			if (auto pGISubsystem = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
			{
				CatalogSubsystem = pGISubsystem->GetCatalogSubsystem();
			}

		}
	}

	if (StoreItemHelper == nullptr || CatalogSubsystem == nullptr)
	{
		Delegate.ExecuteIfBound(false);
		return;
	}

	FRHAPI_Loot LootItem;
	if (CatalogSubsystem->GetVendorItemByLootId(LootId, LootItem))
	{
		if (const auto& SubVendorId = LootItem.GetSubVendorIdOrNull())
		{
			// If we are a bundle, we need to search through all contents and make sure the player owns all items.
			TArray<URHStoreItem*> StoreItems = StoreItemHelper->GetStoreItemsForVendor(*SubVendorId, false, true);
			TArray<URHStoreItem*> StoreItemsToCheck;

			for (int32 i = 0; i < StoreItems.Num(); ++i)
			{
				if (StoreItems[i] && StoreItems[i]->GetItemId().IsValid())
				{
					StoreItemsToCheck.Add(StoreItems[i]);
				}
			}

			if (StoreItemsToCheck.Num())
			{
				URH_PlatformStoreAssetOwnershipHelper* Helper = NewObject<URH_PlatformStoreAssetOwnershipHelper>();
				Helper->PlayerInfo = PlayerInfo;
				Helper->ItemsToCheck.Append(StoreItemsToCheck);
				Helper->Delegate = Delegate;

				Helper->OnCompleteDelegate = FRH_OnOwnershipCompleteDelegate::CreateLambda([this, Helper]()
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

				Helper->StartIsOwnedCheck();
			}
			else
			{
				Delegate.ExecuteIfBound(true);
			}
			return;
		}
	}

	Delegate.ExecuteIfBound(false);
}

void UPlatformStoreAsset::CanOwnMore(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(false);
		return;
	}

	URHStoreItemHelper* StoreItemHelper = nullptr;
	URH_CatalogSubsystem* CatalogSubsystem = nullptr;

	if (UWorld* const World = PlayerInfo->GetWorld())
	{
		if (URHGameInstance* GameInstance = Cast<URHGameInstance>(World->GetGameInstance()))
		{
			StoreItemHelper = Cast<URHStoreItemHelper>(GameInstance->GetStoreItemHelper());

			if (auto pGISubsystem = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
			{
				CatalogSubsystem = pGISubsystem->GetCatalogSubsystem();
			}

		}
	}

	if (StoreItemHelper == nullptr || CatalogSubsystem == nullptr)
	{
		Delegate.ExecuteIfBound(false);
		return;
	}

	FRHAPI_Loot LootItem;
	if (CatalogSubsystem->GetVendorItemByLootId(LootId, LootItem))
	{
		if (const auto& SubVendorId = LootItem.GetSubVendorIdOrNull())
		{
			TArray<URHStoreItem*> StoreItems = StoreItemHelper->GetStoreItemsForVendor(*SubVendorId, false, true);
			TArray<URHStoreItem*> StoreItemsToCheck;

			for (int32 i = 0; i < StoreItems.Num(); ++i)
			{
				if (StoreItems[i])
				{
					StoreItemsToCheck.Add(StoreItems[i]);
				}
			}

			if (StoreItemsToCheck.Num())
			{
				URH_PlatformStoreAssetOwnershipHelper* Helper = NewObject<URH_PlatformStoreAssetOwnershipHelper>();
				Helper->PlayerInfo = PlayerInfo;
				Helper->ItemsToCheck.Append(StoreItemsToCheck);
				Helper->Delegate = Delegate;
				
				Helper->OnCompleteDelegate = FRH_OnOwnershipCompleteDelegate::CreateLambda([this, Helper]()
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

				Helper->StartCanOwnMoreCheck();
				return;
			}
			else
			{
				Delegate.ExecuteIfBound(false);
			}

			return;
		}
	}

	Delegate.ExecuteIfBound(false);
}

void URH_PlatformStoreAssetOwnershipHelper::StartIsOwnedCheck()
{
	for (auto& StoreItem : ItemsToCheck)
	{
		StoreItem->IsOwned(PlayerInfo, FRH_GetInventoryStateDelegate::CreateUObject(this, &URH_PlatformStoreAssetOwnershipHelper::OnIsOwnedResponse));
	}
}

void URH_PlatformStoreAssetOwnershipHelper::OnIsOwnedResponse(bool bSuccess)
{
	Results++;

	if (!bSuccess && !HasResponded)
	{
		Delegate.ExecuteIfBound(false);
		HasResponded = true;
	}

	if (Results == ItemsToCheck.Num())
	{
		if (!HasResponded)
		{
			Delegate.ExecuteIfBound(true);
		}

		IsComplete = true;
		OnCompleteDelegate.ExecuteIfBound();
	}
}

void URH_PlatformStoreAssetOwnershipHelper::StartCanOwnMoreCheck()
{
	for (auto& StoreItem : ItemsToCheck)
	{
		StoreItem->CanOwnMore(PlayerInfo, FRH_GetInventoryStateDelegate::CreateUObject(this, &URH_PlatformStoreAssetOwnershipHelper::OnOwnMoreResponse));
	}
}

void URH_PlatformStoreAssetOwnershipHelper::OnOwnMoreResponse(bool bSuccess)
{
	Results++;

	if (bSuccess && !HasResponded)
	{
		Delegate.ExecuteIfBound(true);
		HasResponded = true;
	}

	if (Results == ItemsToCheck.Num())
	{
		if (!HasResponded)
		{
			Delegate.ExecuteIfBound(false);
		}

		IsComplete = true;
		OnCompleteDelegate.ExecuteIfBound();
	}
}