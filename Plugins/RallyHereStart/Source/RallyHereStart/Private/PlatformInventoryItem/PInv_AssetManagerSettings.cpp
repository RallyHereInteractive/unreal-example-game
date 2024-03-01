// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.


#include "PlatformInventoryItem/PInv_AssetManagerSettings.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"

UPInv_AssetManagerSettings::UPInv_AssetManagerSettings(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
	: Super(ObjectInitializer)
{
    bQuickCook=false;
    CookProfile = TEXT("Default");
}

#if WITH_EDITOR

bool UPInv_AssetManagerSettings::IgnoreQuickCookRulesByType(const FPrimaryAssetType& InType) const
{
	return QuickCookTypeIgnoreSet.Contains(InType);
}

bool UPInv_AssetManagerSettings::IgnoreQuickCookRulesByAsset(const FPrimaryAssetId& InAsset) const
{
	return QuickCookAssetIgnoreSet.Contains(InAsset) || QuickCookTypeIgnoreSet.Contains(InAsset.PrimaryAssetType);
}

void UPInv_AssetManagerSettings::RebuildQuickCookSets()
{
	QuickCookTypeIgnoreSet.Empty(PrimaryTypesToIgnoreQuickCook.Num());
	QuickCookAssetIgnoreSet.Empty(PrimaryAssetsToIgnoreQuickCook.Num());

	for (const FName& TypeName : PrimaryTypesToIgnoreQuickCook)
	{
		QuickCookTypeIgnoreSet.Add(TypeName);
	}

	for (const FString& PrimaryAssetStr : PrimaryAssetsToIgnoreQuickCook)
	{
		FPrimaryAssetId AsAssetId = FPrimaryAssetId::FromString(PrimaryAssetStr);
		if(AsAssetId.IsValid())
		{
			QuickCookAssetIgnoreSet.Add(AsAssetId);
			QuickCookAssetIngoreSetTypes.Add(AsAssetId.PrimaryAssetType);
		}
	}
}

void UPInv_AssetManagerSettings::PostReloadConfig(class FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);

	RebuildQuickCookSets();
}

void UPInv_AssetManagerSettings::PostInitProperties()
{
	Super::PostInitProperties();

	RebuildQuickCookSets();
}

void UPInv_AssetManagerSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty != nullptr)
	{
		if ((PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPInv_AssetManagerSettings, PrimaryTypesToIgnoreQuickCook))
			|| (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPInv_AssetManagerSettings, PrimaryAssetsToIgnoreQuickCook)))
		{
			RebuildQuickCookSets();
		}
	}

	if (PropertyChangedEvent.Property)
	{
		UPInv_AssetManager* pAssetManger = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
		if(pAssetManger != nullptr)
		{
			pAssetManger->ReinitializeFromConfig();
		}
	}
}
#endif