#include "GameFramework/RHLandingPanelJSONHandler.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "Interfaces/IHttpResponse.h"
#include "Runtime/Launch/Resources/Version.h"

DEFINE_LOG_CATEGORY_STATIC(RHLandingPanel, Warning, All);

static int32 GJsonHandlerAllowCachedImages = 1;
static FAutoConsoleVariableRef CVarJsonHandlerAllowCachedImages(
    TEXT("rh.JsonHandler.AllowCachedImages"),
    GJsonHandlerAllowCachedImages,
    TEXT(" 0: Force downloading json images from http\n")
    TEXT(" 1: Allow json images to be cached on user hard drive"),
    ECVF_Cheat
);

#if PLATFORM_WINDOWS
  #define PATH_SEPARATOR "\\"
#else
  #define PATH_SEPARATOR "/"
#endif


// ripped from async task DownloadImage
static void RHWriteRawToTexture_RenderThread(FTexture2DDynamicResource* TextureResource, const TArray<uint8>& RawData, bool bUseSRGB = true)
{
    check(IsInRenderingThread());

    auto TextureRHI = TextureResource->GetTexture2DRHI();

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

URHLandingPanelJSONHandler::URHLandingPanelJSONHandler(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void URHLandingPanelJSONHandler::Initialize(FString strName)
{
    strLandingPanelName = strName;
    strRawJSON = "";
    bImagesLoaded = false;
    bJSONReady = false;

    // create a new image downloader
    CreateVerifyDownloadDirectories();
}

/* Set up the download directories */
bool URHLandingPanelJSONHandler::CreateVerifyDownloadDirectories()
{
    FString strDownloadDirPath = FPlatformMisc::GamePersistentDownloadDir();
    strDownloadDirPath = FPaths::ConvertRelativePathToFull(strDownloadDirPath);

    FString strBaseDir = strDownloadDirPath.Append(PATH_SEPARATOR).Append("landingpanels");

    strLocalDir = FString(strBaseDir).Append(PATH_SEPARATOR).Append(strLandingPanelName);
    FString strLandingPanelImagePath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("images");
    FString strLandingPanelJSONPath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("json");

    FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetThreeLetterISOLanguageName();
    FString strLanguagePath = FString(strLandingPanelImagePath).Append(PATH_SEPARATOR).Append(CurrentCulture);

#if defined(PLATFORM_XBOXCOMMON) && PLATFORM_XBOXCOMMON
    // Xbox does not allow for directory trees to be fully created at once in the file system, the folders have to be made in order up or it fails horribly
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    bool bCreated = PlatformFile.CreateDirectory(*strBaseDir);

    if (bCreated)
    {
        bCreated = PlatformFile.CreateDirectory(*strLocalDir);
    }

    if (bCreated)
    {
        bCreated = PlatformFile.CreateDirectory(*strLandingPanelImagePath);
    }

    if (bCreated)
    {
        bCreated = PlatformFile.CreateDirectory(*strLandingPanelJSONPath);
    }

    if (bCreated)
    {
        bCreated = PlatformFile.CreateDirectory(*strLanguagePath);
    }

    return bCreated;
#else
    // set up the directories
    if (CreateVerifyFileDirectoryTree(strLandingPanelImagePath) && CreateVerifyFileDirectoryTree(strLandingPanelJSONPath)) {
        return true;
    }
#endif

    return false;
}


/* Remove the download directories */
bool URHLandingPanelJSONHandler::DeleteDirectory(FString dirPath)
{
    // get the file system
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    // set up the directories
    if (PlatformFile.DeleteDirectoryRecursively(*dirPath)) {
        return true;
    }
    return false;
}

/* Create a file system directory or verify that one exists */
bool URHLandingPanelJSONHandler::CreateVerifyFileDirectory(const FString& strDirPath)
{
    // get the file system
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.CreateDirectory(*strDirPath))
    {
        UE_LOG(RHLandingPanel, Warning, TEXT("URHLandingPanelJSONHandler::CreateVerifyFileDirectory failed to create a directory at %s"), *strDirPath);
        return false;
    }
    UE_LOG(RHLandingPanel, Warning, TEXT("URHLandingPanelJSONHandler::CreateVerifyFileDirectory path %s verified"), *strDirPath);
    return true;
}

/* Create a file system directory and any non existent parent dirs or verify that one exists */
bool URHLandingPanelJSONHandler::CreateVerifyFileDirectoryTree(const FString& strDirPath)
{
    // get the file system
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    if (!PlatformFile.CreateDirectoryTree(*strDirPath))
    {
        UE_LOG(RHLandingPanel, Warning, TEXT("URHLandingPanelJSONHandler::CreateVerifyFileDirectoryTree failed to create a directory at %s"), *strDirPath);
        return false;
    }
    UE_LOG(RHLandingPanel, Warning, TEXT("URHLandingPanelJSONHandler::CreateVerifyFileDirectoryTree path %s verified"), *strDirPath);
    return true;
}

// attempts to load and store the JSON data for the given URL
void URHLandingPanelJSONHandler::StartLoadJson(FString strJsonUrl)
{
    //UE_LOG(RHLandingPanel, Warning, TEXT("LandingPanelJSONHandler::StartLoadJson(%s) invoked"), *strJsonUrl);

    strJSONConfigURL = strJsonUrl;

    // allow for a test json override
    FString testJSONPath = FString(strLocalDir).Append("/json/test.json");

    if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*testJSONPath))
    {
        UE_LOG(RHLandingPanel, Warning, TEXT("LandingPanelJSONHandler -- URL %s overridden by test file %s"), *strJsonUrl, *testJSONPath);
        // load the JSON from the test file path
        if (LoadJsonFromFile(testJSONPath, strRawJSON))
        {
            ParseJson();
        }
    }
    else
    {
        if (!strJsonUrl.IsEmpty())
        {
            // try to get the newest version of the JSON based from the given URL
            RequestNewJson();
        }
        else
        {
            // last ditch, try to load what's already there
            ParseJson();
        }
    }
    return;
}

