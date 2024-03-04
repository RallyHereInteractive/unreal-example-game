// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "PlayerExperience/PlayerExp_MatchReportSender.h"
#include "PlayerExperience/PlayerExperienceGlobals.h"

#include "Engine/GameInstance.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_ConfigSubsystem.h"

#include "RallyHereIntegrationModule.h"
#include "RH_Integration.h"
#include "RH_IntegrationSettings.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "DataPipeline/DataPiplineURLHandle.h"
#include "DataPipeline/DataPiplinePushRequest.h"

UPlayerExp_MatchReportSender::UPlayerExp_MatchReportSender(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
    : Super(ObjectInitializer)
    , bInitialized(false)
{

}

void UPlayerExp_MatchReportSender::DoInit()
{
    if (!bInitialized)
    {
        bInitialized = true;
        Init();
    }
}

void UPlayerExp_MatchReportSender::DoCleanup()
{
    if (bInitialized)
    {
        bInitialized = false;
        Cleanup();
    }
}

void UPlayerExp_MatchReportSender::Init()
{
    check(bInitialized);
}

void UPlayerExp_MatchReportSender::Cleanup()
{
    check(!bInitialized);
}

void UPlayerExp_MatchReportSender::RequestSendReport(class UGameInstance* GameInstance, const TSharedRef<FJsonObject>& InReportJsonObject)
{
	/*
	if (GameInstance == nullptr)
	{
		UE_LOG(LogPlayerExperience, Warning, TEXT("No game instance when attempting to send report."));
		return;
	}
	auto* pGISS = GameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
	if (!pGISS || pGISS->GetConfigSubsystem() == nullptr)
	{
		UE_LOG(LogPlayerExperience, Warning, TEXT("Could not find game instance subsystem or config subsystem when attempting to send match report"));
		return;
	}

	FString ReportURL;
	{
		auto& RHIntegration = FRallyHereIntegrationModule::Get();
		auto* Settings = GetDefault<URH_IntegrationSettings>();
		check(Settings != nullptr);

		const auto SandboxId = RHIntegration.GetEnvironmentId();

		// check the main sandbox configuration
		const auto* Sandbox = Settings->GetEnvironmentConfiguration(SandboxId);
		if (Sandbox != nullptr && !Sandbox->PlayerExperienceReportURL.IsEmpty())
		{
			ReportURL = Sandbox->PlayerExperienceReportURL;
		}
		else
		{
			// if main sandbox was not configured, check the default as a fallback
			Sandbox = &Settings->DefaultEnvironmentConfiguration;
			if (Sandbox != nullptr && !Sandbox->PlayerExperienceReportURL.IsEmpty())
			{
				ReportURL = Sandbox->PlayerExperienceReportURL;
			}
		}
		
	}

	if (ReportURL.Len() <= 0)
	{
		UE_LOG(LogPlayerExperience, Warning, TEXT("Could not find a valid URL for PEX data"));
		return;
	}

	if (InReportJsonObject->Values.Num() == 0)
	{
		UE_LOG(LogPlayerExperience, Warning, TEXT("The Json Report is empty."));
		return;
	}

	FString ReportName = TEXT("PlayerExpReport_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S"));
	FString ReportAsString;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ReportAsString);
	if (!FJsonSerializer::Serialize(InReportJsonObject, Writer))
	{
		UE_LOG(LogPlayerExperience, Warning, TEXT("Unable to serialize Json Report."));
		return;
	}

	FDataPipelinePushRequestManager* pManager = FDataPipelinePushRequestManager::Get();
	if (pManager == nullptr)
	{
		UE_LOG(LogPlayerExperience, Warning, TEXT("Data Pipline request manager not available."));
		return;
	}

	UE_LOG(LogPlayerExperience, Log, TEXT("POST'ing file: %s"), *ReportName);

	TSharedPtr<FDataPipelineURLHandle> URLHandle = nullptr;
	URLHandle = MakeShared<FDataPipelineURLHandle_Direct>(ReportURL);

	TSharedRef< FDataPipelinePushPayload> Payload = MakeShared<FDataPipelinePushPayload>(ReportName, ReportAsString);
	pManager->CreateNewRequest(URLHandle, Payload);

	return;
	*/
}