// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "PlatformInventoryItem/PInv_Delegates.h"
#include "PlatformInventoryItem/PInv_AssetManagerSettings.h"

#if WITH_EDITOR
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PlatformInventoryItem/PlatformInventoryItem.h"
#endif

#define LOCTEXT_NAMESPACE "PInv_AssetManager"

DEFINE_LOG_CATEGORY(LogPInv_AssetManager);

const FPrimaryAssetType UPInv_AssetManager::PlatformInventoryItemType = FName(TEXT("PlatformInventoryItem"));
const FPrimaryAssetType UPInv_AssetManager::PlatformStoreAssetType = FName(TEXT("PlatformStoreAsset"));
const FPrimaryAssetType UPInv_AssetManager::ItemCollectionType = FName(TEXT("ItemCollection"));

UPInv_AssetManager::UPInv_AssetManager()
	: Super()
{
	bHasCompletedInitialAssetScan = false;
    bIsQuickCook = false;
    CookProfile = TEXT("Default");
}

void UPInv_AssetManager::PostInitProperties()
{
    Super::PostInitProperties();

#if WITH_EDITOR
    FName CmdLineCookProfile = NAME_None;

    const TCHAR* pCommandStr = FCommandLine::Get();
    const UPInv_AssetManagerSettings& Settings = GetPInvSettings();

    if (FParse::Value(pCommandStr, TEXT("CookProfile="), CmdLineCookProfile) && CmdLineCookProfile != NAME_None)
    {
        CookProfile = CmdLineCookProfile;
    }
    else
    {
        CookProfile = Settings.CookProfile;
    }

    if (IsRunningCommandlet())
    {
        bIsQuickCook = Settings.bQuickCook || FParse::Param(pCommandStr, TEXT("QuickCook"));
        if (bIsQuickCook)
        {
            FString FullAdditionalQuickCookAssets;
            FParse::Value(pCommandStr, TEXT("-QuickCookAssets="), FullAdditionalQuickCookAssets);
            FullAdditionalQuickCookAssets.ParseIntoArray(AdditionalQuickCookPrimaryAssets
                , TEXT("+"));
        }
    }
#endif

}

void UPInv_AssetManager::StartInitialLoading()
{
	Super::StartInitialLoading();

	PInv_Delegates::OnReadyForBundleData.Broadcast();
}

void UPInv_AssetManager::PostInitialAssetScan()
{
	Super::PostInitialAssetScan();

	InternalPostInitialAssetScan();

	bHasCompletedInitialAssetScan = true;
	PInv_Delegates::OnPostInitialAssetScan.Broadcast();
}

void UPInv_AssetManager::InitializeDisabledItems(URH_ConfigSubsystem* ConfigSubsystem)
{
	if (ConfigSubsystem == nullptr)
	{
		return;
	}

	FString DisabledItemIdsSettingStr;
	FString TempDisabledItemIdsSettingStr;
	TArray<FString> DisabledItemIdsSetting;
	TArray<FString> TempDisabledItemIdsSetting;

	ConfigSubsystem->GetKV(TEXT("game.disabled_item_ids"), DisabledItemIdsSettingStr);
	ConfigSubsystem->GetKV(TEXT("game.temp_disabled_item_ids"), TempDisabledItemIdsSettingStr);

	DisabledItemIdsSettingStr.ParseIntoArray(DisabledItemIdsSetting, TEXT(","));
	TempDisabledItemIdsSettingStr.ParseIntoArray(TempDisabledItemIdsSetting, TEXT(","));

	DisabledItemIds.Empty();
	TempDisabledItemIds.Empty();

	for (const auto& value : DisabledItemIdsSetting)
	{
		DisabledItemIds.Add(FCString::Atoi(*value));
	}

	for (const auto& value : TempDisabledItemIdsSetting)
	{
		TempDisabledItemIds.Add(FCString::Atoi(*value));
	}
}

bool UPInv_AssetManager::GetPrimaryAssetDataByItemId(const FRH_ItemId& ItemId, FAssetData& OutAssetData) const
{
	if (const FPrimaryAssetId* FoundAssetId = ItemIdToPrimaryAssetIdMap.Find(ItemId))
	{
		return GetPrimaryAssetData(*FoundAssetId, OutAssetData);
	}

	UE_LOG(LogPInv_AssetManager, Warning, TEXT("Failed to find Primary Asset associated with Item Id %s"), *ItemId.ToString());
	return false;
}