/* Creates an HTTP request for the new JSON and binds a callback */
void URHLandingPanelJSONHandler::RequestNewJson()
{

    if (!strJSONConfigURL.IsEmpty())
    {
        // go get it!
        TSharedRef<IHttpRequest> JSONRequest = FHttpModule::Get().CreateRequest();
        JSONRequest->SetVerb("GET");
        JSONRequest->SetURL(strJSONConfigURL);
        JSONRequest->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));
        JSONRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        JSONRequest->SetHeader(TEXT("Accept-Charset"), TEXT("utf-8"));
        JSONRequest->OnProcessRequestComplete().BindUObject(this, &URHLandingPanelJSONHandler::OnJSONRequestComplete);
        if (!JSONRequest->ProcessRequest())
        {
            // request failed
            UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::RequestNewJson Failed to process request"));
        }
    }
    else
    {
        // last ditch, try to load what's already there
        ParseJson();
    }

}

void URHLandingPanelJSONHandler::OnJSONRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
    {
        FString responseRawJSON = Response->GetContentAsString();
        // dealing with a possible BOM
        if (!responseRawJSON.IsEmpty() && responseRawJSON[0] == (TCHAR)0xFEFF)
        {
            responseRawJSON.RemoveAt(0);
        }

        // first, check if the JSON file already exists
        FString targetFilePath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("json").Append(PATH_SEPARATOR).Append(strLandingPanelName).Append(".json");
        if (LoadJsonFromFile(targetFilePath, strRawJSON))
        {
            // TODO: In the future we will have a file has library JSON that comes down and then it compares to the file hash, then downloads
            //       the new JSON if hashes don't match, for now we are just going to compare the response to the file contents because
            //       there is no point to hash the two and then compare.
            //FMD5Hash::HashFile(*targetFilePath);

            // check version
            if (responseRawJSON != strRawJSON)
            {
                // remove old files, and directories
                FString strLandingPanelImagePath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("images");
                FString strLandingPanelJSONPath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("json");

                if (!DeleteDirectory(strLandingPanelImagePath) || !DeleteDirectory(strLandingPanelJSONPath)) {
                    UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::OnJSONRequestComplete ERROR trouble trouble removing existing download directories"), *targetFilePath);
                }
                // set up the directories (again)
                if (CreateVerifyDownloadDirectories())
                {
                    // store the new json response JSON string in the file system
                    if (SaveJsonToFile(targetFilePath, responseRawJSON))
                    {
                        // set the local raw string to the new one
                        strRawJSON = responseRawJSON;
                    }
                    else
                    {
                        UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::OnJSONRequestComplete ERROR trouble replacing json file at %s"), *targetFilePath);
                    }
                }
                else
                {
                    UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::OnJSONRequestComplete ERROR trouble creating download directories."));
                }
            }
        }
        else
        {
            // set the local raw string to the new one
            strRawJSON = responseRawJSON;
            // store the new json response JSON string in the file system
            if (!SaveJsonToFile(targetFilePath, responseRawJSON))
            {
                UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::OnJSONRequestComplete ERROR trouble saving json file at %s )"), *targetFilePath);
            }
        }
        // flag JSON as ready
        bJSONReady = true;
        // begin parsing it!
        ParseJson();
    }
    else
    {
        // invalid response
        if (Response.IsValid())
        {
            UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::OnJSONRequestComplete Invalid Response (response code %d, response body: %s, success flag: %s)"), Response->GetResponseCode(), *Response->GetContentAsString(), bWasSuccessful ? TEXT("True") : TEXT("False"));
        }
        else if(!bWasSuccessful)
        {
            UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::OnJSONRequestComplete Response was flagged as unsuccessful"));
        }
        else
        {
            UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::OnJSONRequestComplete Invalid Response Pointer"));
        }
    }
    return;
}

