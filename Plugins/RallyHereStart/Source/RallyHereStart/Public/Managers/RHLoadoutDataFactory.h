// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/StreamableManager.h"
#include "Inventory/RHLoadoutTypes.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RHLoadoutDataFactory.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSavedSeenAcquiredItemsUpdated);

UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_TwoParams(FRH_GetPlayerInfoLoadoutsDynamicDelegate, URH_PlayerInfo*, PlayerInfo, const TArray<URH_PlayerLoadout*>&, Loadouts);
DECLARE_DELEGATE_TwoParams(FRH_GetPlayerInfoLoadoutsDelegate, URH_PlayerInfo*, const TArray<URH_PlayerLoadout*>&);
DECLARE_RH_DELEGATE_BLOCK(FRH_GetPlayerInfoLoadoutsBlock, FRH_GetPlayerInfoLoadoutsDelegate, FRH_GetPlayerInfoLoadoutsDynamicDelegate, URH_PlayerInfo*, const TArray<URH_PlayerLoadout*>&);

UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_TwoParams(FRH_GetPlayerInfoLoadoutDynamicDelegate, URH_PlayerInfo*, PlayerInfo, URH_PlayerLoadout*, Loadout);
DECLARE_DELEGATE_TwoParams(FRH_GetPlayerInfoLoadoutDelegate, URH_PlayerInfo*, URH_PlayerLoadout*);
DECLARE_RH_DELEGATE_BLOCK(FRH_GetPlayerInfoLoadoutBlock, FRH_GetPlayerInfoLoadoutDelegate, FRH_GetPlayerInfoLoadoutDynamicDelegate, URH_PlayerInfo*, URH_PlayerLoadout*);

UDELEGATE()
DECLARE_DYNAMIC_DELEGATE_TwoParams(FRH_SetPlayerInfoLoadoutsDynamicDelegate, URH_PlayerInfo*, PlayerInfo, bool, bSuccess);
DECLARE_DELEGATE_TwoParams(FRH_SetPlayerInfoLoadoutsDelegate, URH_PlayerInfo*, bool);
DECLARE_RH_DELEGATE_BLOCK(FRH_SetPlayerInfoLoadoutsBlock, FRH_SetPlayerInfoLoadoutsDelegate, FRH_SetPlayerInfoLoadoutsDynamicDelegate, URH_PlayerInfo*, bool);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerInfoLoadoutsUpdated, URH_PlayerInfo*, PlayerInfo, const TArray<URH_PlayerLoadout*>&, Loadouts);

USTRUCT(BlueprintType)
struct FRHPlayerLoadoutsWrapper
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, URH_PlayerLoadout*> LoadoutsById;

	FRHPlayerLoadoutsWrapper()
	{
		LoadoutsById.Empty();
	}

};

USTRUCT(BlueprintType)
struct FRHEquippedLoadoutItemWrapper
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	UPlatformInventoryItem* Item;

	FRHEquippedLoadoutItemWrapper() :
		Item(nullptr)
	{ }
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnGetEquippedLoadoutItem, FRHEquippedLoadoutItemWrapper, ItemWrapper);

USTRUCT(BlueprintType)
struct FAccountLoadoutDefaults : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "View Route")
	ERHLoadoutSlotTypes SlotType;

	UPROPERTY(EditAnywhere, Category = "View Route")
	TSoftObjectPtr<UPlatformInventoryItem> Default;

	FAccountLoadoutDefaults() :
		SlotType(ERHLoadoutSlotTypes::AVATAR_SLOT)
	{}
};

UCLASS(BlueprintType)
class RALLYHERESTART_API URH_PlayerLoadoutItem : public UObject
{
	GENERATED_BODY()
public:
	/*
	* Getters
	*/
	UFUNCTION(BlueprintGetter, Category = "Loadout Data Factory | Player Loadout Item")
	FString GetLoadoutItemId() const { return LoadoutItemId; }

	UFUNCTION(BlueprintGetter, Category = "Loadout Data Factory | Player Loadout Item")
	const FRH_ItemId& GetItemId() const { return ItemId; }

	UFUNCTION(BlueprintGetter, Category = "Loadout Data Factory | Player Loadout Item")
	int32 GetCount() const { return Count; }

