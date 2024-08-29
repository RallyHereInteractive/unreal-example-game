// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Managers/RHJsonDataFactory.h"
#include "GameFramework/RHGameInstance.h"
#include "GameFramework/RHLandingPanelJSONHandler.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "IImageWrapper.h"
#include "RenderingThread.h"

// ripped from async task DownloadImage
static void RHJsonFactory_WriteRawToTexture_RenderThread(FTexture2DDynamicResource* TextureResource, const TArray<uint8>& RawData, bool bUseSRGB = true)
{
    check(IsInRenderingThread());

    FRHITexture2D* TextureRHI = TextureResource->GetTexture2DRHI();

    int32 Width = TextureRHI->GetSizeX();
    int32 Height = TextureRHI->GetSizeY();

    uint32 DestStride = 0;
    uint8* DestData = reinterpret_cast<uint8*>(RHILockTexture2D(TextureRHI, 0, RLM_WriteOnly, DestStride, false, false));

    for (int32 y = 0; y < Height; y++)
    {
        uint8* DestPtr = &DestData[(Height - 1 - y) * DestStride];

        const FColor* SrcPtr = &((FColor*)(RawData.GetData()))[(Height - 1 - y) * Width];
        for (int32 x = 0; x < Width; x++)
        {
            *DestPtr++ = SrcPtr->B;
            *DestPtr++ = SrcPtr->G;
            *DestPtr++ = SrcPtr->R;
            *DestPtr++ = SrcPtr->A;
            SrcPtr++;
        }
    }

    RHIUnlockTexture2D(TextureRHI, 0, false, false);
}

URHStoreItemHelper* GetStoreHelper(const UObject* WorldContextObject)
{
	if (WorldContextObject != nullptr)
	{
		if (UWorld* const World = WorldContextObject->GetWorld())
		{
			if (URHGameInstance* GameInstance = Cast<URHGameInstance>(World->GetGameInstance()))
			{
				return GameInstance->GetStoreItemHelper();
			}
		}
	}

	return nullptr;
}

void URHJsonDataFactory::Initialize(ARHHUDCommon* InHud)
{
    Super::Initialize(InHud);

	if (PlayerProgressionXpClass.IsValid())
	{
		PlayerXpProgression = TSoftObjectPtr<URHProgression>(PlayerProgressionXpClass);

		if (!PlayerXpProgression.IsValid())
		{
			UAssetManager::GetStreamableManager().RequestAsyncLoad(PlayerXpProgression.ToSoftObjectPath());
		}
	}

	if (IsLoggedIn())
	{
		TryLoadLandingPanels();
	}
	else if (GameInstance.IsValid())
	{
		GameInstance->OnLocalPlayerLoginChanged.AddUObject(this, &URHJsonDataFactory::OnLocalPlayerLoginChanged);
	}
}

void URHJsonDataFactory::Uninitialize()
{
    Super::Uninitialize();
}

