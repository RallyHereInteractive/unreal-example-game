#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "GameFramework/RHGameInstance.h"
#include "Interfaces/OnlineAchievementsInterface.h"
#include "RH_CatalogSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_MatchmakingBrowser.h"
#include "RH_EntitlementSubsystem.h"
#include "Managers/RHStoreItemHelper.h"
#include "Managers/RHUISessionManager.h"

void URHUISessionManager::Initialize(URHGameInstance* InGameInstance)
{
	GameInstance = InGameInstance;

	if (GameInstance)
	{
		GameInstance->OnLocalPlayerLoginChanged.AddUObject(this, &URHUISessionManager::OnLoginPlayerChanged);
	}
}

void URHUISessionManager::Uninitialize()
{
	if (GameInstance)
	{
		GameInstance->OnLocalPlayerLoginChanged.RemoveAll(this);
	}
}

void URHUISessionManager::OnLoginPlayerChanged(ULocalPlayer* LocalPlayer)
{
	URHUISessionData* SessionData = nullptr;
	URH_LocalPlayerSubsystem* RHSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>();
	URH_PlayerInfo* PlayerInfo = nullptr;

	if (RHSS != nullptr)
	{
		PlayerInfo = RHSS->GetLocalPlayerInfo();
		SessionData = SessionDataPerPlayer.FindRef(PlayerInfo);

		if (SessionData == nullptr)
		{
			SessionData = NewObject<URHUISessionData>();
			SessionDataPerPlayer.Add(PlayerInfo, SessionData);
		}

		if (PlayerInfo != nullptr)
		{		
			if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
			{
				PlayerInventory->OnInventoryCacheUpdated.AddWeakLambda(this, [this](const TArray<int32>& Items, URH_PlayerInfo* PlayerInfo)
					{
						if (PlayerInfo != nullptr)
						{
							if (URHUISessionData* SessionData = SessionDataPerPlayer.FindRef(PlayerInfo))
							{
								SessionData->bHasFullPlayerInventory = true;
								SessionData->OnPlayerInventoryReady.Broadcast();
							}

							if (RewardRedemptionVendorId > 0)
							{
								if (URHStoreItemHelper* StoreItemHelper = GameInstance->GetStoreItemHelper())
								{
									TArray<int32> VendorIds;
									VendorIds.Push(RewardRedemptionVendorId);

									if (VendorIds.Num())
									{
										StoreItemHelper->RequestVendorData(VendorIds, FRH_CatalogCallDelegate::CreateLambda([this, WeakPlayerInfo = MakeWeakObjectPtr(PlayerInfo)](bool bSuccess)
											{
												if (WeakPlayerInfo.IsValid())
												{
													ProcessRedemptionRewards(WeakPlayerInfo.Get());
												}
											}));
									}
								}
							}
						}

						// unbind the delegate - this must happen last, as it will invalidate the capture data
						auto Inventory = PlayerInfo->GetPlayerInventory();
						if (Inventory != nullptr)
						{
							Inventory->OnInventoryCacheUpdated.RemoveAll(this);
						}
					});
			}
		}

		if (URH_EntitlementSubsystem* ESS = RHSS->GetEntitlementSubsystem())
		{
			ESS->SubmitEntitlementsForPlatform(ERHAPI_Platform::Amazon);
			ESS->SubmitEntitlementsForPlatform(ERHAPI_Platform::Twitch);
		}

		if (PlayerInfo != nullptr)
		{
			TWeakObjectPtr<URH_LocalPlayerSubsystem> RHSSWeak = RHSS;
			TWeakObjectPtr<URH_PlayerInfo> PlayerInfoWeak = PlayerInfo;
			PlayerInfo->GetLinkedPlatformInfo(FTimespan(), false, FRH_PlayerInfoGetPlatformsDelegate::CreateWeakLambda(this, [this, RHSSWeak, PlayerInfoWeak](bool bSuccess, const TArray<URH_PlayerPlatformInfo*>& Platforms)
				{
					if (!RHSSWeak.IsValid() || !PlayerInfoWeak.IsValid())
					{
						return;
					}
				
					auto RHSS = RHSSWeak.Get();
					auto PlayerInfo = PlayerInfoWeak.Get();
				
					const auto LoggedInPlatform = RHSS->GetLoggedInPlatform();
					if (LoggedInPlatform.IsSet())
					{
						for (const auto& Platform : Platforms)
						{
							if (Platform != nullptr && Platform->GetPlatform() == LoggedInPlatform.GetValue())
							{
								const auto IdentityInterface = Online::GetIdentityInterface();
								TSharedPtr<const FUniqueNetId> PlayerUniqueNetId = IdentityInterface->CreateUniquePlayerId(Platform->GetPlatformUserId());

								if (PlayerUniqueNetId.IsValid())
								{
									if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
									{
										IOnlineAchievementsPtr Achievements = OSS->GetAchievementsInterface();
										if (Achievements.IsValid())
										{
											FOnQueryAchievementsCompleteDelegate QueryAchievementsDel;
											FOnQueryAchievementsCompleteDelegate QueryAchievementDescriptionsDel;

											QueryAchievementsDel.BindUObject(this, &URHUISessionManager::OnQueryAchievementsComplete, PlayerInfo);
											QueryAchievementDescriptionsDel.BindUObject(this, &URHUISessionManager::OnQueryAchievementDescriptionsComplete, PlayerInfo);

											Achievements->QueryAchievements(*PlayerUniqueNetId.Get(), QueryAchievementsDel);
											Achievements->QueryAchievementDescriptions(*PlayerUniqueNetId.Get(), QueryAchievementDescriptionsDel);
										}
									}
								}
							}
						}
					}
				}));
		}
	}

	if (SessionData != nullptr)
	{
		SessionData->Clear();
		SessionData->LoggedInTime = FDateTime::UtcNow(); // #RHTODO - PLAT-4591 - We need to actual server timestamp not players local time
	}

	if (GameInstance != nullptr)
	{
		// Fetch region settings on login
		if (URH_GameInstanceSubsystem* pGISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
		{
			if (auto* pMMCache = pGISS->GetMatchmakingCache())
			{
				pMMCache->SearchRegions();
			}
		}
	}
}

void URHUISessionManager::OnQueryAchievementsComplete(const FUniqueNetId& UserId, const bool bSuccess, URH_PlayerInfo* PlayerInfo)
{
	if (URHUISessionData* SessionData = SessionDataPerPlayer.FindRef(PlayerInfo))
	{
		SessionData->bHasPlatformAchievements = bSuccess;
		SessionData->OnPlatformActivitiesReceived.Broadcast();

		if (HasPlatformAchievements(PlayerInfo))
		{
			SessionData->OnPlatformActivitiesReady.Broadcast();
		}
	}
}

void URHUISessionManager::OnQueryAchievementDescriptionsComplete(const FUniqueNetId& UserId, const bool bSuccess, URH_PlayerInfo* PlayerInfo)
{
	if (URHUISessionData* SessionData = SessionDataPerPlayer.FindRef(PlayerInfo))
	{
		SessionData->bHasPlatformAchievementDescriptions = bSuccess;
		SessionData->OnPlatformActivityDescriptionsReceived.Broadcast();

		if (HasPlatformAchievements(PlayerInfo))
		{
			SessionData->OnPlatformActivitiesReady.Broadcast();
		}
	}
}

void URHUISessionManager::ProcessRedemptionRewards(URH_PlayerInfo* PlayerInfo)
{
	// Traverse the redemption vendor to check for payouts from it
	URH_CatalogSubsystem* CatalogSubsystem = nullptr;

	if (GameInstance != nullptr)
	{
		if (auto pGISubsystem = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
		{
			CatalogSubsystem = pGISubsystem->GetCatalogSubsystem();
		}
	}

	if (CatalogSubsystem != nullptr)
	{		
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(RewardRedemptionVendorId, Vendor))
		{
			if (const auto& LootItems = Vendor.GetLootOrNull())
			{
				for (const auto& VendorItemPair : (*LootItems))
				{
					const FRHAPI_Loot& LootItem = VendorItemPair.Value;
					
					if (LootItem.GetActive(false) && LootItem.GetIsClaimableByClient(false) && LootItem.GetSubVendorId(0) != 0)
					{
						GetClaimableQuantity(LootItem, CatalogSubsystem, PlayerInfo, 0, 0, FRH_GetInventoryCountDelegate::CreateWeakLambda(this, [this, LootItem, PlayerInfo](int32 QuantityToRedeem)
							{
								if (QuantityToRedeem > 0)
								{
									TArray<URH_PlayerOrderEntry*> PlayerOrderEntries;

									URH_PlayerOrderEntry* NewPlayerOrderEntry = NewObject<URH_PlayerOrderEntry>();
									NewPlayerOrderEntry->FillType = ERHAPI_PlayerOrderEntryType::PurchaseLoot;
									NewPlayerOrderEntry->LootId = LootItem.GetLootId();
									NewPlayerOrderEntry->Quantity = QuantityToRedeem;
									NewPlayerOrderEntry->ExternalTransactionId = "Reward Redemption During Login";
									PlayerOrderEntries.Push(NewPlayerOrderEntry);

									PlayerInfo->GetPlayerInventory()->CreateNewPlayerOrder(ERHAPI_Source::Client, false, PlayerOrderEntries, FRH_OrderResultDelegate::CreateLambda([this](const URH_PlayerInfo* PlayerInfo, TArray<URH_PlayerOrderEntry*> OrderEntries, const FRHAPI_PlayerOrder& PlayerOrder)
										{
											if (URHOrderManager* OrderManager = GameInstance->GetOrderManager())
											{
												TArray<FRHAPI_PlayerOrder> OrderResults;
												OrderResults.Push(PlayerOrder);
												OrderManager->OnPlayerOrder(OrderResults, PlayerInfo);
											}
										}));
								}
							}));
					}
				}
			}
		}
	}
}

void URHUISessionManager::GetClaimableQuantity(FRHAPI_Loot LootItem, URH_CatalogSubsystem* CatalogSubsystem, URH_PlayerInfo* PlayerInfo, int32 LootIndex, int32 Quantity, const FRH_GetInventoryCountBlock& Delegate)
{
	int32 QuantityToRedeem = Quantity;

	if (CatalogSubsystem != nullptr)
	{
		FRHAPI_Vendor Vendor;
		if (CatalogSubsystem->GetVendorById(LootItem.GetSubVendorId(0), Vendor))
		{
			TArray<FRHAPI_Loot> RedemptionVendorRecipeItems;

			if (const auto& LootItems = Vendor.GetLootOrNull())
			{
				for (const auto& VendorItemPair : (*LootItems))
				{
					// Build out and sort the items from the given vendor so we can evaluate the recipe
					const FRHAPI_Loot& RedemptionItem = VendorItemPair.Value;

					if (RedemptionItem.GetActive(false))
					{
						RedemptionVendorRecipeItems.Add(RedemptionItem);
					}
				}
			}

			RedemptionVendorRecipeItems.Sort([](const FRHAPI_Loot& A, const FRHAPI_Loot& B) { return (A.GetSortOrder(0) < B.GetSortOrder(0)); });

			for (int32 i = LootIndex; i < RedemptionVendorRecipeItems.Num(); ++i)
			{
				const FRHAPI_Loot& RecipeItem = RedemptionVendorRecipeItems[i];

				int32 InternalRedeemCount = 0;

				if (RecipeItem.GetSubVendorId(0) != 0)
				{
					// Create a sub thread for this recipe item, then when it completes continue where we left off.
					GetClaimableQuantity(RecipeItem, CatalogSubsystem, PlayerInfo, 0, 0, FRH_GetInventoryCountDelegate::CreateWeakLambda(this, [this, LootItem, PlayerInfo, CatalogSubsystem, QuantityToRedeem, i, Delegate](int32 InternalRedeemCount)
						{
							// If an internal bundle has more redeem counts than we have externally redeem this multiple times
							int32 NewQty = QuantityToRedeem;
							if (InternalRedeemCount > QuantityToRedeem)
							{
								NewQty = InternalRedeemCount;
							}

							GetClaimableQuantity(LootItem, CatalogSubsystem, PlayerInfo, i + 1, NewQty, Delegate);
						}));
					return;
				}
				else if (RecipeItem.GetItemId(0) != 0)
				{
					if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
					{
						PlayerInventory->GetInventoryCount(RecipeItem.GetItemId(0), FRH_GetInventoryCountDelegate::CreateWeakLambda(this, [this, RecipeItem, LootItem, PlayerInfo, CatalogSubsystem, QuantityToRedeem, i, Delegate](int32 InventoryCount)
							{
								int32 NewQty = QuantityToRedeem;

								switch (RecipeItem.GetInventoryOperation(ERHAPI_InventoryOperation::Invalid))
								{
									case ERHAPI_InventoryOperation::CheckLessThan:
										if (InventoryCount >= RecipeItem.GetQuantity())
										{
											// Early out, redeem whatever amount we have calculated up to this point
											Delegate.ExecuteIfBound(QuantityToRedeem);
											return;
										}
										break;
									case ERHAPI_InventoryOperation::CheckGreaterThanOrEqualAndSubtract:
										// If we are going to start consuming something of yours, count how many times we can consume it
										if (InventoryCount > 0)
										{
											int32 InternalRedeemCount = InventoryCount / RecipeItem.GetQuantity();

											if (InternalRedeemCount > QuantityToRedeem)
											{
												NewQty = InternalRedeemCount;
											}
										}
										// FALLTHOUGH
									case ERHAPI_InventoryOperation::CheckGreaterThanOrEqual:
										if (InventoryCount < RecipeItem.GetQuantity())
										{
											// Early out, redeem whatever amount we have calculated up to this point
											Delegate.ExecuteIfBound(QuantityToRedeem);
											return;
										}
										break;
									case ERHAPI_InventoryOperation::Subtract:
									case ERHAPI_InventoryOperation::Set:
									case ERHAPI_InventoryOperation::Add:
										// If we make it to this level of item type then we have are actually getting something, claim this LTI
										Delegate.ExecuteIfBound(QuantityToRedeem > 0 ? QuantityToRedeem : 1);
										return;
										break;
									case ERHAPI_InventoryOperation::Invalid:
									default:
										// Do nothing in these cases with evaluating the item
										break;
								}
								GetClaimableQuantity(LootItem, CatalogSubsystem, PlayerInfo, i + 1, NewQty, Delegate);
							}));
						return;
					}
				}
			}
		}
	}
	Delegate.ExecuteIfBound(QuantityToRedeem);
}