	UFUNCTION(BlueprintGetter, Category = "Loadout Data Factory | Player Loadout Item")
	int32 GetSortOrder() const { return SortOrder; }

	UFUNCTION(BlueprintGetter, Category = "Loadout Data Factory | Player Loadout Item")
	int32 GetItemValueId() const { return ItemValueId; }

	UFUNCTION(BlueprintGetter, Category = "Loadout Data Factory | Player Loadout Item")
	FDateTime GetCreationDate() const { return CreationDate; }

	UFUNCTION(BlueprintGetter, Category = "Loadout Data Factory | Player Loadout Item")
	FDateTime GetLastModifiedTime() const { return LastModifiedTimestamp; }

	/*
	* Setters
	*/
	UFUNCTION(BlueprintSetter, Category = "Loadout Data Factory | Player Loadout Item")
	void SetLoadoutItemId(FString InLoadoutItemId);

	UFUNCTION(BlueprintSetter, Category = "Loadout Data Factory | Player Loadout Item")
	void SetItemId(const FRH_ItemId& InItemId);

	UFUNCTION(BlueprintSetter, Category = "Loadout Data Factory | Player Loadout Item")
	void SetCount(int32 InCount);

	UFUNCTION(BlueprintSetter, Category = "Loadout Data Factory | Player Loadout Item")
	void SetSortOrder(int32 InSortOrder);

	UFUNCTION(BlueprintSetter, Category = "Loadout Data Factory | Player Loadout Item")
	void SetItemValueId(int32 InItemValueId);

	UFUNCTION(BlueprintSetter, Category = "Loadout Data Factory | Player Loadout Item")
	void SetCreationDate(FDateTime InCreationDate);

	UFUNCTION(BlueprintSetter, Category = "Loadout Data Factory | Player Loadout Item")
	void SetLastModifiedTime(FDateTime InLastModifiedTime);


	/*
	* Properties
	*/
	UPROPERTY(BlueprintGetter = GetLoadoutItemId, BlueprintSetter = SetLoadoutItemId)
	FString LoadoutItemId;

	UPROPERTY(BlueprintGetter = GetItemId, BlueprintSetter = SetItemId)
	FRH_ItemId ItemId;

	UPROPERTY(BlueprintGetter = GetCount, BlueprintSetter = SetCount)
	int32 Count;

	UPROPERTY(BlueprintGetter = GetSortOrder, BlueprintSetter = SetSortOrder)
	int32 SortOrder;

	UPROPERTY(BlueprintGetter = GetItemValueId, BlueprintSetter = SetItemValueId)
	int32 ItemValueId;

	UPROPERTY(BlueprintGetter = GetCreationDate, BlueprintSetter = SetCreationDate)
	FDateTime CreationDate;

	UPROPERTY(BlueprintGetter = GetLastModifiedTime, BlueprintSetter = SetLastModifiedTime)
	FDateTime LastModifiedTimestamp;
};

UCLASS(BlueprintType)
class RALLYHERESTART_API URH_PlayerLoadout : public UObject
{
	GENERATED_BODY()

