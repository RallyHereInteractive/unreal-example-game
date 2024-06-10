// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "Managers/RHLoadoutDataFactory.h"
#include "Inventory/RHLoadoutTypes.h"
#include "GameFramework/RHGameInstance.h"
#include "GameFramework/RHGameUserSettings.h"
#include "Managers/RHStoreItemHelper.h"
#include "Managers/RHStoreItemHelper.h"
#include "RHUIBlueprintFunctionLibrary.h"

void URH_PlayerLoadoutItem::SetLoadoutItemId(FString InLoadoutItemId)
{
	LoadoutItemId = InLoadoutItemId;
	LastModifiedTimestamp = FDateTime::UtcNow();
}

void URH_PlayerLoadoutItem::SetItemId(const FRH_ItemId& InItemId)
{
	ItemId = InItemId;
	LastModifiedTimestamp = FDateTime::UtcNow();
}

void URH_PlayerLoadoutItem::SetCount(int32 InCount)
{
	Count = InCount;
	LastModifiedTimestamp = FDateTime::UtcNow();
}

void URH_PlayerLoadoutItem::SetSortOrder(int32 InSortOrder)
{
	SortOrder = InSortOrder;
	LastModifiedTimestamp = FDateTime::UtcNow();
}

void URH_PlayerLoadoutItem::SetItemValueId(int32 InItemValueId)
{
	ItemValueId = InItemValueId;
	LastModifiedTimestamp = FDateTime::UtcNow();
}

void URH_PlayerLoadoutItem::SetCreationDate(FDateTime InCreationDate)
{
	CreationDate = InCreationDate;
	LastModifiedTimestamp = FDateTime::UtcNow();
}

void URH_PlayerLoadoutItem::SetLastModifiedTime(FDateTime InLastModifiedTime)
{
	LastModifiedTimestamp = InLastModifiedTime;
}

bool URH_PlayerLoadout::GetEquippedItemInSlot(FOnGetEquippedLoadoutItem Event, ERHLoadoutSlotTypes LoadoutSlot)
{
	if (!Event.IsBound())
	{
		return false;
	}

	FRH_ItemId FoundItemId;
	for (const auto& Item : Items)
	{
		if (Item != nullptr && Item->GetItemValueId() == (int32)LoadoutSlot)
		{
			FoundItemId = Item->GetItemId();
		}
	}

	if (FoundItemId.IsValid())
	{
		if (UPInv_AssetManager* AssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized()))
		{
			AsyncLoadItem(Event, AssetManager->GetSoftPrimaryAssetByItemId<UPlatformInventoryItem>(FoundItemId));
			return true;
		}
	}
	
	if (auto FoundDefaultItem = DefaultItems.Find(LoadoutSlot))
	{
		FRHEquippedLoadoutItemWrapper Result;
		Result.Item = *FoundDefaultItem;
		Event.Execute(Result);
		return true;
	}

	return false;
}

bool URH_PlayerLoadout::IsItemEquippedInSlot(UPlatformInventoryItem* InventoryItem, ERHLoadoutSlotTypes LoadoutSlot)
{
	if (InventoryItem != nullptr)
	{
		bool bItemInSlot = false;

		for (const auto& Item : Items)
		{
			if (Item != nullptr && Item->GetItemValueId() == (int32)LoadoutSlot)
			{
				bItemInSlot = true;

				if (Item->GetItemId() == InventoryItem->GetItemId())
				{
					return true;
				}
			}
		}

		if (!bItemInSlot)
		{
			if (auto FoundDefaultItem = DefaultItems.Find(LoadoutSlot))
			{
				return (*FoundDefaultItem)->GetItemId() == InventoryItem->GetItemId();
			}
		}
	}

	return false;
}

