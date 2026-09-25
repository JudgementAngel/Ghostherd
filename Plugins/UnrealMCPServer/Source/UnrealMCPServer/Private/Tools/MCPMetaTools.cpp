// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPMetaTools.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPTaskManager.h"
#include "MCPInputValidator.h"
#include "ScopedTransaction.h"

#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Common/MCPAssetCreate.h"

namespace MCPMetaTools
{

namespace
{
	/** First sentence (or first 140 chars) of a tool description. */
	FString Summarize(const FString& Description)
	{
		int32 DotIdx;
		if (Description.FindChar(TEXT('.'), DotIdx) && DotIdx > 0 && DotIdx < 200)
		{
			return Description.Left(DotIdx + 1);
		}
		return Description.Len() > 140 ? Description.Left(140) + TEXT("...") : Description;
	}

	/** Relevance score for search_tools. 0 = no match. */
	int32 ScoreTool(const FMCPToolDefinition& Tool, const FString& QueryLower,
		const TArray<FString>& QueryTokens)
	{
		const FString NameLower = Tool.Name.ToLower();
		const FString DescLower = Tool.Description.ToLower();

		if (NameLower == QueryLower)               { return 1000; }
		int32 Score = 0;
		if (NameLower.Contains(QueryLower))        { Score += 400; }
		if (DescLower.Contains(QueryLower))        { Score += 150; }
		for (const FString& Token : QueryTokens)
		{
			if (Token.Len() < 2) continue;
			if (NameLower.Contains(Token))         { Score += 60; }
			else if (DescLower.Contains(Token))    { Score += 20; }
		}
		return Score;
	}

	// ---- run_tool_script ----------------------------------------------------

	constexpr int32 MaxScriptSteps = 100;
    thread_local bool ScriptExecuting = false;
	constexpr int32 MaxScriptInvocations = 1000;

	/** Resolve "$name" / "$name.path.to.field" against saved step results. */
	TSharedPtr<FJsonValue> ResolveRef(const FString& Ref,
		const TMap<FString, TSharedPtr<FJsonValue>>& Vars, FString& OutError)
	{
		FString Path = Ref.Mid(1); // strip '$'
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Bad reference '%s'"), *Ref);
			return nullptr;
		}

		const TSharedPtr<FJsonValue>* Root = Vars.Find(Parts[0]);
		if (!Root)
		{
			OutError = FString::Printf(TEXT("Unknown variable '%s' (no prior step saved it with save_as)"), *Parts[0]);
			return nullptr;
		}

		TSharedPtr<FJsonValue> Current = *Root;
		for (int32 i = 1; i < Parts.Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* AsObj = nullptr;
			if (!Current.IsValid() || !Current->TryGetObject(AsObj) || !AsObj)
			{
				OutError = FString::Printf(TEXT("'%s': segment '%s' is not an object"), *Ref, *Parts[i - 1]);
				return nullptr;
			}
			Current = (*AsObj)->TryGetField(Parts[i]);
			if (!Current.IsValid())
			{
				OutError = FString::Printf(TEXT("'%s': field '%s' not found"), *Ref, *Parts[i]);
				return nullptr;
			}
		}
		return Current;
	}

