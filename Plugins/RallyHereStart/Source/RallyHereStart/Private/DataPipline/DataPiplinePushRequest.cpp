// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.
#include "DataPipeline/DataPiplinePushRequest.h"
#include "DataPipeline/DataPiplineURLHandle.h"
#include "Containers/StringConv.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpResponse.h"

DEFINE_LOG_CATEGORY_STATIC(LogPGameDataPipelinePush, Log, All);

bool FDataPipelinePushRequestManager::bIsAvailable = false;
FDataPipelinePushRequestManager* FDataPipelinePushRequestManager::Singleton = nullptr;

FDataPipelinePushPayload::FDataPipelinePushPayload(const FString& InPayloadName, const FString& InContent)
    : PayloadName(InPayloadName)
    , Buffer()
{
    FTCHARToUTF8 Converter(*InContent, InContent.Len());
    Buffer = TArray<uint8>((uint8*)Converter.Get(), Converter.Length());
}

FDataPipelinePushPayload::FDataPipelinePushPayload(const FString& InPayloadName, const TArray<uint8>& InContent)
    : PayloadName(InPayloadName)
    , Buffer(InContent)
{
}

FDataPipelinePushPayload::FDataPipelinePushPayload(const FString& InPayloadName, TArray<uint8>&& InContent)
    : PayloadName(InPayloadName)
    , Buffer(MoveTemp(InContent))
{
}

FDataPipelinePushPayload::~FDataPipelinePushPayload()
{
}

FDataPipelinePushRequest::FDataPipelinePushRequest(const TSharedPtr<FDataPipelineURLHandle>& InURLHandle, const TSharedPtr<FDataPipelinePushPayload>& InPayload)
    : URLHandle(InURLHandle)
    , PayloadPtr(InPayload)
    , HttpRequestPtr(nullptr)
    , State(EDataPipelinePushState::NotStarted)
{
}

FDataPipelinePushRequest::~FDataPipelinePushRequest()
{

}

void FDataPipelinePushRequest::ProcessRequest()
{
    if (State == EDataPipelinePushState::NotStarted)
    {
        State = EDataPipelinePushState::Processing;

        if (!URLHandle.IsValid())
        {
            UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Pipeline Push failed. No URL Handle Provided"));
            State = EDataPipelinePushState::Failed;
            FinishRequest();
            return;
        }

        if (!PayloadPtr.IsValid())
        {
            UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Pipeline Push failed. No Payload Provided"));
            State = EDataPipelinePushState::Failed;
            FinishRequest();
            return;
        }

        if (URLHandle->GetDataPipelineURLState() == EDataPipelineURLState::NotReady)
        {
            URLHandle->Init();
        }
        check(URLHandle->GetDataPipelineURLState() != EDataPipelineURLState::NotReady);

        if(URLHandle->GetDataPipelineURLState() == EDataPipelineURLState::Requesting)
        {
            URLHandle->DataPipelineURLCallback.AddSP(this, &FDataPipelinePushRequest::OnURLHandleReady);
        }
        else
        {
            OnURLHandleReady();
        }
    }
}

void FDataPipelinePushRequest::OnURLHandleReady()
{
    if(State != EDataPipelinePushState::Processing)
    {
        return;
    }

    if (!URLHandle.IsValid())
    {
        UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Pipeline Push failed. URL Handle disappeared."));
        State = EDataPipelinePushState::Failed;
        FinishRequest();
        return;
    }

    if (!ensure(URLHandle->GetDataPipelineURLState() != EDataPipelineURLState::Requesting && URLHandle->GetDataPipelineURLState() != EDataPipelineURLState::NotReady))
    {
        UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Pipeline Push failed. URL Handle somehow reverted to an old state."));
        HandleURLUnavailable();
    }
    else if (URLHandle->GetDataPipelineURLState() == EDataPipelineURLState::Ready)
    {
        BeginDataPipelinePush();
    }
    else
    {
        HandleURLUnavailable();
    }
}

