#include "RallyHereStart.h"
#include "Managers/RHStoreItemHelper.h"
#include "Managers/RHUISessionManager.h"
#include "Online.h"
#include "OnlineSubsystem.h"
#include "GameFramework/RHGameInstance.h"
#include "GameFramework/RHGameUserSettings.h"
#include "Inventory/RHCurrency.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "RH_OnlineSubsystemNames.h"
#include "RH_CatalogSubsystem.h"
#include "RH_EntitlementSubsystem.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"

IOnlineSubsystem* URHStoreItemHelper::GetStoreOSS()
{
	URHStoreItemHelper* CDO = URHStoreItemHelper::StaticClass()->GetDefaultObject<URHStoreItemHelper>();
	FName StoreOSS = ensure(CDO) ? CDO->StoreOSS : NAME_None;
	return IOnlineSubsystem::Get(StoreOSS);
}

void URHStoreItemHelper::Initialize(UGameInstance* InGameInstance)
{
	UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::Initialize()."));

	GameInstance = InGameInstance;
	XpTablesLoaded = false;
	PricePointsLoaded = false;
	PortalOffersLoaded = false;
	IsQueryingPortalOffers = false;
	StoreVendorsLoaded = false;

	if (URHGameInstance* RHGameInstance = Cast<URHGameInstance>(GameInstance))
	{
		RHGameInstance->OnLocalPlayerLoginChanged.AddUObject(this, &URHStoreItemHelper::OnLoginPlayerChanged);
	}

    // Because Premium and Free Currencies are so ubiqutous, we will load them and keep them cached
	if (UPInv_AssetManager* AssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized()))
	{
		if (FreeCurrencyItemId.IsValid())
		{
			TSoftObjectPtr<URHCurrency> FreeCurrencyItemPtr = AssetManager->GetSoftPrimaryAssetByItemId<URHCurrency>(FreeCurrencyItemId);

			if (FreeCurrencyItemPtr.IsValid())
			{
				FreeCurrencyItem = FreeCurrencyItemPtr.Get();
			}
			else
			{
				FreeCurrencyItem = FreeCurrencyItemPtr.LoadSynchronous();
			}
		}

		if (PremiumCurrencyItemId.IsValid())
		{
			TSoftObjectPtr<URHCurrency> PremiumCurrencyItemPtr = AssetManager->GetSoftPrimaryAssetByItemId<URHCurrency>(PremiumCurrencyItemId);

			if (PremiumCurrencyItemPtr.IsValid())
			{
				PremiumCurrencyItem = PremiumCurrencyItemPtr.Get();
			}
			else
			{
				PremiumCurrencyItem = PremiumCurrencyItemPtr.LoadSynchronous();
			}
		}
	}
}

void URHStoreItemHelper::Uninitialize()
{
	UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::Uninitialize()."));

	if (URHGameInstance* RHGameInstance = Cast<URHGameInstance>(GameInstance))
	{
		RHGameInstance->OnLocalPlayerLoginChanged.RemoveAll(this);
	}
}

void URHStoreItemHelper::OnLoginPlayerChanged(ULocalPlayer* LocalPlayer)
{
	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		// Don't re-request if we have the data
		if (!XpTablesLoaded)
		{
			CatalogSubsystem->GetCatalogXpAll(FRH_CatalogCallDelegate::CreateUObject(this, &URHStoreItemHelper::OnXpTablesUpdated));
		}

		if (!InventoryBucketUseRuleSetsLoaded)
		{
			CatalogSubsystem->GetCatalogInventoryBucketUseRuleSetsAll(FRH_CatalogCallDelegate::CreateUObject(this, &URHStoreItemHelper::OnInventoryBucketUseRuleSetsUpdated));
		}

		if (!PricePointsLoaded)
		{
			CatalogSubsystem->GetCatalogPricePointsAll(FRH_CatalogCallDelegate::CreateUObject(this, &URHStoreItemHelper::OnPricePointsUpdated));
		}

		if (!BaseStoreVendorsLoaded)
		{
			TArray<int32> VendorIds;

			if (DoesPortalHaveOffers())
			{
				VendorIds.Push(GetPortalOffersVendorId());
			}
			else
			{
				PortalOffersLoaded = true;
			}

			GetAdditionalStoreVendors(VendorIds);

			if (CouponVendorId > 0)
			{
				VendorIds.Push(CouponVendorId);
			}

			if (VendorIds.Num())
			{
				RequestVendorData(VendorIds, FRH_CatalogCallDelegate::CreateLambda([this](bool bSuccess)
					{
						if (DoesPortalHaveOffers())
						{
							if (!QueryPortalOffers())
							{
								// If we don't actually query the portal offers, set them as loaded
								PortalOffersLoaded = true;
							}
						}

						BaseStoreVendorsLoaded = true;
					}));
			}
			else
			{
				BaseStoreVendorsLoaded = true;
			}
		}
	}
}

void URHStoreItemHelper::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	URHStoreItemHelper* This = CastChecked<URHStoreItemHelper>(InThis);

	for (auto Entry : This->PricePoints)
	{
		TArray<TObjectPtr<URHStoreItemPrice>> PricePoints;
		PricePoints.Reserve(Entry.Value.Num());
		for (auto PricePoint : Entry.Value)
		{
			PricePoints.Add(PricePoint);
		}
		Collector.AddReferencedObjects(PricePoints, This);
	}
}

URHStoreItem* URHStoreItemHelper::GetStoreItem(const FRH_LootId& LootId)
{
	if (LootId.IsValid())
	{
		if (auto findItem = StoreItems.Find(LootId))
		{
			return *findItem;
		}

		if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
		{
			FRHAPI_Loot LootItem;
			if (CatalogSubsystem->GetVendorItemByLootId(LootId, LootItem))
			{
				return CreateStoreItem(LootItem);
			}
		}
	}

	return nullptr;
}

URHStoreItem* URHStoreItemHelper::GetStoreItem(const FRHAPI_Loot& LootItem)
{
	if (auto findItem = StoreItems.Find(LootItem.GetLootId()))
	{
		return *findItem;
	}

	return CreateStoreItem(LootItem);
}

URHStoreItem* URHStoreItemHelper::CreateStoreItem(const FRHAPI_Loot& LootItem)
{
	if (URHStoreItem* StoreItem = NewObject<URHStoreItem>())
	{
		StoreItem->Initialize(this, LootItem);
		StoreItems.Add(LootItem.GetLootId(), StoreItem);
		return StoreItem;
	}

	return nullptr;
}

URHStoreItem* URHStoreItemHelper::GetStoreItemForVendor(int32 nVendorId, const FRH_LootId& nLootItemId)
{
	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(nVendorId, Vendor))
		{
			FRHAPI_Loot LootItem;
			if (URH_CatalogBlueprintLibrary::GetVendorItemById(Vendor, nLootItemId, LootItem))
			{
				return GetStoreItem(LootItem);
			}
		}
	}

	return nullptr;
}

TArray<URHStoreItem*> URHStoreItemHelper::GetStoreItemsForVendor(int32 nVendorId, bool bIncludeInactiveItems, bool bSearchSubContainers)
{
	TMap<FRH_LootId, int32> QuantityMap;

	return GetStoreItemsAndQuantitiesForVendor(nVendorId, bIncludeInactiveItems, bSearchSubContainers, QuantityMap);
}

TArray<URHStoreItem*> URHStoreItemHelper::GetStoreItemsAndQuantitiesForVendor(int32 nVendorId, bool bIncludeInactiveItems, bool bSearchSubContainers, TMap<FRH_LootId, int32>& QuantityMap, int32 ExternalQuantity/* = 1*/)
{
	TArray<URHStoreItem*> StoreItemsOut;

	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(nVendorId, Vendor))
		{
			if (const auto& LootItems = Vendor.GetLootOrNull())
			{
				for (const auto& pItemPair : (*LootItems))
				{
					if (bIncludeInactiveItems || pItemPair.Value.GetActive(false))
					{
						if (bSearchSubContainers && pItemPair.Value.GetSubVendorId(0) != 0)
						{
							TArray<URHStoreItem*> InternalItems = GetStoreItemsAndQuantitiesForVendor(pItemPair.Value.GetSubVendorId(), bIncludeInactiveItems, bSearchSubContainers, QuantityMap, ExternalQuantity * pItemPair.Value.GetQuantity(1));

							for (int32 j = 0; j < InternalItems.Num(); ++j)
							{
								StoreItemsOut.Add(InternalItems[j]);
							}
						}
						else if (URHStoreItem* StoreItem = GetStoreItem(pItemPair.Value))
						{
							StoreItemsOut.Add(StoreItem);

							int32& QuantityMapValue = QuantityMap.FindOrAdd(StoreItem->GetLootId());
							QuantityMapValue += pItemPair.Value.GetQuantity(1) * ExternalQuantity;
						}
					}
				}
			}
		}
	}

	return StoreItemsOut;
}