	/** Deep-copy a JSON value, replacing "$ref" strings from Vars. */
	TSharedPtr<FJsonValue> SubstituteRefs(const TSharedPtr<FJsonValue>& Value,
		const TMap<FString, TSharedPtr<FJsonValue>>& Vars, FString& OutError)
	{
		if (!Value.IsValid()) { return Value; }

		switch (Value->Type)
		{
		case EJson::String:
		{
			const FString Str = Value->AsString();
			if (Str.StartsWith(TEXT("$")) && Str.Len() > 1)
			{
				return ResolveRef(Str, Vars, OutError);
			}
			return Value;
		}
		case EJson::Object:
		{
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			for (const auto& Pair : Value->AsObject()->Values)
			{
				TSharedPtr<FJsonValue> Sub = SubstituteRefs(Pair.Value, Vars, OutError);
				if (!OutError.IsEmpty()) { return nullptr; }
				Out->SetField(Pair.Key, Sub);
			}
			return MakeShared<FJsonValueObject>(Out);
		}
		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const TSharedPtr<FJsonValue>& Elem : Value->AsArray())
			{
				TSharedPtr<FJsonValue> Sub = SubstituteRefs(Elem, Vars, OutError);
				if (!OutError.IsEmpty()) { return nullptr; }
				Out.Add(Sub);
			}
			return MakeShared<FJsonValueArray>(Out);
		}
		default:
			return Value;
		}
	}

    bool VariableName(const FString& Name)
    {
        if (Name.IsEmpty() || Name.Len() > 128) return false;
        for (TCHAR C : Name)
            if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_')) return false;
        return true;
    }

    // Preserve saved-result references as exact deferred nodes; substitute only
    // known literal loop values. The registry always validates resolved calls fully.
    TSharedPtr<FJsonValue> PrepareArgs(const TSharedPtr<FJsonValue>& Value, const TSet<FString>& Saved,
        const FString& LoopName, const TSharedPtr<FJsonValue>& LoopValue, bool bUnknownLoop,
        TSet<const FJsonValue*>& Deferred, FString& Error)
    {
        if (Value->Type == EJson::String && Value->AsString().StartsWith(TEXT("$")) && Value->AsString().Len() > 1)
        {
            const FString Ref = Value->AsString();
            TArray<FString> Parts; Ref.Mid(1).ParseIntoArray(Parts, TEXT("."), false);
            if (Parts.IsEmpty() || !VariableName(Parts[0]) || Parts.Contains(FString()))
            { Error = TEXT("Malformed reference: ") + Ref; return nullptr; }
            if (!LoopName.IsEmpty() && Parts[0] == LoopName)
            {
                if (!bUnknownLoop)
                {
                    TMap<FString, TSharedPtr<FJsonValue>> Vars; Vars.Add(LoopName, LoopValue);
                    return ResolveRef(Ref, Vars, Error);
                }
            }
            else if (!Saved.Contains(Parts[0]))
            { Error = TEXT("Unknown or forward reference: ") + Ref; return nullptr; }
            Deferred.Add(Value.Get());
            return Value;
        }
        if (Value->Type == EJson::Object)
        {
            auto Out = MakeShared<FJsonObject>();
            for (const auto& Pair : Value->AsObject()->Values)
            {
                auto Child = PrepareArgs(Pair.Value, Saved, LoopName, LoopValue, bUnknownLoop, Deferred, Error);
                if (!Error.IsEmpty()) return nullptr;
                Out->SetField(Pair.Key, Child);
            }
            return MakeShared<FJsonValueObject>(Out);
        }
        if (Value->Type == EJson::Array)
        {
            TArray<TSharedPtr<FJsonValue>> Out;
            for (const auto& Child : Value->AsArray())
            {
                Out.Add(PrepareArgs(Child, Saved, LoopName, LoopValue, bUnknownLoop, Deferred, Error));
                if (!Error.IsEmpty()) return nullptr;
            }
            return MakeShared<FJsonValueArray>(Out);
        }
        return Value;
    }

    FMCPToolResult PreflightScript(const TSharedPtr<FJsonObject>& Script, const FMCPRequestContext& Context)
    {
        const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
        if (!Script.IsValid() || !Script->TryGetArrayField(TEXT("steps"), Steps) || Steps->IsEmpty() || Steps->Num() > MaxScriptSteps)
            return FMCPToolResult::Error(TEXT("script.steps must contain 1..100 steps"));
        TMap<FString, FMCPToolDefinition> Tools;
        for (const auto& Tool : FMCPToolRegistry::Get().GetAllTools()) Tools.Add(Tool.Name, Tool);
        TSet<FString> Saved;
        int32 KnownInvocations = 0, DeferredCalls = 0;
        bool bDynamicLoops = false;
        for (int32 I = 0; I < Steps->Num(); ++I)
        {
            auto Fail = [I](const FString& Message)
            { return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, FString::Printf(TEXT("Preflight step %d: %s"), I, *Message)); };
            if (Context.IsCancelled()) return Fail(TEXT("cancelled"));
            const auto& Value = (*Steps)[I];
            if (Value->Type != EJson::Object) return Fail(TEXT("step must be an object"));
            auto Step = Value->AsObject();
            static const TSet<FString> Fields = { TEXT("tool"), TEXT("args"), TEXT("save_as"), TEXT("foreach"), TEXT("as") };
            for (const auto& Pair : Step->Values)
                if (!Fields.Contains(FString(*Pair.Key))) return Fail(TEXT("unknown step field: ") + FString(*Pair.Key));
            FString Name;
            if (!Step->TryGetStringField(TEXT("tool"), Name) || Name.IsEmpty()) return Fail(TEXT("tool must be a non-empty name"));
            if (Name == TEXT("run_tool_script")) return Fail(TEXT("nested scripts are unsupported"));
            const auto* Tool = Tools.Find(Name);
            if (!Tool) return Fail(TEXT("unknown tool: ") + Name);
            if ((!Tool->bReadOnlyHint && !Context.HasScope(EMCPScope::Scene)) || (Tool->bDestructiveHint && !Context.HasScope(EMCPScope::Destructive)))
                return Fail(TEXT("caller scope cannot execute ") + Name);
            if (!Tool->Handler.IsBound() && !Tool->HandlerCtx.IsBound()) return Fail(TEXT("tool has no handler"));
            // Audit all schema assertions even if every invocation depends on results.
            auto SchemaCheck = FMCPInputValidator::Validate(Tool->InputSchema, MakeShared<FJsonObject>());
            if (!SchemaCheck.bOk && SchemaCheck.Error.StructuredContent->GetStringField(TEXT("code")) == MCPErrorCodeToString(EMCPError::Unsupported))
                return Fail(TEXT("unsupported child schema"));
            FString SaveAs, LoopName = TEXT("item");
            if (Step->HasField(TEXT("save_as")) && (!Step->TryGetStringField(TEXT("save_as"), SaveAs) || !VariableName(SaveAs))) return Fail(TEXT("invalid save_as name"));
            if (Step->HasField(TEXT("as")) && (!Step->TryGetStringField(TEXT("as"), LoopName) || !VariableName(LoopName) || !Step->HasField(TEXT("foreach")))) return Fail(TEXT("invalid loop alias"));
            TArray<TSharedPtr<FJsonValue>> LoopValues;
            bool bLoop = Step->HasField(TEXT("foreach")), bUnknown = false;
            if (bLoop)
            {
                auto Foreach = Step->TryGetField(TEXT("foreach"));
                if (Foreach->Type == EJson::Array) LoopValues = Foreach->AsArray();
                else if (Foreach->Type == EJson::String && Foreach->AsString().StartsWith(TEXT("$")))
                {
                    TSet<const FJsonValue*> Deferred; FString Error;
                    PrepareArgs(Foreach, Saved, TEXT(""), nullptr, false, Deferred, Error);
                    if (!Error.IsEmpty()) return Fail(Error);
                    if (Deferred.IsEmpty()) return Fail(TEXT("foreach must reference a prior result array"));
                    bUnknown = true; bDynamicLoops = true; LoopValues.Add(nullptr);
                }
                else return Fail(TEXT("foreach must be a literal array or prior result reference"));
            }
            else LoopValues.Add(nullptr);
            auto RawArgs = Step->HasField(TEXT("args")) ? Step->TryGetField(TEXT("args")) : MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
            for (const auto& Item : LoopValues)
            {
                if (!bUnknown && ++KnownInvocations > MaxScriptInvocations) return Fail(TEXT("invocation budget exceeds 1000"));
                TSet<const FJsonValue*> Deferred; FString Error;
                auto Resolved = PrepareArgs(RawArgs, Saved, bLoop ? LoopName : FString(), Item, bUnknown, Deferred, Error);
                if (!Error.IsEmpty()) return Fail(Error);
                if (Deferred.Contains(Resolved.Get())) { ++DeferredCalls; continue; }
                if (Resolved->Type != EJson::Object) return Fail(TEXT("args must resolve to an object"));
                const auto& Object = Resolved->AsObject();
                auto Check = FMCPInputValidator::ValidateDeferred(Tool->InputSchema, Object, Deferred);
                if (!Check.bOk) return Fail(Check.Error.Content[0].Text);
                if (auto Dry = Object->TryGetField(TEXT("dry_run")))
                {
                    if (Deferred.Contains(Dry.Get())) return Fail(TEXT("dry_run cannot depend on prior results"));
                    if (Dry->Type != EJson::Boolean || (Dry->AsBool() && !Tool->SupportsSafePreview())) return Fail(TEXT("invalid or unsupported child preview"));
                }
                if (!Deferred.IsEmpty()) ++DeferredCalls;
            }
            // An empty literal loop produces no saved result.
            if (!SaveAs.IsEmpty() && !LoopValues.IsEmpty()) Saved.Add(SaveAs);
            if (bLoop && !LoopValues.IsEmpty()) Saved.Add(LoopName); // existing script language retains the last loop variable
        }
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("preflight_passed"), true);
        Report->SetNumberField(TEXT("steps"), Steps->Num());
        Report->SetNumberField(TEXT("known_invocations"), KnownInvocations);
        Report->SetNumberField(TEXT("calls_with_deferred_arguments"), DeferredCalls);
        Report->SetBoolField(TEXT("dynamic_loop_counts"), bDynamicLoops);
        Report->SetBoolField(TEXT("all_arguments_validated"), DeferredCalls == 0 && !bDynamicLoops);
        Report->SetBoolField(TEXT("editor_state_validated"), false);
        Report->SetBoolField(TEXT("applied"), false);
        return FMCPToolResult::SuccessStructured(TEXT("Preflight completed without running child tools. State preconditions and result-dependent arguments are checked during execution."), Report);
    }

	/** What a step's save_as stores: structuredContent when present, else {text}. */
	TSharedPtr<FJsonValue> ResultToVar(const FMCPToolResult& Result)
	{
		if (Result.StructuredContent.IsValid())
		{
			return MakeShared<FJsonValueObject>(Result.StructuredContent);
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("text"), Result.Content.Num() > 0 ? Result.Content[0].Text : FString());
		return MakeShared<FJsonValueObject>(Obj);
	}
}