bool URH_PlayerLoadout::LocalEquipItemToSlot(UPlatformInventoryItem* Item, ERHLoadoutSlotTypes LoadoutSlot)
{
	if (Item != nullptr && CanEquipItem(Item, LoadoutSlot))
	{
		URH_PlayerLoadoutItem* NewLoadoutItem = nullptr;

		// Search for Existing Item to replace
		bool bItemInSlot = false;
		for (URH_PlayerLoadoutItem* EquippedItem : Items)
		{
			if (EquippedItem != nullptr && EquippedItem->GetItemValueId() == (int32)LoadoutSlot)
			{
				bItemInSlot = true;
				NewLoadoutItem = EquippedItem;
			}
		}
		// Create new Item if none equipped
		if (!bItemInSlot)
		{
			NewLoadoutItem = NewObject<URH_PlayerLoadoutItem>();
			Items.Push(NewLoadoutItem);
		}

		NewLoadoutItem->LoadoutItemId = "create";
		NewLoadoutItem->ItemId = Item->GetItemId();
		NewLoadoutItem->Count = 1;
		NewLoadoutItem->SortOrder = 0; // This value isn't currently used/important in this scope
		NewLoadoutItem->ItemValueId = (int32)LoadoutSlot;
		NewLoadoutItem->CreationDate = FDateTime::UtcNow();
		NewLoadoutItem->LastModifiedTimestamp = FDateTime::UtcNow();
	
		return true;
	}

	return false;
}

bool URH_PlayerLoadout::CanEquipItem(UPlatformInventoryItem* Item, ERHLoadoutSlotTypes LoadoutSlot)
{
	if (Item != nullptr)
	{
		ERHLoadoutTypes LoadoutType = (ERHLoadoutTypes)TypeValueId;

		switch (LoadoutType)
		{
		case ERHLoadoutTypes::PLAYER_ACCOUNT:
		{
			switch (LoadoutSlot)
			{
			case ERHLoadoutSlotTypes::AVATAR_SLOT:
				return Item->GetCollectionContainer().HasTag(FGameplayTag::RequestGameplayTag(CollectionNames::AvatarCollectionName));
			case ERHLoadoutSlotTypes::BANNER_SLOT:
				return Item->GetCollectionContainer().HasTag(FGameplayTag::RequestGameplayTag(CollectionNames::BannerCollectionName));
			case ERHLoadoutSlotTypes::BORDER_SLOT:
				return Item->GetCollectionContainer().HasTag(FGameplayTag::RequestGameplayTag(CollectionNames::BorderCollectionName));
			case ERHLoadoutSlotTypes::TITLE_SLOT:
				return Item->GetCollectionContainer().HasTag(FGameplayTag::RequestGameplayTag(CollectionNames::TitleCollectionName));
			}
			break;
		}
		}
	}

	return false;
}

void URH_PlayerLoadout::AsyncLoadItem(FOnGetEquippedLoadoutItem Event, TSoftObjectPtr<UPlatformInventoryItem> SoftItem)
{
	if (SoftItem.IsValid())
	{
		FRHEquippedLoadoutItemWrapper Result;
		Result.Item = SoftItem.Get();
		Event.Execute(Result);
	}
	else
	{
		Streamable.RequestAsyncLoad(SoftItem.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &URH_PlayerLoadout::AsyncLoadItemComplete, Event, SoftItem));
	}
}

void URH_PlayerLoadout::AsyncLoadItemComplete(FOnGetEquippedLoadoutItem Event, TSoftObjectPtr<UPlatformInventoryItem> SoftItem)
{
	if (Event.IsBound() && SoftItem.IsValid())
	{
		FRHEquippedLoadoutItemWrapper Result;
		Result.Item = SoftItem.Get();
		Event.Execute(Result);
	}
}

void URHLoadoutDataFactory::Initialize()
{
	if (AccountDefaultLoadoutsDataTableClassName.ToString().Len() > 0)
	{
		AccountDefaultsDT = LoadObject<UDataTable>(nullptr, *AccountDefaultLoadoutsDataTableClassName.ToString(), nullptr, LOAD_None, nullptr);
	}
	else
	{
		// If nothing is configured load the datatable for the plugin
		AccountDefaultsDT = LoadObject<UDataTable>(nullptr, TEXT("/RallyHereStart/Content/AccountCosmetics/DT_AccountDefaultLoadouts.DT_AccountDefaultLoadouts"), nullptr, LOAD_None, nullptr);
	}

	// As the Default items are used in a lot of places, we will load them into memory to be able to quickly reference
	if (AccountDefaultsDT != nullptr)
	{
		for (FName RowName : AccountDefaultsDT->GetRowNames())
		{
			if (FAccountLoadoutDefaults* LoadoutData = AccountDefaultsDT->FindRow<FAccountLoadoutDefaults>(RowName, "Lookup Default Cosmetics"))
			{
				if (LoadoutData->Default.IsValid())
				{
					DefaultLoadoutItems.Add(LoadoutData->SlotType, LoadoutData->Default.Get());
				}
				else
				{
					DefaultLoadoutItems.Add(LoadoutData->SlotType, LoadoutData->Default.LoadSynchronous());
				}
			}
		}
	}
}