TArray<FRH_LootId> URHStoreItemHelper::GetLootIdsForVendor(int32 nVendorId, bool bIncludeInactiveItems, bool bSearchSubContainers)
{
	TArray<FRH_LootId> StoreItemsOut;

	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(nVendorId, Vendor))
		{
			if (const auto& LootItems = Vendor.GetLootOrNull())
			{
				for (const auto& pItemPair : (*LootItems))
				{
					if (bIncludeInactiveItems || pItemPair.Value.GetActive(false))
					{
						if (bSearchSubContainers && pItemPair.Value.GetSubVendorId(0) != 0)
						{
							TArray<FRH_LootId> InternalItems = GetLootIdsForVendor(pItemPair.Value.GetSubVendorId(), bIncludeInactiveItems, bSearchSubContainers);

							for (int32 j = 0; j < InternalItems.Num(); ++j)
							{
								StoreItemsOut.AddUnique(InternalItems[j]);
							}
						}
						else
						{
							StoreItemsOut.AddUnique(pItemPair.Value.GetLootId());
						}
					}
				}
			}
		}
	}

	return StoreItemsOut;
}

void URHStoreItemHelper::RequestVendorData(TArray<int32> VendorIds, FRH_CatalogCallBlock Delegate)
{
	if (VendorIds.Num() > 0)
	{
		if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
		{
			FRHVendorGetRequest Request = FRHVendorGetRequest(Delegate, VendorIds);
			CatalogSubsystem->GetCatalogVendor(Request);
		}
	}
}

void URHStoreItemHelper::OnXpTablesUpdated(bool Success)
{
	UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::OnXpTablesUpdated Success: %d"), Success);

	if (Success)
	{
		XpTablesLoaded = true;
		OnReceiveXpTables.Broadcast();
	}
}

void URHStoreItemHelper::OnPricePointsUpdated(bool Success)
{
	UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::OnPricePointsUpdated Success: %d"), Success);

	if (Success)
	{
		PricePointsLoaded = true;
		OnReceivePricePoints.Broadcast();
	}
}

void URHStoreItemHelper::OnInventoryBucketUseRuleSetsUpdated(bool Success)
{
	UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::OnPortalUseRulesetsUpdated Success: %d"), Success);

	if (Success)
	{
		InventoryBucketUseRuleSetsLoaded = true;
	}
}

void URHStoreItemHelper::UIX_CompletePurchaseItem(URHStorePurchaseRequest* pPurchaseRequest, const FRH_CatalogCallDynamicDelegate& Delegate)
{
	TArray<URHStorePurchaseRequest*> PurchaseRequests;

	PurchaseRequests.Add(pPurchaseRequest);

	SubmitFinalPurchase(PurchaseRequests, Delegate);
}

void URHStoreItemHelper::SubmitFinalPurchase(TArray<URHStorePurchaseRequest*> PurchaseRequests, const FRH_CatalogCallBlock& Delegate)
{
	// Don't allow the client to submit multiple purchases
	// NOTE: In case we end up wanting this, we might loop back and modify this to allow multiple pendings.
	if (HasPendingPurchase() || PurchaseRequests.Num() == 0)
	{
		Delegate.ExecuteIfBound(false);
		return;
	}

	const URH_PlayerInfo* RequestingPlayerInfo = nullptr;

	for (int32 i = 0; i < PurchaseRequests.Num(); ++i)
	{
		if (RequestingPlayerInfo == nullptr)
		{
			RequestingPlayerInfo = PurchaseRequests[i]->RequestingPlayerInfo;
		}

		if (RequestingPlayerInfo != PurchaseRequests[i]->RequestingPlayerInfo)
		{
			UE_LOG(RallyHereStart, Error, TEXT("URHStoreItemHelper::SubmitFinalPurchase - PurchaseRequests have mismatched RequestingPlayerUuids!"));
			Delegate.ExecuteIfBound(false);
			return;
		}
	}

	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		PurchaseTaskHelper = NewObject<URH_PurchaseAsyncTaskHelper>();
		PurchaseTaskHelper->PurchaseRequests = PurchaseRequests;
		PurchaseTaskHelper->CatalogSubsystem = CatalogSubsystem;
		PurchaseTaskHelper->StoreItemHelper = this;
		PurchaseTaskHelper->OnCompleteDelegate = FRH_CatalogCallDelegate::CreateWeakLambda(this, [this, Delegate, CatalogSubsystem, PurchaseRequests, RequestingPlayerInfo](bool bSuccess)
			{
				PurchaseTaskHelper = nullptr;

				if (!bSuccess)
				{
					Delegate.ExecuteIfBound(false);
				}
				else
				{
					TArray<URH_PlayerOrderEntry*> PlayerOrderEntries;

					for (int32 i = 0; i < PurchaseRequests.Num(); ++i)
					{
						FRHAPI_Vendor Vendor;
						if (CatalogSubsystem->GetVendorById(PurchaseRequests[i]->VendorId, Vendor))
						{
							FRHAPI_Loot LootItem;
							if (URH_CatalogBlueprintLibrary::GetVendorItemById(Vendor, PurchaseRequests[i]->LootTableItemId, LootItem))
							{
								if (LootItem.GetActive(false))
								{
									URH_PlayerOrderEntry* NewPlayerOrderEntry = NewObject<URH_PlayerOrderEntry>();

									NewPlayerOrderEntry->FillType = ERHAPI_PlayerOrderEntryType::PurchaseLoot;
									NewPlayerOrderEntry->LootItem = LootItem;
									NewPlayerOrderEntry->Quantity = PurchaseRequests[i]->Quantity;
									NewPlayerOrderEntry->ExternalTransactionId = PurchaseRequests[i]->ExternalTransactionId;

									// old price mechanism, only supports single currency, left for redundancy
									PRAGMA_DISABLE_DEPRECATION_WARNINGS
									NewPlayerOrderEntry->PriceItemId = PurchaseRequests[i]->CurrencyType->GetItemId();
									NewPlayerOrderEntry->Price = PurchaseRequests[i]->PriceInUI / PurchaseRequests[i]->Quantity;
									PRAGMA_ENABLE_DEPRECATION_WARNINGS

									// new price mechanism, supports multiple currencies
									FRHAPI_PurchasePriceCurrency PriceCurrency;
									PriceCurrency.SetPriceItemId(PurchaseRequests[i]->CurrencyType->GetItemId());
									PriceCurrency.SetPrice(PurchaseRequests[i]->PriceInUI / PurchaseRequests[i]->Quantity);
									NewPlayerOrderEntry->Prices = {PriceCurrency};
									
									NewPlayerOrderEntry->CouponItemId = PurchaseRequests[i]->CouponId;

									PlayerOrderEntries.Push(NewPlayerOrderEntry);
								}
								else
								{
									// If any individual item is invalid, we skip the whole purchase
									Delegate.ExecuteIfBound(false);
									return;
								}
							}
							else
							{
								// If any individual item is invalid, we skip the whole purchase
								Delegate.ExecuteIfBound(false);
								return;
							}
						}
						else
						{
							// If any individual item is invalid, we skip the whole purchase
							Delegate.ExecuteIfBound(false);
							return;
						}
					}

					if (PlayerOrderEntries.Num() == PurchaseRequests.Num())
					{
						PendingPurchaseData = PurchaseRequests;

						RequestingPlayerInfo->GetPlayerInventory()->CreateNewPlayerOrder(ERHAPI_Source::Client, true, PlayerOrderEntries, FRH_OrderResultDelegate::CreateLambda([this](const URH_PlayerInfo* PlayerInfo, TArray<URH_PlayerOrderEntry*> OrderEntries, const FRHAPI_PlayerOrder& PlayerOrderResult)
							{
								PendingPurchaseData.Empty();
								OnPendingPurchaseReceived.Broadcast();
								OnPendingPurchaseReceivedNative.ExecuteIfBound(PlayerInfo, PlayerOrderResult);
							}));

						OnPurchaseSubmitted.Broadcast();

						Delegate.ExecuteIfBound(true);
						return;
					}

					Delegate.ExecuteIfBound(false);
				}
			});

		PurchaseTaskHelper->CheckPurchaseValidity();
		return;
	}

	Delegate.ExecuteIfBound(false);
}

void URH_PurchaseAsyncTaskHelper::CheckPurchaseValidity()
{
	TArray<URH_PlayerOrderEntry*> PlayerOrderEntries;

	URH_PlayerInfo* RequestingPlayerInfo = nullptr;

	RequestsCompleted = 0;
	HasResponded = false;

	for (int32 i = 0; i < PurchaseRequests.Num(); ++i)
	{
		StoreItemHelper->CheckPurchaseRequestValidity(PurchaseRequests[i], FRH_CatalogCallDelegate::CreateWeakLambda(this, [this](bool bSuccess)
			{
				// If any individual item is invalid, we skip the whole purchase
				RequestsCompleted++;

				if (!bSuccess && !HasResponded)
				{
					HasResponded = true;
					OnCompleteDelegate.ExecuteIfBound(false);
					return;
				}

				if (RequestsCompleted == PurchaseRequests.Num() && !HasResponded)
				{
					OnCompleteDelegate.ExecuteIfBound(true);
				}
			}));
	}
}