bool URHJsonDataFactory::IsLoggedIn()
{
	if (GameInstance.IsValid())
	{
		for (const auto& LocalPlayer : GameInstance->GetLocalPlayers())
		{
			if (URH_LocalPlayerSubsystem* LPSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
			{
				if (LPSS->IsLoggedIn())
				{
					return true;
				}
			}
		}
	}

	return Super::IsLoggedIn();
}

void URHJsonDataFactory::HandleJsonReady(URHLandingPanelJSONHandler* pHandler)
{
    if (pHandler)
    {
        JsonPanels.Add(pHandler->GetName(), pHandler->GetJsonObject());

    	pHandler->OnJsonReady.RemoveDynamic(this, &URHJsonDataFactory::HandleJsonReady);
    	JsonPanelUpdated.Broadcast(pHandler->GetName());
    }
}

void URHJsonDataFactory::HandleImagesReady(URHLandingPanelJSONHandler* pHandler)
{
    if (pHandler)
    {
		MapFilePathToTexture.Append(pHandler->GetFilePathToTextureMap());
		MapRemoteUrlToFilePath.Append(pHandler->GetRemoteUrlToFilePathMap());

    	pHandler->OnImagesDownloaded.RemoveDynamic(this, &URHJsonDataFactory::HandleImagesReady);
    	JsonPanelUpdated.Broadcast(pHandler->GetName());
    }
}

TSharedPtr<FJsonObject> URHJsonDataFactory::GetJsonPanelByName(const FString& name)
{
    if (auto findItem = JsonPanels.Find(name))
    {
        return *findItem;
    }

    return nullptr;
}

FString URHJsonDataFactory::GetLocalizedStringFromObject(const TSharedPtr<FJsonObject>* StringObject)
{
    FString OutString = "";

    if (StringObject)
    {
        static const FString DefaultLocal = "default";

        if (!(*StringObject)->TryGetStringField(FInternationalization::Get().GetCurrentCulture()->GetThreeLetterISOLanguageName(), OutString))
        {
            (*StringObject)->TryGetStringField(DefaultLocal, OutString);
        }
    }

    return OutString;
}

FName URHJsonDataFactory::GetNameFromObject(const TSharedPtr<FJsonObject>* NameObject)
{
	FName OutName = NAME_None;
	FString StringName = "";

	if((*NameObject)->TryGetStringField(TEXT("id"), StringName))
	{
		OutName = FName(*StringName);
	}

	return OutName;
}

UTexture2DDynamic* URHJsonDataFactory::GetTextureByRemoteURL(const TSharedPtr<FJsonObject>* ImageUrlJson)
{
    if (ImageUrlJson)
    {
        FString ImageDownloadPath;
        static const FString DefaultLocal = "INT";
        FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetThreeLetterISOLanguageName();

        if (!(*ImageUrlJson)->TryGetStringField(CurrentCulture, ImageDownloadPath))
        {
            (*ImageUrlJson)->TryGetStringField(DefaultLocal, ImageDownloadPath);
        }

        FString* FoundFileLocation = MapRemoteUrlToFilePath.Find(ImageDownloadPath);
        if (FoundFileLocation != nullptr)
        {
            FString FileLocation = *FoundFileLocation;

			// Check the force loaded textures from URHLandingPanelJSONHandler
			UTexture2DDynamic** FoundTexture = MapFilePathToTexture.Find(FileLocation);
			if (FoundTexture && *FoundTexture)
			{
				return *FoundTexture;
			}

			// Check to see if we have already lazy loaded this texture
			TWeakObjectPtr<UTexture2DDynamic>* WeakTexture = FilePathToWeakTexture.Find(FileLocation);
			UTexture2DDynamic* CachedTexture = WeakTexture ? WeakTexture->Get() : nullptr;
			if (CachedTexture)
			{
				return CachedTexture;
			}

			// No hits, try and load it
            TArray<uint8> pByteArray;
            if (FFileHelper::LoadFileToArray(pByteArray, *FileLocation))
            {
                return LoadTexture(pByteArray.GetData(), pByteArray.Num(), FileLocation);
            }
        }
    }

    return nullptr;
}

UTexture2DDynamic* URHJsonDataFactory::LoadTexture(const uint8* contentData, int32 contentLength, FString strSaveFilePath)
{
    // see if we can create a texture out of this
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrappers[3] =
    {
        ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG),
        ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG),
        ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP),
    };

    for (auto ImageWrapper : ImageWrappers)
    {
        if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(contentData, contentLength))
        {
            TArray<uint8> RawData;
            if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
            {
                if (UTexture2DDynamic* Texture = UTexture2DDynamic::Create(ImageWrapper->GetWidth(), ImageWrapper->GetHeight()))
                {
                    Texture->SRGB = true;

					if (UGameplayStatics::GetPlatformName() == TEXT("Switch"))
					{
						// Fix the texture tiling settings for Switch so they render properly
						Texture->bNoTiling = false;
					}

                    Texture->UpdateResource();

                    FTexture2DDynamicResource* TextureResource = static_cast<FTexture2DDynamicResource*>(Texture->GetResource());
                    TArray<uint8> RawDataCopy = RawData;
                    ENQUEUE_RENDER_COMMAND(FWriteRawDataToTexture)(
                        [TextureResource, RawDataCopy](FRHICommandListImmediate& RHICmdList)
                    {
                        RHJsonFactory_WriteRawToTexture_RenderThread(TextureResource, RawDataCopy);
                    });

					FilePathToWeakTexture.Add(strSaveFilePath, Texture);

                    return Texture;
                }
            }
        }
    }

    return nullptr;
}

