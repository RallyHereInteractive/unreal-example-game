// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "GameFramework/RHGameInstance.h"
#include "GameFramework/RHGameUserSettings.h"
#include "Managers/RHOrderManager.h"
#include "Managers/RHStoreItemHelper.h"
#include "Managers/RHUISessionManager.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "Lobby/Widgets/RHWhatsNewModal.h"
#include "Lobby/Widgets/RHHomeScreenWidget.h"


void URHHomeScreenWidget::InitializeWidget_Implementation()
{
	if (MyHud != nullptr)
	{
		if (URH_LocalPlayerSubsystem* LPSS = MyHud->GetLocalPlayerSubsystem())
		{
			if (URH_LocalPlayerLoginSubsystem* LPLS = LPSS->GetLoginSubsystem())
			{
				LPLS->OnCrossplaySettingChanged.AddWeakLambda(this, [this]()
					{
						OnCrossplaySettingChanged();
					});
			}
		}
	}
}

void URHHomeScreenWidget::CheckForOnShownEvents()
{
	if (CheckForVoucherRedemption())
	{
		return;
	}

	if (CheckForWhatsNewModal())
	{
		return;
	}
}

bool URHHomeScreenWidget::CheckForVoucherRedemption()
{
	if (URHOrderManager* OrderManager = MyHud->GetOrderManager())
	{
		if (URHGameInstance* GameInstance = Cast<URHGameInstance>(GetGameInstance()))
		{
			if (URHUISessionManager* SessionManager = GameInstance->GetUISessionManager())
			{
				if (SessionManager->ShouldCheckForVouchers(MyHud->GetLocalPlayerInfo()))
				{
					if (URHStoreItemHelper* StoreItemHelper = MyHud->GetItemHelper())
					{
						TArray<URHStoreItem*> StoreItems;
						TArray<URHStoreItem*> VoucherItems;

						SessionManager->SetHasCheckedForVouchers(MyHud->GetLocalPlayerInfo());

						StoreItemHelper->GetAffordableVoucherItems(MyHud->GetLocalPlayerInfo(), FRH_GetAffordableItemsInVendorDelegate::CreateWeakLambda(this, [this, OrderManager, SessionManager](TArray<URHStoreItem*> StoreItems, TArray<URHStoreItem*> VoucherItems)
							{
								if (VoucherItems.Num() > 0)
								{
									for (int32 i = 0; i < VoucherItems.Num(); ++i)
									{
										OrderManager->CreateOrderForItem(VoucherItems[i], MyHud->GetLocalPlayerInfo());
									}
								}
								else
								{
									CheckForOnShownEvents();
								}
							}));

						return true;
					}
				}
			}
		}
	}

	return false;
}

bool URHHomeScreenWidget::CheckForWhatsNewModal()
{
	TArray<URHJsonData*> PanelsToCheck;

	if (ARHLobbyHUD* LobbyHud = Cast<ARHLobbyHUD>(MyHud))
	{
		if (URHUISessionManager* SessionManager = LobbyHud->GetUISessionManager())
		{
			if (SessionManager->ShouldShowNewsPanel(LobbyHud->GetLocalPlayerInfo()))
			{
				if (URHJsonDataFactory* pJsonDataFactory = Cast<URHJsonDataFactory>(LobbyHud->GetJsonDataFactory()))
				{
					TSharedPtr<FJsonObject> LandingPanelJson = pJsonDataFactory->GetJsonPanelByName(TEXT("landingpanel"));

					if (LandingPanelJson.IsValid())
					{
						const TSharedPtr<FJsonObject>* WhatsNewObj;

						if (LandingPanelJson.Get()->TryGetObjectField(TEXT("whatsNewPanel"), WhatsNewObj))
						{
							// Check panel data, only proceed if there is a valid imageUrl
							const TArray<TSharedPtr<FJsonValue>>* WhatsNewPanelContents;

							if ((*WhatsNewObj)->TryGetArrayField(TEXT("content"), WhatsNewPanelContents))
							{
								TArray<TSharedPtr<FJsonValue>> SortedPanelContents = *WhatsNewPanelContents;

								for (TSharedPtr<FJsonValue> WhatsNewPanels : SortedPanelContents)
								{
									const TSharedPtr<FJsonObject>* WhatsNewPanelObj;

									if (WhatsNewPanels->TryGetObject(WhatsNewPanelObj))
									{
										if (URHWhatsNewPanel* Panel = NewObject<URHWhatsNewPanel>())
										{
											// Only show if panel should show on this platform
											pJsonDataFactory->LoadData(Panel, WhatsNewPanelObj);
											const TSharedPtr<FJsonObject>* ObjectField;
											FString PanelIdString = "";

											if ((*WhatsNewPanelObj)->TryGetObjectField(TEXT("imageUrl"), ObjectField))
											{
												Panel->Image = pJsonDataFactory->GetTextureByRemoteURL(ObjectField);

												// Only show if there is a valid image
												if (Panel->Image)
												{
													int32 VersionNumber;
													bool ShowOnlyIfNew;

													if ((*WhatsNewObj)->TryGetBoolField(TEXT("showOnlyIfNew"), ShowOnlyIfNew))
													{
														if (ShowOnlyIfNew)
														{
															// Check if we have a new version
															if ((*WhatsNewObj)->TryGetNumberField(TEXT("version"), VersionNumber))
															{
																if (URHGameUserSettings* const pRHGameSettings = Cast<URHGameUserSettings>(GEngine->GetGameUserSettings()))
																{
																	if (VersionNumber > pRHGameSettings->GetWhatsNewVersion())
																	{
																		pRHGameSettings->SaveWhatsNewVersion(VersionNumber);
																		pRHGameSettings->SaveSettings();
																		PanelsToCheck.Add(Panel);
																	}
																}
															}
														}
														else
														{
															PanelsToCheck.Add(Panel);
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}

					// Clear the session manager for this login of the news seen
					SessionManager->SetNewsPanelSeen(LobbyHud->GetLocalPlayerInfo());

					pJsonDataFactory->CheckShouldShowForPlayer(PanelsToCheck, LobbyHud->GetLocalPlayerInfo(), FOnGetShouldShowPanels::CreateLambda([this](FRHShouldShowPanelsWrapper Wrapper)
						{
							for (const auto& pair : Wrapper.ShouldShowByPanel)
							{
								if (pair.Value)
								{
									AddViewRoute("WhatsNew");
									return;
								}
							}
						
							CheckForOnShownEvents();
						}));

					return true;
				}
			}
		}
	}

	return false;
}