void FDataPipelinePushRequest::BeginDataPipelinePush()
{
    if (State != EDataPipelinePushState::Processing)
    {
         return;
    }

    if (!URLHandle.IsValid())
    {
        UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Pipeline Push failed. URL Handle Disappeared"));
        State = EDataPipelinePushState::Failed;
        FinishRequest();
        return;
    }

    if (URLHandle->GetDataPipelineURLState() != EDataPipelineURLState::Ready)
    {
        HandleURLUnavailable();
        return;
    }

    if (!PayloadPtr.IsValid())
    {
        UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Pipeline Push failed. Payload Disappeared"));
        State = EDataPipelinePushState::Failed;
        FinishRequest();
        return;
    }

    // it looks like we DON'T have to make this static so the object can hang around a long as needed
    HttpRequestPtr = FHttpModule::Get().CreateRequest();
    if (HttpRequestPtr == nullptr)
    {
        UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Failed to allocate HTTPRequest instance."));
        State = EDataPipelinePushState::Failed;
        FinishRequest();
        return;
    }

    FString ContentDisposition = TEXT("attachment; filename = \"") + PayloadPtr->GetPayloadName() + TEXT("\"");
    HttpRequestPtr->SetHeader(TEXT("content-disposition"), ContentDisposition);
    HttpRequestPtr->SetHeader(TEXT("content-type"), TEXT("application/json"));
    HttpRequestPtr->SetVerb(TEXT("POST"));
    HttpRequestPtr->SetURL(URLHandle->GetDataPipelineURL());

    float ConnectionTimeout = FHttpModule::Get().GetHttpConnectionTimeout();
    //FHttpModule::Get().SetHttpTimeout(ConnectionTimeout + 60);

    HttpRequestPtr->SetContent(PayloadPtr->GetBuffer());

    //int RemainingTryCount = FMath::Max(FMath::Min(AttemptCountMax, 1), 10);
    //RetryIntervalSecs = FMath::Min(FMath::Max(RetryIntervalSecs, 10), 300);

    HttpRequestPtr->OnProcessRequestComplete().BindSP(this, &FDataPipelinePushRequest::DataPiplineHttpRequestComplete);
    HttpRequestPtr->ProcessRequest();

    UE_LOG(LogPGameDataPipelinePush, Log, TEXT("DataPipelinePost started with status [%s]."), EHttpRequestStatus::ToString(HttpRequestPtr->GetStatus()));
}

void FDataPipelinePushRequest::DataPiplineHttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool Success)
{
    if (State != EDataPipelinePushState::Processing)
    {
        return;
    }

    if (HttpRequest != HttpRequestPtr)
    {
        return;
    }

    //RemainingTryCount--; // that was one try
    UE_LOG(LogPGameDataPipelinePush, Log, TEXT("DataPiplineHttpRequestComplete - next retry count [%d] and success [%d]"), 0 /*RemainingTryCount*/, (int32)Success);

    float ElapsedTime = HttpRequest->GetElapsedTime();
    EHttpRequestStatus::Type HTTPStatus = HttpRequest->GetStatus();
    UE_LOG(LogPGameDataPipelinePush, Log, TEXT("DataPiplineHttpRequestComplete - HttpRequest->ProcessRequest returned [%s] in [%f] seconds; remaining trys [%d]."),
        EHttpRequestStatus::ToString(HTTPStatus), ElapsedTime, 0 /*RemainingTryCount*/);

    if (HttpResponse == nullptr)
    {
        UE_LOG(LogPGameDataPipelinePush, Error, TEXT("OnDataPipelinePostComplete - HTTPResponse==nullptr; quitting"));
        State = EDataPipelinePushState::Failed;
        FinishRequest();
        return;
    }

    const int32 ResponseCode = HttpResponse->GetResponseCode();
    FString ResponseString = HttpResponse->GetContentAsString();
    UE_LOG(LogPGameDataPipelinePush, Log, TEXT("OnDataPipelinePostComplete - HTTPResponse returned code [%d] Message [%s]."), ResponseCode, *ResponseString);
    
    State = EHttpResponseCodes::IsOk(ResponseCode) ? EDataPipelinePushState::Success : EDataPipelinePushState::Failed;
    FinishRequest();
    return; // no retry for now
    // force retry
    /*
    switch (ResponseCode)
    {
    default: 	// if unrecoverable failure, do not retry
    case 200:	// if success, do not retry
        UE_LOG(LogDataPipelinePush, Log, TEXT("OnDataPipelinePostComplete - nothing further to do here; quitting"));
        break;

        // possibly recoverable
    case 408: // Request Timeout
    //case 500: // Internal Server Error
    case 504: // Gateway Timeout
        {
            if (RemainingTryCount > 0)
            {
                UE_LOG(LogDataPipelinePush, Warning, TEXT("OnDataPipelinePostComplete - HTTP retrying [%d] after [%d] seconds"), RemainingTryCount, RetryIntervalSecs);

                // this approach to retry may cause a crash?
                HttpRequest->OnProcessRequestComplete().BindStatic(&OnDataPipelinePostComplete, RemainingTryCount, RetryIntervalSecs);
                // this runs on the main thread, so no sleeping here.
                //FPlatformProcess::Sleep(RetryIntervalSecs);
                HttpRequest->ProcessRequest();
            }
            else
            {
                UE_LOG(LogDataPipelinePush, Warning, TEXT("OnDataPipelinePostComplete - no more retries remaining; quitting"), RemainingTryCount, RetryIntervalSecs);
            }
        }	break;

    }
*/
}

