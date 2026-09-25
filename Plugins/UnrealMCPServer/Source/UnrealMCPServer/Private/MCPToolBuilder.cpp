// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "MCPToolBuilder.h"

FMCPToolBuilder::FMCPToolBuilder(FMCPToolRegistry& InRegistry, const FString& InName)
	: Registry(InRegistry)
{
	Def.Name = InName;
	Def.Category = InRegistry.GetActiveCategory();  // v4 Phase 1: stamped by the module per family
	Def.InputSchema = FMCPSchemaBuilder::Begin();
}

FMCPToolBuilder& FMCPToolBuilder::Description(const FString& InDescription)
{
	Def.Description = InDescription;
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::ReadOnly()        { Def.bReadOnlyHint = true; return *this; }
FMCPToolBuilder& FMCPToolBuilder::Destructive()     { Def.bDestructiveHint = true; return *this; }
FMCPToolBuilder& FMCPToolBuilder::Idempotent()      { Def.bIdempotentHint = true; return *this; }
FMCPToolBuilder& FMCPToolBuilder::ClosedWorld()     { Def.bOpenWorldHint = false; return *this; }
FMCPToolBuilder& FMCPToolBuilder::RequiresPieOff()  { Def.bRequiresPieOff = true; return *this; }
FMCPToolBuilder& FMCPToolBuilder::SupportsDryRun()  { Def.bSupportsDryRun = true; return *this; }
FMCPToolBuilder& FMCPToolBuilder::LongRunning()     { Def.bLongRunningHint = true; return *this; }

FMCPToolBuilder& FMCPToolBuilder::StringArg(const FString& Name, const FString& Desc, bool bRequired)
{
	FMCPSchemaBuilder::AddString(Def.InputSchema, Name, Desc, bRequired);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::NumberArg(const FString& Name, const FString& Desc, bool bRequired)
{
	FMCPSchemaBuilder::AddNumber(Def.InputSchema, Name, Desc, bRequired);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::IntArg(const FString& Name, const FString& Desc, bool bRequired)
{
	FMCPSchemaBuilder::AddInteger(Def.InputSchema, Name, Desc, bRequired);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::BoolArg(const FString& Name, const FString& Desc, bool bRequired)
{
	FMCPSchemaBuilder::AddBoolean(Def.InputSchema, Name, Desc, bRequired);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::EnumArg(const FString& Name, const FString& Desc, const TArray<FString>& Values, bool bRequired)
{
	FMCPSchemaBuilder::AddEnum(Def.InputSchema, Name, Desc, Values, bRequired);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::StringArrayArg(const FString& Name, const FString& Desc, bool bRequired)
{
	FMCPSchemaBuilder::AddStringArray(Def.InputSchema, Name, Desc, bRequired);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::ObjectArg(const FString& Name, const FString& Desc, TSharedPtr<FJsonObject> SubSchema, bool bRequired)
{
	FMCPSchemaBuilder::AddObject(Def.InputSchema, Name, Desc, SubSchema, bRequired);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::Example(const FString& ExampleJson)
{
	// JSON Schema 2020-12 "examples" keyword on the input schema. Anthropic's
	// data shows concrete invocation examples lift complex-param accuracy
	// 72% -> 90%; clients that don't read it ignore it harmlessly.
	TSharedPtr<FJsonObject> Parsed = StringToJson(ExampleJson);
	if (!Parsed.IsValid())
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("MCP_TOOL '%s': Example() is not valid JSON, ignored"), *Def.Name);
		return *this;
	}
	TArray<TSharedPtr<FJsonValue>> Examples;
	if (Def.InputSchema->HasField(TEXT("examples")))
	{
		Examples = Def.InputSchema->GetArrayField(TEXT("examples"));
	}
	Examples.Add(MakeShared<FJsonValueObject>(Parsed));
	Def.InputSchema->SetArrayField(TEXT("examples"), Examples);
	return *this;
}

FMCPToolBuilder& FMCPToolBuilder::OutputSchema(const FString& SchemaJson)
{
	TSharedPtr<FJsonObject> Parsed = StringToJson(SchemaJson);
	if (!Parsed.IsValid())
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("MCP_TOOL '%s': OutputSchema() is not valid JSON, ignored"), *Def.Name);
		return *this;
	}
	Def.OutputSchema = Parsed;
	return *this;
}

void FMCPToolBuilder::Handle(TFunction<FMCPToolResult(const TSharedPtr<FJsonObject>&)> InHandler)
{
	checkf(!Def.Name.IsEmpty(), TEXT("MCP_TOOL: name is required"));
	checkf(!Def.Description.IsEmpty(), TEXT("MCP_TOOL '%s': Description() is required"), *Def.Name);
	Def.Handler.BindLambda(MoveTemp(InHandler));
	Registry.RegisterTool(Def);
}

void FMCPToolBuilder::HandleCtx(TFunction<FMCPToolResult(const TSharedPtr<FJsonObject>&, const FMCPRequestContext&)> InHandler)
{
	checkf(!Def.Name.IsEmpty(), TEXT("MCP_TOOL: name is required"));
	checkf(!Def.Description.IsEmpty(), TEXT("MCP_TOOL '%s': Description() is required"), *Def.Name);
	Def.HandlerCtx.BindLambda(MoveTemp(InHandler));
	Registry.RegisterTool(Def);
}

FMCPToolBuilder& FMCPToolBuilder::Preview(TFunction<FMCPToolResult(const TSharedPtr<FJsonObject>&, const FMCPRequestContext&)> InPreview)
{
	Def.PreviewHandlerCtx.BindLambda(MoveTemp(InPreview));
	Def.bSupportsDryRun = true;
	return *this;
}