void URHLoadoutDataFactory::PostLogin()
{
}

void URHLoadoutDataFactory::PostLogoff()
{
}

URHLoadoutDataFactory* URHLoadoutDataFactory::GetRHLoadoutDataFactory(const UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	return URHLoadoutDataFactory::Get(World);
}

URHLoadoutDataFactory* URHLoadoutDataFactory::Get(const UWorld* MyWorld)
{
	if (MyWorld != nullptr)
	{
		URHGameInstance* pGameInstance = MyWorld->GetGameInstance<URHGameInstance>();
		return pGameInstance != nullptr ? pGameInstance->GetLoadoutDataFactory() : nullptr;
	}

	return nullptr;
}

void URHLoadoutDataFactory::GetPlayerLoadoutSettings(URH_PlayerInfo* PlayerInfo, const FTimespan& StaleThreshold /* = FTimespan()*/, bool bForceRefresh /*= false*/, FRH_GetPlayerInfoLoadoutsBlock Delegate /*= FRH_GetPlayerInfoLoadoutsBlock()*/)
{
	if (PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(nullptr, TArray<URH_PlayerLoadout*>());
		return;
	}

	if (auto FoundLastRequested = LastRequestSettings.Find(PlayerInfo))
	{
		FDateTime Now = FDateTime::UtcNow();
		if (FoundLastRequested->GetTicks() != 0 && !bForceRefresh)
		{
			if ((*FoundLastRequested) + StaleThreshold < Now || StaleThreshold.IsZero())
			{
				if (auto FoundLoadouts = PlayerLoadouts.Find(PlayerInfo))
				{
					Delegate.ExecuteIfBound(PlayerInfo, GetLoadoutsFromWrapper(*FoundLoadouts));
					return;
				}
			}
		}
	}

	PlayerInfo->GetPlayerSettings(TEXT("player_loadout"), StaleThreshold, bForceRefresh, FRH_PlayerInfoGetPlayerSettingsDelegate::CreateUObject(this, &URHLoadoutDataFactory::OnGetPlayerLoadoutSettingsResponse, PlayerInfo, Delegate));
}

void URHLoadoutDataFactory::OnGetPlayerLoadoutSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& Response, URH_PlayerInfo* PlayerInfo, FRH_GetPlayerInfoLoadoutsBlock Delegate)
{
	if (bSuccess)
	{
		FRHPlayerLoadoutsWrapper UnpackagedLoadouts;
		if (UnpackageLoadoutSettings(Response.Content, UnpackagedLoadouts))
		{
			PlayerLoadouts.Add(PlayerInfo, UnpackagedLoadouts);
			LastRequestSettings.Add(PlayerInfo, FDateTime::UtcNow());
			
			Delegate.ExecuteIfBound(PlayerInfo, GetLoadoutsFromWrapper(UnpackagedLoadouts));
			OnPlayerLoadoutsUpdated.Broadcast(PlayerInfo, GetLoadoutsFromWrapper(UnpackagedLoadouts));
		}
	}
	else
	{
		TArray<URH_PlayerLoadout*> EmptyResult;
		Delegate.ExecuteIfBound(PlayerInfo, EmptyResult);
	}
}

