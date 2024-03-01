// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "PlatformInventoryItem/PlatformInventoryItem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "OnlineSubsystem.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "Engine/Texture2D.h"
#include "Misc/ConfigCacheIni.h"
#include "RH_OnlineSubsystemNames.h"
#include "PlatformInventoryItem/PInv_Delegates.h"

UPlatformInventoryItem::UPlatformInventoryItem(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
	: Super(ObjectInitializer)
	, ActiveItemIconStreamableHandle()
{
	ItemId = INDEX_NONE;
	CanOwnMultiple = false;
}

FPrimaryAssetId UPlatformInventoryItem::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(UPInv_AssetManager::PlatformInventoryItemType, GetFName());
}

void UPlatformInventoryItem::PostLoad()
{
    Super::PostLoad();

    UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
    if (pAssetManager != nullptr && pAssetManager->HasCompletedInitialAssetScan())
    {
        PostInitialAssetScan();
    }
    else
    {
        InitialAssetScanHandle = PInv_Delegates::OnPostInitialAssetScan.AddUObject(this, &UPlatformInventoryItem::PostInitialAssetScan);
    }
}

void UPlatformInventoryItem::PostInitialAssetScan()
{
    UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
    if (pAssetManager != nullptr && pAssetManager->HasCompletedInitialAssetScan())
    {
        if (InitialAssetScanHandle.IsValid())
        {
            PInv_Delegates::OnPostInitialAssetScan.Remove(InitialAssetScanHandle);
            InitialAssetScanHandle.Reset();
        }
    }
}

void UPlatformInventoryItem::SetFriendlySearchName(const FString& InFriendlyName)
{
	if (InFriendlyName != FriendlySearchName)
	{
		FriendlySearchName = InFriendlyName;
		MarkPackageDirty();
	}
}

FSoftObjectPath UPlatformInventoryItem::GetItemByFriendlyName(const FString& InFriendlyName, const FTopLevelAssetPath& ClassPathName)
{
	if (UAssetManager::IsInitialized())
	{
		UAssetManager& Manager = UAssetManager::Get();
		IAssetRegistry& AssetRegistry = Manager.GetAssetRegistry();

		FARFilter Filter;
		Filter.ClassPaths.Add(ClassPathName);
		Filter.bRecursiveClasses = true;

		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;

		static const FName nmFriendlySearchName(TEXT("FriendlySearchName"));
		Filter.TagsAndValues.Add(nmFriendlySearchName, InFriendlyName);

		TArray<FAssetData> AssetData;
		AssetRegistry.GetAssets(Filter, AssetData);
		if (AssetData.Num() > 0)
		{
			return AssetData[0].ToSoftObjectPath();
		}

	}
	return FSoftObjectPath();
}

bool UPlatformInventoryItem::GetIconInfoByName(FName IconType, UIconInfo*& Icon) const
{
	for (int32 i = 0; i < Icons.Num(); i++)
	{
		if (Icons[i].IconType == IconType)
		{
			Icon = Icons[i].IconInfo;
			return true;
		}
	}

	return false;
}

void UPlatformInventoryItem::GetTextureAsync(TSoftObjectPtr<UTexture2D>& Texture, const FOnAsyncItemIconLoadComplete& IconLoadedEvent)
{
	if (Texture.IsValid())
	{
		IconLoadedEvent.ExecuteIfBound(Texture.Get());
	}
	else
	{
		// TODO: We might have to expand this to allow multiple icons to be loaded on a single item at the same time at some point
		if (!ActiveItemIconStreamableHandle.IsValid())
		{
			ActiveItemIconStreamableHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
				Texture.ToSoftObjectPath()
				, FStreamableDelegate::CreateUObject(this, &UPlatformInventoryItem::OnAsyncItemIconLoadComplete)
			);
		}

		if (IconLoadedEvent.IsBound())
		{
			OnAsyncItemIconLoaded.Add(IconLoadedEvent);
		}
	}
}

void UPlatformInventoryItem::RemoveItemIconLoadedCallback(UObject* RequestingObject)
{
	OnAsyncItemIconLoaded.RemoveAll(RequestingObject);
}

void UPlatformInventoryItem::OnAsyncItemIconLoadComplete()
{
	check(ActiveItemIconStreamableHandle.IsValid() && ActiveItemIconStreamableHandle->HasLoadCompleted());

	OnAsyncItemIconLoaded.Broadcast(Cast<UTexture2D>(ActiveItemIconStreamableHandle->GetLoadedAsset()));
	OnAsyncItemIconLoaded.Clear();

	TSharedPtr<FStreamableHandle> Temp(ActiveItemIconStreamableHandle);
	ActiveItemIconStreamableHandle.Reset();
}

bool UPlatformInventoryItem::ShouldDisplayToUser(const FRH_LootId& LootId) const
{
	return (IsOwnableInventoryItem || IsWhitelistedLootId(LootId)) && !IsBlacklistedLootId(LootId);
}

