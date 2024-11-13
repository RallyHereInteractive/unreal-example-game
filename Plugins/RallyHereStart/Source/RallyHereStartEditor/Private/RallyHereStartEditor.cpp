#include "RallyHereStartEditor.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_PlatformInventoryItem.h"

DEFINE_LOG_CATEGORY(LogRallyHereStartEditor);

#define LOCTEXT_NAMESPACE "RallyHereStartEditor"

class FRallyHereStartEditorModule : public FRallyHereStartEditorTools
{
public:
	TSharedPtr<FAssetTypeActions_PlatformInventoryItem> PlatformInventoryItemAssetTypeActions;

	EAssetTypeCategories::Type RallyHereAssetCategoryBit;

public:
	virtual void StartupModule() override
	{
		UE_LOG(LogRallyHereStartEditor, Log, TEXT("LogRallyHereStartEditor: Log Started"));

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		// Register Asset Category
		RallyHereAssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("RallyHereStart")), INVTEXT("RallyHereStart"));

		// Register the asset type
		PlatformInventoryItemAssetTypeActions = MakeShareable(new FAssetTypeActions_PlatformInventoryItem(RallyHereAssetCategoryBit));
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(PlatformInventoryItemAssetTypeActions.ToSharedRef());
	}

	virtual void ShutdownModule() override
	{
		check(PlatformInventoryItemAssetTypeActions.IsValid());

		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			FModuleManager::GetModuleChecked< FAssetToolsModule >("AssetTools").Get().UnregisterAssetTypeActions(PlatformInventoryItemAssetTypeActions.ToSharedRef());
		}
		PlatformInventoryItemAssetTypeActions.Reset();

		UE_LOG(LogRallyHereStartEditor, Log, TEXT("LogRallyHereStartEditor: Log Ended"));
	}
};

IMPLEMENT_MODULE(FRallyHereStartEditorModule, RallyHereStartEditor);

#undef LOCTEXT_NAMESPACE