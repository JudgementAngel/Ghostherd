// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.5 (UE 5.8) — Epic first-party MCP interop adapter.
//
// UE 5.8 ships an experimental ModelContextProtocol plugin. Projects (and other
// plugins) can register tools with it via IModelContextProtocolModule::AddTool /
// the UToolsetDefinition + UFUNCTION(meta=(AICallable)) auto-discovery. This
// adapter pulls those tools into OUR registry as `epic_<name>` so a single MCP
// endpoint exposes both toolsets — and Epic's tools inherit our catalog mode,
// scope gating, background tasks, transactions, resources and prompts (none of
// which the first-party plugin provides).
//
// Soft dependency: we never link the ModelContextProtocol module. We include its
// public interface headers (PrivateIncludePathModuleNames) and reach the live
// module through FModuleManager at runtime; if the plugin is disabled this is a
// clean no-op. We only touch virtual interface methods and the result's public
// JsonObject member, so no symbols from that module are required at link time.

#include "Tools/MCPToolsetAdapter.h"

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Modules/ModuleManager.h"

// Epic MCP plugin interface (header-only use; module accessed at runtime).
#include "IModelContextProtocolModule.h"
#include "IModelContextProtocolTool.h"
#include "ModelContextProtocolToolResults.h"

namespace MCPToolsetAdapter
{

/** Convert Epic's MCP tool result (a complete MCP result JSON object) into our
 *  FMCPToolResult, preserving text/image content, structuredContent and isError. */
static FMCPToolResult ConvertEpicResult(const FModelContextProtocolToolResult& EpicResult)
{
	const TSharedPtr<FJsonObject>& J = EpicResult.JsonObject;
	if (!J.IsValid())
	{
		return FMCPToolResult::Error(TEXT("Epic toolset returned an empty result."));
	}

	FMCPToolResult Out;
	J->TryGetBoolField(TEXT("isError"), Out.bIsError);

	const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
	if (J->TryGetArrayField(TEXT("content"), Content) && Content)
	{
		for (const TSharedPtr<FJsonValue>& V : *Content)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }
			FString Type;
			(*Obj)->TryGetStringField(TEXT("type"), Type);
			if (Type == TEXT("text"))
			{
				FString Text;
				(*Obj)->TryGetStringField(TEXT("text"), Text);
				Out.Content.Add(FMCPContentBlock::MakeText(Text));
			}
			else if (Type == TEXT("image"))
			{
				FString Data, Mime;
				(*Obj)->TryGetStringField(TEXT("data"), Data);
				(*Obj)->TryGetStringField(TEXT("mimeType"), Mime);
				Out.Content.Add(FMCPContentBlock::MakeImage(Data, Mime.IsEmpty() ? TEXT("image/png") : Mime));
			}
		}
	}

	const TSharedPtr<FJsonObject>* SC = nullptr;
	if (J->TryGetObjectField(TEXT("structuredContent"), SC) && SC)
	{
		Out.StructuredContent = *SC;
	}

	if (Out.Content.Num() == 0 && !Out.StructuredContent.IsValid())
	{
		Out.Content.Add(FMCPContentBlock::MakeText(
			Out.bIsError ? TEXT("(epic toolset error)") : TEXT("(epic toolset result)")));
	}
	return Out;
}

int32 ImportEpicToolsets(FMCPToolRegistry& Registry)
{
#if WITH_EDITOR
	// Reach the live module without a link-time dependency. LoadModule returns null
	// if the experimental plugin isn't installed/enabled.
	IModuleInterface* Base = FModuleManager::Get().LoadModule(TEXT("ModelContextProtocol"));
	IModelContextProtocolModule* Mcp = static_cast<IModelContextProtocolModule*>(Base);
	if (!Mcp)
	{
		return 0;
	}

	int32 Imported = 0;
	for (const TSharedRef<IModelContextProtocolTool>& Tool : Mcp->GetTools())
	{
		const FString EpicName = Tool->GetName();
		if (EpicName.IsEmpty()) { continue; }

		FMCPToolDefinition Def;
		Def.Name = FString::Printf(TEXT("epic_%s"), *EpicName);
		Def.Category = Registry.GetActiveCategory();
		Def.Description = FString::Printf(
			TEXT("[Epic MCP toolset] %s"),
			*(Tool->GetDescription().IsEmpty() ? FString::Printf(TEXT("'%s' from the engine's first-party MCP plugin."), *EpicName)
			                                   : Tool->GetDescription()));

		// Epic tools advertise a JSON-Schema input object; reuse it verbatim.
		Def.InputSchema = Tool->GetInputJsonSchema();
		if (TSharedPtr<FJsonObject> OutSchema = Tool->GetOutputJsonSchema())
		{
			Def.OutputSchema = OutSchema;
		}

		// Unreviewed provider effects may include deletion or arbitrary code.
		// Require the highest existing grant until individual contracts are audited.
		Def.bDestructiveHint = true;
		Def.bOpenWorldHint = true;

		TSharedRef<IModelContextProtocolTool> ToolRef = Tool;
		Def.Handler = FMCPToolHandler::CreateLambda(
			[ToolRef](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
			{
				const TSharedPtr<FJsonObject> Params = Args.IsValid() ? Args : MakeShared<FJsonObject>();
				return ConvertEpicResult(ToolRef->Run(Params));
			});

		Registry.RegisterTool(Def);
		++Imported;
	}

	if (Imported > 0)
	{
		UE_LOG(LogUnrealMCP, Log, TEXT("Imported %d tool(s) from Epic's first-party MCP plugin as epic_*"), Imported);
	}
	return Imported;
#else
	(void)Registry;
	return 0;
#endif
}

} // namespace MCPToolsetAdapter