bool UPlatformInventoryItem::ShouldDisplayItemOrder(const FRH_LootId& LootId) const
{
    if (OnlyDisplayAcqusitionIfWhitelisted)
    {
        return IsWhitelistedLootId(LootId);
    }

    return ShouldDisplayToUser(LootId);
}

bool UPlatformInventoryItem::GetProductSku(FString& SKU) const
{
	EExternalSkuSource SkuSource = GetSkuSource();

	if (SkuSource != EExternalSkuSource::ESS_No_Souce)
	{
		if (auto pSku = ExternalProductSkus.Find(SkuSource))
		{
			SKU = *pSku;
			return true;
		}
	}

	return false;
}

static EExternalSkuSource GetAppleSkuSource()
{
    // For Apple App Store, allow custom SKU entries for a development bundle of
    // the game compared to the retail bundle (as all products must have unique IDs across bundles)
    static bool bInit = false;
    static EExternalSkuSource AppleSkuSource = EExternalSkuSource::ESS_AppleRetailSandbox;

    if (!bInit)
    {
        bInit = true;

        const UEnum* ExternalSkuSourceNum = StaticEnum<EExternalSkuSource>();
        const FString BundleIdentifier = FPlatformProcess::GetGameBundleId();
        FString SkuSourceAsString;
        if (ExternalSkuSourceNum && GConfig->GetString(TEXT("PlatformInventoryItem.AppleSkuSource"), *BundleIdentifier, SkuSourceAsString, GEngineIni))
        {
            int64 SkuSouceInt = ExternalSkuSourceNum->GetValueByNameString(SkuSourceAsString);
            if (SkuSouceInt != INDEX_NONE)
            {
                AppleSkuSource = EExternalSkuSource(SkuSouceInt);
            }
        }
    }

    return AppleSkuSource;
}

EExternalSkuSource UPlatformInventoryItem::GetSkuSource()
{
	FName OSSName = IOnlineSubsystem::Get()->GetSubsystemName();

	if (OSSName == LIVE_SUBSYSTEM || OSSName == GDK_SUBSYSTEM)
	{
#if defined(PLATFORM_XBOXCOMMON) && PLATFORM_XBOXCOMMON
		return EExternalSkuSource::ESS_Microsoft_Xbox_GDK;
#else
		return EExternalSkuSource::ESS_Microsoft_Xbox;
#endif
	}
	else if (OSSName == PS4_SUBSYSTEM || OSSName == PS5_SUBSYSTEM)
	{
		return EExternalSkuSource::ESS_Sony;
	}
	else if (OSSName == STEAM_SUBSYSTEM || OSSName == STEAMV2_SUBSYSTEM)
	{
		return EExternalSkuSource::ESS_Valve;
	}
	else if (OSSName == SWITCH_SUBSYSTEM)
	{
		return EExternalSkuSource::ESS_Nintendo;
	}
	else if (OSSName == EOS_SUBSYSTEM || OSSName == EOSPLUS_SUBSYSTEM)
	{
		return EExternalSkuSource::ESS_Epic;
	}
	else if (OSSName == APPLE_SUBSYSTEM)
	{
		return GetAppleSkuSource();
	}

	return EExternalSkuSource::ESS_No_Souce;
}

bool UPlatformInventoryItem::IsItemDisabled(bool bIncludeTempDisabled) const
{
	if (UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized()))
	{
		if (bIncludeTempDisabled)
		{
			return pAssetManager->IsItemIdDisabled(GetItemId()) || pAssetManager->IsItemIdTempDisabled(GetItemId());
		}

		return pAssetManager->IsItemIdDisabled(GetItemId());
	}

	return true;
}

bool UPlatformInventoryItem::IsItemTempDisabled() const
{
	if (UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized()))
	{
		return pAssetManager->IsItemIdTempDisabled(GetItemId());
	}

	return true;
}

void UPlatformInventoryItem::GetQuantityOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryCountBlock& Delegate)
{
	if (PlayerInfo != nullptr)
	{
		if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
		{
			PlayerInventory->GetInventoryCount(GetItemId(), Delegate);
			return;
		}
	}

	Delegate.ExecuteIfBound(0);
}

void UPlatformInventoryItem::IsOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (PlayerInfo != nullptr)
	{
		if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
		{
			PlayerInventory->IsInventoryItemOwned(GetItemId(), Delegate);
			return;
		}
	}

	Delegate.ExecuteIfBound(false);
}

void UPlatformInventoryItem::CanOwnMore(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (CanOwnMultiple)
	{
		Delegate.ExecuteIfBound(true);
		return;
	}

	IsOwned(PlayerInfo, FRH_GetInventoryStateDelegate::CreateLambda([Delegate](bool bSuccess)
		{
			// By default assume we can't own more than 1 of something
			Delegate.ExecuteIfBound(!bSuccess);
		}));
}

void UPlatformInventoryItem::IsRented(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate)
{
	if (PlayerInfo != nullptr)
	{
		if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
		{
			PlayerInventory->IsInventoryItemRented(GetItemId(), Delegate);
			return;
		}
	}

	Delegate.ExecuteIfBound(false);
}