void URHStoreItemHelper::CheckPurchaseRequestValidity(URHStorePurchaseRequest* PurchaseRequest, const FRH_CatalogCallBlock& Delegate)
{
	if (PurchaseRequest && PurchaseRequest->IsValid() && PurchaseRequest->Quantity > 0)
	{
		if (PurchaseRequest->SkipCurrencyAmountValidation)
		{
			Delegate.ExecuteIfBound(true);
		}
		else
		{
			HasEnoughCurrency(PurchaseRequest, FRH_CatalogCallDelegate::CreateWeakLambda(this, [this, PurchaseRequest, Delegate](bool bSuccess)
				{
					if (!bSuccess)
					{
						OnNotEnoughCurrency.Broadcast(PurchaseRequest);
					}

					Delegate.ExecuteIfBound(bSuccess);
				}));
		}
		return;
	}

	Delegate.ExecuteIfBound(false);
}

void URHStoreItemHelper::HasEnoughCurrency(URHStorePurchaseRequest* pPurchaseRequest, const FRH_CatalogCallBlock& Delegate)
{
	if (pPurchaseRequest == nullptr || !pPurchaseRequest->IsValid())
	{
		Delegate.ExecuteIfBound(false);
		return;
	}

	GetCurrencyOwned(pPurchaseRequest->CurrencyType, pPurchaseRequest->RequestingPlayerInfo, FRH_GetInventoryCountDelegate::CreateWeakLambda(this, [this, pPurchaseRequest, Delegate](int32 Count)
		{
			Delegate.ExecuteIfBound(pPurchaseRequest->PriceInUI <= Count);
		}));
}

void URHStoreItemHelper::GetCurrencyOwned(TSoftObjectPtr<UPlatformInventoryItem> CurrencyType, const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryCountBlock& Delegate)
{
	// If we need to know how much a player owns of a currency and its not loaded, we need to load it now or we get invalid data.
	if (!CurrencyType.IsNull())
	{
		if (!CurrencyType.IsValid())
		{
			UAssetManager::GetStreamableManager().RequestAsyncLoad(CurrencyType.ToSoftObjectPath(), FStreamableDelegate::CreateWeakLambda(this, [this, CurrencyType, PlayerInfo, Delegate]()
				{
					GetCurrencyOwned(CurrencyType.Get(), PlayerInfo, Delegate);
				}));
		}
		else
		{
			GetCurrencyOwned(CurrencyType.Get(), PlayerInfo, Delegate);
		}
		return;
	}

	Delegate.ExecuteIfBound(0);
}

void URHStoreItemHelper::GetCurrencyOwned(UPlatformInventoryItem* pPlatformInvItem, const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryCountBlock& Delegate)
{
	if (pPlatformInvItem == nullptr || PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(0);
		return;
	}

	if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
	{
		PlayerInventory->GetInventoryCount(pPlatformInvItem->GetItemId(), Delegate);
	}
	else
	{
		Delegate.ExecuteIfBound(0);
	}
}

bool URHStoreItemHelper::GetXpTable(int32 XpTableId, FRHAPI_XpTable& XpTable)
{
	if (XpTablesLoaded)
	{
		if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
		{
			return CatalogSubsystem->GetXpTable(XpTableId, XpTable);
		}
	}

	return false;
}

bool URHStoreItemHelper::QueryPortalOffers()
{
	if (IsQueryingPortalOffers)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPortalOffers query already in progress"));
		return false;
	}

	if (!GWorld || !GWorld->GetFirstLocalPlayerFromController())
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPortalOffers failed to find world or first local player controller"));
		return false;
	}

	auto* OSS = GetStoreOSS();
	if (!OSS)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPortalOffers failed to get online subsystem"));
		return false;
	}

	auto Store = OSS->GetStoreV2Interface();
	if (!Store.IsValid())
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPortalOffers failed to get store interface"));
		return false;
	}

	const auto PlayerId = GWorld->GetFirstLocalPlayerFromController()->GetPreferredUniqueNetId();
	if (!PlayerId.IsValid())
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPortalOffers failed to get player id"));
		return false;
	}

	TArray<FString> ProductIds;
	TArray<FSoftObjectPath> AssetsToLoad;
	SkuToStoreItem.Empty();

	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(GetPortalOffersVendorId(), Vendor))
		{
			if (const auto& LootItems = Vendor.GetLootOrNull())
			{
				for (const auto& pItemPair : (*LootItems))
				{
					if (pItemPair.Value.GetActive(false))
					{
						if (URHStoreItem* StoreItem = GetStoreItem(pItemPair.Value.GetLootId()))
						{
							FString SKU;

							if (!StoreItem->GetInventoryItem().IsValid())
							{
								AssetsToLoad.Add(StoreItem->GetInventoryItem().ToSoftObjectPath());
							}
							else if (AssetsToLoad.Num() == 0 && StoreItem->GetSku(SKU))
							{
								UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::QueryPortalOffers adding product id %s for loot id %i"), *SKU, pItemPair.Value.GetLootId());
								ProductIds.Add(SKU);
								SkuToStoreItem.Add(SKU, StoreItem);
							}
						}
					}
				}
			}
		}
		else
		{
			UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPortalOffers failed to get mcts shop vendor %i"), GetPortalOffersVendorId());
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPortalOffers failed to get mcts shop"));
	}

	if (AssetsToLoad.Num())
	{
		// If we need to load more assets to check put us in the actively working on a query state
		IsQueryingPortalOffers = true;
		UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetsToLoad, FStreamableDelegate::CreateUObject(this, &URHStoreItemHelper::PortalOffersItemsFullyLoaded));
		return true;
	}
	else if (ProductIds.Num())
	{
		IsQueryingPortalOffers = true;
		Store->QueryOffersById(*PlayerId, ProductIds, FOnQueryOnlineStoreOffersComplete::CreateUObject(this, &URHStoreItemHelper::QueryPlatformStoreOffersComplete));
		return true;
	}

	return false;
}

void URHStoreItemHelper::PortalOffersItemsFullyLoaded()
{
	// We have fully loaded all our portal offer items, kick off the skue request now
	IsQueryingPortalOffers = false;
	if (!QueryPortalOffers())
	{
		// If we don't actually query the portal offers, set them as loaded
		PortalOffersLoaded = true;
	}
}

void URHStoreItemHelper::QueryPlatformStoreOffersComplete(bool bSuccess, const TArray<FUniqueOfferId>& offers, const FString& error)
{
	IsQueryingPortalOffers = false;

	if (!bSuccess)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHStoreItemHelper::QueryPlatformStoreOffersComplete returned error %s"), *error);
	}

	if (offers.Num() > 0)
	{
		if (auto OSS = GetStoreOSS())
		{
			auto Store = OSS->GetStoreV2Interface();
			if (Store.IsValid())
			{
				TArray<FOnlineStoreOfferRef> Offers;
				Store->GetOffers(Offers);

				UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::QueryPlatformStoreOffersComplete found %i offers"), Offers.Num());
				for (auto& offer : Offers)
				{
					if (auto StoreItem = SkuToStoreItem.Find(offer->OfferId))
					{
						if (URHPortalOffer* portalOffer = NewObject<URHPortalOffer>())
						{
							portalOffer->CurrencyCode = FText::FromString(offer->CurrencyCode);
							portalOffer->Name = offer->Title;
							portalOffer->Desc = offer->LongDescription;
							portalOffer->ShortDesc = offer->Description;
							portalOffer->DisplayCost = offer->GetDisplayPrice();
							portalOffer->Cost = offer->NumericPrice;
							portalOffer->DisplayPreSaleCost = offer->GetDisplayRegularPrice();
							portalOffer->PreSaleCost = offer->RegularPrice;
							portalOffer->SKU = offer->OfferId;

							(*StoreItem)->SetPortalOffer(portalOffer);
						}
					}
				}
			}
		}
	}

	PortalOffersLoaded = true;
	OnPortalOffersReceived.Broadcast();
}

void URHStoreItemHelper::EnterInGameStoreUI()
{
	if (IOnlineSubsystem* OSS = GetStoreOSS())
	{
		IOnlineExternalUIPtr UI = OSS->GetExternalUIInterface();
		if (UI.IsValid())
		{
			UI->ReportEnterInGameStoreUI();
		}
	}
}

void URHStoreItemHelper::ExitInGameStoreUI()
{
	if (IOnlineSubsystem* OSS = GetStoreOSS())
	{
		IOnlineExternalUIPtr UI = OSS->GetExternalUIInterface();
		if (UI.IsValid())
		{
			UI->ReportExitInGameStoreUI();
		}
	}
}