	/*
	* Getters
	*/
public:

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	FString GetLoadoutId() const { return LoadoutId; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	int32 GetV() const { return V; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	int32 GetType() const { return TypeValueId; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	int32 GetSortOrder() const { return SortOrder; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	FString GetName() const { return Name; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	FDateTime GetCreationDate() const { return CreationDate; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	FDateTime GetLastModifiedTime() const { return LastModifiedTimestamp; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	TArray<URH_PlayerLoadoutItem*> GetItems() const { return Items; }

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	TMap<ERHLoadoutSlotTypes, UPlatformInventoryItem*> GetDefaultItems() const { return DefaultItems; }


	/*
	* Items Functions
	*/
public:

	// Gets the item that is equipped to a specific player ItemValueId
	UFUNCTION(BlueprintCallable, Category = "Loadout Data Factory | Player Loadout")
	bool GetEquippedItemInSlot(FOnGetEquippedLoadoutItem Event, ERHLoadoutSlotTypes LoadoutSlot);

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory | Player Loadout")
	bool IsItemEquippedInSlot(UPlatformInventoryItem* Item, ERHLoadoutSlotTypes LoadoutSlot);

	// Equips an item to a given player account loadout slot
	UFUNCTION(BlueprintCallable, Category = "Loadout Data Factory | Player Loadout")
	bool LocalEquipItemToSlot(UPlatformInventoryItem* Item, ERHLoadoutSlotTypes LoadoutSlot);


protected:

	bool CanEquipItem(UPlatformInventoryItem* InventoryItem, ERHLoadoutSlotTypes LoadoutSlot);

	void AsyncLoadItem(FOnGetEquippedLoadoutItem Event, TSoftObjectPtr<UPlatformInventoryItem> SoftItem);

	// Callback when a Async Item Load event has completed for an equipped loadout item.
	void AsyncLoadItemComplete(FOnGetEquippedLoadoutItem Event, TSoftObjectPtr<UPlatformInventoryItem> SoftItem);

	FStreamableManager Streamable;


	/* 
	* Loadout Properties
	*/
public:

	UPROPERTY()
	FString LoadoutId;

	UPROPERTY()
	int32 V;

	UPROPERTY()
	int32 TypeValueId;

	UPROPERTY()
	int32 SortOrder;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FDateTime CreationDate;

	UPROPERTY()
	FDateTime LastModifiedTimestamp;

	UPROPERTY()
	TArray<URH_PlayerLoadoutItem*> Items;

	UPROPERTY()
	TMap<ERHLoadoutSlotTypes, UPlatformInventoryItem*> DefaultItems;
};

UCLASS(Config=Game)
class RALLYHERESTART_API URHLoadoutDataFactory : public UObject
{
    GENERATED_BODY()

/*
* initialization and bootstrapping
*/
public:

	virtual void Initialize();
    virtual void PostLogin();
    virtual void PostLogoff();

/*
* Getters
*/
public:

	UFUNCTION(BlueprintPure, Category = "Loadout")
	static URHLoadoutDataFactory* GetRHLoadoutDataFactory(const UObject* WorldContextObject);
	static URHLoadoutDataFactory* Get(const UWorld* MyWorld);

/*
* interface
*/
public:
	// Request for Loadout Settings info for given PlayerInfo.
	// Passed Delegate handles response when requested Loadout info is received, contains retrieved URH_PlayerLoadout* objects
	UFUNCTION(BlueprintCallable, Category = "Loadout Data Factory", meta = (DisplayName = "Get Player Loadout Settings", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_RequestPlayerLoadoutSettings(URH_PlayerInfo* PlayerInfo, const FTimespan& StaleThreshold, bool bForceRefresh, const FRH_GetPlayerInfoLoadoutsDynamicDelegate& Delegate) { GetPlayerLoadoutSettings(PlayerInfo, StaleThreshold, bForceRefresh, Delegate); }
	void GetPlayerLoadoutSettings(URH_PlayerInfo* PlayerInfo, const FTimespan& StaleThreshold = FTimespan(), bool bForceRefresh = false, FRH_GetPlayerInfoLoadoutsBlock Delegate = FRH_GetPlayerInfoLoadoutsBlock());
	void OnGetPlayerLoadoutSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& Response, URH_PlayerInfo* PlayerInfo, FRH_GetPlayerInfoLoadoutsBlock Delegate);

	// Request for Loadout Settings info for given PlayerInfo and Loadout Type
	// Passed Delegate handles response when requested Loadout info is received, contains retrieved URH_PlayerLoadout* objects
	UFUNCTION(BlueprintCallable, Category = "Loadout Data Factory", meta = (DisplayName = "Get Player Loadout Setting By Loadout Type", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_RequestPlayerLoadoutSettingByLoadoutType(URH_PlayerInfo* PlayerInfo, ERHLoadoutTypes LoadoutType, bool bCreateIfNeeded, const FTimespan& StaleThreshold, bool bForceRefresh, const FRH_GetPlayerInfoLoadoutDynamicDelegate& Delegate) { GetPlayerLoadoutSettingByLoadoutType(PlayerInfo, LoadoutType, bCreateIfNeeded, StaleThreshold, bForceRefresh, Delegate); }
	void GetPlayerLoadoutSettingByLoadoutType(URH_PlayerInfo* PlayerInfo, ERHLoadoutTypes LoadoutType, bool bCreateIfNeeded, const FTimespan& StaleThreshold = FTimespan(), bool bForceRefresh = false, FRH_GetPlayerInfoLoadoutBlock Delegate = FRH_GetPlayerInfoLoadoutBlock());
	void OnGetPlayerLoadoutSettingByLoadoutTypeResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& Response, URH_PlayerInfo* PlayerInfo, ERHLoadoutTypes LoadoutType, bool bCreateIfNeeded, FRH_GetPlayerInfoLoadoutBlock Delegate);

	// Request to update Loadout Settings for given PlayerInfo.
	// Passed Delegate handles response received for whether or not the request was successful
	UFUNCTION(BlueprintCallable, Category = "Loadout Data Factory", meta = (DisplayName = "Set Player Loadout Settings", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_SetPlayerLoadoutSettings(URH_PlayerInfo* PlayerInfo, const TArray<URH_PlayerLoadout*>& Loadouts, const FRH_SetPlayerInfoLoadoutsDynamicDelegate& Delegate) { SetPlayerLoadoutSettings(PlayerInfo, Loadouts, Delegate); }
	void SetPlayerLoadoutSettings(URH_PlayerInfo* PlayerInfo, const TArray<URH_PlayerLoadout*>& Loadouts, FRH_SetPlayerInfoLoadoutsBlock Delegate = FRH_SetPlayerInfoLoadoutsBlock());
	void OnSetPlayerLoadoutSettingsResponse(bool bSuccess, const FRH_PlayerSettingsDataWrapper& ResponseData, const FRH_ErrorInfo& ErrorInfo, TWeakObjectPtr<URH_PlayerInfo> PlayerInfo, FRH_SetPlayerInfoLoadoutsBlock Delegate);

	UFUNCTION(BlueprintPure, Category = "Loadout Data Factory")
	UPlatformInventoryItem* GetDefaultItemForLoadoutSlotType(const ERHLoadoutSlotTypes SlotType);
	
public:

	UPROPERTY(BlueprintAssignable)
	FOnPlayerInfoLoadoutsUpdated OnPlayerLoadoutsUpdated;

protected:

    // Creates a loadout with the given type
	bool CreateLocalLoadout(URH_PlayerLoadout*& Loadout, ERHLoadoutTypes LoadoutType, int32 SortOrder);

/*
* Loadout Settings
*/
protected:
	bool PackagePlayerLoadouts(const TArray<URH_PlayerLoadout*>& InLoadouts, TMap<FString, FRHAPI_SetSinglePlayerSettingRequest>& OutSettingsContent);
	bool PackagePlayerLoadout(URH_PlayerLoadout* InLoadout, FRHAPI_SetSinglePlayerSettingRequest& OutSettingData);

	bool UnpackageLoadoutSettings(const TMap<FString, FRHAPI_SettingData>& InSettingsContent, FRHPlayerLoadoutsWrapper& OutLoadouts);
	bool UnpackageLoadoutSetting(const FString& InLoadoutId, const FRHAPI_SettingData& InSettingData, URH_PlayerLoadout*& OutLoadout);

	TArray<URH_PlayerLoadout*> GetLoadoutsFromWrapper(const FRHPlayerLoadoutsWrapper& InWrapper);


protected:
	/** Path to the DataTable to load, configurable per game. If empty, it will not spawn one */
	UPROPERTY(Config)
	FSoftObjectPath AccountDefaultLoadoutsDataTableClassName;

	// Table of all available Context Actions
	UPROPERTY(Transient)
	UDataTable* AccountDefaultsDT;

	UPROPERTY(Transient, BlueprintReadWrite)
	TMap<URH_PlayerInfo*, FRHPlayerLoadoutsWrapper> PlayerLoadouts;

	UPROPERTY(Transient)
	TMap<ERHLoadoutSlotTypes, UPlatformInventoryItem*> DefaultLoadoutItems;

	TMap<URH_PlayerInfo*, FDateTime> LastRequestSettings;
};