void URHLoadoutDataFactory::GetPlayerLoadoutSettingByLoadoutType(URH_PlayerInfo* PlayerInfo, ERHLoadoutTypes LoadoutType, bool bCreateIfNeeded, const FTimespan& StaleThreshold /* = FTimespan()*/, bool bForceRefresh /*= false*/, FRH_GetPlayerInfoLoadoutBlock Delegate /*= FRH_GetPlayerInfoLoadoutBlock()*/)
{
	if (PlayerInfo == nullptr)
	{
		Delegate.ExecuteIfBound(nullptr, nullptr);
		return;
	}

	if (auto FoundLastRequested = LastRequestSettings.Find(PlayerInfo))
	{
		FDateTime Now = FDateTime::UtcNow();
		if (FoundLastRequested->GetTicks() != 0 && !bForceRefresh)
		{
			if ((*FoundLastRequested) + StaleThreshold < Now || StaleThreshold.IsZero())
			{
				if (auto FoundLoadouts = PlayerLoadouts.Find(PlayerInfo))
				{
					for (URH_PlayerLoadout* Loadout : GetLoadoutsFromWrapper(*FoundLoadouts))
					{
						if (Loadout != nullptr && Loadout->GetType() == int32(LoadoutType))
						{
							Delegate.ExecuteIfBound(PlayerInfo, Loadout);
							return;
						}
					}
				}
			}
		}
	}

	PlayerInfo->GetPlayerSettings(TEXT("player_loadouts"), StaleThreshold, bForceRefresh, FRH_PlayerInfoGetPlayerSettingsDelegate::CreateUObject(this, &URHLoadoutDataFactory::OnGetPlayerLoadoutSettingByLoadoutTypeResponse, PlayerInfo, LoadoutType, bCreateIfNeeded, Delegate));
}

void URHLoadoutDataFactory::OnGetPlayerLoadoutSettingByLoadoutTypeResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& Response, URH_PlayerInfo* PlayerInfo, ERHLoadoutTypes LoadoutType, bool bCreateIfNeeded, FRH_GetPlayerInfoLoadoutBlock Delegate)
{
	if (bSuccess)
	{
		FRHPlayerLoadoutsWrapper UnpackagedLoadoutsWrapper;
		if (UnpackageLoadoutSettings(Response.Content, UnpackagedLoadoutsWrapper))
		{
			PlayerLoadouts.Add(PlayerInfo, UnpackagedLoadoutsWrapper);
			LastRequestSettings.Add(PlayerInfo, FDateTime::UtcNow());
			OnPlayerLoadoutsUpdated.Broadcast(PlayerInfo, GetLoadoutsFromWrapper(UnpackagedLoadoutsWrapper));

			for (URH_PlayerLoadout* Loadout : GetLoadoutsFromWrapper(UnpackagedLoadoutsWrapper))
			{
				if (Loadout != nullptr && Loadout->GetType() == int32(LoadoutType))
				{
					Delegate.ExecuteIfBound(PlayerInfo, Loadout);
					return;
				}
			}
		}

		if (bCreateIfNeeded)
		{
			URH_PlayerLoadout* NewLoadout = nullptr;
			if (CreateLocalLoadout(NewLoadout, LoadoutType, 1))
			{
				TArray<URH_PlayerLoadout*> LoadoutsToUpdate;
				LoadoutsToUpdate.Add(NewLoadout);
				SetPlayerLoadoutSettings(PlayerInfo, LoadoutsToUpdate, FRH_SetPlayerInfoLoadoutsDelegate::CreateLambda([this, LoadoutType, Delegate](URH_PlayerInfo* RespPlayerInfo, bool bSuccess)
					{
						if (bSuccess && RespPlayerInfo != nullptr)
						{
							if (auto FoundLoadouts = PlayerLoadouts.Find(RespPlayerInfo))
							{
								for (URH_PlayerLoadout* Loadout : GetLoadoutsFromWrapper(*FoundLoadouts))
								{
									if (Loadout != nullptr && Loadout->GetType() == int32(LoadoutType))
									{
										Delegate.ExecuteIfBound(RespPlayerInfo, Loadout);
										return;
									}
								}
							}
						}
						
						URH_PlayerLoadout* DefaultLoadout = nullptr;
						if (CreateLocalLoadout(DefaultLoadout, LoadoutType, 1))
						{
							Delegate.ExecuteIfBound(RespPlayerInfo, DefaultLoadout);
							return;
						}

						Delegate.ExecuteIfBound(RespPlayerInfo, nullptr);
					}));
				return;
			}
		}
	}

	URH_PlayerLoadout* DefaultLoadout = nullptr;
	if (CreateLocalLoadout(DefaultLoadout, LoadoutType, 1))
	{
		Delegate.ExecuteIfBound(PlayerInfo, DefaultLoadout);
		return;
	}

	Delegate.ExecuteIfBound(PlayerInfo, nullptr);
}