/* Deserializes the JSON, stores it in the file system and in a variable for reading */
void URHLandingPanelJSONHandler::ParseJson()
{
    // parse the JSON into an object
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(strRawJSON);
    // attempt to store a reference to the JSON object
    if (FJsonSerializer::Deserialize(Reader, MyJsonObject))
    {
        // get the image urls
        TMap<FString, TArray<FString>> LocalizedImageUrls;
        const FString strImageUrlProperty = TEXT("imageUrl");
        bool bHasImages = false;

        if (FindInJson(LocalizedImageUrls, strImageUrlProperty, MyJsonObject))
        {
            if (LocalizedImageUrls.Num() > 0)
            {
                bHasImages = true;

                // set the default culture
                const FString DefaultCulture = TEXT("INT");
                // get the current culture
                FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetThreeLetterISOLanguageName();
                // TODO: convert the culture code to whatever the landing panel uses, or have Jonny change the landing panel for Tactics
                TArray<FString> downloadURLs;

                // get the result URLs for the current culture matching: 'imageUrl' : { 'en-US' : 'http://someresource.com/image.jpeg' }
                if (LocalizedImageUrls.Find(CurrentCulture) != nullptr)
                {
                    downloadURLs = *LocalizedImageUrls.Find(CurrentCulture);
                }
                // always include the INT fallback URLs, in case we need them
                if (LocalizedImageUrls.Find(DefaultCulture) != nullptr)
                {
                    TArray<FString>* tempArr = LocalizedImageUrls.Find(DefaultCulture);
                    for (auto& StrPath : *tempArr)
                    {
                        if (!downloadURLs.Contains(StrPath))
                        {
                            downloadURLs.Add(StrPath);
                        }
                    }
                }
                // fallback to any plain ole URLs provided by the search matching: 'imageUrl' : 'http://someresource.com/image.jpeg'
                if (downloadURLs.Num() == 0 && LocalizedImageUrls.Find(strImageUrlProperty) != nullptr)
                {
                    downloadURLs = *LocalizedImageUrls.Find(strImageUrlProperty);
                }

                if (downloadURLs.Num() > 0)
                {
                    // save off the references for comparison later
                    imageURLsToDownload = downloadURLs;
                    // download the images
                    DownloadImage(imageURLsToDownload);
                }
            }
        }

        // Broadcast that our JSON is parsed and ready.
        OnJsonReady.Broadcast(this);

        // If we don't need to fetch any images, just report we are ready now
        if (!bHasImages)
        {
            bImagesLoaded = true;
            OnImagesDownloaded.Broadcast(this);
        }
    }
    else
    {
        // invalid JSON response
        UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::ParseJson Invalid JSON received ( strRawJSON : %s )"), *strRawJSON);
    }
    return;
}

/* Attempts to retrieve a JSON string from a system file, setting a string with its contents */
bool URHLandingPanelJSONHandler::LoadJsonFromFile(const FString& strFilePath, FString& JsonString)
{
    // get the file's content
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (PlatformFile.FileExists(*strFilePath))
    {
        if (FFileHelper::LoadFileToString(JsonString, *strFilePath))
        {
            return true;
        }
    }
    return false;
}

/* Stores a copy of the JSON string as a local file */
bool URHLandingPanelJSONHandler::SaveJsonToFile(const FString& strFilePath, FString strJson)
{
    // save the file's content
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (PlatformFile.DirectoryExists(*strLocalDir))
    {
        // set the file's path -- see no reason to not hardcode it for now...
        FString strSaveFilePath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("json").Append(PATH_SEPARATOR).Append(strLandingPanelName).Append(".json");

        // create or overwrite file
        return FFileHelper::SaveStringToFile(strJson, *strSaveFilePath);
    }
    return false;
}