bool URHStoreItemHelper::CheckEmptyInGameStore(const class UObject* WorldContextObject)
{
	if (auto OSS = GetStoreOSS())
	{
		auto Store = OSS->GetStoreV2Interface();
		if (Store.IsValid())
		{
			TArray<FOnlineStoreOfferRef> Offers;
			Store->GetOffers(Offers);
			UE_LOG(RallyHereStart, Verbose, TEXT("URHStoreItemHelper::CheckEmptyInGameStore got %i offers"), Offers.Num());
			if (Offers.Num() <= 0)
			{
				if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
				{
					APlayerController* PlayerController = World->GetGameInstance() ? World->GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
					if (PlayerController && PlayerController->PlayerState && PlayerController->PlayerState->GetUniqueId().IsValid())
					{
						TSharedPtr<const FUniqueNetId> UserId = PlayerController->PlayerState->GetUniqueId().GetUniqueNetId();
						if (UserId.IsValid())
						{
							const auto ExternalUI = Online::GetExternalUIInterface();
							if (ExternalUI.IsValid())
							{
								UE_LOG(RallyHereStart, Log, TEXT("URHStoreItemHelper::CheckEmptyInGameStore called ShowPlatformMessageBox"));
								ExternalUI->ShowPlatformMessageBox(*UserId, EPlatformMessageType::EmptyStore);
								return true;
							}
						}
					}
				}
			}
		}
	}

	return false;
}

void URHStoreItemHelper::EntitlementProcessingComplete(bool bSuccessful, FRHAPI_PlatformEntitlementProcessResult Result)
{
	if (bSuccessful)
	{
		if (GWorld)
		{
			if (const ULocalPlayer* LocalPlayer = GWorld->GetFirstLocalPlayerFromController())
			{
				if (const URH_LocalPlayerSubsystem* RHSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
				{
					if (const URH_PlayerInfo* PlayerInfo = RHSS->GetLocalPlayerInfo())
					{
						PlayerInfo->GetPlayerInventory()->AddPendingOrdersFromEntitlementResult(Result, FRH_OrderDetailsDelegate::CreateUObject(this, &URHStoreItemHelper::OnEntitlementResult, PlayerInfo));
					}
				}
			}
		}
	}
}

void URHStoreItemHelper::OnEntitlementResult(const TArray<FRHAPI_PlayerOrder>& OrderResults, const URH_PlayerInfo* PlayerInfo)
{
	OnPendingPurchaseReceived.Broadcast();
	for (const auto& OrderResult : OrderResults)
	{
		OnPendingPurchaseReceivedNative.ExecuteIfBound(PlayerInfo, OrderResult);
	}
	Cast<URHGameInstance>(GameInstance)->GetOrderManager()->OnPlayerOrder(OrderResults, PlayerInfo);
}

TSoftObjectPtr<UPlatformInventoryItem> URHStoreItemHelper::GetInventoryItem(const FRH_ItemId& ItemId) const
{
	if (ItemId.IsValid())
	{
		UPInv_AssetManager* pManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
		if (pManager != nullptr)
		{
			return pManager->GetSoftPrimaryAssetByItemId<UPlatformInventoryItem>(ItemId);
		}
	}

	return nullptr;
}

bool URHStoreItemHelper::GetPriceForGUID(FStorePriceKey PricePoint, TArray<URHStoreItemPrice*>& Prices)
{
	if (PricePointsLoaded)
	{
		if (auto Price = PricePoints.Find(PricePoint))
		{
			Prices = *Price;
			return true;
		}
		else
		{
			GeneratePriceData(PricePoint, Prices);
		}
	}

	return false;
}


void URHStoreItemHelper::GeneratePriceData(FStorePriceKey PricePointKey, TArray<URHStoreItemPrice*>& Prices)
{
	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		FRHAPI_PricePoint PricePoint;

		if (!CatalogSubsystem->GetPricePointById(PricePointKey.PricePointGuid, PricePoint))
		{
			return;
		}

		TArray<URHStoreItemPrice*> NewPrices;

		if (const auto& CurrentBreakpoints = PricePoint.GetCurrentBreakpointsOrNull())
		{
			for (const auto& Price : *CurrentBreakpoints)
			{
				auto PriceItem = GetInventoryItem(Price.GetPriceItemId());

				if (URHStoreItemPrice* NewPrice = NewObject<URHStoreItemPrice>())
				{
					NewPrice->pItemHelper = this;
					NewPrice->Price = Price.GetPrice();
					// Presale price only differs from current price if there is a sale.
					NewPrice->PreSalePrice = NewPrice->Price;

					FRHAPI_PricePoint PreSalesPricePoint;
					bool PresalePriceIsSet = false;

					if (PricePointKey.PreSalePricePointGuid.IsValid())
					{
						if (CatalogSubsystem->GetPricePointById(PricePointKey.PreSalePricePointGuid, PreSalesPricePoint))
						{
							if (const auto& PresaleBreakpoints = PreSalesPricePoint.GetPreSaleBreakpointsOrNull())
							{
								URH_CatalogBlueprintLibrary::GetUnitPrice(*PresaleBreakpoints, Price.GetPriceItemId(), 1, NewPrice->PreSalePrice);
							}
							else if (const auto& PresaleCurrentBreakpoints = PreSalesPricePoint.GetCurrentBreakpointsOrNull())
							{
								URH_CatalogBlueprintLibrary::GetUnitPrice(*PresaleCurrentBreakpoints, Price.GetPriceItemId(), 1, NewPrice->PreSalePrice);
							}
						}
					}

					NewPrice->CurrencyType = PriceItem;

					NewPrices.Add(NewPrice);
				}
			}
		}

		SortPriceData(NewPrices);
		Prices = PricePoints.Add(PricePointKey, NewPrices);
	}
}

void URHStoreItemHelper::GetCouponsForItem(const URHStoreItem* StoreItem, TArray<FRHAPI_Loot>& Coupons) const
{
	CouponsForLootTableItems.MultiFind(StoreItem->GetLootId(), Coupons);
}

void URHStoreItemHelper::ProcessCouponVendor()
{
	CouponsForLootTableItems.Empty();

	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(CouponVendorId, Vendor))
		{
			if (const auto& LootItems = Vendor.GetLootOrNull())
			{
				for (const auto& pItemPair : (*LootItems))
				{
					if (URH_CatalogItem* pItem = CatalogSubsystem->GetCatalogItemByItemId(pItemPair.Value.GetItemId(0)))
					{
						for (const int32& LootId : pItem->GetCouponDiscountLoot())
						{
							CouponsForLootTableItems.Add(LootId, pItemPair.Value);
						}
					}
				}
			}
		}
	}
}

int32 URHStoreItemHelper::GetVoucherVendorId() const
{
	switch (URHUIBlueprintFunctionLibrary::GetPlatformIdByOSS(GetStoreOSS()))
	{
	case ERHAPI_Platform::XboxLive:
		return MicrosoftVoucherVendorId;
	case ERHAPI_Platform::Psn:
		return SonyVoucherVendorId;
	case ERHAPI_Platform::NintendoNaid:
	case ERHAPI_Platform::NintendoSwitch:
	case ERHAPI_Platform::NintendoPpid:
		return NintendoVoucherVendorId;
	case ERHAPI_Platform::Epic:
		return EpicVoucherVendorId;
	case ERHAPI_Platform::Steam:
		return ValveVoucherVendorId;
	case ERHAPI_Platform::Anon:
#if !UE_BUILD_SHIPPING
		// If we are connected some other way in dev, use epic vendor
		return EpicVoucherVendorId;
#endif
		break;
	}

	return INDEX_NONE;
}

void URHStoreItemHelper::GetAdditionalStoreVendors(TArray<int32>& StoreVendorsIds)
{
	if (GameCurrencyVendorId > 0)
	{
		StoreVendorsIds.Push(GameCurrencyVendorId);
	}

	int32 VoucherVendorId = GetVoucherVendorId();
	if (VoucherVendorId > 0)
	{
		StoreVendorsIds.Push(VoucherVendorId);
	}
}

URHStoreItem* URHStoreItemHelper::GetDLCForVoucher(const URHStoreItem* DLCVoucher)
{
    if (DLCVoucher)
    {
        if (TSoftObjectPtr<UPlatformInventoryItem> DLCVoucherCurrency = DLCVoucher->GetInventoryItem())
        {
            if (DLCVoucherCurrency.IsValid())
            {
                TArray<URHStoreItem*> VoucherStoreItems = GetStoreItemsForVendor(GetVoucherVendorId(), false, false);

                for (int32 i = 0; i < VoucherStoreItems.Num(); ++i)
                {
                    if (VoucherStoreItems[i])
                    {
                        TArray<URHStoreItemPrice*> Prices = VoucherStoreItems[i]->GetPrices();

                        for (int32 j = 0; j < Prices.Num(); ++j)
                        {
                            if (Prices[j]->CurrencyType == DLCVoucherCurrency.Get())
                            {
                                return VoucherStoreItems[i];
                            }
                        }
                    }
                }
            }
        }
    }

    return nullptr;
}