bool UPInv_AssetManager::GetPrimaryAssetDataByLootId(const FRH_LootId& LootId, FAssetData& OutAssetData) const
{
	if (const FPrimaryAssetId* FoundAssetId = LootIdToPrimaryAssetIdMap.Find(LootId))
	{
		return GetPrimaryAssetData(*FoundAssetId, OutAssetData);
	}

	UE_LOG(LogPInv_AssetManager, Warning, TEXT("Failed to find Primary Asset associated with Loot Id %s"), *LootId.ToString());
	return false;
}

bool UPInv_AssetManager::ShouldScanPrimaryAssetTypeForItemId(const FPrimaryAssetTypeInfo& TypeInfo) const
{
	return TypeInfo.PrimaryAssetType == PlatformInventoryItemType.GetName() || TypeInfo.PrimaryAssetType == PlatformStoreAssetType.GetName();
}

bool UPInv_AssetManager::ShouldScanPrimaryAssetTypeForLootId(const FPrimaryAssetTypeInfo& TypeInfo) const
{
	return TypeInfo.PrimaryAssetType == PlatformStoreAssetType.GetName();
}

bool UPInv_AssetManager::ShouldScanPrimaryAssetTypeForCollectionContainer(const FPrimaryAssetTypeInfo& TypeInfo) const
{
    return TypeInfo.PrimaryAssetType == PlatformInventoryItemType.GetName() || TypeInfo.PrimaryAssetType == PlatformStoreAssetType.GetName();
}

bool UPInv_AssetManager::GetPrimaryAssetIdListByCollectionQuery(const FGameplayTagQuery& InCollectionQuery, TArray<FPrimaryAssetId>& OutPrimaryAssetIds) const
{
    bool bAnyFound = false;

    if (!InCollectionQuery.IsEmpty())
    {
        for (const TPair<FPrimaryAssetId, FGameplayTagContainer>& Pair : ItemCollectionMap)
        {
            if (Pair.Value.MatchesQuery(InCollectionQuery))
            {
                OutPrimaryAssetIds.Add(Pair.Key);
                bAnyFound = true;
            }
        }
    }

    return bAnyFound;
}

#if WITH_EDITOR
void UPInv_AssetManager::ReinitializeFromConfig()
{
    FName CmdLineCookProfile = NAME_None;

    const TCHAR* pCommandStr = FCommandLine::Get();
    const UPInv_AssetManagerSettings& Settings = GetPInvSettings();
    if (FParse::Value(pCommandStr, TEXT("CookProfile="), CmdLineCookProfile) && CmdLineCookProfile != NAME_None)
    {
        CookProfile = CmdLineCookProfile;
    }
    else
    {
        CookProfile = Settings.CookProfile;
    }

    Super::ReinitializeFromConfig();
}

void UPInv_AssetManager::SetPrimaryAssetTypeRules(FPrimaryAssetType PrimaryAssetType, const FPrimaryAssetRules& Rules)
{
    if (bIsQuickCook && !GetPInvSettings().IgnoreQuickCookRulesByType(PrimaryAssetType))
    {
        FPrimaryAssetRules ModifiedRules = Rules;
        ModifiedRules.CookRule = EPrimaryAssetCookRule::Unknown;
        Super::SetPrimaryAssetTypeRules(PrimaryAssetType, ModifiedRules);
        return;
    }

    Super::SetPrimaryAssetTypeRules(PrimaryAssetType, Rules);
}

void UPInv_AssetManager::SetPrimaryAssetRules(FPrimaryAssetId PrimaryAssetId, const FPrimaryAssetRules& Rules)
{
    if (bIsQuickCook && !GetPInvSettings().IgnoreQuickCookRulesByAsset(PrimaryAssetId))
    {
        FPrimaryAssetRules ModifiedRules = Rules;
        ModifiedRules.CookRule = EPrimaryAssetCookRule::Unknown;
        Super::SetPrimaryAssetRules(PrimaryAssetId, ModifiedRules);
        return;
    }

    Super::SetPrimaryAssetRules(PrimaryAssetId, Rules);
}

void UPInv_AssetManager::ApplyPrimaryAssetLabels()
{
	Super::ApplyPrimaryAssetLabels();

    InternalApplyPrimaryAssetLabels(PlatformStoreAssetType);
}

