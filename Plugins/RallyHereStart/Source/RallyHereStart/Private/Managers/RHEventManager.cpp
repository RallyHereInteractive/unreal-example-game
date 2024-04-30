
#include "Managers/RHStoreItemHelper.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "GameFramework/RHGameInstance.h"
#include "RH_GameInstanceSubsystem.h"
#include "Managers/RHEventManager.h"

void URHEventManager::Initialize()
{
	if (EventManagerDataTableClassName.ToString().Len() > 0)
	{
		EventsDataDT = LoadObject<UDataTable>(nullptr, *EventManagerDataTableClassName.ToString(), nullptr, LOAD_None, nullptr);
	}

	bEventsInitialized = false;

	if (UWorld* World = GetWorld())
	{
		if (URHGameInstance* GameInstance = Cast<URHGameInstance>(World->GetGameInstance()))
		{
			GameInstance->OnLocalPlayerLoginChanged.AddUObject(this, &URHEventManager::OnLoginPlayerChanged);
		}
	}
}

void URHEventManager::Uninitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (URHGameInstance* GameInstance = Cast<URHGameInstance>(World->GetGameInstance()))
		{
			GameInstance->OnLocalPlayerLoginChanged.RemoveAll(this);
		}
	}
}

void URHEventManager::OnLoginPlayerChanged(ULocalPlayer* LocalPlayer)
{
	if (!bEventsInitialized)
	{
		TArray<int32> VendorIdsToRequest;

		if (EventsDataDT != nullptr)
		{
			for (FName EventTag : EventsDataDT->GetRowNames())
			{
				// Only check active events so we don't try and load everything
				if (IsEventActive(EventTag))
				{
					if (FRHEventData* EventRow = EventsDataDT->FindRow<FRHEventData>(EventTag, "Load Activity to get its required loot table Ids"))
					{
						if (URHEvent* Event = EventRow->DataObject.LoadSynchronous())
						{
							Event->AppendRequiredLootTableIds(VendorIdsToRequest);
						}
					}
				}
			}
		}

		if (VendorIdsToRequest.Num() > 0)
		{
			if (URHStoreItemHelper* StoreItemHelper = URHUIBlueprintFunctionLibrary::GetStoreItemHelper(this))
			{
				StoreItemHelper->RequestVendorData(VendorIdsToRequest);
			}
		}

		bEventsInitialized = true;
	}
}

URHEvent* URHEventManager::GetEventByTag(FName EventTag) const
{
	if (EventsDataDT != nullptr)
	{
		if (FRHEventData* EventRow = EventsDataDT->FindRow<FRHEventData>(EventTag, "Load Requested Event"))
		{
			URHEvent* Event = EventRow->DataObject.IsValid() ? EventRow->DataObject.Get() : EventRow->DataObject.LoadSynchronous();
			
			if (Event != nullptr)
			{
				Event->EventTag = EventTag;
				return Event;
			}
		}
	}

	return nullptr;
}

bool URHEventManager::IsEventActive(FName EventTag) const
{
	if (IsEventPastStartDate(EventTag) && IsEventBeforeEndDate(EventTag))
	{
		return true;
	}

	return false;
}

bool URHEventManager::IsEventPastStartDate(FName EventTag) const
{
	FDateTime StartTime = GetEventStartTime(EventTag);
	FDateTime CurrentTime = GetCurrentTime();

	if (StartTime.GetTicks() > 0 && CurrentTime.GetTicks() > 0)
	{
		if (StartTime <= CurrentTime)
		{
			return true;
		}
	}

	return false;
}

bool URHEventManager::IsEventBeforeEndDate(FName EventTag) const
{
	FDateTime EndTime = GetEventEndTime(EventTag);
	FDateTime CurrentTime = GetCurrentTime();

	if (EndTime.GetTicks() > 0 && CurrentTime.GetTicks() > 0)
	{
		if (CurrentTime < EndTime)
		{
			return true;
		}
	}

	return false;
}

FDateTime URHEventManager::GetCurrentTime() const
{
	return FDateTime::UtcNow();
}

FDateTime URHEventManager::GetEventStartTime(FName EventTag) const
{
	FDateTime TimeStart;
	FString TimeStartStr;
	if (URH_ConfigSubsystem* ConfigSubsystem = GetConfigSubsystem())
	{
		FString ConfigEntryNameStartTime = EventTag.ToString() + ".StartTime";
		ConfigSubsystem->GetKV(ConfigEntryNameStartTime, TimeStartStr);
		RallyHereAPI::ParseDateTime(*TimeStartStr, TimeStart);
	}
	
	return TimeStart;
}

FDateTime URHEventManager::GetEventEndTime(FName EventTag) const
{
	FDateTime TimeEnd;
	FString TimeEndStr;
	if (URH_ConfigSubsystem* ConfigSubsystem = GetConfigSubsystem())
	{
		FString ConfigEntryNameEndTime = EventTag.ToString() + ".EndTime";
		ConfigSubsystem->GetKV(ConfigEntryNameEndTime, TimeEndStr);
		RallyHereAPI::ParseDateTime(*TimeEndStr, TimeEnd);
	}
	
	return TimeEnd;
}

URH_ConfigSubsystem* URHEventManager::GetConfigSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (URH_GameInstanceSubsystem* pGISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
			{
				return pGISS->GetConfigSubsystem();
			}
		}
	}

	return nullptr;
}