void URHStoreItemHelper::RedeemDLCVoucher(URHStoreItem* DLCVoucher, URH_PlayerInfo* PlayerInfo, const FRH_CatalogCallDynamicDelegate& Delegate)
{
    if (DLCVoucher)
    {
        if (TSoftObjectPtr<UPlatformInventoryItem> DLCVoucherCurrency = DLCVoucher->GetInventoryItem())
        {
            if (DLCVoucherCurrency.IsValid())
            {
                if (URHStoreItem* DLCItem = GetDLCForVoucher(DLCVoucher))
                {
                    if (URHStorePurchaseRequest* PurchaseRequest = DLCItem->GetPurchaseRequest())
                    {
						PurchaseRequest->RequestingPlayerInfo = PlayerInfo;
                        PurchaseRequest->PriceInUI = 1;
                        PurchaseRequest->CurrencyType = DLCVoucherCurrency.Get();
						PurchaseRequest->ExternalTransactionId = "Redeem DLC Voucher";
                        PurchaseRequest->SubmitPurchaseRequest(Delegate);
						return;
                    }
                }
            }
        }
    }

	Delegate.ExecuteIfBound(false);
}

void URHStoreItemHelper::GetUpdatedStoreContents()
{
    TArray<int32> VendorIds;

	if (StoreVendorId > 0)
	{
		VendorIds.Push(StoreVendorId);
	}

	if (DailyRotationVendorId > 0)
	{
		VendorIds.Push(DailyRotationVendorId);
	}

	if (VendorIds.Num())
	{
		RequestVendorData(VendorIds, FRH_CatalogCallDelegate::CreateLambda([this](bool bSuccess)
			{
				StoreVendorsLoaded = true;
			}));
	}
	else
	{
		StoreVendorsLoaded = true;
	}

	OnStoreItemsReady.Broadcast();
}

void URHStoreItemHelper::SortPriceData(TArray<URHStoreItemPrice*>& Prices)
{
	// Note: Probably not the most efficient doing all this work in a sort function, but most items have at most 2 prices, so this isn't sorting a TON of data
	Prices.Sort([](URHStoreItemPrice& A, URHStoreItemPrice& B)
	{
		// Both valid, check if they are actual currencies
		if (A.CurrencyType.IsValid() == B.CurrencyType.IsValid())
		{
			if (A.CurrencyType.IsValid())
			{
				URHCurrency* CurrencyA = Cast<URHCurrency>(A.CurrencyType.Get());
				URHCurrency* CurrencyB = Cast<URHCurrency>(B.CurrencyType.Get());

				if (CurrencyA && CurrencyB)
				{
					return CurrencyA->SortOrder <= CurrencyB->SortOrder;
				}

				return ((CurrencyA != nullptr) || (CurrencyB == nullptr));
			}
			else
			{
				// Both not valid, just return true
				return true;
			}
		}

		return A.CurrencyType.IsValid();
	});
}

void URHStoreItemHelper::GetAffordableVoucherItems(URH_PlayerInfo* PlayerInfo, FRH_GetAffordableItemsInVendorBlock Delegate)
{
	GetRedeemableItemsInVendor(PlayerInfo, PortalOffersVendorId, GetVoucherVendorId(), Delegate);
}

void URHStoreItemHelper::GetRedeemableItemsInVendor(URH_PlayerInfo* PlayerInfo, int32 CurrencyVendorId, int32 RedemptionVendorId, FRH_GetAffordableItemsInVendorBlock Delegate)
{
	if (URH_CatalogSubsystem* CatalogSubsystem = GetRH_CatalogSubsystem())
	{
		// Make sure we have the needed vendors before we try to get the items
		FRHAPI_Vendor Vendor;
		if (!CatalogSubsystem->GetVendorById(CurrencyVendorId, Vendor) || !CatalogSubsystem->GetVendorById(RedemptionVendorId, Vendor))
		{
			TArray<int32> VendorIds;
			VendorIds.Add(CurrencyVendorId);
			VendorIds.Add(RedemptionVendorId);

			RequestVendorData(VendorIds, FRH_CatalogCallDelegate::CreateLambda([this, PlayerInfo, CurrencyVendorId, RedemptionVendorId, Delegate](bool bSuccess)
				{
					GetRedeemableItemsInVendor_INTERNAL(PlayerInfo, CurrencyVendorId, RedemptionVendorId, Delegate);
				}));
		}
		else
		{
			GetRedeemableItemsInVendor_INTERNAL(PlayerInfo, CurrencyVendorId, RedemptionVendorId, Delegate);

		}
		return;
	}
	Delegate.ExecuteIfBound(TArray<URHStoreItem*>(), TArray<URHStoreItem*>());
}

void URHStoreItemHelper::GetRedeemableItemsInVendor_INTERNAL(URH_PlayerInfo* PlayerInfo, int32 CurrencyVendorId, int32 RedemptionVendorId, FRH_GetAffordableItemsInVendorBlock Delegate)
{
	TArray<URHStoreItem*> PossibleRedemptionItems = GetStoreItemsForVendor(RedemptionVendorId, false, false);

	URH_GetAllAffordableItemsHelper* Helper = NewObject<URH_GetAllAffordableItemsHelper>();
	
	for (int32 i = 0; i < PossibleRedemptionItems.Num(); ++i)
	{
		if (PossibleRedemptionItems[i])
		{
			TArray<URHStoreItemPrice*> Prices = PossibleRedemptionItems[i]->GetPrices();

			for (int32 j = 0; j < Prices.Num(); ++j)
			{
				if (Prices[j]->CurrencyType.IsValid())
				{
					Helper->ItemsToCheck.Add(PossibleRedemptionItems[i], Prices[j]);
				}
				else if (!Prices[j]->CurrencyType.IsNull())
				{
					Prices[j]->CurrencyType.LoadSynchronous();

					if (Prices[j]->CurrencyType.IsValid())
					{
						Helper->ItemsToCheck.Add(PossibleRedemptionItems[i], Prices[j]);
					}
				}
			}
		}
	}

	if (Helper->ItemsToCheck.Num() > 0)
	{
		Helper->PlayerInfo = PlayerInfo;
		Helper->StoreItemHelper = this;
		Helper->CatalogSubsystem = GetRH_CatalogSubsystem();
		Helper->CurrencyVendorId = CurrencyVendorId;
		Helper->OnCompleteDelegate = Delegate;

		GetOwnershipTrackers.Add(Helper);
		Helper->Start();
	}
	else
	{
		Delegate.ExecuteIfBound(TArray<URHStoreItem*>(), TArray<URHStoreItem*>());
	}
}

bool URHStoreItemHelper::CanRedeemVoucher(URHStoreItem* VoucherItem)
{
	if (VoucherItem != nullptr)
	{
		TArray<URHStoreItem*> PossibleStoreItems = GetStoreItemsForVendor(GetVoucherVendorId(), false, false);

		for (int32 i = 0; i < PossibleStoreItems.Num(); ++i)
		{
			if (PossibleStoreItems[i])
			{
				TArray<URHStoreItemPrice*> Prices = PossibleStoreItems[i]->GetPrices();

				for (int32 j = 0; j < Prices.Num(); ++j)
				{
					// We allow players to redeem the same items they have redeemed before, if they have the right vouchers.
					if (Prices[j]->CurrencyType.IsValid() && Prices[j]->CurrencyType.Get()->GetItemId() == VoucherItem->GetItemId())
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

URHCurrency* URHStoreItemHelper::GetCurrencyItem(const FRH_ItemId& ItemId) const
{
	URHCurrency* CurrencyItem = nullptr;
	if (UPInv_AssetManager* AssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized()))
	{
		CurrencyItem = AssetManager->GetSoftPrimaryAssetByItemId<URHCurrency>(ItemId).Get();
	}

	return CurrencyItem;
}

URH_PlayerInfo* URHStoreItemHelper::GetPlayerInfo(const FGuid& PlayerUuid) const
{
	if (GameInstance)
	{
		if (const auto* GameInstanceSubsystem = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
		{
			if (auto* PlayerInfoSubsystem = GameInstanceSubsystem->GetPlayerInfoSubsystem())
			{
				return PlayerInfoSubsystem->GetOrCreatePlayerInfo(PlayerUuid);
			}
		}
	}

	return nullptr;
}

URH_CatalogSubsystem* URHStoreItemHelper::GetRH_CatalogSubsystem() const
{
	if (GameInstance == nullptr)
	{
		return nullptr;
	}

	auto pGISubsystem = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
	if (pGISubsystem == nullptr)
	{
		return nullptr;
	}

	return pGISubsystem->GetCatalogSubsystem();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////                                                                                                                                  ////
////                                                                                                                                  ////
////                                                            RHStoreItem                                                           ////
////                                                                                                                                  ////
////                                                                                                                                  ////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void URHStoreItem::Initialize(URHStoreItemHelper* pHelper, const FRHAPI_Loot& LootItem)
{
	LootId = LootItem.GetLootId();
	VendorId = LootItem.GetVendorId();
	pItemHelper = pHelper;

	UPInv_AssetManager* pManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());

	if (pManager != nullptr)
	{
		if (const auto& ItemId = LootItem.GetItemIdOrNull())
		{
			if (*ItemId != 0)
			{
				InventoryItem = pManager->GetSoftPrimaryAssetByItemId<UPlatformInventoryItem>(*ItemId);
			}
			else if (LootItem.GetLootId() != 0)
			{
				InventoryItem = pManager->GetSoftPrimaryAssetByLootId<UPlatformInventoryItem>(LootItem.GetLootId());
			}
		}
		// Attempt to look up the item by loot id if we have no item id
		else if (LootItem.GetLootId() != 0)
		{
			InventoryItem = pManager->GetSoftPrimaryAssetByLootId<UPlatformInventoryItem>(LootItem.GetLootId());
		}
    }
}

URHStoreItem* URHStoreItem::GetDLCForVoucher()
{
	if (pItemHelper.IsValid())
	{
		if (URHStoreItemHelper* RHStoreItemHelper = Cast<URHStoreItemHelper>(pItemHelper.Get()))
		{
			return RHStoreItemHelper->GetDLCForVoucher(this);
		}
	}

	return nullptr;
}

bool URHStoreItem::GetSku(FString& SKU)
{
	if (InventoryItem.IsValid())
	{
		if (URHCurrency* CurrencyItem = Cast<URHCurrency>(InventoryItem.Get()))
		{
			return CurrencyItem->GetProductSkuForQuantity(GetLootQuantity(), SKU);
		}

		return InventoryItem->GetProductSku(SKU);
	}

	return false;
}

TSoftObjectPtr<UPlatformInventoryItem> URHStoreItem::GetInventoryItem() const
{
	return InventoryItem;
}

bool URHStoreItem::GetCatalogVendorItem(FRHAPI_Loot& LootItem) const
{
	if (pItemHelper.IsValid())
	{
		if (URH_CatalogSubsystem* CatalogSubsystem = pItemHelper.Get()->GetRH_CatalogSubsystem())
		{
			return CatalogSubsystem->GetVendorItemByLootId(GetLootId(), LootItem);
		}
	}

	return false;
}

URH_CatalogItem* URHStoreItem::GetCatalogItem() const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (pItemHelper.IsValid())
		{
			if (URH_CatalogSubsystem* CatalogSubsystem = pItemHelper.Get()->GetRH_CatalogSubsystem())
			{
				if (const auto& ItemId = LootItem.GetItemIdOrNull())
				{
					return CatalogSubsystem->GetCatalogItemByItemId(*ItemId);
				}
			}
		}
	}
	
	return nullptr;
}

FRH_ItemId URHStoreItem::GetItemId() const
{
	if (InventoryItem.IsValid())
	{
		return InventoryItem.Get()->GetItemId();
	}
	else if (URH_CatalogItem* pCatalogItem = GetCatalogItem())
	{
		return pCatalogItem->GetItemId();
	}

	return FRH_ItemId();
}

const FRH_LootId& URHStoreItem::GetLootId() const
{
	return LootId;
}

int32 URHStoreItem::GetVendorId() const
{
	return VendorId;
}

int32 URHStoreItem::GetSortOrder() const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& SortOrder = LootItem.GetSortOrderOrNull())
		{
			return *SortOrder;
		}
	}

	return 0;
}