void URHLoadoutDataFactory::SetPlayerLoadoutSettings(URH_PlayerInfo* PlayerInfo, const TArray<URH_PlayerLoadout*>& Loadouts, FRH_SetPlayerInfoLoadoutsBlock Delegate /*= FRH_SetPlayerInfoLoadoutsBlock()*/)
{
	if (Loadouts.Num() > 0 && PlayerInfo != nullptr)
	{
		TMap<FString, FRHAPI_SetSinglePlayerSettingRequest> SettingsContent;
		if (PackagePlayerLoadouts(Loadouts, SettingsContent))
		{
			static FString SettingTypeId = TEXT("player_loadout");
			TWeakObjectPtr<URH_PlayerInfo> PlayerInfoWeak = PlayerInfo;
			for (const auto& Setting : SettingsContent)
			{
				PlayerInfo->SetPlayerSetting(TEXT("player_loadout"), Setting.Key, Setting.Value, FRH_PlayerInfoSetPlayerSettingDelegate::CreateUObject(this, &URHLoadoutDataFactory::OnSetPlayerLoadoutSettingsResponse, PlayerInfoWeak, Delegate));
			}
		}
	}
}

void URHLoadoutDataFactory::OnSetPlayerLoadoutSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& ResponseData, const FRH_ErrorInfo& ErrorInfo, TWeakObjectPtr<URH_PlayerInfo> PlayerInfoWeak, FRH_SetPlayerInfoLoadoutsBlock Delegate)
{
	auto PlayerInfo = PlayerInfoWeak.Get();
	if (bSuccess && PlayerInfo != nullptr)
	{
		// Update local cache with Loadout data that was just pushed
		// The response Blob contains the updated loadout schemas
		PlayerLoadouts.FindOrAdd(PlayerInfo);

		if (auto FoundLoadoutsForPlayer = PlayerLoadouts.Find(PlayerInfo))
		{
			FRHPlayerLoadoutsWrapper UnpackagedResponseLoadouts;
			if (UnpackageLoadoutSettings(ResponseData.Content, UnpackagedResponseLoadouts))
			{
				for (const auto& pair : UnpackagedResponseLoadouts.LoadoutsById)
				{
					FoundLoadoutsForPlayer->LoadoutsById.Add(pair);
				}

				if (UnpackagedResponseLoadouts.LoadoutsById.Num() > 0)
				{
					LastRequestSettings.Add(PlayerInfo, FDateTime::UtcNow());
					OnPlayerLoadoutsUpdated.Broadcast(PlayerInfo, GetLoadoutsFromWrapper(UnpackagedResponseLoadouts));
					Delegate.ExecuteIfBound(PlayerInfo, true);
					return;
				}
			}
		}
	}

	Delegate.ExecuteIfBound(PlayerInfo, false);
}

UPlatformInventoryItem* URHLoadoutDataFactory::GetDefaultItemForLoadoutSlotType(const ERHLoadoutSlotTypes SlotType)
{
	if (auto FoundDefaultItem = DefaultLoadoutItems.Find(SlotType))
	{
		return *(FoundDefaultItem);
	}

	return nullptr;
}

bool URHLoadoutDataFactory::CreateLocalLoadout(URH_PlayerLoadout*& Loadout, ERHLoadoutTypes LoadoutType, int32 SortOrder)
{
	URH_PlayerLoadout* NewLoadout = NewObject<URH_PlayerLoadout>();

	if (NewLoadout != nullptr)
	{
		// Name the loadout based off passed LoadoutType
		switch (LoadoutType)
		{
		case ERHLoadoutTypes::INVALID_LOADOUT:
			return false;
			break;
		case ERHLoadoutTypes::PLAYER_ACCOUNT:
			NewLoadout->Name = "Player Account";
			break;
		default:
			break;
		}

		NewLoadout->LoadoutId = "create";
		NewLoadout->V = 1;
		NewLoadout->TypeValueId = (int32)LoadoutType;
		NewLoadout->SortOrder = SortOrder; // This value isn't currently used/important in this scope
		NewLoadout->CreationDate = FDateTime::UtcNow();
		NewLoadout->LastModifiedTimestamp = FDateTime::UtcNow();
		NewLoadout->Items.Empty();
		NewLoadout->DefaultItems.Empty();

		Loadout = NewLoadout;
		return true;
	}

    return false;
}

