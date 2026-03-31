#include "Tools/MCPMaterialGraphTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Async/Async.h"

namespace MCPMaterialGraphTools
{

// ============================================================================
// Internal helpers
// ============================================================================

/** Load a UMaterial from a content path. Returns nullptr and sets OutError on failure. */
static UMaterial* LoadMaterial(const FString& MaterialPath, FString& OutError)
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Material)
	{
		OutError = FString::Printf(TEXT("Material not found at path: %s"), *MaterialPath);
	}
	return Material;
}

/** Get the expression array from a material as raw pointers. */
static TArray<UMaterialExpression*> GetExpressions(UMaterial* Material)
{
	TArray<UMaterialExpression*> Result;
	for (const auto& Expr : Material->GetExpressionCollection().Expressions)
	{
		Result.Add(Expr.Get());
	}
	return Result;
}

/** Serialize a JSON object to a string. */
static FString SerializeJson(const TSharedRef<FJsonObject>& JsonObj)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(JsonObj, Writer);
	return Output;
}

// ============================================================================
// RegisterAll
// ============================================================================

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_material_expression - Add a material expression node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the target UMaterial asset (e.g., '/Game/Materials/M_MyMat')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("expression_class"),
			TEXT("Unreal expression class name without the leading 'U' prefix, e.g. 'MaterialExpressionAdd', "
				 "'MaterialExpressionMultiply', 'MaterialExpressionConstant', 'MaterialExpressionVectorParameter', "
				 "'MaterialExpressionTextureSample'"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("node_x"),
			TEXT("Editor graph X position for the new node (default: 0)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("node_y"),
			TEXT("Editor graph Y position for the new node (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name        = TEXT("add_material_expression");
		Def.Description = TEXT("Add a material expression node to a UMaterial asset's expression graph. "
			"Specify the class by its short Unreal name (e.g. 'MaterialExpressionAdd'). "
			"Returns the zero-based index of the newly created expression for use in subsequent connect/set calls.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath, ExpressionClass;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));
			if (!Args->TryGetStringField(TEXT("expression_class"), ExpressionClass))
				return FMCPToolResult::Error(TEXT("expression_class is required"));

			int32 NodeX = 0, NodeY = 0;
			if (Args->HasField(TEXT("node_x"))) NodeX = (int32)Args->GetNumberField(TEXT("node_x"));
			if (Args->HasField(TEXT("node_y"))) NodeY = (int32)Args->GetNumberField(TEXT("node_y"));

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;
			int32 NewExpressionIndex = -1;
			FString NewExpressionName;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				// Resolve the UClass. The user passes the short name without leading 'U'.
				// Try "U<name>" first (standard UObject naming convention), then bare name.
				FString FullClassName = FString::Printf(TEXT("U%s"), *ExpressionClass);
				UClass* ExprClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::ExactClass);
				if (!ExprClass)
				{
					ExprClass = FindFirstObject<UClass>(*ExpressionClass, EFindFirstObjectOptions::ExactClass);
				}
				if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
				{
					ErrorMessage = FString::Printf(
						TEXT("Could not find a UMaterialExpression subclass for '%s'. "
							 "Tried 'U%s' and '%s'. Check the class name spelling."),
						*ExpressionClass, *ExpressionClass, *ExpressionClass);
					DoneEvent->Trigger();
					return;
				}

				Material->PreEditChange(nullptr);

				UMaterialExpression* NewExpression = NewObject<UMaterialExpression>(
					Material, ExprClass, NAME_None, RF_Transactional);

				if (!NewExpression)
				{
					ErrorMessage = FString::Printf(TEXT("NewObject failed for class '%s'"), *ExpressionClass);
					DoneEvent->Trigger();
					return;
				}

				NewExpression->MaterialExpressionEditorX = NodeX;
				NewExpression->MaterialExpressionEditorY = NodeY;

				Material->GetExpressionCollection().Expressions.Add(NewExpression);
				NewExpressionIndex = Material->GetExpressionCollection().Expressions.Num() - 1;
				NewExpressionName  = NewExpression->GetName();

				Material->PostEditChange();
				Material->MarkPackageDirty();

				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			ResultJson->SetStringField(TEXT("expression_name"),  NewExpressionName);
			ResultJson->SetNumberField(TEXT("expression_index"), NewExpressionIndex);
			ResultJson->SetStringField(TEXT("expression_class"), ExpressionClass);
			ResultJson->SetNumberField(TEXT("node_x"),           NodeX);
			ResultJson->SetNumberField(TEXT("node_y"),           NodeY);
			ResultJson->SetStringField(TEXT("material_path"),    MaterialPath);

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// connect_material_expression - Wire expression outputs to inputs
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the UMaterial asset"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("source_expression_index"),
			TEXT("Zero-based index of the source expression in the material's Expressions array"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("source_output_index"),
			TEXT("Index of the output pin on the source expression (default: 0)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("target_expression_index"),
			TEXT("Zero-based index of the target expression. Use -1 to target a material output slot "
				 "(BaseColor=0, Metallic=1, Specular=2, Roughness=3, EmissiveColor=4, "
				 "Opacity=5, Normal=6, WorldPositionOffset=7)"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("target_input_index"),
			TEXT("Index of the input pin on the target expression, or material output slot index when "
				 "target_expression_index is -1 (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name        = TEXT("connect_material_expression");
		Def.Description = TEXT("Connect a material expression's output pin to another expression's input pin "
			"or to a material output slot (BaseColor, Metallic, etc.). "
			"Set target_expression_index to -1 to connect to a material output; "
			"target_input_index then selects the slot: 0=BaseColor 1=Metallic 2=Specular "
			"3=Roughness 4=EmissiveColor 5=Opacity 6=Normal 7=WorldPositionOffset.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));

			int32 SrcIdx = -1, TgtIdx = -1;
			if (!Args->HasField(TEXT("source_expression_index")))
				return FMCPToolResult::Error(TEXT("source_expression_index is required"));
			if (!Args->HasField(TEXT("target_expression_index")))
				return FMCPToolResult::Error(TEXT("target_expression_index is required"));

			SrcIdx = (int32)Args->GetNumberField(TEXT("source_expression_index"));
			TgtIdx = (int32)Args->GetNumberField(TEXT("target_expression_index"));
			int32 SrcOutIdx = Args->HasField(TEXT("source_output_index")) ? (int32)Args->GetNumberField(TEXT("source_output_index")) : 0;
			int32 TgtInIdx  = Args->HasField(TEXT("target_input_index"))  ? (int32)Args->GetNumberField(TEXT("target_input_index"))  : 0;

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;
			FString ConnectionDesc;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				TArray<UMaterialExpression*> Expressions = GetExpressions(Material);

				if (SrcIdx < 0 || SrcIdx >= Expressions.Num() || !Expressions[SrcIdx])
				{
					ErrorMessage = FString::Printf(
						TEXT("source_expression_index %d is out of range (0..%d)"),
						SrcIdx, Expressions.Num() - 1);
					DoneEvent->Trigger();
					return;
				}

				UMaterialExpression* SrcExpression = Expressions[SrcIdx];

				// Gather outputs of the source expression
				TArray<FExpressionOutput>& SrcOutputs = SrcExpression->GetOutputs();
				if (SrcOutputs.Num() == 0)
				{
					ErrorMessage = FString::Printf(
						TEXT("Source expression '%s' has no outputs"), *SrcExpression->GetName());
					DoneEvent->Trigger();
					return;
				}
				if (SrcOutIdx >= SrcOutputs.Num())
				{
					ErrorMessage = FString::Printf(
						TEXT("source_output_index %d is out of range for expression '%s' (has %d outputs)"),
						SrcOutIdx, *SrcExpression->GetName(), SrcOutputs.Num());
					DoneEvent->Trigger();
					return;
				}

				Material->PreEditChange(nullptr);

				if (TgtIdx == -1)
				{
					// Connect to a material output slot.
					// UE5.4+ uses GetEditorOnlyData(); older builds expose members directly on UMaterial.
					// We use a lambda to select the correct FExpressionInput* for the requested slot.
					auto GetMaterialInputForSlot = [&](int32 SlotIndex) -> FExpressionInput*
					{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
						UMaterialEditorOnlyData* EdData = Material->GetEditorOnlyData();
						if (!EdData) return nullptr;
						switch (SlotIndex)
						{
						case 0:  return &EdData->BaseColor;
						case 1:  return &EdData->Metallic;
						case 2:  return &EdData->Specular;
						case 3:  return &EdData->Roughness;
						case 4:  return &EdData->EmissiveColor;
						case 5:  return &EdData->Opacity;
						case 6:  return &EdData->Normal;
						case 7:  return &EdData->WorldPositionOffset;
						default: return nullptr;
						}
#else
						switch (SlotIndex)
						{
						case 0:  return &Material->BaseColor;
						case 1:  return &Material->Metallic;
						case 2:  return &Material->Specular;
						case 3:  return &Material->Roughness;
						case 4:  return &Material->EmissiveColor;
						case 5:  return &Material->Opacity;
						case 6:  return &Material->Normal;
						case 7:  return &Material->WorldPositionOffset;
						default: return nullptr;
						}
#endif
					};

					static const TCHAR* SlotNames[] = {
						TEXT("BaseColor"), TEXT("Metallic"), TEXT("Specular"), TEXT("Roughness"),
						TEXT("EmissiveColor"), TEXT("Opacity"), TEXT("Normal"), TEXT("WorldPositionOffset")
					};

					FExpressionInput* MaterialInput = GetMaterialInputForSlot(TgtInIdx);
					if (!MaterialInput)
					{
						ErrorMessage = FString::Printf(
							TEXT("target_input_index %d is not a valid material output slot (0-7)"), TgtInIdx);
						DoneEvent->Trigger();
						return;
					}

					MaterialInput->Connect(SrcOutIdx, SrcExpression);

					const TCHAR* SlotName = (TgtInIdx >= 0 && TgtInIdx <= 7) ? SlotNames[TgtInIdx] : TEXT("Unknown");
					ConnectionDesc = FString::Printf(
						TEXT("Connected '%s'[output %d] -> Material.%s"),
						*SrcExpression->GetName(), SrcOutIdx, SlotName);
				}
				else
				{
					// Connect to another expression's input.
					if (TgtIdx < 0 || TgtIdx >= Expressions.Num() || !Expressions[TgtIdx])
					{
						ErrorMessage = FString::Printf(
							TEXT("target_expression_index %d is out of range (0..%d)"),
							TgtIdx, Expressions.Num() - 1);
						DoneEvent->Trigger();
						return;
					}

					UMaterialExpression* TgtExpression = Expressions[TgtIdx];
					TArray<FExpressionInput*> TgtInputs;
					for (int32 ii = 0; ii < TgtExpression->CountInputs(); ++ii)
					{
						TgtInputs.Add(TgtExpression->GetInput(ii));
					}

					if (TgtInputs.Num() == 0)
					{
						ErrorMessage = FString::Printf(
							TEXT("Target expression '%s' has no inputs"), *TgtExpression->GetName());
						DoneEvent->Trigger();
						return;
					}
					if (TgtInIdx >= TgtInputs.Num())
					{
						ErrorMessage = FString::Printf(
							TEXT("target_input_index %d is out of range for expression '%s' (has %d inputs)"),
							TgtInIdx, *TgtExpression->GetName(), TgtInputs.Num());
						DoneEvent->Trigger();
						return;
					}

					TgtInputs[TgtInIdx]->Connect(SrcOutIdx, SrcExpression);

					ConnectionDesc = FString::Printf(
						TEXT("Connected '%s'[output %d] -> '%s'[input %d]"),
						*SrcExpression->GetName(), SrcOutIdx,
						*TgtExpression->GetName(), TgtInIdx);
				}

				Material->PostEditChange();
				Material->MarkPackageDirty();
				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			ResultJson->SetStringField(TEXT("connection"), ConnectionDesc);
			ResultJson->SetStringField(TEXT("material_path"), MaterialPath);

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_material_expression_value - Set a property on an expression
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the UMaterial asset"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("expression_index"),
			TEXT("Zero-based index of the expression in the Expressions array"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("property_name"),
			TEXT("Property to set. Common values: 'Const' (UMaterialExpressionConstant scalar), "
				 "'ConstR'/'ConstG'/'ConstB'/'ConstA' (Constant3/4Vector channels), "
				 "'DefaultValue' (vector parameter FLinearColor as 'R,G,B,A'), "
				 "'ParameterName' (FName for parameter expressions)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("value"),
			TEXT("String representation of the value. Floats: '0.5'. "
				 "FLinearColor: '1.0,0.5,0.0,1.0'. FName: 'MyParamName'."), true);

		FMCPToolDefinition Def;
		Def.Name        = TEXT("set_material_expression_value");
		Def.Description = TEXT("Set a default value or property on a material expression node by index. "
			"Supports setting scalar constants (Const), per-channel vector values (ConstR/ConstG/ConstB/ConstA), "
			"vector parameter defaults (DefaultValue as 'R,G,B,A'), and parameter names (ParameterName).");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath, PropertyName, Value;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));
			if (!Args->HasField(TEXT("expression_index")))
				return FMCPToolResult::Error(TEXT("expression_index is required"));
			if (!Args->TryGetStringField(TEXT("property_name"), PropertyName))
				return FMCPToolResult::Error(TEXT("property_name is required"));
			if (!Args->TryGetStringField(TEXT("value"), Value))
				return FMCPToolResult::Error(TEXT("value is required"));

			int32 ExprIdx = (int32)Args->GetNumberField(TEXT("expression_index"));

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;
			FString OperationDesc;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				TArray<UMaterialExpression*> Expressions = GetExpressions(Material);
				if (ExprIdx < 0 || ExprIdx >= Expressions.Num() || !Expressions[ExprIdx])
				{
					ErrorMessage = FString::Printf(
						TEXT("expression_index %d is out of range (0..%d)"),
						ExprIdx, Expressions.Num() - 1);
					DoneEvent->Trigger();
					return;
				}

				UMaterialExpression* Expression = Expressions[ExprIdx];
				Material->PreEditChange(nullptr);

				bool bPropertySet = false;

				// --- UMaterialExpressionConstant: Const (scalar) ---
				if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expression))
				{
					if (PropertyName == TEXT("Const") || PropertyName == TEXT("R"))
					{
						ConstExpr->R = FCString::Atof(*Value);
						bPropertySet = true;
						OperationDesc = FString::Printf(TEXT("Set Const.R = %s on '%s'"), *Value, *Expression->GetName());
					}
				}

				// --- UMaterialExpressionScalarParameter: DefaultValue, ParameterName ---
				if (!bPropertySet)
				{
					if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
					{
						if (PropertyName == TEXT("DefaultValue") || PropertyName == TEXT("Const"))
						{
							ScalarParam->DefaultValue = FCString::Atof(*Value);
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set DefaultValue = %s on ScalarParameter '%s'"), *Value, *Expression->GetName());
						}
						else if (PropertyName == TEXT("ParameterName"))
						{
							ScalarParam->ParameterName = FName(*Value);
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set ParameterName = '%s' on ScalarParameter"), *Value);
						}
					}
				}

				// --- UMaterialExpressionVectorParameter: DefaultValue (R,G,B,A), ParameterName ---
				if (!bPropertySet)
				{
					if (UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expression))
					{
						if (PropertyName == TEXT("DefaultValue"))
						{
							TArray<FString> Components;
							Value.ParseIntoArray(Components, TEXT(","), true);
							FLinearColor Color = FLinearColor::Black;
							if (Components.Num() >= 1) Color.R = FCString::Atof(*Components[0]);
							if (Components.Num() >= 2) Color.G = FCString::Atof(*Components[1]);
							if (Components.Num() >= 3) Color.B = FCString::Atof(*Components[2]);
							if (Components.Num() >= 4) Color.A = FCString::Atof(*Components[3]);
							VecParam->DefaultValue = Color;
							bPropertySet = true;
							OperationDesc = FString::Printf(
								TEXT("Set DefaultValue = (%.3f,%.3f,%.3f,%.3f) on VectorParameter '%s'"),
								Color.R, Color.G, Color.B, Color.A, *Expression->GetName());
						}
						else if (PropertyName == TEXT("ParameterName"))
						{
							VecParam->ParameterName = FName(*Value);
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set ParameterName = '%s' on VectorParameter"), *Value);
						}
					}
				}

				// --- UMaterialExpressionTextureObjectParameter: ParameterName ---
				if (!bPropertySet)
				{
					if (UMaterialExpressionTextureObjectParameter* TexParam = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
					{
						if (PropertyName == TEXT("ParameterName"))
						{
							TexParam->ParameterName = FName(*Value);
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set ParameterName = '%s' on TextureObjectParameter"), *Value);
						}
					}
				}

				// --- Generic FProperty fallback for any other numeric or name property ---
				if (!bPropertySet)
				{
					UClass* ExprClass = Expression->GetClass();
					FProperty* Prop = ExprClass->FindPropertyByName(FName(*PropertyName));
					if (Prop)
					{
						void* PropertyAddr = Prop->ContainerPtrToValuePtr<void>(Expression);

						if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
						{
							FloatProp->SetPropertyValue(PropertyAddr, FCString::Atof(*Value));
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set float property '%s' = %s on '%s'"),
								*PropertyName, *Value, *Expression->GetName());
						}
						else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
						{
							DoubleProp->SetPropertyValue(PropertyAddr, FCString::Atod(*Value));
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set double property '%s' = %s on '%s'"),
								*PropertyName, *Value, *Expression->GetName());
						}
						else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
						{
							NameProp->SetPropertyValue(PropertyAddr, FName(*Value));
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set name property '%s' = '%s' on '%s'"),
								*PropertyName, *Value, *Expression->GetName());
						}
						else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
						{
							StrProp->SetPropertyValue(PropertyAddr, Value);
							bPropertySet = true;
							OperationDesc = FString::Printf(TEXT("Set string property '%s' = '%s' on '%s'"),
								*PropertyName, *Value, *Expression->GetName());
						}
					}
				}

				if (!bPropertySet)
				{
					ErrorMessage = FString::Printf(
						TEXT("Property '%s' is not recognized or settable on expression class '%s'. "
							 "Supported: Const, DefaultValue, ParameterName, ConstR/G/B/A, or any float/name FProperty."),
						*PropertyName, *Expression->GetClass()->GetName());
					DoneEvent->Trigger();
					return;
				}

				Material->PostEditChange();
				Material->MarkPackageDirty();
				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			ResultJson->SetStringField(TEXT("operation"), OperationDesc);
			ResultJson->SetStringField(TEXT("material_path"), MaterialPath);

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_material_expressions - List all expressions in a material
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the UMaterial asset to inspect"), true);

		FMCPToolDefinition Def;
		Def.Name           = TEXT("get_material_expressions");
		Def.Description    = TEXT("List every expression node in a UMaterial's graph with its index, class name, "
			"editor graph position, and connected input information. "
			"Use the returned indices with add/connect/set/remove tools.");
		Def.InputSchema    = Schema;
		Def.bReadOnlyHint  = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				TArray<UMaterialExpression*> Expressions = GetExpressions(Material);

				TArray<TSharedPtr<FJsonValue>> ExprArray;
				for (int32 i = 0; i < Expressions.Num(); ++i)
				{
					UMaterialExpression* Expr = Expressions[i];
					if (!Expr) continue;

					TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
					ExprObj->SetNumberField(TEXT("index"),      i);
					ExprObj->SetStringField(TEXT("class_name"), Expr->GetClass()->GetName());
					ExprObj->SetStringField(TEXT("name"),       Expr->GetName());
					ExprObj->SetNumberField(TEXT("position_x"), Expr->MaterialExpressionEditorX);
					ExprObj->SetNumberField(TEXT("position_y"), Expr->MaterialExpressionEditorY);

					// Describe inputs
					TArray<FExpressionInput*> Inputs;
					for (int32 ii = 0; ii < Expr->CountInputs(); ++ii)
					{
						Inputs.Add(Expr->GetInput(ii));
					}
					TArray<TSharedPtr<FJsonValue>> InputsArray;
					for (int32 k = 0; k < Inputs.Num(); ++k)
					{
						FExpressionInput* Input = Inputs[k];
						TSharedPtr<FJsonObject> InputObj = MakeShared<FJsonObject>();
						InputObj->SetNumberField(TEXT("input_index"), k);
						InputObj->SetStringField(TEXT("input_name"),  Expr->GetInputName(k).ToString());
						bool bConnected = (Input && Input->Expression != nullptr);
						InputObj->SetBoolField(TEXT("connected"), bConnected);
						if (bConnected)
						{
							// Find the connected expression's index in the array
							int32 ConnectedIdx = Expressions.Find(Input->Expression);
							InputObj->SetNumberField(TEXT("connected_to_index"), ConnectedIdx);
							InputObj->SetStringField(TEXT("connected_to_name"), Input->Expression->GetName());
							InputObj->SetNumberField(TEXT("connected_output_index"), Input->OutputIndex);
						}
						InputsArray.Add(MakeShared<FJsonValueObject>(InputObj));
					}
					ExprObj->SetArrayField(TEXT("inputs"), InputsArray);

					// Describe outputs
					TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
					TArray<TSharedPtr<FJsonValue>> OutputsArray;
					for (int32 k = 0; k < Outputs.Num(); ++k)
					{
						TSharedPtr<FJsonObject> OutputObj = MakeShared<FJsonObject>();
						OutputObj->SetNumberField(TEXT("output_index"), k);
						OutputObj->SetStringField(TEXT("output_name"),  Outputs[k].OutputName.ToString());
						OutputsArray.Add(MakeShared<FJsonValueObject>(OutputObj));
					}
					ExprObj->SetArrayField(TEXT("outputs"), OutputsArray);

					ExprArray.Add(MakeShared<FJsonValueObject>(ExprObj));
				}

				ResultJson->SetArrayField(TEXT("expressions"),      ExprArray);
				ResultJson->SetNumberField(TEXT("expression_count"), Expressions.Num());
				ResultJson->SetStringField(TEXT("material_path"),    MaterialPath);
				ResultJson->SetStringField(TEXT("material_name"),    Material->GetName());

				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_texture_sample_expression - Add a TextureSample with a texture assigned
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the UMaterial asset"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("texture_path"),
			TEXT("Content path of the UTexture asset to assign to the sample node "
				 "(e.g., '/Game/Textures/T_Rock_D')"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("node_x"),
			TEXT("Editor graph X position (default: -300)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("node_y"),
			TEXT("Editor graph Y position (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name        = TEXT("add_texture_sample_expression");
		Def.Description = TEXT("Convenience tool: add a UMaterialExpressionTextureSample node to a material "
			"with a specific texture asset already assigned. "
			"Returns the new expression's index for use in connect_material_expression.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath, TexturePath;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));
			if (!Args->TryGetStringField(TEXT("texture_path"), TexturePath))
				return FMCPToolResult::Error(TEXT("texture_path is required"));

			int32 NodeX = -300, NodeY = 0;
			if (Args->HasField(TEXT("node_x"))) NodeX = (int32)Args->GetNumberField(TEXT("node_x"));
			if (Args->HasField(TEXT("node_y"))) NodeY = (int32)Args->GetNumberField(TEXT("node_y"));

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;
			int32 NewExpressionIndex = -1;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
				if (!Texture)
				{
					ErrorMessage = FString::Printf(TEXT("Texture not found at path: %s"), *TexturePath);
					DoneEvent->Trigger();
					return;
				}

				Material->PreEditChange(nullptr);

				UMaterialExpressionTextureSample* TexSample = NewObject<UMaterialExpressionTextureSample>(
					Material, UMaterialExpressionTextureSample::StaticClass(), NAME_None, RF_Transactional);

				if (!TexSample)
				{
					ErrorMessage = TEXT("Failed to create UMaterialExpressionTextureSample");
					DoneEvent->Trigger();
					return;
				}

				TexSample->Texture = Texture;
				TexSample->MaterialExpressionEditorX = NodeX;
				TexSample->MaterialExpressionEditorY = NodeY;
				// Auto-set sampler type based on texture
				TexSample->AutoSetSampleType();

				Material->GetExpressionCollection().Expressions.Add(TexSample);
				NewExpressionIndex = Material->GetExpressionCollection().Expressions.Num() - 1;

				Material->PostEditChange();
				Material->MarkPackageDirty();
				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			ResultJson->SetNumberField(TEXT("expression_index"), NewExpressionIndex);
			ResultJson->SetStringField(TEXT("expression_class"), TEXT("MaterialExpressionTextureSample"));
			ResultJson->SetStringField(TEXT("texture_path"),     TexturePath);
			ResultJson->SetNumberField(TEXT("node_x"),           NodeX);
			ResultJson->SetNumberField(TEXT("node_y"),           NodeY);
			ResultJson->SetStringField(TEXT("material_path"),    MaterialPath);

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_material_parameter_expression - Add Scalar/Vector/Texture parameter
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the UMaterial asset"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("parameter_type"),
			TEXT("Type of parameter expression to add"),
			{ TEXT("Scalar"), TEXT("Vector"), TEXT("Texture") }, true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parameter_name"),
			TEXT("Name for the parameter as it appears in material instances (e.g., 'Roughness', 'BaseColor')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("default_value"),
			TEXT("Default value for the parameter. "
				 "Scalar: '0.5'. Vector: '1.0,0.5,0.0,1.0' (R,G,B,A). Texture: omit or unused."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("node_x"),
			TEXT("Editor graph X position (default: -400)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("node_y"),
			TEXT("Editor graph Y position (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name        = TEXT("add_material_parameter_expression");
		Def.Description = TEXT("Add a scalar, vector, or texture parameter expression to a material. "
			"Parameter expressions are exposed when creating material instances, "
			"allowing per-instance value overrides without recompiling. "
			"Returns the new expression's index.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath, ParamType, ParamName;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));
			if (!Args->TryGetStringField(TEXT("parameter_type"), ParamType))
				return FMCPToolResult::Error(TEXT("parameter_type is required"));
			if (!Args->TryGetStringField(TEXT("parameter_name"), ParamName))
				return FMCPToolResult::Error(TEXT("parameter_name is required"));

			FString DefaultValue;
			Args->TryGetStringField(TEXT("default_value"), DefaultValue);

			int32 NodeX = -400, NodeY = 0;
			if (Args->HasField(TEXT("node_x"))) NodeX = (int32)Args->GetNumberField(TEXT("node_x"));
			if (Args->HasField(TEXT("node_y"))) NodeY = (int32)Args->GetNumberField(TEXT("node_y"));

			if (ParamType != TEXT("Scalar") && ParamType != TEXT("Vector") && ParamType != TEXT("Texture"))
				return FMCPToolResult::Error(TEXT("parameter_type must be one of: Scalar, Vector, Texture"));

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;
			int32 NewExpressionIndex = -1;
			FString ActualClassName;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				Material->PreEditChange(nullptr);

				UMaterialExpression* NewExpression = nullptr;

				if (ParamType == TEXT("Scalar"))
				{
					UMaterialExpressionScalarParameter* ScalarParam = NewObject<UMaterialExpressionScalarParameter>(
						Material, UMaterialExpressionScalarParameter::StaticClass(), NAME_None, RF_Transactional);
					if (!ScalarParam)
					{
						ErrorMessage = TEXT("Failed to create UMaterialExpressionScalarParameter");
						DoneEvent->Trigger();
						return;
					}
					ScalarParam->ParameterName = FName(*ParamName);
					if (!DefaultValue.IsEmpty())
					{
						ScalarParam->DefaultValue = FCString::Atof(*DefaultValue);
					}
					NewExpression = ScalarParam;
					ActualClassName = TEXT("MaterialExpressionScalarParameter");
				}
				else if (ParamType == TEXT("Vector"))
				{
					UMaterialExpressionVectorParameter* VecParam = NewObject<UMaterialExpressionVectorParameter>(
						Material, UMaterialExpressionVectorParameter::StaticClass(), NAME_None, RF_Transactional);
					if (!VecParam)
					{
						ErrorMessage = TEXT("Failed to create UMaterialExpressionVectorParameter");
						DoneEvent->Trigger();
						return;
					}
					VecParam->ParameterName = FName(*ParamName);
					if (!DefaultValue.IsEmpty())
					{
						TArray<FString> Components;
						DefaultValue.ParseIntoArray(Components, TEXT(","), true);
						FLinearColor Color = FLinearColor::Black;
						if (Components.Num() >= 1) Color.R = FCString::Atof(*Components[0]);
						if (Components.Num() >= 2) Color.G = FCString::Atof(*Components[1]);
						if (Components.Num() >= 3) Color.B = FCString::Atof(*Components[2]);
						if (Components.Num() >= 4) Color.A = FCString::Atof(*Components[3]);
						else Color.A = 1.0f;
						VecParam->DefaultValue = Color;
					}
					NewExpression = VecParam;
					ActualClassName = TEXT("MaterialExpressionVectorParameter");
				}
				else // Texture
				{
					UMaterialExpressionTextureObjectParameter* TexParam = NewObject<UMaterialExpressionTextureObjectParameter>(
						Material, UMaterialExpressionTextureObjectParameter::StaticClass(), NAME_None, RF_Transactional);
					if (!TexParam)
					{
						ErrorMessage = TEXT("Failed to create UMaterialExpressionTextureObjectParameter");
						DoneEvent->Trigger();
						return;
					}
					TexParam->ParameterName = FName(*ParamName);
					NewExpression = TexParam;
					ActualClassName = TEXT("MaterialExpressionTextureObjectParameter");
				}

				NewExpression->MaterialExpressionEditorX = NodeX;
				NewExpression->MaterialExpressionEditorY = NodeY;

				Material->GetExpressionCollection().Expressions.Add(NewExpression);
				NewExpressionIndex = Material->GetExpressionCollection().Expressions.Num() - 1;

				Material->PostEditChange();
				Material->MarkPackageDirty();
				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			ResultJson->SetNumberField(TEXT("expression_index"), NewExpressionIndex);
			ResultJson->SetStringField(TEXT("expression_class"), ActualClassName);
			ResultJson->SetStringField(TEXT("parameter_name"),   ParamName);
			ResultJson->SetStringField(TEXT("parameter_type"),   ParamType);
			ResultJson->SetNumberField(TEXT("node_x"),           NodeX);
			ResultJson->SetNumberField(TEXT("node_y"),           NodeY);
			ResultJson->SetStringField(TEXT("material_path"),    MaterialPath);

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// remove_material_expression - Remove an expression by index
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the UMaterial asset"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("expression_index"),
			TEXT("Zero-based index of the expression to remove from the material's Expressions array"), true);

		FMCPToolDefinition Def;
		Def.Name             = TEXT("remove_material_expression");
		Def.Description      = TEXT("Remove a material expression node at the given index. "
			"Any connections referencing this expression will be cleared automatically. "
			"WARNING: Removing an expression will shift the indices of all subsequent expressions. "
			"Use get_material_expressions to verify current indices before calling.");
		Def.InputSchema      = Schema;
		Def.bDestructiveHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));
			if (!Args->HasField(TEXT("expression_index")))
				return FMCPToolResult::Error(TEXT("expression_index is required"));

			int32 ExprIdx = (int32)Args->GetNumberField(TEXT("expression_index"));

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;
			FString RemovedName;
			FString RemovedClass;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				TArray<UMaterialExpression*> Expressions = GetExpressions(Material);
				if (ExprIdx < 0 || ExprIdx >= Expressions.Num() || !Expressions[ExprIdx])
				{
					ErrorMessage = FString::Printf(
						TEXT("expression_index %d is out of range (0..%d)"),
						ExprIdx, Expressions.Num() - 1);
					DoneEvent->Trigger();
					return;
				}

				UMaterialExpression* ExprToRemove = Expressions[ExprIdx];
				RemovedName  = ExprToRemove->GetName();
				RemovedClass = ExprToRemove->GetClass()->GetName();

				Material->PreEditChange(nullptr);

				// Clear any input connections on other expressions that reference this one.
				for (int32 i = 0; i < Expressions.Num(); ++i)
				{
					if (i == ExprIdx || !Expressions[i]) continue;
					TArray<FExpressionInput*> Inputs;
					for (int32 ii = 0; ii < Expressions[i]->CountInputs(); ++ii)
					{
						Inputs.Add(Expressions[i]->GetInput(ii));
					}
					for (FExpressionInput* Input : Inputs)
					{
						if (Input && Input->Expression == ExprToRemove)
						{
							Input->Expression = nullptr;
						}
					}
				}

				// Clear any material output connections pointing to this expression.
				auto ClearIfTarget = [&](FExpressionInput& Input)
				{
					if (Input.Expression == ExprToRemove)
					{
						Input.Expression = nullptr;
					}
				};

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
				if (UMaterialEditorOnlyData* EdData = Material->GetEditorOnlyData())
				{
					ClearIfTarget(EdData->BaseColor);
					ClearIfTarget(EdData->Metallic);
					ClearIfTarget(EdData->Specular);
					ClearIfTarget(EdData->Roughness);
					ClearIfTarget(EdData->EmissiveColor);
					ClearIfTarget(EdData->Opacity);
					ClearIfTarget(EdData->Normal);
					ClearIfTarget(EdData->WorldPositionOffset);
				}
#else
				ClearIfTarget(Material->BaseColor);
				ClearIfTarget(Material->Metallic);
				ClearIfTarget(Material->Specular);
				ClearIfTarget(Material->Roughness);
				ClearIfTarget(Material->EmissiveColor);
				ClearIfTarget(Material->Opacity);
				ClearIfTarget(Material->Normal);
				ClearIfTarget(Material->WorldPositionOffset);
#endif

				Material->GetExpressionCollection().Expressions.RemoveAt(ExprIdx);
				ExprToRemove->MarkAsGarbage();

				Material->PostEditChange();
				Material->MarkPackageDirty();
				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			ResultJson->SetStringField(TEXT("removed_name"),        RemovedName);
			ResultJson->SetStringField(TEXT("removed_class"),       RemovedClass);
			ResultJson->SetNumberField(TEXT("removed_index"),       ExprIdx);
			ResultJson->SetStringField(TEXT("material_path"),       MaterialPath);
			ResultJson->SetStringField(TEXT("warning"),
				TEXT("Indices of remaining expressions have shifted. Call get_material_expressions to get updated indices."));

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// compile_material - Recompile a material after expression changes
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"),
			TEXT("Content path of the UMaterial asset to recompile"), true);

		FMCPToolDefinition Def;
		Def.Name            = TEXT("compile_material");
		Def.Description     = TEXT("Trigger a full recompile of a UMaterial by calling PreEditChange + PostEditChange. "
			"Call this after making expression graph changes (add/connect/set/remove) to apply them. "
			"The material will be saved with pending changes marked dirty.");
		Def.InputSchema     = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MaterialPath;
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));

			TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			bool bSuccess = false;
			FString ErrorMessage;
			FString MaterialName;
			int32 ExpressionCount = 0;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				FString LoadError;
				UMaterial* Material = LoadMaterial(MaterialPath, LoadError);
				if (!Material)
				{
					ErrorMessage = LoadError;
					DoneEvent->Trigger();
					return;
				}

				MaterialName = Material->GetName();
				ExpressionCount = GetExpressions(Material).Num();

				// This pair triggers the full shader compilation pipeline.
				Material->PreEditChange(nullptr);
				Material->PostEditChange();
				Material->MarkPackageDirty();

				bSuccess = true;
				DoneEvent->Trigger();
			});

			DoneEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

			if (!bSuccess)
				return FMCPToolResult::Error(ErrorMessage);

			ResultJson->SetStringField(TEXT("status"),            TEXT("compile_triggered"));
			ResultJson->SetStringField(TEXT("material_name"),     MaterialName);
			ResultJson->SetNumberField(TEXT("expression_count"),  ExpressionCount);
			ResultJson->SetStringField(TEXT("material_path"),     MaterialPath);
			ResultJson->SetStringField(TEXT("note"),
				TEXT("Shader compilation runs asynchronously in the background. "
					 "The material is marked dirty; save the asset to persist the compiled shaders."));

			return FMCPToolResult::Success(SerializeJson(ResultJson.ToSharedRef()));
		});
		Registry.RegisterTool(Def);
	}

} // void RegisterAll

} // namespace MCPMaterialGraphTools