bool URHStoreItem::GetPriceData(TArray<URHStoreItemPrice*>& Prices)
{
	FRHAPI_Loot LootItem;
	if (pItemHelper.IsValid())
	{
		if (GetCatalogVendorItem(LootItem))
		{
			const auto& CurrentPrice = LootItem.GetCurrentPricePointGuidOrNull();
			const auto& PreSalePrice = LootItem.GetPreSalePricePointGuidOrNull();

			return pItemHelper.Get()->GetPriceForGUID(FStorePriceKey(CurrentPrice ? FGuid(*CurrentPrice) : FGuid(), PreSalePrice ? FGuid(*PreSalePrice) : FGuid()), Prices);
		}
	}

	return false;
}

TArray<URHStoreItemPrice*> URHStoreItem::GetPrices()
{
	TArray<URHStoreItemPrice*> PriceItems;
	GetPriceData(PriceItems);

	return PriceItems;
}

URHStoreItemPrice* URHStoreItem::GetPrice(TSoftObjectPtr<UPlatformInventoryItem> nCurrencyType)
{
	TArray<URHStoreItemPrice*> PriceItems;
	GetPriceData(PriceItems);

	for (URHStoreItemPrice* Price : PriceItems)
	{
		if (Price->CurrencyType == nCurrencyType)
		{
			return Price;
		}
	}

	return nullptr;
}

int32 URHStoreItem::GetBestDiscount()
{
	int32 BestDiscountPercentage = 0;
	TArray<URHStoreItemPrice*> PricesArr;
	GetPriceData(PricesArr);

	for (int32 i = 0; i < PricesArr.Num(); ++i)
	{
		if (PricesArr[i])
		{
			int32 DiscountCandidate = PricesArr[i]->GetDiscountPercentage();

			if (DiscountCandidate > BestDiscountPercentage)
			{
				BestDiscountPercentage = DiscountCandidate;
			}
		}
	}

	if (PortalOffer)
	{
		int32 DiscountCandidate = PortalOffer->GetDiscountPercentage();

		if (DiscountCandidate > BestDiscountPercentage)
		{
			BestDiscountPercentage = DiscountCandidate;
		}
	}

	return BestDiscountPercentage;
}

bool URHStoreItem::IsOnSale()
{
	return GetBestDiscount() > 0;
}

void URHStoreItem::IsOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(false);
	}

	if (!InventoryItem.IsNull())
	{
		if (!InventoryItem.IsValid())
		{
			UAssetManager::GetStreamableManager().RequestAsyncLoad(InventoryItem.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &URHStoreItem::OnAsyncItemLoadedForOwnership, InventoryItem, PlayerInfo, Delegate));
		}
		else
		{
			InventoryItem.Get()->IsOwned(PlayerInfo, Delegate);
		}
	}
	else
	{
		Delegate.ExecuteIfBound(false);
	}
}

void URHStoreItem::OnAsyncItemLoadedForOwnership(TSoftObjectPtr<UPlatformInventoryItem> InvItem, const URH_PlayerInfo* PlayerInfo, FRH_GetInventoryStateBlock Delegate)
{
	if (InvItem.IsValid())
	{
		InvItem.Get()->IsOwned(PlayerInfo, Delegate);
	}
}

void URHStoreItem::CanOwnMore(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(false);
	}

	if (!InventoryItem.IsNull())
	{
		if (!InventoryItem.IsValid())
		{
			UAssetManager::GetStreamableManager().RequestAsyncLoad(InventoryItem.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &URHStoreItem::OnAsyncItemLoadedForOwnMore, InventoryItem, PlayerInfo, Delegate));
		}
		else
		{
			InventoryItem.Get()->CanOwnMore(PlayerInfo, Delegate);
		}
	}
	else
	{
		Delegate.ExecuteIfBound(false);
	}
}

void URHStoreItem::OnAsyncItemLoadedForOwnMore(TSoftObjectPtr<UPlatformInventoryItem> InvItem, const URH_PlayerInfo* PlayerInfo, FRH_GetInventoryStateBlock Delegate)
{
	if (InvItem.IsValid())
	{
		InvItem.Get()->CanOwnMore(PlayerInfo, Delegate);
	}
}

void URHStoreItem::IsRented(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(false);
	}

	if (!InventoryItem.IsNull())
	{
		if (!InventoryItem.IsValid())
		{
			UAssetManager::GetStreamableManager().RequestAsyncLoad(InventoryItem.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &URHStoreItem::OnAsyncItemLoadedForRental, InventoryItem, PlayerInfo, Delegate));
		}
		else
		{
			InventoryItem.Get()->IsRented(PlayerInfo, Delegate);
		}
	}
	else
	{
		Delegate.ExecuteIfBound(false);
	}
}

void URHStoreItem::OnAsyncItemLoadedForRental(TSoftObjectPtr<UPlatformInventoryItem> InvItem, const URH_PlayerInfo* PlayerInfo, FRH_GetInventoryStateBlock Delegate)
{
	if (InvItem.IsValid())
	{
		InvItem.Get()->IsRented(PlayerInfo, Delegate);
	}
}

void URHStoreItem::GetQuantityOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryCountBlock& Delegate)
{
	if (!InventoryItem.IsNull())
	{
		if (!InventoryItem.IsValid())
		{
			UAssetManager::GetStreamableManager().RequestAsyncLoad(InventoryItem.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &URHStoreItem::OnAsyncItemLoadedForQuantityOwned, InventoryItem, PlayerInfo, Delegate));
		}
		else
		{
			InventoryItem->GetQuantityOwned(PlayerInfo, Delegate);
		}
	}
	else
	{
		Delegate.ExecuteIfBound(0);
	}
}

void URHStoreItem::OnAsyncItemLoadedForQuantityOwned(TSoftObjectPtr<UPlatformInventoryItem> InvItem, const URH_PlayerInfo* PlayerInfo, FRH_GetInventoryCountBlock Delegate)
{
	if (InvItem.IsValid())
	{
		InvItem.Get()->GetQuantityOwned(PlayerInfo, Delegate);
	}
}

bool URHStoreItem::IsActive() const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& IsActive = LootItem.GetActiveOrNull())
		{
			return *IsActive;
		}
	}

	return false;
}

int32 URHStoreItem::GetLootQuantity() const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& Quantity = LootItem.GetQuantityOrNull())
		{
			return *Quantity;
		}
	}

	return 0;
}