bool URHLoadoutDataFactory::PackagePlayerLoadouts(const TArray<URH_PlayerLoadout*>& InLoadouts, TMap<FString, FRHAPI_SetSinglePlayerSettingRequest>& OutSettingsContent)
{
	OutSettingsContent.Empty();

	if (InLoadouts.Num() > 0)
	{
		for (URH_PlayerLoadout* InLoadout : InLoadouts)
		{
			if (InLoadout != nullptr)
			{
				FRHAPI_SetSinglePlayerSettingRequest PackagedData;
				if (PackagePlayerLoadout(InLoadout, PackagedData))
				{
					OutSettingsContent.Add(InLoadout->GetLoadoutId(), PackagedData);
				}
				else
				{
					UE_LOG(RallyHereStart, Warning, TEXT("URHLoadoutDataFactory::PackagePlayerLoadouts failed to Package Loadout with Id=%s"), *(InLoadout->GetLoadoutId()));
				}
			}
		}
	}

	return OutSettingsContent.Num() > 0;
}

bool URHLoadoutDataFactory::PackagePlayerLoadout(URH_PlayerLoadout* InLoadout, FRHAPI_SetSinglePlayerSettingRequest& OutSettingData)
{
	if (InLoadout == nullptr)
	{
		return false;
	}

	TSharedPtr<FJsonObject> NewObject = MakeShared<FJsonObject>();
	TSharedPtr<FJsonValueObject> NewValueObject = MakeShared<FJsonValueObject>(NewObject);

	if (FJsonObject* LoadoutObject = NewObject.Get())
	{
		LoadoutObject->SetNumberField("type_value_id", InLoadout->GetType());
		LoadoutObject->SetNumberField("sort_order", InLoadout->GetSortOrder());
		LoadoutObject->SetStringField("name", InLoadout->GetName());
		
		FDateTime DateTime = InLoadout->GetCreationDate();
		LoadoutObject->SetStringField("creation_datetime", DateTime.ToIso8601());
		
		DateTime = FDateTime::UtcNow();
		LoadoutObject->SetStringField("last_modified_timestamp", DateTime.ToIso8601());

		TSharedPtr<FJsonObject> LoadoutItemsObject = MakeShared<FJsonObject>();

		for (URH_PlayerLoadoutItem* LoadoutItem : InLoadout->GetItems())
		{
			if (LoadoutItem != nullptr)
			{
				TSharedPtr<FJsonObject> NewLoadoutItemObject = MakeShared<FJsonObject>();
				TSharedPtr<FJsonValueObject> NewLoadoutItemValueObject = MakeShared<FJsonValueObject>(NewLoadoutItemObject);
				if (FJsonObject* LoadoutItemObject = NewLoadoutItemObject.Get())
				{
					LoadoutItemObject->SetNumberField("item_id", LoadoutItem->GetItemId());
					LoadoutItemObject->SetNumberField("count", LoadoutItem->GetCount());
					LoadoutItemObject->SetNumberField("sort_order", LoadoutItem->GetSortOrder());
					LoadoutItemObject->SetNumberField("loadout_item_value_id", LoadoutItem->GetItemValueId());
					
					DateTime = LoadoutItem->GetCreationDate();
					LoadoutItemObject->SetStringField("creation_datetime", DateTime.ToIso8601());

					DateTime = LoadoutItem->GetLastModifiedTime();
					LoadoutItemObject->SetStringField("last_modified_timestamp", DateTime.ToIso8601());

					LoadoutItemsObject->Values.Add(LoadoutItem->GetLoadoutItemId(), NewLoadoutItemValueObject);
				}
			}
		}

		LoadoutObject->SetObjectField("items", LoadoutItemsObject);
	}

	OutSettingData.V = InLoadout->GetV();
	OutSettingData.SetValue(FRHAPI_JsonValue(NewValueObject));
	return true;
}