void UPInv_AssetManager::RefreshPrimaryAssetDirectory(bool bForceRefresh /*= false*/)
{
	if (bForceRefresh || !bIsPrimaryAssetDirectoryCurrent)
	{
		// Clear the existing ItemId maps as these will be rebuild.
		PrimaryAssetIdToItemIdMap.Empty();
		PrimaryAssetIdToLootIdMap.Empty();
		ItemIdToPrimaryAssetIdMap.Empty();
        LootIdToPrimaryAssetIdMap.Empty();
	}

	Super::RefreshPrimaryAssetDirectory(bForceRefresh);
}

void UPInv_AssetManager::InternalApplyPrimaryAssetLabels(const FPrimaryAssetType& InType)
{
    // Load all of them off disk. Turn off string asset reference tracking to avoid them getting cooked
    FSoftObjectPathSerializationScope SerializationScope(NAME_None, NAME_None, ESoftObjectPathCollectType::NeverCollect, ESoftObjectPathSerializeType::AlwaysSerialize);

    const UPInv_AssetManagerSettings& Settings = GetPInvSettings();

    TSharedPtr<FStreamableHandle> Handle = nullptr;

    if (!bIsQuickCook || Settings.IgnoreQuickCookRulesByType(InType))
    {
        Handle = LoadPrimaryAssetsWithType(InType);
    }
    else if (Settings.QuickCookAssetIngoreSetTypes.Contains(InType))
    {
        TArray<FPrimaryAssetId> AssetsToLoad;

        TArray<FPrimaryAssetId> AssetIds;
        if (GetPrimaryAssetIdList(InType, AssetIds))
        {
            for (const FPrimaryAssetId& PrimaryAssetId : AssetIds)
            {
                if (Settings.QuickCookAssetIgnoreSet.Contains(PrimaryAssetId))
                {
                    AssetsToLoad.Add(PrimaryAssetId);
                }
            }
        }

        if (AssetsToLoad.Num() > 0)
        {
            Handle = LoadPrimaryAssets(AssetsToLoad);
        }
    }

    if (Handle.IsValid())
    {
        Handle->WaitUntilComplete();
    }
}

void UPInv_AssetManager::ModifyCook(TConstArrayView<const ITargetPlatform*> TargetPlatforms, TArray<FName>& PackagesToCook, TArray<FName>& PackagesToNeverCook)
{
    // Add additional quick cook packages.
    if (bIsQuickCook)
    {
        auto TryToAddQuickCookAsset = [&](const TArray<FString>& QuickCookAssets)
        {
            for (const FString& PrimaryAssetStr : QuickCookAssets)
            {
                FName PackageName = NAME_None;
                FAssetData AssetData;
                if (GetPrimaryAssetData(FPrimaryAssetId::FromString(PrimaryAssetStr), AssetData))
                {
                    PackageName = AssetData.PackageName;
                }
                else
                {
                    TSoftObjectPtr<UPlatformInventoryItem> ItemPtr = UPlatformInventoryItem::GetItemByFriendlyName<UPlatformInventoryItem>(PrimaryAssetStr);
                    if (!ItemPtr.IsNull())
                    {
                        PackageName = *(ItemPtr.GetLongPackageName());
                    }
                }

                if (PackageName != NAME_None)
                {
                    if (!PackagesToNeverCook.Contains(PackageName) && VerifyCanCookPackage(nullptr, PackageName, false))
                    {
                        PackagesToCook.AddUnique(PackageName);
                    }
                }
            }
        };

        TryToAddQuickCookAsset(GetPInvSettings().PrimaryAssetsToIncludeQuickCook);
        TryToAddQuickCookAsset(AdditionalQuickCookPrimaryAssets);
    }

    Super::ModifyCook(TargetPlatforms, PackagesToCook, PackagesToNeverCook);
}

void UPInv_AssetManager::RemovePrimaryAssetId(const FPrimaryAssetId& PrimaryAssetId)
{
	RemoveFromItemIdMap(PrimaryAssetId);
	RemoveFromLootIdMap(PrimaryAssetId);
    RemoveFromCollectionContainerMap(PrimaryAssetId);

	Super::RemovePrimaryAssetId(PrimaryAssetId);
}
#endif