/* Recurses a JSON object and returns the values of properties, as strings, matching the provided string */
bool URHLandingPanelJSONHandler::FindInJson(TMap<FString, TArray<FString>>& Results, FString strNeedle, TSharedPtr<FJsonObject>& jsonObjectHayStack)
{
    TMap<FString, TSharedPtr<FJsonValue>> jsonValues = jsonObjectHayStack->Values;
    if (jsonValues.Num() > 0)
    {
        DeepJsonStringSearch(Results, strNeedle, jsonValues);
    }

    return Results.Num() > 0;
}

void URHLandingPanelJSONHandler::DeepJsonStringSearch(TMap<FString, TArray<FString>>& Results, FString strNeedle, TMap<FString, TSharedPtr<FJsonValue>>& jsonValues)
{
    // iterate over the kaystack's values and find any properties matching the string
    for (auto ValuesIt = jsonValues.CreateIterator(); ValuesIt; ++ValuesIt)
    {
        // grab this property
        FString Key = ValuesIt.Key();
        TSharedPtr<FJsonValue> Value = ValuesIt.Value();

        // the imageUrl properties should have a nested object as their value
        // in this object, the urls will be stored with culture codes as their keys and URLs as their values
        // check if it's an object and if the key is a match for our needle
        const TSharedPtr<FJsonObject>* newJsonObj;
        if (Value->TryGetObject(newJsonObj))
        {
            if (Key == strNeedle)
            {
                TMap<FString, TSharedPtr<FJsonValue>> nestedJsonValues = newJsonObj->Get()->Values;
                for (auto NestedValuesIt = nestedJsonValues.CreateIterator(); NestedValuesIt; ++NestedValuesIt)
                {
                    FString nestedKey = NestedValuesIt.Key();
                    TSharedPtr<FJsonValue> NestedValue = NestedValuesIt.Value();
                    FString strTest = TEXT("");
                    if (NestedValue->TryGetString(strTest))
                    {
                        // store it
                        TArray<FString>* existingEntry = Results.Find(nestedKey);
                        if (existingEntry != nullptr)
                        {
                            existingEntry->Add(strTest);
                            Results.Add(nestedKey, *existingEntry);
                        }
                        else
                        {
                            TArray<FString> newEntry;
                            newEntry.Add(strTest);
                            Results.Add(nestedKey, newEntry);
                        }
                    }
                }
            }
            else
            {
                // go deeper
                TSharedPtr<FJsonObject> nestedObject = Value->AsObject();
                TMap<FString, TSharedPtr<FJsonValue>> nestedValues = nestedObject->Values;
                DeepJsonStringSearch(Results, strNeedle, nestedValues); // potential infinite recursion
            }
        }

        // check if it's just a regular string
        FString matchStr = TEXT("");
        if (Value->TryGetString(matchStr) && Key == strNeedle)
        {
            TArray<FString> newEntry;
            newEntry.Add(matchStr);
            Results.Add(Key, newEntry);
        }

        // check if it's an array of objects
        const TArray<TSharedPtr<FJsonValue>>* arrayOfValues;
        if (Value->TryGetArray(arrayOfValues))
        {
            // get from the array
            for (auto It = arrayOfValues->CreateConstIterator(); It; ++It)
            {
                TSharedPtr<FJsonValue> thisValue = *It;
                if (thisValue->TryGetObject(newJsonObj))
                {
                    // go deeper
                    TSharedPtr<FJsonObject> nestedObject = thisValue->AsObject();
                    TMap<FString, TSharedPtr<FJsonValue>> nestedValues = nestedObject->Values;
                    DeepJsonStringSearch(Results, strNeedle, nestedValues); // potential infinite recursion
                }
            }
        }
    }
}

/* Starts an image download */
void URHLandingPanelJSONHandler::DownloadImage(FString strImageUrl)
{
    if (!strImageUrl.IsEmpty())
    {
        // first check to see if the image is already on the file system
        int32 nLastSlashIndex;
        strImageUrl.FindLastChar(*TEXT("/"), nLastSlashIndex);
        FString fileName = strImageUrl.RightChop(nLastSlashIndex + 1);
        FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetThreeLetterISOLanguageName();
        FString strSaveFilePath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("images").Append(PATH_SEPARATOR).Append(CurrentCulture).Append(PATH_SEPARATOR).Append(fileName);

        TArray<uint8> pByteArray;
        if (GJsonHandlerAllowCachedImages && FFileHelper::LoadFileToArray(pByteArray, *strSaveFilePath))
        {
            // it does, load the local copy instead
            // add an entry to our url->filepath mapping
            mapRemoteUrlToFilePath.Add(strImageUrl, strSaveFilePath);

            // prepare the texture
            if (PrepareTexture(pByteArray.GetData(), pByteArray.Num(), strSaveFilePath))
            {
                // check if all image downloads are complete
                if (mapRemoteUrlToFilePath.Num() >= imageURLsToDownload.Num())
                {
                    bImagesLoaded = true;
                    OnImagesDownloaded.Broadcast(this);
                }
            }

        }
        else
        {
            // Create the Http request and add to pending request list
            TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();

            HttpRequest->OnProcessRequestComplete().BindUObject(this, &URHLandingPanelJSONHandler::HandleImageDLResponse);
            HttpRequest->SetURL(strImageUrl);
            HttpRequest->SetVerb(TEXT("GET"));
            HttpRequest->ProcessRequest();
        }
    }
}