bool URHLoadoutDataFactory::UnpackageLoadoutSettings(const TMap<FString, FRHAPI_SettingData>& InSettingsContent, FRHPlayerLoadoutsWrapper& OutLoadouts)
{
	OutLoadouts.LoadoutsById.Empty();

	if (InSettingsContent.Num() > 0)
	{
		for (const auto& tuple : InSettingsContent)
		{
			URH_PlayerLoadout* UnpackagedLoadout = nullptr;
			if (UnpackageLoadoutSetting(tuple.Key, tuple.Value, UnpackagedLoadout))
			{
				OutLoadouts.LoadoutsById.Add(UnpackagedLoadout->GetLoadoutId(), UnpackagedLoadout);
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHLoadoutDataFactory::UnpackageLoadoutSettings failed to Unpackage Loadout Setting with Id=%s"), *(tuple.Key));
			}
		}
	}

	return OutLoadouts.LoadoutsById.Num() > 0;
}

bool URHLoadoutDataFactory::UnpackageLoadoutSetting(const FString& InLoadoutId, const FRHAPI_SettingData& InSettingData, URH_PlayerLoadout*& OutLoadout)
{
	const auto Value = InSettingData.GetValueOrNull();
	if (Value == nullptr)
	{
		return false;
	}
	
	const TSharedPtr<FJsonObject>* LoadoutData = nullptr;
	if (Value->GetValue().Get()->TryGetObject(LoadoutData))
	{
		if (const FJsonObject* LoadoutObject = LoadoutData->Get())
		{
			OutLoadout = NewObject<URH_PlayerLoadout>();

			OutLoadout->LoadoutId = InLoadoutId;
			OutLoadout->V = InSettingData.V;
			LoadoutObject->TryGetNumberField("type_value_id", OutLoadout->TypeValueId);
			LoadoutObject->TryGetNumberField("sort_order", OutLoadout->SortOrder);
			LoadoutObject->TryGetStringField("name", OutLoadout->Name);
			FString DateTime;
			LoadoutObject->TryGetStringField("creation_datetime", DateTime);
			RallyHereAPI::ParseDateTime(DateTime, OutLoadout->CreationDate);
			LoadoutObject->TryGetStringField("last_modified_timestamp", DateTime);
			RallyHereAPI::ParseDateTime(DateTime, OutLoadout->LastModifiedTimestamp);

			const TSharedPtr<FJsonObject>* LoadoutItemData = nullptr;
			if (LoadoutObject->TryGetObjectField("items", LoadoutItemData))
			{
				if (const FJsonObject* LoadoutItemObject = LoadoutItemData->Get())
				{
					for (const auto& itemPair : LoadoutItemObject->Values)
					{
						const TSharedPtr<FJsonObject>* ItemData = nullptr;
						if (itemPair.Value.Get()->TryGetObject(ItemData))
						{
							if (const FJsonObject* ItemObject = ItemData->Get())
							{
								auto NewLoadoutItem = NewObject<URH_PlayerLoadoutItem>();

								NewLoadoutItem->LoadoutItemId = itemPair.Key;

								int32 LegacyItemId;
								ItemObject->TryGetNumberField("item_id", LegacyItemId);
								NewLoadoutItem->ItemId = LegacyItemId;
								ItemObject->TryGetNumberField("count", NewLoadoutItem->Count);
								ItemObject->TryGetNumberField("sort_order", NewLoadoutItem->SortOrder);
								ItemObject->TryGetNumberField("loadout_item_value_id", NewLoadoutItem->ItemValueId);
								ItemObject->TryGetStringField("creation_datetime", DateTime);
								RallyHereAPI::ParseDateTime(DateTime, NewLoadoutItem->CreationDate);
								ItemObject->TryGetStringField("last_modified_timestamp", DateTime);
								RallyHereAPI::ParseDateTime(DateTime, NewLoadoutItem->LastModifiedTimestamp);

								OutLoadout->Items.Push(NewLoadoutItem);
							}
						}
					}

					for (const auto& pair : DefaultLoadoutItems)
					{
						OutLoadout->DefaultItems.Add(pair);
					}
				}
			}
		}
	}

	return OutLoadout != nullptr && OutLoadout->LoadoutId == InLoadoutId;
}

TArray<URH_PlayerLoadout*> URHLoadoutDataFactory::GetLoadoutsFromWrapper(const FRHPlayerLoadoutsWrapper& InWrapper)
{
	TArray<URH_PlayerLoadout*> Result;

	for (const auto& pair : InWrapper.LoadoutsById)
	{
		Result.Add(pair.Value);
	}

	return Result;
}