int32 URHStoreItem::GetBundleId() const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& SubVendorId = LootItem.GetSubVendorIdOrNull())
		{
			return *SubVendorId;
		}
	}

	return 0;
}

bool URHStoreItem::BundleContainsItemId(const FRH_ItemId& nItemId, bool bSearchSubContainers) const
{
	// TODO: This doesn't evaluate the full rules of the bundle recipe and might make errors
	FRHAPI_Loot LootItem;

	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& SubVendorId = LootItem.GetSubVendorIdOrNull())
		{
			if (pItemHelper.IsValid())
			{
				TArray<URHStoreItem*> ContainedItems = pItemHelper.Get()->GetStoreItemsForVendor(*SubVendorId, false, bSearchSubContainers);

				for (URHStoreItem* ContainedItem : ContainedItems)
				{
					if (ContainedItem->GetItemId() == nItemId)
					{
						if (ContainedItem->GetInventoryOperation() == ERHAPI_InventoryOperation::Add ||
							(ContainedItem->GetInventoryOperation() == ERHAPI_InventoryOperation::Set && ContainedItem->GetLootQuantity() > 0))
						{
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

bool URHStoreItem::GetBundledContents(TArray<URHStoreItem*>& ContainedItems) const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& SubVendorId = LootItem.GetSubVendorIdOrNull())
		{
			if (pItemHelper.IsValid())
			{
				ContainedItems = pItemHelper.Get()->GetStoreItemsForVendor(*SubVendorId, false, false);
				return ContainedItems.Num() > 0;
			}
		}
	}

	return false;
}

UIconInfo* URHStoreItem::GetIconInfo() const
{
	if (InventoryItem.IsValid())
	{
		return InventoryItem->GetItemIconInfo();
	}

	return nullptr;
}

FText URHStoreItem::GetFormattedNameDisplay(int32 ExternalQuantity /*= 1*/) const
{
	// If we have a portal offer, always display the name from the portal
	if (HasPortalOffer())
	{
		return PortalOffer->Name;
	}

	// If we have a quantity concat the quantity before the item name
	int32 TotalQuantity = ExternalQuantity * GetLootQuantity();
	if (TotalQuantity > 1)
	{
		return FText::FromString(FString::Printf(L"%d %s", TotalQuantity, *GetName().ToString()));
	}

	return GetName();
}

FText URHStoreItem::GetName() const
{
	if (InventoryItem.IsValid())
	{
		return InventoryItem->GetItemName();
	}

	return FText();
}

FText URHStoreItem::GetFormattedDescDisplay() const
{
	// If we have a portal offer, always display the name from the portal
	if (HasPortalOffer())
	{
		return PortalOffer->Desc;
	}

	return GetDescription();
}

FText URHStoreItem::GetDescription() const
{
	if (InventoryItem.IsValid())
	{
		return InventoryItem->GetItemDescription();
	}

	return FText();
}

ERHAPI_InventoryOperation URHStoreItem::GetInventoryOperation() const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& InventoryOperation = LootItem.GetInventoryOperationOrNull())
		{
			return *InventoryOperation;
		}
	}

	return ERHAPI_InventoryOperation::Invalid;
}

int32 URHStoreItem::GetUIHint() const
{
	FRHAPI_Loot LootItem;
	if (GetCatalogVendorItem(LootItem))
	{
		if (const auto& UIHint = LootItem.GetUiHintOrNull())
		{
			return *UIHint;
		}
	}

	return 0;
}

bool URHStoreItem::ShouldDisplayToUser() const
{
	if (InventoryItem.IsValid())
	{
		const bool IsDisplayableRecipeType = GetInventoryOperation() == ERHAPI_InventoryOperation::Add
			|| GetInventoryOperation() == ERHAPI_InventoryOperation::Set;

		return InventoryItem->ShouldDisplayToUser() && IsDisplayableRecipeType;
	}

	return false;
}

void URHStoreItem::UIX_ShowPurchaseConfirmation(URHStoreItemPrice* pPrice)
{
	// ShowPurchaseConfimration takes a specific price, if a team wishes to use this differently they can ignore the pointer
	if (pItemHelper.IsValid())
	{
		pItemHelper.Get()->OnPurchaseItem.Broadcast(this, pPrice);
	}
}

URHStorePurchaseRequest* URHStoreItem::GetPurchaseRequest()
{
	// This fills out all the data that is required for a purchase other than price.
	// The rest of the values can be set as needed.
	if (URHStorePurchaseRequest* NewPurchaseRequest = NewObject<URHStorePurchaseRequest>())
	{
		NewPurchaseRequest->LocationId = 0;
		NewPurchaseRequest->LootTableItemId = 0;
		NewPurchaseRequest->Quantity = 1;
		NewPurchaseRequest->PriceInUI = 0;
		NewPurchaseRequest->CurrencyType = nullptr;
		NewPurchaseRequest->CouponId = 0;
		NewPurchaseRequest->pItemHelper = pItemHelper;

		FRHAPI_Loot LootItem;

		if (GetCatalogVendorItem(LootItem))
		{
			NewPurchaseRequest->LootTableItemId = LootItem.GetLootId();
			NewPurchaseRequest->VendorId = LootItem.GetVendorId();
		}

		return NewPurchaseRequest;
	}

	return nullptr;
}

void URHStoreItem::PurchaseFromPortal()
{
	const FName OSSName = URHStoreItemHelper::GetStoreOSS()->GetSubsystemName();
	if (OSSName == PS4_SUBSYSTEM || OSSName == PS5_SUBSYSTEM)
	{
		if (pItemHelper.IsValid())
		{
			// Pop up the portal offer confirmation screen for Playstation
			pItemHelper.Get()->OnPurchasePortalItem.Broadcast(this);
		}
	}
	else
	{
		// Other systems we just push you to the external portal immediately
		ConfirmGotoPortalOffer();
	}
}

void URHStoreItem::ConfirmGotoPortalOffer()
{
	if (GWorld && PortalOffer)
	{
		if (const IOnlineSubsystem* OSS = URHStoreItemHelper::GetStoreOSS())
		{
			if (const ULocalPlayer* LocalPlayer = GWorld->GetFirstLocalPlayerFromController())
			{
				const auto PlayerId = LocalPlayer->GetPreferredUniqueNetId();
				const IOnlinePurchasePtr Purchase = OSS->GetPurchaseInterface();
				if (Purchase.IsValid() && PlayerId.IsValid())
				{
					FPurchaseCheckoutRequest Request;
					Request.AddPurchaseOffer(TEXT(""), PortalOffer->SKU, 1);
					Purchase->Checkout(*PlayerId, Request, FOnPurchaseCheckoutComplete::CreateUObject(this, &URHStoreItem::PortalPurchaseComplete));
				}
			}
		}
	}
}