/* Queue image downloads */
void URHLandingPanelJSONHandler::DownloadImage(TArray<FString> ImageUrls)
{
    if (ImageUrls.Num() > 0)
    {
        for (const auto& ImageUrl : ImageUrls)
        {
            DownloadImage(ImageUrl);
        }
    }
}

/* Handles a completed image download */
void URHLandingPanelJSONHandler::HandleImageDLResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
    if (bSucceeded && HttpResponse.IsValid() && HttpResponse->GetResponseCode() == EHttpResponseCodes::Ok && HttpResponse->GetContentLength() > 0)
    {
        // save the image locally
        int32 nLastSlashIndex;
        HttpRequest->GetURL().FindLastChar(*TEXT("/"), nLastSlashIndex);
        FString fileName = HttpRequest->GetURL().RightChop(nLastSlashIndex + 1);
        FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetThreeLetterISOLanguageName();
        FString strSaveFilePath = FString(strLocalDir).Append(PATH_SEPARATOR).Append("images").Append(PATH_SEPARATOR).Append(CurrentCulture).Append(PATH_SEPARATOR).Append(fileName);

        // save the new image
        if (!FFileHelper::SaveArrayToFile(HttpResponse->GetContent(), *strSaveFilePath))
        {
            // throw a warning
            UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::HandleImageDLResponse ERROR: Failed to save image %s )"), *strSaveFilePath);
        }

        // add an entry to our url->filepath mapping
        mapRemoteUrlToFilePath.Add(HttpRequest->GetURL(), strSaveFilePath);

        // create the texture from the response
        PrepareTexture(HttpResponse->GetContent().GetData(), HttpResponse->GetContentLength(), strSaveFilePath);
    }
    else
    {
        int32 nLastSlashIndex;
        HttpRequest->GetURL().FindLastChar(*TEXT("/"), nLastSlashIndex);
        FString fileName = HttpRequest->GetURL().RightChop(nLastSlashIndex + 1);

        // throw a warning
        UE_LOG(RHLandingPanel, Error, TEXT("LandingPanelJSONHandler::HandleImageDLResponse ERROR: Failed to download image %s )"), *fileName);

        // remove invalid URL from list of image URLs to Download
        imageURLsToDownload.Remove(HttpRequest->GetURL());
    }

    // check if all image downloads are complete
    if (mapRemoteUrlToFilePath.Num() >= imageURLsToDownload.Num())
    {
        bImagesLoaded = true;
        OnImagesDownloaded.Broadcast(this);
    }
}

bool URHLandingPanelJSONHandler::PrepareTexture(const uint8* contentData, int32 contentLength, FString strSaveFilePath)
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
#if PLATFORM_SWITCH
                    // Fix the texture tiling settings for Switch so they render properly
                    Texture->bNoTiling = false;
#endif
                    Texture->UpdateResource();

#if (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 22) || (ENGINE_MAJOR_VERSION > 4)
                    FTexture2DDynamicResource* TextureResource = static_cast<FTexture2DDynamicResource*>(Texture->GetResource());
                    TArray<uint8> RawDataCopy = RawData;

                    ENQUEUE_RENDER_COMMAND(FWriteRawDataToTexture)(
                        [TextureResource, RawDataCopy](FRHICommandListImmediate& RHICmdList)
                        {
							RHWriteRawToTexture_RenderThread(TextureResource, RawDataCopy);
                        });
#else
                    ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
                        FWriteRawDataToTexture,
                        FTexture2DDynamicResource*, TextureResource, static_cast<FTexture2DDynamicResource*>(Texture->GetResource()),
                        TArray<uint8>, RawData, *RawData,
                        {
							RHWriteRawToTexture_RenderThread(TextureResource, RawData);
                        });
#endif

                    // create a texture reference locally
                    mapFilePathToTexture.Add(strSaveFilePath, Texture);
                    return true;
                }
            }
        }
    }
    return false;
}
