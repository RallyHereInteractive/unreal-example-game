// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "Managers/RHJsonDataFactory.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "Lobby/Widgets/RHNewsRotatorWidget.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"

void URHNewsRotatorWidget::InitializeWidget_Implementation()
{
    Super::InitializeWidget_Implementation();

    if (URHJsonDataFactory* pJsonDataFactory = GetJsonDataFactory())
    {
        pJsonDataFactory->JsonPanelUpdated.AddDynamic(this, &URHNewsRotatorWidget::OnJsonChanged);
    }
}

void URHNewsRotatorWidget::UninitializeWidget_Implementation()
{
    Super::UninitializeWidget_Implementation();

    if (URHJsonDataFactory* pJsonDataFactory = GetJsonDataFactory())
    {
        pJsonDataFactory->JsonPanelUpdated.RemoveDynamic(this, &URHNewsRotatorWidget::OnJsonChanged);
    }
}

void URHNewsRotatorWidget::GetPanelDataAsync(FOnGetNewsRotatorPanelsBlock Delegate /*= FOnGetNewsRotatorPanelsBlock()*/)
{
    TArray<URHNewsRotatorData*> Panels;

    if (URHJsonDataFactory* pJsonDataFactory = GetJsonDataFactory())
    {
        TSharedPtr<FJsonObject> LandingPanelJson = pJsonDataFactory->GetJsonPanelByName(TEXT("landingpanel"));

        if (LandingPanelJson.IsValid())
        {
            const TSharedPtr<FJsonObject>* JsonSectionObj;

            if (LandingPanelJson.Get()->TryGetObjectField(JsonSection, JsonSectionObj))
            {
                const TArray<TSharedPtr<FJsonValue>>* JsonSectionContents;

                if ((*JsonSectionObj)->TryGetArrayField(TEXT("content"), JsonSectionContents))
                {
                    for (TSharedPtr<FJsonValue> JsonContents : *JsonSectionContents)
                    {
                        const TSharedPtr<FJsonObject>* JsonRotatorObj;

                        if (JsonContents->TryGetObject(JsonRotatorObj))
                        {
                            if (URHNewsRotatorData* Panel = NewObject<URHNewsRotatorData>())
                            {
                                const TSharedPtr<FJsonObject>* ObjectField;
                                int32 NumValue;

                                if ((*JsonRotatorObj)->TryGetNumberField(TEXT("type"), NumValue))
                                {
                                    Panel->PanelAction = (ENewsActions)NumValue;
                                }

                                if ((*JsonRotatorObj)->TryGetObjectField(TEXT("actionDetails"), ObjectField))
                                {
                                    Panel->ActionDetails = URHJsonDataFactory::GetLocalizedStringFromObject(ObjectField);
                                }

								if ((*JsonRotatorObj)->TryGetObjectField(TEXT("headerText"), ObjectField))
								{
									Panel->Header = FText::FromString(URHJsonDataFactory::GetLocalizedStringFromObject(ObjectField));
								}

								if ((*JsonRotatorObj)->TryGetObjectField(TEXT("bodyText"), ObjectField))
								{
									Panel->Body = FText::FromString(URHJsonDataFactory::GetLocalizedStringFromObject(ObjectField));
								}

                                if ((*JsonRotatorObj)->TryGetObjectField(TEXT("imageUrl"), ObjectField))
                                {
                                    Panel->Image = pJsonDataFactory->GetTextureByRemoteURL(ObjectField);

									// If the URL was invalid (404) we didn't save the texture, so skip adding this panel
									if (!Panel->Image)
									{
										continue;
									}
                                }

								(*JsonRotatorObj)->TryGetBoolField(TEXT("trackClicks"), Panel->bTrackClicks);
								if (!(*JsonRotatorObj)->TryGetNumberField(TEXT("numCohortGroups"), Panel->NumGroups))
								{
									Panel->NumGroups = INDEX_NONE;
								}
								if (!(*JsonRotatorObj)->TryGetNumberField(TEXT("cohortGroupToShowFor"), Panel->GroupToShowFor))
								{
									Panel->GroupToShowFor = INDEX_NONE;
								}

                                pJsonDataFactory->LoadData(Panel, JsonRotatorObj);

                                Panels.Push(Panel);
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        UE_LOG(RallyHereStart, Warning, TEXT("URHNewsRotatorWidget::GetPanelData failed to get JsonDataFactory -- delegates will not be bound."));
    }

	CheckShouldShowPanels(Panels, Delegate);
}

void URHNewsRotatorWidget::CheckShouldShowPanels(TArray<URHNewsRotatorData*> Panels, FOnGetNewsRotatorPanelsBlock Delegate /*= FOnGetNewsRotatorPanelsBlock()*/)
{
	if (URH_PlayerInfo* LocalPlayerInfo = MyHud->GetLocalPlayerInfo())
	{
		if (URHJsonDataFactory* pJsonDataFactory = GetJsonDataFactory())
		{
			TArray<URHJsonData*> PanelsToCheck;

			for (URHNewsRotatorData* Panel : Panels)
			{
				// if not tracking clicks OR failed to get group number info for version testing OR there's only 1 group for whatever reason then just show to everyone
				if (!Panel->bTrackClicks || Panel->NumGroups <= 1 || Panel->GroupToShowFor == INDEX_NONE)
				{
					PanelsToCheck.Add(Panel);
				}
				else
				{
					if (Panel->NumGroups > 1 && Panel->GroupToShowFor > 0)
					{
						int32 LocalPlayerCohortGroup = URHUIBlueprintFunctionLibrary::GetPlayerCohortGroup(LocalPlayerInfo, Panel->NumGroups);

						// group in Panel starts at 1
						if (LocalPlayerCohortGroup == (Panel->GroupToShowFor - 1))
						{
							PanelsToCheck.Add(Panel);
						}
					}
				}
			}

			pJsonDataFactory->CheckShouldShowForPlayer(PanelsToCheck, LocalPlayerInfo, FOnGetShouldShowPanels::CreateLambda([this, Delegate](FRHShouldShowPanelsWrapper Wrapper)
				{
					TArray<URHNewsRotatorData*> DataToShow;
					for (const auto& pair : Wrapper.ShouldShowByPanel)
					{
						if (pair.Value)
						{
							DataToShow.Add(Cast<URHNewsRotatorData>(pair.Key));
						}
					}
					Delegate.ExecuteIfBound(DataToShow);
				}));
		}
	}
}

void URHNewsRotatorWidget::OnNewsPanelClicked(URHNewsRotatorData* Panel)
{
	if (!Panel)
	{
		return;
	}

	switch (Panel->PanelAction)
	{
		case ENewsActions::ENewsActions_NavToRoute:
			AddViewRoute(FName(*Panel->ActionDetails));
			break;
		case ENewsActions::ENewsActions_ExternalURL:
			FPlatformProcess::LaunchURL(*Panel->ActionDetails, nullptr, nullptr);
			break;
		case ENewsActions::ENewsActions_NavToStoreItem:
		{
			FRH_ItemId ItemId = FCString::Atoi(*Panel->ActionDetails);

			if (UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized()))
			{
				TSoftObjectPtr<UPlatformInventoryItem> SoftItemPtr = pAssetManager->GetSoftPrimaryAssetByItemId<UPlatformInventoryItem>(ItemId);
				if (SoftItemPtr.IsValid())
				{
					AddViewRoute("Store", false, false, SoftItemPtr.Get());
				}
			}
			break;
		}
	}
}

URHJsonDataFactory* URHNewsRotatorWidget::GetJsonDataFactory()
{
    if (MyHud.IsValid())
    {
        if (ARHLobbyHUD* LobbyHud = Cast<ARHLobbyHUD>(MyHud))
        {
            return Cast<URHJsonDataFactory>(LobbyHud->GetJsonDataFactory());
        }
        else
        {
            UE_LOG(RallyHereStart, Warning, TEXT("URHNewsRotatorWidget::GetJsonDataFactory Warning: MyHud failed to cast to ARHLobbyHUD."));
        }
    }
    else
    {
        UE_LOG(RallyHereStart, Warning, TEXT("URHNewsRotatorWidget::GetJsonDataFactory Warning: MyHud is not currently valid."));
    }

    return nullptr;
}