void URHJsonDataFactory::LoadData(URHJsonData* JsonData, const TSharedPtr<FJsonObject>* JsonObject)
{
	FString UniqueIdString = "";
	if ((*JsonObject)->TryGetStringField(TEXT("id"), UniqueIdString))
	{
		JsonData->UniqueId = FName(*UniqueIdString);
	}

	const TArray<TSharedPtr<FJsonValue>>* HideIfItemOwned;
	if ((*JsonObject)->TryGetArrayField(TEXT("hideIfItemOwned"), HideIfItemOwned))
	{
		for (const TSharedPtr<FJsonValue>& JsonValue : *HideIfItemOwned)
		{
			int32 itemId;
			if (JsonValue->TryGetNumber(itemId))
			{
				JsonData->HideIfItemOwned.Add(itemId);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ShowIfItemOwned;
	if ((*JsonObject)->TryGetArrayField(TEXT("showIfItemOwned"), ShowIfItemOwned))
	{
		for (const TSharedPtr<FJsonValue>& JsonValue : *ShowIfItemOwned)
		{
			int32 itemId;
			if (JsonValue->TryGetNumber(itemId))
			{
				JsonData->ShowIfItemOwned.Add(itemId);
			}
		}
	}

	(*JsonObject)->TryGetNumberField(TEXT("lootTableId"), JsonData->AssociatedLootId);

	if (!(*JsonObject)->TryGetNumberField(TEXT("minLevel"), JsonData->MinLevel))
	{
		JsonData->MinLevel = INDEX_NONE;
	}
	if (!(*JsonObject)->TryGetNumberField(TEXT("maxLevel"), JsonData->MaxLevel))
	{
		JsonData->MaxLevel = INDEX_NONE;
	}

	FString DateTime;
	if ((*JsonObject)->TryGetStringField(TEXT("startTime"), DateTime))
	{
		FDateTime::ParseIso8601(*DateTime, JsonData->StartTime);
	}
	if ((*JsonObject)->TryGetStringField(TEXT("endTime"), DateTime))
	{
		FDateTime::ParseIso8601(*DateTime, JsonData->EndTime);
	}

	(*JsonObject)->TryGetBoolField(TEXT("showSteam"), JsonData->showSteam);
	(*JsonObject)->TryGetBoolField(TEXT("showEpic"), JsonData->showEpic);
	(*JsonObject)->TryGetBoolField(TEXT("showPS4"), JsonData->showPS4);
	(*JsonObject)->TryGetBoolField(TEXT("showPS5"), JsonData->showPS5);
	(*JsonObject)->TryGetBoolField(TEXT("showXB1"), JsonData->showXB1);
	(*JsonObject)->TryGetBoolField(TEXT("showXSX"), JsonData->showXSX);
	(*JsonObject)->TryGetBoolField(TEXT("showNX"), JsonData->showNX);
	(*JsonObject)->TryGetBoolField(TEXT("showIOS"), JsonData->showIOS);
	(*JsonObject)->TryGetBoolField(TEXT("showAndroid"), JsonData->showAndroid);
}

void URHJsonDataFactory::OnInventoryItemsUpdated(const TArray<int32>& UpdatedInventoryIds, URH_PlayerInfo* PlayerInfo)
{
    UE_LOG(RallyHereStart, Log, TEXT("URHJsonDataFactory::OnInventoryItemsUpdated"));

	TSet<int32> RelevantItemIds;
	if (const auto& FoundJsonDataWrapper = CachedJsonDataByPlayer.Find(PlayerInfo))
	{
		for (const URHJsonData* JsonData : FoundJsonDataWrapper->JsonDataSet)
		{
			if (JsonData->HideIfOwned && JsonData->AssociatedLootId > 0)
			{
				RelevantItemIds.Add(JsonData->AssociatedLootId);
			}

			for (const int32& ItemId : JsonData->HideIfItemOwned)
			{
				RelevantItemIds.Add(ItemId);
			}

			for (const int32& ItemId : JsonData->ShowIfItemOwned)
			{
				RelevantItemIds.Add(ItemId);
			}

			if (PlayerXpProgression.IsValid())
			{
				RelevantItemIds.Add(PlayerXpProgression->GetItemId());
			}
		}
	}

	for (const int32& UpdatedId : UpdatedInventoryIds)
	{
		if (RelevantItemIds.Contains(UpdatedId))
		{
			JsonPanelUpdated.Broadcast(TEXT("itemsUpdated"));
			break;
		}
	}	
}

void URHJsonDataFactory::CheckShouldShowForPlayer(URHJsonData* JsonData, URH_PlayerInfo* PlayerInfo, FOnShouldShow Delegate, bool bCheckPlatform)
{
	// null check
	if (JsonData == nullptr)
	{
		Delegate.Execute(false);
		return;
	}
	if (PlayerInfo == nullptr)
	{
		Delegate.Execute(false);
		return;
	}

	// Make sure Player is registered for Inventory Item Updated callbacks
	if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
	{
		if (!CachedJsonDataByPlayer.Contains(PlayerInfo))
		{
			PlayerInventory->OnInventoryCacheUpdated.AddUObject(this, &URHJsonDataFactory::OnInventoryItemsUpdated);
		}

		CachedJsonDataByPlayer.FindOrAdd(PlayerInfo).JsonDataSet.Add(JsonData);
	}

	// Check for time window
	FDateTime NowTime = FDateTime::UtcNow();
	
	if (JsonData->StartTime.GetTicks() > 0)
	{
		if (NowTime < JsonData->StartTime)
		{
			Delegate.Execute(false);
			return;
		}
	}
	if (JsonData->EndTime.GetTicks() > 0)
	{
		if (NowTime > JsonData->EndTime)
		{
			Delegate.Execute(false);
			return;
		}
	}

	// Check for platform
	if (bCheckPlatform)
	{
		bool isSteam = false;
		bool isEpic = false;

		if (GameInstance.IsValid())
		{
			for (const auto& LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (URH_LocalPlayerSubsystem* LPSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
				{
					isSteam |= LPSS->GetPlayerPlatformId().PlatformType == ERHAPI_Platform::Steam;
					isEpic |= LPSS->GetPlayerPlatformId().PlatformType == ERHAPI_Platform::Epic;
				}
			}
		}

		const FString PlatformName = UGameplayStatics::GetPlatformName();
		const bool isPc = PLATFORM_DESKTOP;
		const bool isPS4 = PlatformName == TEXT("PS4");
		const bool isPS5 = PlatformName == TEXT("PS5");
		const bool isXB1 = PlatformName == TEXT("XboxOne") || TEXT("XB1");
		const bool isXSX = PlatformName == TEXT("XSX");
		const bool isSwitch = PlatformName == TEXT("Switch");
		const bool isIOS = PlatformName == TEXT("IOS");
		const bool isAndroid = PlatformName == TEXT("Android");

		if (isSteam && !JsonData->showSteam)
		{
			Delegate.Execute(false);
			return;
		}
		else if (isEpic && !JsonData->showEpic)
		{
			Delegate.Execute(false);
			return;
		}
#if !UE_BUILD_SHIPPING // for oss=anon in dev
		else if (isPc && !JsonData->showSteam && !JsonData->showEpic)
		{
			Delegate.Execute(false);
			return;
		}
#endif
		else if (isPS4 && !JsonData->showPS4)
		{
			Delegate.Execute(false);
			return;
		}
		else if (isPS5 && !JsonData->showPS5)
		{
			Delegate.Execute(false);
			return;
		}
		else if (isXB1 && !JsonData->showXB1)
		{
			Delegate.Execute(false);
			return;
		}
		else if (isXSX && !JsonData->showXSX)
		{
			Delegate.Execute(false);
			return;
		}
		else if (isSwitch && !JsonData->showNX)
		{
			Delegate.Execute(false);
			return;
		}
		else if (isIOS && !JsonData->showIOS)
		{
			Delegate.Execute(false);
			return;
		}
		else if (isAndroid && !JsonData->showAndroid)
		{
			Delegate.Execute(false);
			return;
		}
	}

	// Check for required Inventory Counts
	URH_PlayerInventoryCountHelper* Helper = NewObject<URH_PlayerInventoryCountHelper>();
	Helper->PlayerInfo = PlayerInfo;
	
	if (JsonData->HideIfOwned && JsonData->AssociatedLootId > 0)
	{
		Helper->ItemIdsToCheck.Add(JsonData->AssociatedLootId);
	}

	for (const int32& itemId : JsonData->HideIfItemOwned)
	{
		Helper->ItemIdsToCheck.Add(itemId);
	}

	for (const int32& itemId : JsonData->ShowIfItemOwned)
	{
		Helper->ItemIdsToCheck.Add(itemId);
	}

	FRH_ItemId PlayerXpProgressionItemId;
	if (PlayerXpProgression.IsValid())
	{
		PlayerXpProgressionItemId = PlayerXpProgression->GetItemId();
		Helper->ItemIdsToCheck.Add(PlayerXpProgressionItemId);
	}

	Helper->Event = FOnGetInventoryCounts::CreateLambda([this, JsonData, Helper, PlayerXpProgressionItemId, Delegate](FRHInventoryCountWrapper CountWrapper)
		{
			int32 Count = 0;
			if (Helper->Results.InventoryCountsById.Contains(PlayerXpProgressionItemId))
			{
				Count = Helper->Results.InventoryCountsById[PlayerXpProgressionItemId];
			}

			// if somehow player level data is wrong, we should show the thing rather than not
			if (Count > 0)
			{
				if (JsonData->MinLevel != INDEX_NONE)
				{
					if (Count < JsonData->MinLevel)
					{
						Delegate.Execute(false);
						return;
					}
				}

				if (JsonData->MaxLevel != INDEX_NONE)
				{
					if (Count > JsonData->MaxLevel)
					{
						Delegate.Execute(false);
						return;
					}
				}
			}

			TSet<int32> HideIfOwnedSet(JsonData->HideIfItemOwned);
			for (const auto& pair : Helper->Results.InventoryCountsById)
			{
				// check hideifowned mask set
				if (HideIfOwnedSet.Contains(pair.Key) && pair.Value > 0)
				{
					Delegate.Execute(false);
					return;
				}

				// Ignore the PlayerXp Id, since it's always passed into the inventory check
				if (pair.Key == PlayerXpProgressionItemId)
				{
					continue;
				}

				// all other existing results should be checking for ownership
				if (pair.Value <= 0)
				{
					Delegate.Execute(false);
					return;
				}
			}

			Delegate.Execute(true);
		});
	
	Helper->StartCheck();
}

void URHJsonDataFactory::CheckShouldShowForPlayer(TArray<URHJsonData*> pData, URH_PlayerInfo* PlayerInfo, FOnGetShouldShowPanels Delegate, bool bCheckPlatform /*= true*/)
{
	URH_PlayerShouldShowPanelsHelper* Helper = NewObject<URH_PlayerShouldShowPanelsHelper>();
	Helper->PlayerInfo = PlayerInfo;
	Helper->JsonDataFactory = this;
	Helper->Event = Delegate;
	Helper->PanelsToCheck = pData;
	Helper->StartCheck();
}

void URHJsonDataFactory::TryLoadLandingPanels()
{
	// #RHTODO - PLAT-4578 - NEEDS API - Update this to use the News Endpoint through the News Integration API

	if (!GameInstance.IsValid())
	{
		return;
	}

	auto* pGISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
	if (pGISS == nullptr)
	{
		return;
	}

	URH_ConfigSubsystem* pRH_ConfigSubsystem = pGISS->GetConfigSubsystem();
	if (pRH_ConfigSubsystem == nullptr)
	{
		return;
	}

	FString url;
	pRH_ConfigSubsystem->GetKV("JSonConfig.url", url);

	FString ConfigFiles;
	TArray<FString> ConfigFilesArray;
	pRH_ConfigSubsystem->GetKV("JSonConfig.config_files", ConfigFiles);
	ConfigFiles.ParseIntoArray(ConfigFilesArray, TEXT(","), true);

	for (const FString& uniqueName : ConfigFilesArray)
	{
		GameInstance->NameToURLJsonMapping.Add(uniqueName, url);

		// create a json handler instance for this json url
		URHLandingPanelJSONHandler* jsonLPHandler = NewObject<URHLandingPanelJSONHandler>();
		jsonLPHandler->Initialize(uniqueName);
		jsonLPHandler->OnJsonReady.AddDynamic(this, &URHJsonDataFactory::HandleJsonReady);
		jsonLPHandler->OnImagesDownloaded.AddDynamic(this, &URHJsonDataFactory::HandleImagesReady);

		// now that we have the url and we've subscribed to the callback, let's load it
		FString fullUrl = url + uniqueName + TEXT(".json");
		jsonLPHandler->StartLoadJson(fullUrl);
	}
}

void URHJsonDataFactory::OnLocalPlayerLoginChanged(ULocalPlayer* LocalPlayer)
{
	if (URH_LocalPlayerSubsystem* LPSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
	{
		if (LPSS->IsLoggedIn())
		{
			TryLoadLandingPanels();
		}
	}
}

void URH_PlayerShouldShowPanelsHelper::StartCheck()
{
	if (PanelsToCheck.Num() > 0)
	{
		for (URHJsonData* Panel : PanelsToCheck)
		{
			JsonDataFactory->CheckShouldShowForPlayer(Panel, PlayerInfo, FOnShouldShow::CreateUObject(this, &URH_PlayerShouldShowPanelsHelper::OnGetShouldShowResponse, Panel));
		}
	}
	else
	{
		Event.Execute(Results);
		bComplete = true;
	}
}

void URH_PlayerShouldShowPanelsHelper::OnGetShouldShowResponse(bool bShouldShow, URHJsonData* JsonData)
{
	Responses++;

	Results.ShouldShowByPanel.Add(JsonData, bShouldShow);

	if (Responses == PanelsToCheck.Num())
	{
		Event.Execute(Results);
		bComplete = true;
	}
}

void URH_PlayerInventoryCountHelper::StartCheck()
{
	if (URH_PlayerInventory* PlayerInventory = PlayerInfo->GetPlayerInventory())
	{
		for (const auto& ItemId : ItemIdsToCheck)
		{
			PlayerInventory->GetInventoryCount(ItemId, FRH_GetInventoryCountDelegate::CreateUObject(this, &URH_PlayerInventoryCountHelper::OnGetCountResponse, ItemId));
		}
	}
}

void URH_PlayerInventoryCountHelper::OnGetCountResponse(int32 Count, FRH_ItemId ItemId)
{
	Responses++;

	Results.InventoryCountsById.Add(ItemId, Count);

	if (Responses == ItemIdsToCheck.Num())
	{
		Event.Execute(Results);
		bComplete = true;
	}
}
