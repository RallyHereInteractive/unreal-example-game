// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "PlatformInventoryItem/ItemCollection.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "PlatformInventoryItem/PInv_Delegates.h"
#include "GameplayTags.h"

UItemCollection::UItemCollection(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
    : Super(ObjectInitializer)
{
    Rules.bApplyRecursively = false;
    Rules.Priority = 150;
	Rules.ChunkId = 0;
    Rules.CookRule = EPrimaryAssetCookRule::AlwaysCook;

	PrimaryAssetType = UPInv_AssetManager::ItemCollectionType;
}

FPrimaryAssetId UItemCollection::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(UPInv_AssetManager::ItemCollectionType, GetFName());
}

void UItemCollection::PostLoad()
{
    // Asset manager will not be initialized before engine is available, so cannot update collection in that case

    UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
	if (pAssetManager != nullptr && pAssetManager->HasCompletedInitialAssetScan())
	{
		// Don't need to call PostInitialAssetScan() since UpdateAssetBundleData() will be called by Super::PostLoad()
		UpdateCollection();
	}
    PostInitialAssetScanHandle = PInv_Delegates::OnPostInitialAssetScan.AddUObject(this, &UItemCollection::PostInitialAssetScan);

    Super::PostLoad();
}

void UItemCollection::BeginDestroy()
{
	if (PostInitialAssetScanHandle.IsValid())
	{
		PInv_Delegates::OnPostInitialAssetScan.Remove(PostInitialAssetScanHandle);
	}
	PostInitialAssetScanHandle.Reset();

	Super::BeginDestroy();
}

void UItemCollection::UpdateCollection()
{
	// The collection query is empty. Nothing will match.
	if (CollectionQuery.IsEmpty())
	{
		return;
	}

	UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
	if (pAssetManager == nullptr)
	{
		return;
	}

	const TMap<FPrimaryAssetId, FGameplayTagContainer>& CollectionMap = pAssetManager->GetItemCollectionMap();
	for (const TPair<FPrimaryAssetId, FGameplayTagContainer>& Pair : CollectionMap)
	{
		if (Pair.Value.MatchesQuery(CollectionQuery))
		{
			FAssetData Data;
			if (pAssetManager->GetPrimaryAssetData(Pair.Key, Data))
			{
				TryToAddAsset(Data);
			}
		}
	}
}

void UItemCollection::TryToAddAsset(const FAssetData& InAsset)
{
}

#if WITH_EDITORONLY_DATA
void UItemCollection::UpdateAssetBundleData()
{
    Super::UpdateAssetBundleData();

    if (!UAssetManager::IsInitialized())
    {
        return;
    }

    UAssetManager& Manager = UAssetManager::Get();

    // Update rules
    FPrimaryAssetId PrimaryAssetId = GetPrimaryAssetId();
    Manager.SetPrimaryAssetRules(PrimaryAssetId, Rules);
}
#endif

void UItemCollection::PostInitialAssetScan()
{
	UpdateCollection();
#if WITH_EDITORONLY_DATA
	UpdateAssetBundleData();
#endif
}
