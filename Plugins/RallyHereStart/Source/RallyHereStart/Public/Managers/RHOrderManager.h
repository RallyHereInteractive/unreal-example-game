#pragma once
#include "PlatformInventoryItem/PlatformInventoryItem.h"
#include "Managers/RHStoreItemHelper.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RHOrderManager.generated.h"

UENUM(BlueprintType)
enum class ERHOrderType : uint8
{
    //Always include this default in this enum
	Generic = 0			UMETA(DisplayName = "Generic"),

    //Order Types
    Voucher				UMETA(DisplayName = "Voucher"),
    BattlePass			UMETA(DisplayName = "Battle Pass"),
	ActiveBoost			UMETA(DisplayName = "Active Boost"),
    EventGrandPrize		UMETA(DisplayName = "Event Grand Prize"),
	LootBox				UMETA(DisplayName = "Loot Box"),
    //Always include this default in this enum
    MAX,
};

USTRUCT()
struct FRH_ActiveOrderWatch
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	URH_PlayerInfo* PlayerInfo;

	FRH_OrderDetailsBlock OrderDetailsBlock;

	FDateTime FirstValidOrderTime;

	TArray<FString> SeenOrderIds;

	FRH_ActiveOrderWatch()
		: PlayerInfo(nullptr)
		, OrderDetailsBlock(FRH_OrderDetailsBlock())
		, FirstValidOrderTime(FDateTime::MinValue())
	{
		SeenOrderIds.Empty();
	}
};

UCLASS(BlueprintType)
class RALLYHERESTART_API UOrderItemData : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	URHStoreItem* StoreItem;

	UPROPERTY(BlueprintReadOnly)
	int32 Quantity;

	int32 GetSortOrder() const { return StoreItem ? StoreItem->GetSortOrder() : INDEX_NONE; };

	FRH_LootId GetLootId() const { return StoreItem ? StoreItem->GetLootId() : FRH_LootId(); };
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_OnOrderSuccess, URHStoreItem*, StoreItem);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRH_OnOrderFailed, FText, ErrorMsg);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrderReady, URHOrder*, Order);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInvalidVoucherObtained, URHStoreItem*, StoreItem);

UCLASS(BlueprintType)
class RALLYHERESTART_API URHOrder : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly)
	ERHOrderType OrderType;
    
    // List of all items associated with this order
    UPROPERTY(BlueprintReadOnly)
    TArray<UOrderItemData*> OrderItems;

    // The amount of time passed since creation, used to finalize orders
    float TimeSinceCreation;

    // Purchase Ref Id
    FGuid PurchaseRefId;

    // Returns if there are more than a single item in the order
    UFUNCTION(BlueprintPure)
    bool IsBundleOrder() const { return OrderItems.Num() > 1; }

    // Returns if the order is the result of a purchase
    UFUNCTION(BlueprintPure)
    bool IsPurchase() const { return PurchaseRefId.IsValid(); }

    //Sort the order items array by sort order
    UFUNCTION(BlueprintCallable)
    void SortOrderItemsBySortOrder() { OrderItems.Sort([](const UOrderItemData& A, const UOrderItemData& B) { return (A.GetSortOrder() < B.GetSortOrder()); }); };
};

UCLASS(Config = Game)
class RALLYHERESTART_API URHOrderManager : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

public:
	void Initialize(class URHStoreItemHelper* InStoreItemHelper);
	void Uninitialize();

	void SetOrderWatchForPlayer(URH_PlayerInfo* PlayerInfo, const FDateTime& FirstValidTime);
	void ClearOrderWatchForPlayer(URH_PlayerInfo* PlayerInfo);

    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    UFUNCTION(BlueprintCallable)
    URHOrder* GetNextOrder();

    // Creates and fires an order for a specific item, can be called from screens in case we want to re-pop an order
    UFUNCTION(BlueprintCallable)
    void CreateOrderForItem(URHStoreItem* StoreItem, const URH_PlayerInfo* PlayerInfo);

	void CreateOrderForItem(URHStoreItem* StoreItem, const URH_PlayerInfo* PlayerInfo, int32 Quantity, const FGuid& PurchaseRefId, TOptional<ERHAPI_InventoryBucket> eInventoryBucket);
	
	// Checks the rules for view redirection to an order as well as having at least one
	bool CanViewRedirectToOrder(TArray<ERHOrderType> ValidTypes);

    // Delegate that fires when an order is ready for display
    UPROPERTY(BlueprintAssignable, Category = "Order Manager")
    FOnOrderReady OnOrderReady;

	FOnInvalidVoucherObtained OnInvalidVoucherObtained;

	// Delegate that fires for any successful order
	UPROPERTY(BlueprintAssignable, Category = "Order Manager")
	FRH_OnOrderSuccess OnOrderSuccess;

	// Delegate that fires for any failed order
	UPROPERTY(BlueprintAssignable, Category = "Order Manager")
	FRH_OnOrderFailed OnOrderFailed;

	// Called whenever we find a new order to process, can be from callback or pushed by a direct purchase response
	void OnPlayerOrder(const TArray<FRHAPI_PlayerOrder>& OrderResults, const URH_PlayerInfo* PlayerInfo);

protected:

	// Called whenever a purchase has completed in the StoreItemHelper
	void OnPendingPurchaseReceived(const URH_PlayerInfo* PlayerInfo, const FRHAPI_PlayerOrder& PlayerOrderResult);

    // Checks various rules to tell if we currently can push a new order or if it goes on the queue
    bool CanDisplayOrder();

	// Shared display order checks between View Redirection and normal displaying
	bool CanDisplayOrderSharedCheck();

    //Checks if we can add this store item to a pending order or if we should make a new order
    bool CanAddItemToPendingOrder(URHStoreItem* StoreItem, const FGuid& PurchaseRefId) const;

    //Returns the type of order that this store item is
    ERHOrderType GetItemOrderType(URHStoreItem* StoreItem) const;

    // Attempts to display the next order. Blocks further orders if one was found.
    void DisplayNextOrder();

    // Queued Orders, as orders come in we only show one at a time as players choose to consume them.
    UPROPERTY(Transient)
    TArray<URHOrder*> QueuedOrders;

    // A pending order grouping incoming items before being promoted to a real order
    UPROPERTY(Transient)
    URHOrder* PendingOrder;

	// Pointer to the Store Item Helper
	UPROPERTY(Transient)
	URHStoreItemHelper* StoreItemHelper;

	UPROPERTY(Transient)
	TArray<FRH_ActiveOrderWatch> ActiveOrderWatches;
};