bool UPInv_AssetManager::TryUpdateCachedAssetData(const FPrimaryAssetId& PrimaryAssetId, const FAssetData& NewAssetData, bool bAllowDuplicates)
{
	FPrimaryAssetTypeInfo AssetTypeInfo;
	if (GetPrimaryAssetTypeInfo(PrimaryAssetId.PrimaryAssetType, AssetTypeInfo))
	{
		if(ShouldScanPrimaryAssetTypeForItemId(AssetTypeInfo))
		{
			AddOrUpdateItemIdMap(PrimaryAssetId, NewAssetData, bAllowDuplicates);
		}

		if (ShouldScanPrimaryAssetTypeForLootId(AssetTypeInfo))
		{
			AddOrUpdateLootIdMap(PrimaryAssetId, NewAssetData, bAllowDuplicates);
		}

        if(ShouldScanPrimaryAssetTypeForCollectionContainer(AssetTypeInfo))
        {
            AddOrUpdateCollectionContainerMap(PrimaryAssetId, NewAssetData, bAllowDuplicates);
        }
	}

	return Super::TryUpdateCachedAssetData(PrimaryAssetId, NewAssetData, bAllowDuplicates);
}

void UPInv_AssetManager::InternalPostInitialAssetScan()
{
}

void UPInv_AssetManager::AddOrUpdateItemIdMap(const FPrimaryAssetId& PrimaryAssetId, const FAssetData& AssetData, bool bAllowDuplicates)
{
	if (!PrimaryAssetId.IsValid() || !AssetData.IsValid())
	{
		return;
	}

	static const FName nmItemId(TEXT("ItemId"));
	FString ItemIdAsString;
	if (AssetData.GetTagValue(nmItemId, ItemIdAsString))
	{
		FRH_ItemId NewItemId;

		if (ItemIdAsString.Contains("Id="))
		{
			int32 GuidIndex = ItemIdAsString.Find("Id=");
			int32 LegacyItemIdIndex = ItemIdAsString.Find("LegacyId=");
			int32 EndIndex = ItemIdAsString.Find(")");
			FString GuidAsString = ItemIdAsString.RightChop(GuidIndex + 3).Left(32);
			FString LegacyItemIdAsString = ItemIdAsString.RightChop(LegacyItemIdIndex + 9).Left(EndIndex - 1);
			FGuid ItemId;
			FGuid::Parse(GuidAsString, ItemId);
			int32 LegacyItemId = FCString::Atoi(*LegacyItemIdAsString);
			// Id is full struct data
			NewItemId = FRH_ItemId(ItemId, LegacyItemId);
		}
		else
		{
			// Id is just a number, parse it out.
			NewItemId = FCString::Atoi(*ItemIdAsString);
		}

#if WITH_EDITOR
		if (!NewItemId.IsValid())
		{
			RemoveFromItemIdMap(PrimaryAssetId);
		}
		else
#else
		if(NewItemId.IsValid())
#endif
		{
			FRH_ItemId OldItemId;
			if (FRH_ItemId* FoundItemId = PrimaryAssetIdToItemIdMap.Find(PrimaryAssetId))
			{
				OldItemId = *FoundItemId;
			}

			// If the ID has changed, remove it from the map.
			if (OldItemId.IsValid() && OldItemId != NewItemId)
			{
				FPrimaryAssetId* FoundAssetId = ItemIdToPrimaryAssetIdMap.Find(OldItemId);
				if (FoundAssetId != nullptr && *FoundAssetId == PrimaryAssetId)
				{
					ItemIdToPrimaryAssetIdMap.Remove(OldItemId);
				}
			}

			PrimaryAssetIdToItemIdMap.FindOrAdd(PrimaryAssetId) = NewItemId;

			if (FPrimaryAssetId* FoundAssetId = ItemIdToPrimaryAssetIdMap.Find(NewItemId))
			{
				if (*FoundAssetId != PrimaryAssetId)
				{
					UE_LOG(LogPInv_AssetManager, Error, TEXT("Found multiple primary assets with the same Item Id (%s). %s and %s"), *NewItemId.ToString(), *(PrimaryAssetId.ToString()), *(FoundAssetId->ToString()));
#if WITH_EDITOR
					if (GIsEditor)
					{
						const int MaxNotificationsPerFrame = 5;
						if (NumberOfSpawnedNotifications++ < MaxNotificationsPerFrame || !IsBulkScanning())
						{
							FNotificationInfo Info(FText::Format(LOCTEXT("DuplicateItemId", "Duplicate Item ID {0} used by {1} and {2}, you must delete or chanage one!"),
								FText::FromString(NewItemId.ToString()), FText::FromString(FoundAssetId->ToString()), FText::FromString(PrimaryAssetId.ToString())));
							Info.ExpireDuration = 30.0f;

							TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
							if (Notification.IsValid())
							{
								Notification->SetCompletionState(SNotificationItem::CS_Fail);
							}
						}
					}
#endif
					*FoundAssetId = PrimaryAssetId;
				}
			}
			else
			{
				ItemIdToPrimaryAssetIdMap.Add(NewItemId, PrimaryAssetId);
			}
		}
	}
}