void URHStoreItem::PortalPurchaseComplete(const FOnlineError& Result, const TSharedRef<FPurchaseReceipt>& Receipt)
{
	if (GWorld)
	{
		if (const ULocalPlayer* LocalPlayer = GWorld->GetFirstLocalPlayerFromController())
		{
			if (const URH_LocalPlayerSubsystem* RHSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
			{
				if (URH_EntitlementSubsystem* ESS = RHSS->GetEntitlementSubsystem())
				{
					const URHGameInstance* GameInstance = Cast<URHGameInstance>(GWorld->GetGameInstance());
					URHStoreItemHelper* StoreItemHelper = GameInstance ? GameInstance->GetStoreItemHelper() : nullptr;
					OnPortalPurchaseSubmitted.Broadcast();
					if (StoreItemHelper)
					{
						ESS->SubmitEntitlementsForLoggedInOSS(FRH_ProcessEntitlementCompletedDelegate::CreateUObject(StoreItemHelper, &URHStoreItemHelper::EntitlementProcessingComplete));
					}
					else
					{
						ESS->SubmitEntitlementsForLoggedInOSS();
					}
				}
			}
		}
	}
}

void URHStoreItem::CanAfford(const URH_PlayerInfo* PlayerInfo, const URHStoreItemPrice* Price, int32 Quantity, const FRH_GetInventoryStateBlock& Delegate)
{
	if (pItemHelper.IsValid() && Price != nullptr && PlayerInfo != nullptr)
	{
		GetBestCouponForPrice(Price, PlayerInfo, FRH_GetStoreItemDelegate::CreateWeakLambda(this, [this, PlayerInfo, Price, Quantity, Delegate](URHStoreItem* Coupon)
			{
				int32 EndPrice = Price->Price;

				if (Coupon)
				{
					EndPrice = URH_CatalogBlueprintLibrary::GetCouponDiscountedPrice(Coupon->GetCatalogItem(), EndPrice);
				}

				pItemHelper.Get()->GetCurrencyOwned(Price->CurrencyType, PlayerInfo, FRH_GetInventoryCountDelegate::CreateWeakLambda(this, [this, Quantity, EndPrice, Delegate](int32 Count)
					{
						Delegate.ExecuteIfBound(Count >= (EndPrice * Quantity));
					}));
			}));
		
		return;
	}

	Delegate.ExecuteIfBound(false);
}

void URHStoreItem::GetBestCouponForPrice(const URHStoreItemPrice* Price, const URH_PlayerInfo* PlayerInfo, const FRH_GetStoreItemBlock& Delegate)
{
	GetCouponsForPrice(Price, PlayerInfo, FRH_GetStoreItemsDelegate::CreateWeakLambda(this, [this, Delegate](TArray<URHStoreItem*> Coupons)
		{
			URHStoreItem* BestCoupon = nullptr;

	for (int32 i = 0; i < Coupons.Num(); ++i)
	{
		if (BestCoupon == nullptr ||
			(Coupons[i]->GetCatalogItem() && Coupons[i]->GetCatalogItem()->GetCouponDiscountPercentage() > BestCoupon->GetCatalogItem()->GetCouponDiscountPercentage()))
		{
			BestCoupon = Coupons[i];
		}
	}

	Delegate.ExecuteIfBound(BestCoupon);
		}));
}

void URHStoreItem::GetCouponsForPrice(const URHStoreItemPrice* Price, const URH_PlayerInfo* PlayerInfo, const FRH_GetStoreItemsBlock& Delegate)
{
	if (pItemHelper.IsValid() && Price && Price->CurrencyType.IsValid())
	{
		TArray<FRHAPI_Loot> CouponsToCheck;
		TArray<FRHAPI_Loot> Coupons;
		
		pItemHelper.Get()->GetCouponsForItem(this, Coupons);

		if (URH_CatalogSubsystem* CatalogSubsystem = pItemHelper.Get()->GetRH_CatalogSubsystem())
		{
			for (int32 i = 0; i < Coupons.Num(); ++i)
			{
				if (URH_CatalogItem* CouponItem = CatalogSubsystem->GetCatalogItemByItemId(Coupons[i].GetItemId(0)))
				{
					// Check if the coupon can be applied to the the given price currency type
					if (CouponItem->GetCouponDiscountCurrencyItemId() == Price->CurrencyType.Get()->GetItemId())
					{
						// Double check the coupon is valid for this item
						if (URH_CatalogBlueprintLibrary::IsCouponApplicableForLootId(CouponItem, GetLootId()))
						{
							CouponsToCheck.Add(Coupons[i]);
						}
					}
				}
			}
		}

		if (CouponsToCheck.Num())
		{
			URH_GetOwnedCouponsAsyncTaskHelper* Helper = NewObject<URH_GetOwnedCouponsAsyncTaskHelper>();

			for (const auto& CouponToCheck : CouponsToCheck)
			{
				if (URHStoreItem* StoreItem = pItemHelper.Get()->GetStoreItem(CouponToCheck.GetItemId(0)))
				{
					Helper->ItemsToCheck.Add(StoreItem);
				}
			}
			
			if (!Helper->ItemsToCheck.Num())
			{
				Delegate.ExecuteIfBound(TArray<URHStoreItem*>());
				return;
			}

			Helper->PlayerInventory = PlayerInfo->GetPlayerInventory();
			Helper->RequestsCompleted = 0;
			Helper->OnCompleteDelegate = FRH_GetStoreItemsDelegate::CreateWeakLambda(this, [this, Delegate](TArray<URHStoreItem*> OwnedCoupons)
				{
					Delegate.ExecuteIfBound(OwnedCoupons);
					for (int32 i = GetOwnedCouponsTaskHelpers.Num() - 1; i >= 0; i--)
					{
						if (GetOwnedCouponsTaskHelpers[i]->IsCompleted())
						{
							GetOwnedCouponsTaskHelpers.RemoveAt(i);
						}
					}
				});

			GetOwnedCouponsTaskHelpers.Add(Helper);
			Helper->Start();
			return;
		}
	}

	Delegate.ExecuteIfBound(TArray<URHStoreItem*>());
}

void URH_GetOwnedCouponsAsyncTaskHelper::Start()
{
	if (PlayerInventory != nullptr)
	{
		for (const auto& ItemToCheck : ItemsToCheck)
		{
			PlayerInventory->IsInventoryItemOwned(ItemToCheck->GetItemId(), FRH_GetInventoryStateDelegate::CreateUObject(this, &URH_GetOwnedCouponsAsyncTaskHelper::OnOwnershipCheck, ItemToCheck));
		}
	}
}

void URH_GetOwnedCouponsAsyncTaskHelper::OnOwnershipCheck(bool bOwned, URHStoreItem* CouponItem)
{
	RequestsCompleted++;

	if (bOwned)
	{
		OwnedItems.Add(CouponItem);
	}

	if (IsCompleted())
	{
		OnCompleteDelegate.ExecuteIfBound(OwnedItems);
	}
}

void URH_GetAllAffordableItemsHelper::Start()
{
	for (const auto& ItemToCheck : ItemsToCheck)
	{
		ItemToCheck.Key->CanAfford(PlayerInfo, ItemToCheck.Value, 1, FRH_GetInventoryStateDelegate::CreateUObject(this, &URH_GetAllAffordableItemsHelper::OnIsAffordableResponse, ItemToCheck));
	}
}

void URH_GetAllAffordableItemsHelper::OnIsAffordableResponse(bool bSuccess, TPair<URHStoreItem*, URHStoreItemPrice*> ItemPricePair)
{
	RequestsCompleted++;

	if (bSuccess && CatalogSubsystem != nullptr && StoreItemHelper != nullptr)
	{
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(CurrencyVendorId, Vendor))
		{
			if (const auto& LootItems = Vendor.GetLootOrNull())
			{
				for (const auto& VendorItemPair : (*LootItems))
				{
					const FRHAPI_Loot& LootItem = VendorItemPair.Value;
					int32 ItemId = LootItem.GetItemId(0);

					if (ItemId != 0 && ItemId == ItemPricePair.Value->CurrencyType.Get()->GetItemId())
					{
						PurchaseItems.Add(ItemPricePair.Key);
						CurrencyItems.Add(StoreItemHelper->GetStoreItem(VendorItemPair.Value));
						break;
					}
				}
			}
		}
	}

	if (IsCompleted())
	{
		OnCompleteDelegate.ExecuteIfBound(PurchaseItems, CurrencyItems);

		if (StoreItemHelper != nullptr)
		{
			StoreItemHelper->GetOwnershipTrackers.Remove(this);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////                                                                                                                                  ////
////                                                                                                                                  ////
////                                                    URHStorePurchaseRequest                                                       ////
////                                                                                                                                  ////
////                                                                                                                                  ////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void URHStorePurchaseRequest::SubmitPurchaseRequest(const FRH_CatalogCallDynamicDelegate& Delegate)
{
	if (pItemHelper.IsValid())
	{
		pItemHelper->UIX_CompletePurchaseItem(this, Delegate);
	}
	else
	{
		Delegate.ExecuteIfBound(false);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////                                                                                                                                  ////
////                                                                                                                                  ////
////                                                       URHStoreItemPrice                                                          ////
////                                                                                                                                  ////
////                                                                                                                                  ////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int32 URHStoreItemPrice::GetPriceWithCoupon(URHStoreItem* Coupon) const
{
	if (Coupon && Coupon->GetCatalogItem())
	{
		return URH_CatalogBlueprintLibrary::GetCouponDiscountedPrice(Coupon->GetCatalogItem(), Price);
	}

	return Price;
}

void URHStoreItemPrice::CanAfford(const URH_PlayerInfo* PlayerInfo, int32 Quantity, URHStoreItem* Coupon, const FRH_GetInventoryStateBlock& Delegate)
{
	if (pItemHelper.IsValid() && PlayerInfo != nullptr)
	{
		pItemHelper.Get()->GetCurrencyOwned(CurrencyType, PlayerInfo, FRH_GetInventoryCountDelegate::CreateWeakLambda(this, [this, Quantity, Coupon, Delegate](int32 Count)
			{
				Delegate.ExecuteIfBound(Count >= (GetPriceWithCoupon(Coupon) * Quantity));
			}));
		return;
	}

	Delegate.ExecuteIfBound(false);
}

int32 URHStoreItemPrice::GetDiscountPercentage() const
{
	if (PreSalePrice != Price && PreSalePrice != 0 && PreSalePrice > Price)
	{
		const float PercentageDiscount = (float(Price) / float(PreSalePrice)) * 100;

		// Fuzzy Math of -0.1 added here to account for floating point rounding errors causing this to output 1% higher at some times
		return 100 - FMath::CeilToInt(PercentageDiscount - 0.1);
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////                                                                                                                                  ////
////                                                                                                                                  ////
////                                                        URHPortalOffer                                                            ////
////                                                                                                                                  ////
////                                                                                                                                  ////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int32 URHPortalOffer::GetDiscountPercentage() const
{
	if (PreSaleCost != Cost && PreSaleCost != 0 && PreSaleCost > Cost)
	{
		const float PercentageDiscount = (float(Cost) / float(PreSaleCost)) * 100;

		// Fuzzy Math of -0.1 added here to account for floating point rounding errors causing this to output 1% higher at some times
		return 100 - FMath::CeilToInt(PercentageDiscount - 0.1);
	}

	return 0;
}