namespace
{
	TSharedPtr<FJsonObject> TaskSnapshotToJson(const FMCPTaskManager::FTaskSnapshot& Snap, bool bIncludeResult)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("task_id"), Snap.TaskId);
		Obj->SetStringField(TEXT("tool"), Snap.ToolName);
		Obj->SetStringField(TEXT("status"), FMCPTaskManager::TaskStateToString(Snap.State));
		if (Snap.Progress >= 0.f)
		{
			Obj->SetNumberField(TEXT("progress"), Snap.Progress);
		}
		if (!Snap.ProgressMessage.IsEmpty())
		{
			Obj->SetStringField(TEXT("progress_message"), Snap.ProgressMessage);
		}
		Obj->SetNumberField(TEXT("running_seconds"),
			(Snap.FinishedAt > 0.0 ? Snap.FinishedAt : FPlatformTime::Seconds()) - Snap.CreatedAt);
		if (bIncludeResult && Snap.State != FMCPTaskManager::ETaskState::Working)
		{
			Obj->SetObjectField(TEXT("result"), Snap.Result.ToJson());
		}
		return Obj;
	}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_task_status / cancel_task / list_tasks — pollable tasks (v4)
	// ================================================================
	MCP_TOOL(Registry, "get_task_status")
		.Description(TEXT("Poll a background task started by a long-running tool (the tool returned {task_id, status:'working'}). Returns status (working/completed/failed/cancelled), progress when available, and the final tool result once finished."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("task_id"), TEXT("Task id from the original tool call"), true)
		.HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context) -> FMCPToolResult
		{
			FString TaskIdArg;
			if (!Args->TryGetStringField(TEXT("task_id"), TaskIdArg))
			{
				return FMCPToolResult::Error(TEXT("task_id required"));
			}
			FMCPTaskManager::FTaskSnapshot Snap;
			if (!FMCPTaskManager::Get().GetTask(TaskIdArg, Snap, Context))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Unknown task: %s"), *TaskIdArg),
					TEXT("Finished tasks are kept ~1 hour; list_tasks shows what's tracked."));
			}
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Task %s (%s): %s"), *Snap.TaskId, *Snap.ToolName,
					*FMCPTaskManager::TaskStateToString(Snap.State)),
				TaskSnapshotToJson(Snap, /*bIncludeResult*/ true));
		});

	MCP_TOOL(Registry, "cancel_task")
		.Description(TEXT("Request cooperative cancellation of a working background task. The tool stops at its next cancellation checkpoint; poll get_task_status to confirm."))
		.StringArg(TEXT("task_id"), TEXT("Task id to cancel"), true)
		.HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context) -> FMCPToolResult
		{
			FString TaskIdArg;
			if (!Args->TryGetStringField(TEXT("task_id"), TaskIdArg))
			{
				return FMCPToolResult::Error(TEXT("task_id required"));
			}
			if (FMCPTaskManager::Get().CancelTask(TaskIdArg, Context))
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("Cancellation requested for task %s. Poll get_task_status to confirm."), *TaskIdArg));
			}
			return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Task %s is unknown or already finished."), *TaskIdArg));
		});

	MCP_TOOL(Registry, "list_tasks")
		.Description(TEXT("List tracked background tasks (newest first): working ones plus recently finished (kept ~1 hour)."))
		.ReadOnly()
		.Idempotent()
		.HandleCtx([](const TSharedPtr<FJsonObject>&, const FMCPRequestContext& Context) -> FMCPToolResult
		{
			TArray<FMCPTaskManager::FTaskSnapshot> Snaps = FMCPTaskManager::Get().ListTasks(Context);
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const auto& Snap : Snaps)
			{
				Arr.Add(MakeShared<FJsonValueObject>(TaskSnapshotToJson(Snap, /*bIncludeResult*/ false)));
			}
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetNumberField(TEXT("count"), Arr.Num());
			Out->SetArrayField(TEXT("tasks"), Arr);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d tracked task(s)."), Arr.Num()), Out);
		});

	// ================================================================
	// export_tool_docs — generate the tool reference FROM the registry
	// ================================================================
	MCP_TOOL(Registry, "export_tool_docs")
		.Description(TEXT("Generate the complete tool-reference markdown from the LIVE registry (names, categories, descriptions, argument tables, annotations) and write it to Saved/MCPDocs/ToolReference.md. Kills documentation drift: counts and signatures always match the build."))
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>&) -> FMCPToolResult
		{
			TMap<FName, TArray<FString>> ByCategory = FMCPToolRegistry::Get().GetToolsByCategory();
			TArray<FName> Categories;
			ByCategory.GenerateKeyArray(Categories);
			Categories.Sort(FNameLexicalLess());

			int32 Total = 0;
			FString Md;
			Md += TEXT("# Unreal MCP Server — Tool Reference\n\n");
			Md += FString::Printf(TEXT("> Generated from the live registry (server %s). Do not edit by hand.\n\n"),
				*MCPProtocol::ServerVersion);

			for (const FName& Cat : Categories)
			{
				const TArray<FString>& Names = ByCategory[Cat];
				Md += FString::Printf(TEXT("## %s (%d tools)\n\n"), *Cat.ToString(), Names.Num());
				for (const FString& Name : Names)
				{
					const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(Name);
					if (!Tool) { continue; }
					Total++;

					Md += FString::Printf(TEXT("### `%s`\n\n%s\n\n"), *Tool->Name, *Tool->Description);

					// Annotations line
					TArray<FString> Notes;
					if (Tool->bReadOnlyHint)    { Notes.Add(TEXT("read-only")); }
					if (Tool->bDestructiveHint) { Notes.Add(TEXT("destructive — requires Destructive scope")); }
					if (Tool->bIdempotentHint)  { Notes.Add(TEXT("idempotent")); }
					if (Tool->bRequiresPieOff)  { Notes.Add(TEXT("refuses to run during PIE")); }
					if (Tool->SupportsSafePreview())  { Notes.Add(TEXT("supports dry_run")); }
					if (Tool->bLongRunningHint) { Notes.Add(TEXT("long-running — may return a task handle")); }
					if (Notes.Num() > 0)
					{
						Md += FString::Printf(TEXT("*%s*\n\n"), *FString::Join(Notes, TEXT(" · ")));
					}

					// Argument table
					if (Tool->InputSchema.IsValid() && Tool->InputSchema->HasField(TEXT("properties")))
					{
						TSet<FString> Required;
						if (Tool->InputSchema->HasField(TEXT("required")))
						{
							for (const auto& V : Tool->InputSchema->GetArrayField(TEXT("required")))
							{
								FString R; if (V->TryGetString(R)) { Required.Add(R); }
							}
						}
						const TSharedPtr<FJsonObject> Props = Tool->InputSchema->GetObjectField(TEXT("properties"));
						if (Props->Values.Num() > 0)
						{
							Md += TEXT("| Argument | Type | Required | Description |\n|---|---|---|---|\n");
							for (const auto& Pair : Props->Values)
							{
								const TSharedPtr<FJsonObject>* PropObj = nullptr;
								if (!Pair.Value->TryGetObject(PropObj)) { continue; }
								FString Type, Desc;
								(*PropObj)->TryGetStringField(TEXT("type"), Type);
								(*PropObj)->TryGetStringField(TEXT("description"), Desc);
								if ((*PropObj)->HasField(TEXT("enum")))
								{
									TArray<FString> Vals;
									for (const auto& EV : (*PropObj)->GetArrayField(TEXT("enum")))
									{
										FString S; if (EV->TryGetString(S)) { Vals.Add(S); }
									}
									Type = FString::Printf(TEXT("enum(%s)"), *FString::Join(Vals, TEXT("\\|")));
								}
								Desc.ReplaceInline(TEXT("|"), TEXT("\\|"));
								Desc.ReplaceInline(TEXT("\n"), TEXT(" "));
								Md += FString::Printf(TEXT("| `%s` | %s | %s | %s |\n"),
									*Pair.Key, *Type,
									Required.Contains(FString(*Pair.Key)) ? TEXT("yes") : TEXT(""),
									*Desc);
							}
							Md += TEXT("\n");
						}
					}
				}
			}

			Md += FString::Printf(TEXT("---\n\nTotal: **%d tools** across **%d categories**.\n\nCopyright StraySpark Studio 2026. All Rights Reserved.\n"), Total, Categories.Num());

			const FString OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MCPDocs"), TEXT("ToolReference.md"));
			if (!FFileHelper::SaveStringToFile(Md, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to write %s"), *OutPath));
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("path"), OutPath);
			Out->SetNumberField(TEXT("tools"), Total);
			Out->SetNumberField(TEXT("categories"), Categories.Num());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Wrote tool reference: %d tools, %d categories -> %s"), Total, Categories.Num(), *OutPath),
				Out);
		});

	// ================================================================
	// search_tools — discover tools by keyword without loading schemas
	// ================================================================
	MCP_TOOL(Registry, "search_tools")
		.Description(TEXT("Search the tool catalog by keyword. Returns matching tool names with one-line summaries and categories — call get_tool_schemas next for the full definitions of the tools you want to use. This server exposes 350+ tools; search instead of guessing names."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("query"), TEXT("Keywords describing what you want to do (e.g. 'spawn niagara particles', 'blueprint variable')"), true)
		.StringArg(TEXT("category"), TEXT("Optional: restrict to one category (see list_tool_categories)"))
		.IntArg(TEXT("limit"), TEXT("Max results (default 10, max 50)"))
		.Example(TEXT("{\"query\": \"create material parameter\", \"limit\": 5}"))
		.OutputSchema(TEXT(R"({
			"type": "object",
			"properties": {
				"total_matches": {"type": "integer"},
				"tools": {"type": "array", "items": {"type": "object", "properties": {
					"name": {"type": "string"}, "category": {"type": "string"}, "summary": {"type": "string"}},
					"required": ["name", "category", "summary"]}}
			},
			"required": ["total_matches", "tools"]
		})"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Query;
			if (!Args->TryGetStringField(TEXT("query"), Query) || Query.TrimStartAndEnd().IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("query is required"));
			}
			FString CategoryFilter;
			Args->TryGetStringField(TEXT("category"), CategoryFilter);
			int32 Limit = 10;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 50);
			}

			const FString QueryLower = Query.ToLower();
			TArray<FString> QueryTokens;
			QueryLower.ParseIntoArrayWS(QueryTokens);

			struct FHit { int32 Score; const FMCPToolDefinition* Tool; };
			TArray<FMCPToolDefinition> All = FMCPToolRegistry::Get().GetAllTools();
			TArray<FHit> Hits;
			for (const FMCPToolDefinition& Tool : All)
			{
				if (!CategoryFilter.IsEmpty() && !Tool.Category.ToString().Equals(CategoryFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}
				const int32 Score = ScoreTool(Tool, QueryLower, QueryTokens);
				if (Score > 0)
				{
					Hits.Add({ Score, &Tool });
				}
			}
			Hits.Sort([](const FHit& A, const FHit& B) { return A.Score > B.Score; });

			TArray<TSharedPtr<FJsonValue>> ToolsArr;
			for (int32 i = 0; i < FMath::Min(Limit, Hits.Num()); ++i)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Hits[i].Tool->Name);
				Entry->SetStringField(TEXT("category"), Hits[i].Tool->Category.ToString());
				Entry->SetStringField(TEXT("summary"), Summarize(Hits[i].Tool->Description));
				// v5 increment 24 (V5-15): the same annotations tools/list advertises, so discovery and calling agree.
				Entry->SetBoolField(TEXT("read_only"), Hits[i].Tool->bReadOnlyHint); Entry->SetBoolField(TEXT("destructive"), Hits[i].Tool->bDestructiveHint);
				Entry->SetBoolField(TEXT("idempotent"), Hits[i].Tool->bIdempotentHint); Entry->SetBoolField(TEXT("long_running"), Hits[i].Tool->bLongRunningHint);
				Entry->SetBoolField(TEXT("has_output_schema"), Hits[i].Tool->OutputSchema.IsValid());
				ToolsArr.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetNumberField(TEXT("total_matches"), Hits.Num());
			Out->SetArrayField(TEXT("tools"), ToolsArr);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d tools match '%s' (returning %d). Call get_tool_schemas with the names you need."),
					Hits.Num(), *Query, FMath::Min(Limit, Hits.Num())),
				Out);
		});

	// ================================================================
	// get_tool_schemas — full definitions on demand
	// ================================================================
	MCP_TOOL(Registry, "get_tool_schemas")
		.Description(TEXT("Fetch the complete definitions (description, full input schema with per-parameter docs, annotations) for one or more tools by name. Use after search_tools / list_tool_categories."))
		.ReadOnly()
		.Idempotent()
		.StringArrayArg(TEXT("names"), TEXT("Tool names to fetch (max 25 per call)"), true)
		.Example(TEXT("{\"names\": [\"create_actor\", \"set_actor_transform\"]}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
			if (!Args->TryGetArrayField(TEXT("names"), Names) || Names->Num() == 0)
			{
				return FMCPToolResult::Error(TEXT("names array is required"));
			}
			if (Names->Num() > 25)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Too many names (max 25 per call)."),
					TEXT("Fetch schemas in smaller batches — you rarely need more than a handful at once."));
			}

			TArray<TSharedPtr<FJsonValue>> ToolsArr;
			TArray<TSharedPtr<FJsonValue>> MissingArr;
			for (const TSharedPtr<FJsonValue>& NameVal : *Names)
			{
				FString Name;
				if (!NameVal->TryGetString(Name)) { continue; }
				if (const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(Name))
				{
					ToolsArr.Add(MakeShared<FJsonValueObject>(Tool->ToJson()));
				}
				else
				{
					MissingArr.Add(MakeShared<FJsonValueString>(Name));
				}
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetArrayField(TEXT("tools"), ToolsArr);
			if (MissingArr.Num() > 0)
			{
				Out->SetArrayField(TEXT("missing"), MissingArr);
			}
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Returning %d schema(s)%s."), ToolsArr.Num(),
					MissingArr.Num() > 0 ? *FString::Printf(TEXT(", %d name(s) not found"), MissingArr.Num()) : TEXT("")),
				Out);
		});

	// ================================================================
	// list_tool_categories — the catalog's table of contents
	// ================================================================
	MCP_TOOL(Registry, "list_tool_categories")
		.Description(TEXT("List all tool categories with their tool counts and names. The catalog's table of contents — combine with search_tools and get_tool_schemas to find what you need."))
		.ReadOnly()
		.Idempotent()
		.BoolArg(TEXT("include_tool_names"), TEXT("Include the tool-name list per category (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			bool bIncludeNames = true;
			Args->TryGetBoolField(TEXT("include_tool_names"), bIncludeNames);

			TMap<FName, TArray<FString>> ByCategory = FMCPToolRegistry::Get().GetToolsByCategory();
			TArray<FName> Categories;
			ByCategory.GenerateKeyArray(Categories);
			Categories.Sort(FNameLexicalLess());

			TArray<TSharedPtr<FJsonValue>> CatArr;
			int32 Total = 0;
			for (const FName& Cat : Categories)
			{
				const TArray<FString>& ToolNames = ByCategory[Cat];
				Total += ToolNames.Num();
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("category"), Cat.ToString());
				Entry->SetNumberField(TEXT("tool_count"), ToolNames.Num());
				if (bIncludeNames)
				{
					TArray<TSharedPtr<FJsonValue>> NamesArr;
					for (const FString& N : ToolNames) { NamesArr.Add(MakeShared<FJsonValueString>(N)); }
					Entry->SetArrayField(TEXT("tools"), NamesArr);
				}
				CatArr.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetNumberField(TEXT("total_tools"), Total);
			Out->SetArrayField(TEXT("categories"), CatArr);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d tools across %d categories."), Total, CatArr.Num()),
				Out);
		});

	// ================================================================
	// run_tool_script — restricted multi-step program (one transaction)
	// ================================================================
	{
		TSharedPtr<FJsonObject> ScriptSchema = MakeShared<FJsonObject>();
		ScriptSchema->SetStringField(TEXT("type"), TEXT("object"));

		MCP_TOOL(Registry, "run_tool_script")
			.Description(TEXT("Run a bounded tool script after structural, permission and known-argument preflight. Steps support tool, args, save_as, foreach and as; prior results use $name.field references. Result-dependent arguments and editor state are checked during execution. Failures may leave edits in place: no automatic rollback is claimed, and recorded changes remain available to editor undo. dry_run performs preflight only without executing children. Limits: 100 steps, 1000 invocations; nested scripts are prohibited."))
            .Preview([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
            {
                return PreflightScript(Args->GetObjectField(TEXT("script")), Context);
            })
			.BoolArg(TEXT("dry_run"), TEXT("Preflight only; do not execute child tools"))
            .ObjectArg(TEXT("script"), TEXT("The step program: {steps: [...]}"), ScriptSchema, true)
			.Example(TEXT("{\"script\": {\"steps\": [")
				TEXT("{\"tool\": \"create_actor\", \"args\": {\"class_name\": \"PointLight\", \"name\": \"Key\", \"x\": 0, \"y\": 0, \"z\": 300}, \"save_as\": \"key\"},")
				TEXT("{\"tool\": \"set_light_properties\", \"args\": {\"actor_name\": \"Key\", \"intensity\": 5000}}]}}"))
			.HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context) -> FMCPToolResult
			{
				const TSharedPtr<FJsonObject>* Script = nullptr;
				if (!Args->TryGetObjectField(TEXT("script"), Script))
				{
					return FMCPToolResult::Error(TEXT("script object is required"));
				}
				const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
				if (!(*Script)->TryGetArrayField(TEXT("steps"), Steps) || Steps->Num() == 0)
				{
					return FMCPToolResult::Error(TEXT("script.steps must be a non-empty array"));
				}
				if (Steps->Num() > MaxScriptSteps)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						FString::Printf(TEXT("Too many steps (%d, max %d)."), Steps->Num(), MaxScriptSteps));
				}
                if (ScriptExecuting) return FMCPToolResult::Error(TEXT("Nested script execution, including indirect dispatch, is unsupported"));
                TGuardValue<bool> ScriptGuard(ScriptExecuting, true);
                const FMCPToolResult Preflight = PreflightScript(*Script, Context);
                if (Preflight.bIsError) return Preflight;
				if (!GEditor)
				{
					return FMCPToolResult::Error(TEXT("GEditor unavailable"));
				}

				TMap<FString, TSharedPtr<FJsonValue>> Vars;
				TArray<TSharedPtr<FJsonValue>> StepResults;
				int32 Invocations = 0;

				// Marker for the creation journal: anything recorded after this point
				// was created by this script and will NOT be undone by CancelTransaction.
				const int32 CreationMark = MCPCommon::GetAssetCreationCount();

				FScopedTransaction ScriptTransaction(
					NSLOCTEXT("UnrealMCP", "ToolScript", "MCP Tool Script"));

				auto Fail = [&](const FString& Message, EMCPError Code = EMCPError::Internal) -> FMCPToolResult
				{
                    // End the RAII transaction normally so undo records survive.
                    // CancelTransaction discards history; it does not reverse edits.
					const TArray<FString> Survivors = MCPCommon::GetAssetsCreatedSince(CreationMark);

					TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
					Out->SetStringField(TEXT("code"), MCPErrorCodeToString(Code));
					Out->SetStringField(TEXT("message"), Message);
					TArray<TSharedPtr<FJsonValue>> Completed;
                    for (const auto& StepResult : StepResults)
                        if (StepResult->AsObject()->GetBoolField(TEXT("ok"))) Completed.Add(StepResult);
                    Out->SetArrayField(TEXT("completed_steps"), Completed);
                    Out->SetArrayField(TEXT("step_results"), StepResults);
					Out->SetBoolField(TEXT("rolled_back"), false);
                    Out->SetBoolField(TEXT("effects_may_remain"), Invocations > 0);
                    Out->SetObjectField(TEXT("preflight"), Preflight.StructuredContent);
					Out->SetStringField(TEXT("rollback"), TEXT("not_attempted"));

					TArray<TSharedPtr<FJsonValue>> SurvivorArray;
					for (const FString& Path : Survivors)
					{
						SurvivorArray.Add(MakeShared<FJsonValueString>(Path));
					}
					Out->SetArrayField(TEXT("assets_not_rolled_back"), SurvivorArray);

                    FString Text = Message + TEXT(" — automatic rollback was not attempted. Edits and external effects may remain; inspect the step results before retrying. Undo history for recorded edits is retained.");

					FMCPToolResult R;
					R.bIsError = true;
					R.Content.Add(FMCPContentBlock::MakeText(Text));
					R.StructuredContent = Out;
					return R;
				};

				for (int32 StepIdx = 0; StepIdx < Steps->Num(); ++StepIdx)
				{
					if (Context.IsCancelled())
					{
						return Fail(FString::Printf(TEXT("Cancelled at step %d"), StepIdx));
					}

					const TSharedPtr<FJsonObject>* Step = nullptr;
					if (!(*Steps)[StepIdx]->TryGetObject(Step))
					{
						return Fail(FString::Printf(TEXT("Step %d is not an object"), StepIdx), EMCPError::OutOfRange);
					}

					FString ToolName;
					if (!(*Step)->TryGetStringField(TEXT("tool"), ToolName))
					{
						return Fail(FString::Printf(TEXT("Step %d: 'tool' is required"), StepIdx), EMCPError::OutOfRange);
					}
					if (ToolName == TEXT("run_tool_script"))
					{
						return Fail(FString::Printf(TEXT("Step %d: nested run_tool_script is not allowed"), StepIdx), EMCPError::Unsupported);
					}

					FString SaveAs;
					(*Step)->TryGetStringField(TEXT("save_as"), SaveAs);

					TSharedPtr<FJsonValue> StepArgs = (*Step)->HasField(TEXT("args"))
						? (*Step)->TryGetField(TEXT("args"))
						: MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());

					// foreach: literal array or "$var" reference to a saved array.
					TArray<TSharedPtr<FJsonValue>> LoopValues;
					bool bIsLoop = false;
					FString LoopVar = TEXT("item");
					(*Step)->TryGetStringField(TEXT("as"), LoopVar);
					if ((*Step)->HasField(TEXT("foreach")))
					{
						bIsLoop = true;
						TSharedPtr<FJsonValue> ForeachVal = (*Step)->TryGetField(TEXT("foreach"));
						if (ForeachVal->Type == EJson::String)
						{
							FString RefError;
							ForeachVal = ResolveRef(ForeachVal->AsString(), Vars, RefError);
							if (!RefError.IsEmpty())
							{
								return Fail(FString::Printf(TEXT("Step %d foreach: %s"), StepIdx, *RefError), EMCPError::NotFound);
							}
						}
						const TArray<TSharedPtr<FJsonValue>>* AsArray = nullptr;
						if (!ForeachVal.IsValid() || !ForeachVal->TryGetArray(AsArray))
						{
							return Fail(FString::Printf(TEXT("Step %d: foreach must be an array"), StepIdx), EMCPError::OutOfRange);
						}
						LoopValues = *AsArray;
					}
					else
					{
						LoopValues.Add(nullptr); // single pass
					}

					for (const TSharedPtr<FJsonValue>& LoopValue : LoopValues)
					{
						if (++Invocations > MaxScriptInvocations)
						{
							return Fail(FString::Printf(TEXT("Invocation limit reached (%d)"), MaxScriptInvocations), EMCPError::OutOfRange);
						}

						if (bIsLoop)
						{
							Vars.Add(LoopVar, LoopValue);
						}

						FString SubError;
						TSharedPtr<FJsonValue> ResolvedArgs = SubstituteRefs(StepArgs, Vars, SubError);
						if (!SubError.IsEmpty())
						{
							return Fail(FString::Printf(TEXT("Step %d: %s"), StepIdx, *SubError), EMCPError::NotFound);
						}
						const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
						if (!ResolvedArgs.IsValid() || !ResolvedArgs->TryGetObject(ArgsObj))
						{
							return Fail(FString::Printf(TEXT("Step %d: args must resolve to an object"), StepIdx), EMCPError::OutOfRange);
						}

						// Scope / PIE / dry-run checks all re-apply per inner call.
						FMCPToolResult StepResult = FMCPToolRegistry::Get().ExecuteTool(ToolName, *ArgsObj, Context);

						TSharedPtr<FJsonObject> StepOut = MakeShared<FJsonObject>();
						StepOut->SetNumberField(TEXT("step"), StepIdx);
						StepOut->SetStringField(TEXT("tool"), ToolName);
						StepOut->SetBoolField(TEXT("ok"), !StepResult.bIsError);
						StepOut->SetStringField(TEXT("text"),
							StepResult.Content.Num() > 0 ? StepResult.Content[0].Text : FString());
						if (StepResult.StructuredContent.IsValid())
						{
							StepOut->SetObjectField(TEXT("structured"), StepResult.StructuredContent);
						}
						StepResults.Add(MakeShared<FJsonValueObject>(StepOut));

						if (StepResult.bIsError)
						{
							return Fail(FString::Printf(TEXT("Step %d (%s) failed: %s"), StepIdx, *ToolName,
								StepResult.Content.Num() > 0 ? *StepResult.Content[0].Text : TEXT("unknown error")));
						}

						if (!SaveAs.IsEmpty())
						{
							Vars.Add(SaveAs, ResultToVar(StepResult));
						}
					}
				}


				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetNumberField(TEXT("invocations"), Invocations);
                Out->SetObjectField(TEXT("preflight"), Preflight.StructuredContent);
				Out->SetArrayField(TEXT("steps"), StepResults);
				return FMCPToolResult::SuccessStructured(
					FString::Printf(TEXT("Script completed: %d step(s), %d invocation(s), undo-recorded edits grouped; external effects are not covered by undo."),
						Steps->Num(), Invocations),
					Out);
			});
	}
}

} // namespace MCPMetaTools
