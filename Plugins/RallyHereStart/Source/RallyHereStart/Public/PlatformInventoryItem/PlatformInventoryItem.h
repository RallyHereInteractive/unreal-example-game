// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Engine/AssetManager.h"
#include "GameplayTagContainer.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RH_PlayerInventory.h"
#include "RH_Properties.h"
#include "PlatformInventoryItem.generated.h"

struct FGameplayTagContainer;
class UTexture2D;
class UAssetManager;

UENUM()
enum class EExternalSkuSource : uint8
{
	ESS_No_Souce,
	ESS_Sony,
	ESS_Nintendo,
	ESS_Microsoft_Xbox,
	ESS_Microsoft_Xbox_GDK,
	ESS_Epic,
	ESS_Valve,
    ESS_AppleRetailSandbox,
    ESS_AppleDevSandbox,
    ESS_ApplePlatDevSandbox
};

USTRUCT()
struct RALLYHERESTART_API FIconReference
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Icon", meta = (AssetBundles = "Client,Icon"))
	FName IconType;

	UPROPERTY(EditDefaultsOnly, Category = "Icon", meta = (AssetBundles = "Client,Icon", DisplayName = "Icon"))
	UIconInfo* IconInfo;

	FIconReference() :
		IconInfo(nullptr)
	{ }
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnAsyncItemIconLoadComplete, class UTexture2D*, ItemIcon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAsyncItemIconLoadCompleteMulticast, class UTexture2D*, ItemIcon);

/**
 *
 */