void FDataPipelinePushRequest::HandleURLUnavailable()
{
    State = EDataPipelinePushState::Failed;
    UE_LOG(LogPGameDataPipelinePush, Error, TEXT("URL was unavailable and could not finish DataPipelinePush request."));
    FinishRequest();
}

void FDataPipelinePushRequest::FinishRequest()
{
    TSharedRef<FDataPipelinePushRequest> SafeRef = SharedThis(this);

    if (FDataPipelinePushRequestManager* pManager = FDataPipelinePushRequestManager::Get())
    {
        pManager->RequestFinished(SafeRef);
    }
}

FDataPipelinePushRequestManager::FDataPipelinePushRequestManager()
    : ActivePipelinePushRequests()
{
}

FDataPipelinePushRequestManager::~FDataPipelinePushRequestManager()
{
    if (ActivePipelinePushRequests.Num())
    {
        //TODO: Find a way to old the program until DataPiplinePushRequests are complete.
        UE_LOG(LogPGameDataPipelinePush, Error, TEXT("Shutting down DataPipelinePushRequestManager with [%d] requests still pending."), ActivePipelinePushRequests.Num());
    }
}

TSharedPtr<FDataPipelinePushRequest> FDataPipelinePushRequestManager::CreateNewRequest(const TSharedPtr<FDataPipelineURLHandle>& InURLHandle, const TSharedPtr<FDataPipelinePushPayload>& InPayload)
{
    if (!InURLHandle.IsValid() || !InPayload.IsValid())
    {
        UE_LOG(LogPGameDataPipelinePush, Warning, TEXT("Failed to create DataPipelinePushRequest. InURLHandle.IsValid() = [%d] ------- InPayload.IsValid() = [%d]"), (int32)(InURLHandle.IsValid()), (int32)(InPayload.IsValid()));
        return nullptr;
    }

    TSharedRef<FDataPipelinePushRequest> NewRequest = MakeShared<FDataPipelinePushRequest>(InURLHandle, InPayload);
    ActivePipelinePushRequests.Add(NewRequest);
    NewRequest->ProcessRequest();
    return NewRequest;
}

FDataPipelinePushRequestManager* FDataPipelinePushRequestManager::Get()
{
    if (Singleton != nullptr)
    {
        return Singleton;
    }

    if (bIsAvailable)
    {
        Singleton = new FDataPipelinePushRequestManager;
        return Singleton;
    }

    return nullptr;
}

void FDataPipelinePushRequestManager::StaticInit()
{
    bIsAvailable = true;
}

void FDataPipelinePushRequestManager::StaticShutdown()
{
    bIsAvailable = false;
    delete Singleton;
}

void FDataPipelinePushRequestManager::RequestFinished(const TSharedRef<FDataPipelinePushRequest>& InRequest)
{
    ActivePipelinePushRequests.Remove(InRequest);
}