void UPInv_AssetManager::AddOrUpdateLootIdMap(const FPrimaryAssetId& PrimaryAssetId, const FAssetData& AssetData, bool bAllowDuplicates)
{
	// TODO: Consider updating the map to be a AssetID to a LootId/ItemId pairing so we don't have 2 maps
	static const FName nmLootId(TEXT("LootId"));
	FString LootIdAsString;
	if (AssetData.GetTagValue(nmLootId, LootIdAsString))
	{
		FRH_LootId NewLootId;

		if (LootIdAsString.Contains("Id="))
		{
			int32 GuidIndex = LootIdAsString.Find("Id=");
			int32 LegacyItemIdIndex = LootIdAsString.Find("LegacyId=");
			int32 EndIndex = LootIdAsString.Find(")");
			FString GuidAsString = LootIdAsString.RightChop(GuidIndex + 3).Left(32);
			FString LegacyIdAsString = LootIdAsString.RightChop(LegacyItemIdIndex + 9).Left(EndIndex - 1);
			FGuid Id;
			FGuid::Parse(GuidAsString, Id);
			int32 LegacyId = FCString::Atoi(*LegacyIdAsString);
			// Id is full struct data
			NewLootId = FRH_LootId(Id, LegacyId);
		}
		else
		{
			// Id is just a number, parse it out.
			NewLootId = FCString::Atoi(*LootIdAsString);
		}

#if WITH_EDITOR
		if (!NewLootId.IsValid())
		{
			RemoveFromLootIdMap(PrimaryAssetId);
		}
		else
#else
		if (NewLootId.IsValid())
#endif
		{
			FRH_LootId OldLootId = FRH_LootId();
			if (FRH_LootId* FoundLootId = PrimaryAssetIdToLootIdMap.Find(PrimaryAssetId))
			{
				OldLootId = *FoundLootId;
			}

			// If the ID has changed, remove it from the map.
			if (OldLootId.IsValid() && OldLootId != NewLootId)
			{
				FPrimaryAssetId* FoundAssetId = LootIdToPrimaryAssetIdMap.Find(OldLootId);
				if (FoundAssetId != nullptr && *FoundAssetId == PrimaryAssetId)
				{
					LootIdToPrimaryAssetIdMap.Remove(OldLootId);
				}
			}

			PrimaryAssetIdToLootIdMap.FindOrAdd(PrimaryAssetId) = NewLootId;

			if (FPrimaryAssetId* FoundAssetId = LootIdToPrimaryAssetIdMap.Find(NewLootId))
			{
				if (*FoundAssetId != PrimaryAssetId)
				{
					UE_LOG(LogPInv_AssetManager, Error, TEXT("Found multiple primary assets with the same Loot Id (%s). %s and %s"), *NewLootId.ToString(), *(PrimaryAssetId.ToString()), *(FoundAssetId->ToString()));
#if WITH_EDITOR
					if (GIsEditor)
					{
						const int MaxNotificationsPerFrame = 5;
						if (NumberOfSpawnedNotifications++ < MaxNotificationsPerFrame || !IsBulkScanning())
						{
							FNotificationInfo Info(FText::Format(LOCTEXT("DuplicateItemId", "Duplicate Loot ID {0} used by {1} and {2}, you must delete or chanage one!"),
								FText::FromString(NewLootId.ToString()), FText::FromString(FoundAssetId->ToString()), FText::FromString(PrimaryAssetId.ToString())));
							Info.ExpireDuration = 30.0f;

							TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
							if (Notification.IsValid())
							{
								Notification->SetCompletionState(SNotificationItem::CS_Fail);
							}
						}
					}
#endif
					*FoundAssetId = PrimaryAssetId;
				}
			}
			else
			{
				LootIdToPrimaryAssetIdMap.Add(NewLootId, PrimaryAssetId);
			}
		}
	}
}