UCLASS(Config=GameItems, Abstract, PerObjectConfig, BlueprintType, NotBlueprintable)
class RALLYHERESTART_API UPlatformInventoryItem : public UPrimaryDataAsset
{
	GENERATED_BODY()
		
public:
	UPlatformInventoryItem(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
    virtual void PostLoad() override;
    virtual void PostInitialAssetScan();

	// Returns the Item Name as Text
	UFUNCTION(BlueprintPure, Category = "Item")
	const FRH_ItemId& GetItemId() const { return ItemId; }

	// Returns the Item Name as Text
	UFUNCTION(BlueprintPure, Category = "Item")
	FORCEINLINE FText GetItemName() const { return ItemDisplayName; }

	// Returns the Item Name as String
	UFUNCTION(BlueprintPure, Category = "Item")
	FORCEINLINE FString GetItemNameAsString() const { return GetItemName().ToString(); }

    // Sets the Item Display Name
    UFUNCTION(BlueprintCallable, Category = "Item")
    void SetItemName(FText NewItemDisplayName) { ItemDisplayName = NewItemDisplayName; }

	// Returns the Item Description as Text
	UFUNCTION(BlueprintPure, Category = "Item")
	FORCEINLINE FText GetItemDescription() const { return ItemDescription; }

	// Returns the Item Description as String
	UFUNCTION(BlueprintPure, Category = "Item")
	FORCEINLINE FString GetItemDescriptionAsString() const { return GetItemDescription().ToString(); }

    // Sets the Item Description
    UFUNCTION(BlueprintCallable, Category = "Item")
    void SetItemDescription(FText NewItemDescription) { ItemDescription = NewItemDescription; }

	// Allows you to set a friendly unlocalized searchable name for an item
	UFUNCTION(BlueprintCallable, Category = "Item")
    void SetFriendlySearchName(const FString& InFriendlyName);

    // Returns the Searchable name for the item
    UFUNCTION(BlueprintCallable, Category = "Item")
    FORCEINLINE FString GetFriendlySearchName() const { return FriendlySearchName; }

	UFUNCTION(BlueprintPure, Category = "Item")
	bool ShouldDisplayToUser(const FRH_LootId& LootId = FRH_LootId()) const;

    virtual bool ShouldDisplayItemOrder(const FRH_LootId& LootId) const;

	bool IsWhitelistedLootId(const FRH_LootId& LootId) const { return DisplayableLootIds.Contains(LootId); }

    bool IsBlacklistedLootId(const FRH_LootId& LootId) const { return BlackListedLootIds.Contains(LootId); }

	UFUNCTION(BlueprintPure, Category = "Item")
	static TSoftObjectPtr<UPlatformInventoryItem> GetItemByFriendlyName(const FString& InFriendlyName) { return GetItemByFriendlyName<UPlatformInventoryItem>(InFriendlyName); }

	template<typename T>
	static TSoftObjectPtr<T> GetItemByFriendlyName(const FString& InFriendlyName);

	UFUNCTION(BlueprintPure, Category = "Item")
	virtual UIconInfo* GetItemIconInfo() const { return ItemIconInfo; }

	UFUNCTION(BlueprintPure, Category = "Item")
	bool GetIconInfoByName(FName IconType, UIconInfo*& Icon) const;

    UFUNCTION(BlueprintCallable, Category = "Item")
    void GetTextureAsync(TSoftObjectPtr<UTexture2D>& Texture, const FOnAsyncItemIconLoadComplete& IconLoadedEvent);

    void RemoveItemIconLoadedCallback(UObject* RequestingObject);

    UFUNCTION(BlueprintCallable, Category = "Collection")
    void SetCollectionContainer(const FGameplayTagContainer& InContainer) { CollectionContainer = InContainer; }

    UFUNCTION(BlueprintPure, Category = "Collection")
    const FGameplayTagContainer& GetCollectionContainer() const { return CollectionContainer; }

	// Returns the applicable product sku for the given item
	bool GetProductSku(FString& SKU) const;

	// Returns the SKU Source based on your current platform and portal type
	static EExternalSkuSource GetSkuSource();

	// Checks if the item is currently disabled due to configuration
	UFUNCTION(BlueprintPure, Category = "Item")
	bool IsItemDisabled(bool bIncludeTempDisabled = true) const;

	// Checks if the item is currently temporarily disabled due to configuration
	UFUNCTION(BlueprintPure, Category = "Item")
	bool IsItemTempDisabled() const;

	// Checks if the player owns the item
	UFUNCTION(BlueprintCallable, Category = "Item", meta = (DisplayName = "Get Quantity Owned", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_GetQuantityOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryCountDynamicDelegate& Delegate) { GetQuantityOwned(PlayerInfo, Delegate); }
	void GetQuantityOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryCountBlock& Delegate);

	// Checks if the player owns the item
	UFUNCTION(BlueprintCallable, Category = "Item", meta = (DisplayName = "Is Owned", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_IsOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateDynamicDelegate& Delegate) { IsOwned(PlayerInfo, Delegate); }
	virtual void IsOwned(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate);

	// Checks if the player can own more copies of an item
	UFUNCTION(BlueprintCallable, Category = "Item", meta = (DisplayName = "Can Own More", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_CanOwnMore(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateDynamicDelegate& Delegate) { CanOwnMore(PlayerInfo, Delegate); }
	virtual void CanOwnMore(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate);

	// Checks if the player is renting the item
	UFUNCTION(BlueprintCallable, Category = "Item", meta = (DisplayName = "Is Rented", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_IsRented(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateDynamicDelegate& Delegate) { IsRented(PlayerInfo, Delegate); }
	void IsRented(const URH_PlayerInfo* PlayerInfo, const FRH_GetInventoryStateBlock& Delegate);

    template<typename T>
    static void SortByFriendlySearchName( TArray<TSoftObjectPtr<T>>& Items );

    template<typename T>
    static void SortByDisplayName(TArray<TSoftObjectPtr<T>>& Items);

    template<typename T>
    static FString StaticGetFriendlySearchName(const TSoftObjectPtr<T>& InItem, UAssetManager* InAssetManager = nullptr);

    template<typename T>
    static FText StaticGetDisplayName(const TSoftObjectPtr<T>& InItem, UAssetManager* InAssetManager = nullptr);

    // The Item Name as Text
    UPROPERTY(EditDefaultsOnly, AssetRegistrySearchable, DuplicateTransient, Category = "Item")
    FText ItemDisplayName;

    // The Item Description as Text
    UPROPERTY(EditDefaultsOnly, AssetRegistrySearchable, DuplicateTransient, Category = "Item")
    FText ItemDescription;

    // A searchable name that is used by the cheat manager to grant the item with drop and give console commands.
    UPROPERTY(EditDefaultsOnly, AssetRegistrySearchable, DuplicateTransient, Category = "Item")
    FString FriendlySearchName;

    // This flag means the item must be whitelisted for display in order
    UPROPERTY(EditDefaultsOnly, Category = "Order")
    bool OnlyDisplayAcqusitionIfWhitelisted;

    UPROPERTY(Config, EditDefaultsOnly, AssetRegistrySearchable, Category = "Collection")
    FGameplayTagContainer CollectionContainer;

	// This dictates if the player can own multiple of the given item, or if they are limited to a single copy, defaults to false
	UPROPERTY(EditDefaultsOnly, Category = "Item")
	bool CanOwnMultiple;

protected:

	// Item Id wrapper for the Rally Here system, with the legacy and guid versions of item id.
	UPROPERTY(EditDefaultsOnly, AssetRegistrySearchable, DuplicateTransient, Category = "Configuration Deep Link", meta = (ShowOnlyInnerProperties))
	FRH_ItemId ItemId;

    // This dictates if a given item type is something we physically want to reveal to the user as a reward/order
    UPROPERTY(AssetRegistrySearchable)
    bool IsOwnableInventoryItem;

	// This allows for specific loot IDs to be whitelisted to for display
	UPROPERTY(EditDefaultsOnly, Category = "Order")
	TArray<FRH_LootId> DisplayableLootIds;

    // Soft reference to the items icon
    UPROPERTY(EditDefaultsOnly, Category = "Icon", meta = (AssetBundles = "Client,Icon", DisplayName="Item Icon"))
    UIconInfo* ItemIconInfo;

    UPROPERTY(EditDefaultsOnly, Category = "Icon", meta = (AssetBundles = "Client,Icon"))
    TArray<FIconReference> Icons;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Skus")
	TMap<EExternalSkuSource, FString> ExternalProductSkus;

    // This allows for specific loot IDs to be whitelisted to for display
    UPROPERTY(EditDefaultsOnly, Category = "Order")
    TArray<FRH_LootId> BlackListedLootIds;

private:
    static FSoftObjectPath GetItemByFriendlyName(const FString& InFriendlyName, const FTopLevelAssetPath& ClassPathName);

    void OnAsyncItemIconLoadComplete();
    TSharedPtr<FStreamableHandle> ActiveItemIconStreamableHandle;
    FOnAsyncItemIconLoadCompleteMulticast OnAsyncItemIconLoaded;

    FDelegateHandle InitialAssetScanHandle;
};

template<typename T>
FORCEINLINE TSoftObjectPtr<T> UPlatformInventoryItem::GetItemByFriendlyName(const FString& InFriendlyName)
{
    return TSoftObjectPtr<T>(GetItemByFriendlyName(InFriendlyName, T::StaticClass()->GetClassPathName()));
}

template<typename T>
FORCEINLINE void UPlatformInventoryItem::SortByFriendlySearchName(TArray<TSoftObjectPtr<T>>& Items)
{
    UAssetManager* pAssetManager = UAssetManager::GetIfInitialized();
    Items.Sort([&pAssetManager](const TSoftObjectPtr<T>& A, const TSoftObjectPtr<T>& B) -> bool
    {
        return StaticGetFriendlySearchName(A, pAssetManager) < StaticGetFriendlySearchName(B, pAssetManager);
    });
}

template<typename T>
FORCEINLINE void UPlatformInventoryItem::SortByDisplayName(TArray<TSoftObjectPtr<T>>& Items)
{
    UAssetManager* pAssetManager = UAssetManager::GetIfInitialized();
    Items.Sort([&pAssetManager](const TSoftObjectPtr<T>& A, const TSoftObjectPtr<T>& B) -> bool
    {
        return StaticGetDisplayName(A, pAssetManager).CompareTo(StaticGetDisplayName(B, pAssetManager)) <= 0;
    });
}

template<typename T>
FORCEINLINE FString UPlatformInventoryItem::StaticGetFriendlySearchName(const TSoftObjectPtr<T>& InItem, UAssetManager* InAssetManager /*= nullptr*/)
{
    if (InItem.IsValid())
    {
        return InItem->GetFriendlySearchName();
    }

    if (InItem.IsNull())
    {
        return FString();
    }

    if (InAssetManager == nullptr)
    {
        InAssetManager = UAssetManager::GetIfInitialized();
    }

    if (InAssetManager == nullptr)
    {
        return FString();
    }

    FAssetData Data;
    if (InAssetManager->GetAssetDataForPath(InItem.ToSoftObjectPath(), Data))
    {
        static const FName nmFriendlySearchName(TEXT("FriendlySearchName"));
        return Data.GetTagValueRef<FString>(nmFriendlySearchName);
    }

    return FString();
}

template<typename T>
FORCEINLINE FText UPlatformInventoryItem::StaticGetDisplayName(const TSoftObjectPtr<T>& InItem, UAssetManager* InAssetManager /*= nullptr*/)
{
    if (InItem.IsValid())
    {
        return InItem->GetItemName();
    }

    if (InItem.IsNull())
    {
        return FText();
    }

    if (InAssetManager == nullptr)
    {
        InAssetManager = UAssetManager::GetIfInitialized();
    }

    if (InAssetManager == nullptr)
    {
        return FText();
    }

    FAssetData Data;
    if (InAssetManager->GetAssetDataForPath(InItem.ToSoftObjectPath(), Data))
    {
        static const FName nmItemDisplayName(TEXT("ItemDisplayName"));
        return Data.GetTagValueRef<FText>(nmItemDisplayName);
    }

    return FText();
}