void UPInv_AssetManager::AddOrUpdateCollectionContainerMap(const FPrimaryAssetId& PrimaryAssetId, const FAssetData& AssetData, bool bAllowDuplicates)
{
    if (!PrimaryAssetId.IsValid() || !AssetData.IsValid())
    {
        return;
    }

    static const FName nmCollectionContainer(TEXT("CollectionContainer"));
    FString CollectionContainerAsString;
    if (AssetData.GetTagValue(nmCollectionContainer, CollectionContainerAsString))
    {
        FGameplayTagContainer NewCollectionContainer;
        NewCollectionContainer.FromExportString(CollectionContainerAsString);

#if WITH_EDITOR
        if (NewCollectionContainer.IsEmpty())
        {
            RemoveFromCollectionContainerMap(PrimaryAssetId);
        }
        else
#else
        if (!NewCollectionContainer.IsEmpty())
#endif
        {
            FGameplayTagContainer& ExistingCollectionContainer = ItemCollectionMap.FindOrAdd(PrimaryAssetId);

            FGameplayTagContainer AlreadyMappedTags;
            for(const FGameplayTag& ExistingTag : ExistingCollectionContainer)
            {
                if(NewCollectionContainer.HasTagExact(ExistingTag))
                {
                    AlreadyMappedTags.AddTagFast(ExistingTag);
                }
                else
                {
                    ItemsByGameplayTagMap.Remove(ExistingTag, PrimaryAssetId);
                }
            }

            ExistingCollectionContainer = MoveTemp(NewCollectionContainer);

            for( const FGameplayTag& NewTag : ExistingCollectionContainer )
            {
                if(!AlreadyMappedTags.HasTagExact(NewTag))
                {
                    ItemsByGameplayTagMap.AddUnique(NewTag, PrimaryAssetId);
                }
            }
        }
    }
}

#if WITH_EDITOR
void UPInv_AssetManager::RemoveFromItemIdMap(const FPrimaryAssetId& PrimaryAssetId)
{
	RemovePrimaryAssetFromMap(PrimaryAssetId, ItemIdToPrimaryAssetIdMap, PrimaryAssetIdToItemIdMap);
}

void UPInv_AssetManager::RemoveFromLootIdMap(const FPrimaryAssetId& PrimaryAssetId)
{
	RemovePrimaryAssetFromMap(PrimaryAssetId, LootIdToPrimaryAssetIdMap, PrimaryAssetIdToLootIdMap);
}

void UPInv_AssetManager::RemoveFromCollectionContainerMap(const FPrimaryAssetId& PrimaryAssetId)
{
    FGameplayTagContainer ExistingContainer;
    if(ItemCollectionMap.RemoveAndCopyValue(PrimaryAssetId, ExistingContainer))
    {
        for(const FGameplayTag& RemovedTag : ExistingContainer)
        {
            ItemsByGameplayTagMap.Remove(RemovedTag, PrimaryAssetId);
        }
    }
}

void UPInv_AssetManager::RemovePrimaryAssetFromMap(const FPrimaryAssetId& PrimaryAssetId, TMap<FRH_LootId, FPrimaryAssetId>& IdToAssetMap, TMap<FPrimaryAssetId, FRH_LootId>& AssetToIdMap)
{
	FRH_LootId OldId = FRH_LootId();
	if (FRH_LootId* FoundId = AssetToIdMap.Find(PrimaryAssetId))
	{
		OldId = *FoundId;

		// Remove from ItemId Map.
		AssetToIdMap.Remove(PrimaryAssetId);

		if (OldId.IsValid())
		{
			FPrimaryAssetId* FoundAssetId = IdToAssetMap.Find(OldId);
			if (FoundAssetId != nullptr && *FoundAssetId == PrimaryAssetId)
			{
				IdToAssetMap.Remove(OldId);
			}
		}
	}
}

void UPInv_AssetManager::RemovePrimaryAssetFromMap(const FPrimaryAssetId& PrimaryAssetId, TMap<FRH_ItemId, FPrimaryAssetId>& IdToAssetMap, TMap<FPrimaryAssetId, FRH_ItemId>& AssetToIdMap)
{
	FRH_ItemId OldId;
	if (FRH_ItemId* FoundId = AssetToIdMap.Find(PrimaryAssetId))
	{
		OldId = *FoundId;

		// Remove from ItemId Map.
		AssetToIdMap.Remove(PrimaryAssetId);

		if (OldId.IsValid())
		{
			FPrimaryAssetId* FoundAssetId = IdToAssetMap.Find(OldId);
			if (FoundAssetId != nullptr && *FoundAssetId == PrimaryAssetId)
			{
				IdToAssetMap.Remove(OldId);
			}
		}
	}
}
#endif

const class UPInv_AssetManagerSettings& UPInv_AssetManager::GetPInvSettings() const
{
    if (!CachedPInvSettings)
    {
        CachedPInvSettings = GetDefault<UPInv_AssetManagerSettings>();
    }
    return *CachedPInvSettings;
}

#undef LOCTEXT_NAMESPACE