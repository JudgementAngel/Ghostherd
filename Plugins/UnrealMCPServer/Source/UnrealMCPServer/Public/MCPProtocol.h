#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealMCP, Log, All);

// ============================================================================
// MCP Protocol Constants
// ============================================================================

namespace MCPProtocol
{
	inline const FString Version = TEXT("2025-06-18");
	inline const FString ServerName = TEXT("unreal-mcp-server");
	inline const FString ServerVersion = TEXT("2.0.0");

	namespace Methods
	{
		inline const FString Initialize = TEXT("initialize");
		inline const FString Initialized = TEXT("notifications/initialized");
		inline const FString Ping = TEXT("ping");
		inline const FString ToolsList = TEXT("tools/list");
		inline const FString ToolsCall = TEXT("tools/call");
		inline const FString ResourcesList = TEXT("resources/list");
		inline const FString ResourcesRead = TEXT("resources/read");
		inline const FString PromptsList = TEXT("prompts/list");
		inline const FString PromptsGet = TEXT("prompts/get");
	}
}

// ============================================================================
// Content Types (MCP content blocks)
// ============================================================================

struct FMCPContentBlock
{
	FString Type; // "text", "image", "resource"

	// Text content
	FString Text;

	// Image content
	FString ImageData; // base64
	FString MimeType;

	// Resource content
	FString ResourceUri;
	FString ResourceText;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("type"), Type);

		if (Type == TEXT("text"))
		{
			Obj->SetStringField(TEXT("text"), Text);
		}
		else if (Type == TEXT("image"))
		{
			Obj->SetStringField(TEXT("data"), ImageData);
			Obj->SetStringField(TEXT("mimeType"), MimeType);
		}
		else if (Type == TEXT("resource"))
		{
			TSharedPtr<FJsonObject> ResObj = MakeShared<FJsonObject>();
			ResObj->SetStringField(TEXT("uri"), ResourceUri);
			ResObj->SetStringField(TEXT("text"), ResourceText);
			Obj->SetObjectField(TEXT("resource"), ResObj);
		}

		return Obj;
	}

	static FMCPContentBlock MakeText(const FString& InText)
	{
		FMCPContentBlock Block;
		Block.Type = TEXT("text");
		Block.Text = InText;
		return Block;
	}

	static FMCPContentBlock MakeImage(const FString& Base64Data, const FString& InMimeType = TEXT("image/png"))
	{
		FMCPContentBlock Block;
		Block.Type = TEXT("image");
		Block.ImageData = Base64Data;
		Block.MimeType = InMimeType;
		return Block;
	}

	static FMCPContentBlock MakeResource(const FString& Uri, const FString& InText)
	{
		FMCPContentBlock Block;
		Block.Type = TEXT("resource");
		Block.ResourceUri = Uri;
		Block.ResourceText = InText;
		return Block;
	}
};

// ============================================================================
// Tool Result
// ============================================================================

struct FMCPToolResult
{
	TArray<FMCPContentBlock> Content;
	bool bIsError = false;
	TSharedPtr<FJsonObject> StructuredContent; // Optional: structured data matching tool's outputSchema

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

		TArray<TSharedPtr<FJsonValue>> ContentArray;
		for (const FMCPContentBlock& Block : Content)
		{
			ContentArray.Add(MakeShared<FJsonValueObject>(Block.ToJson()));
		}
		Obj->SetArrayField(TEXT("content"), ContentArray);

		if (bIsError)
		{
			Obj->SetBoolField(TEXT("isError"), true);
		}

		if (StructuredContent.IsValid())
		{
			Obj->SetObjectField(TEXT("structuredContent"), StructuredContent);
		}

		return Obj;
	}

	static FMCPToolResult Success(const FString& Text)
	{
		FMCPToolResult Result;
		Result.Content.Add(FMCPContentBlock::MakeText(Text));
		return Result;
	}

	static FMCPToolResult Error(const FString& ErrorText)
	{
		FMCPToolResult Result;
		Result.Content.Add(FMCPContentBlock::MakeText(ErrorText));
		Result.bIsError = true;
		return Result;
	}

	static FMCPToolResult WithImage(const FString& Text, const FString& Base64, const FString& Mime = TEXT("image/png"))
	{
		FMCPToolResult Result;
		Result.Content.Add(FMCPContentBlock::MakeText(Text));
		Result.Content.Add(FMCPContentBlock::MakeImage(Base64, Mime));
		return Result;
	}

	/** Return both human-readable text and structured JSON data. */
	static FMCPToolResult SuccessStructured(const FString& Text, const TSharedPtr<FJsonObject>& Structured)
	{
		FMCPToolResult Result;
		Result.Content.Add(FMCPContentBlock::MakeText(Text));
		Result.StructuredContent = Structured;
		return Result;
	}
};

// ============================================================================
// Tool Definition
// ============================================================================

DECLARE_DELEGATE_RetVal_OneParam(FMCPToolResult, FMCPToolHandler, const TSharedPtr<FJsonObject>&);

struct FMCPToolDefinition
{
	FString Name;
	FString Description;
	TSharedPtr<FJsonObject> InputSchema;
	TSharedPtr<FJsonObject> OutputSchema; // Optional: structured output schema (MCP spec 2025-06-18)
	FMCPToolHandler Handler;

	// Tool Annotations (MCP spec 2025-06-18)
	bool bReadOnlyHint = false;      // Tool only reads data, no side effects
	bool bDestructiveHint = false;    // Tool performs destructive/irreversible actions
	bool bIdempotentHint = false;     // Safe to retry, same result each time
	bool bOpenWorldHint = true;       // Interacts with external world (UE editor)

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("description"), Description);
		if (InputSchema.IsValid())
		{
			Obj->SetObjectField(TEXT("inputSchema"), InputSchema);
		}
		if (OutputSchema.IsValid())
		{
			Obj->SetObjectField(TEXT("outputSchema"), OutputSchema);
		}

		// Serialize annotations
		TSharedPtr<FJsonObject> Annotations = MakeShared<FJsonObject>();
		if (bReadOnlyHint)     Annotations->SetBoolField(TEXT("readOnlyHint"), true);
		if (bDestructiveHint)  Annotations->SetBoolField(TEXT("destructiveHint"), true);
		if (bIdempotentHint)   Annotations->SetBoolField(TEXT("idempotentHint"), true);
		if (!bOpenWorldHint)   Annotations->SetBoolField(TEXT("openWorldHint"), false);
		if (Annotations->Values.Num() > 0)
		{
			Obj->SetObjectField(TEXT("annotations"), Annotations);
		}

		return Obj;
	}
};

// ============================================================================
// Resource Definition
// ============================================================================

struct FMCPResourceDefinition
{
	FString Uri;
	FString Name;
	FString Description;
	FString MimeType;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("uri"), Uri);
		Obj->SetStringField(TEXT("name"), Name);
		if (!Description.IsEmpty())
		{
			Obj->SetStringField(TEXT("description"), Description);
		}
		if (!MimeType.IsEmpty())
		{
			Obj->SetStringField(TEXT("mimeType"), MimeType);
		}
		return Obj;
	}
};

struct FMCPResourceContent
{
	FString Uri;
	FString Text;
	FString MimeType;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("uri"), Uri);
		if (!MimeType.IsEmpty())
		{
			Obj->SetStringField(TEXT("mimeType"), MimeType);
		}
		Obj->SetStringField(TEXT("text"), Text);
		return Obj;
	}
};

// ============================================================================
// Prompt Definition
// ============================================================================

struct FMCPPromptArgument
{
	FString Name;
	FString Description;
	bool bRequired = false;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		if (!Description.IsEmpty())
		{
			Obj->SetStringField(TEXT("description"), Description);
		}
		Obj->SetBoolField(TEXT("required"), bRequired);
		return Obj;
	}
};

struct FMCPPromptDefinition
{
	FString Name;
	FString Description;
	TArray<FMCPPromptArgument> Arguments;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		if (!Description.IsEmpty())
		{
			Obj->SetStringField(TEXT("description"), Description);
		}
		if (Arguments.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Args;
			for (const FMCPPromptArgument& Arg : Arguments)
			{
				Args.Add(MakeShared<FJsonValueObject>(Arg.ToJson()));
			}
			Obj->SetArrayField(TEXT("arguments"), Args);
		}
		return Obj;
	}
};

struct FMCPPromptMessage
{
	FString Role; // "user" or "assistant"
	FMCPContentBlock Content;

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("role"), Role);
		Obj->SetObjectField(TEXT("content"), Content.ToJson());
		return Obj;
	}
};

// ============================================================================
// Elicitation Support (MCP spec 2025-06-18)
// Server can request additional user input from the client at runtime.
// NOTE: Full elicitation flow requires server-to-client messaging support
// (SSE push or Streamable HTTP reverse channel). These types define the
// protocol structures; see MCPHttpServer for transport implementation.
// ============================================================================

struct FMCPElicitationRequest
{
	FString Message;                         // Human-readable prompt for the user
	TSharedPtr<FJsonObject> RequestedSchema; // JSON Schema describing expected input

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("message"), Message);
		if (RequestedSchema.IsValid())
		{
			Obj->SetObjectField(TEXT("requestedSchema"), RequestedSchema);
		}
		return Obj;
	}
};

struct FMCPElicitationResponse
{
	enum class EAction : uint8
	{
		Accept,  // User provided the requested data
		Decline, // User declined to provide data
		Cancel   // User cancelled the operation
	};

	EAction Action = EAction::Decline;
	TSharedPtr<FJsonObject> Content; // User-provided data (when Action == Accept)

	bool ParseFromJson(const TSharedPtr<FJsonObject>& Json)
	{
		if (!Json.IsValid()) return false;

		FString ActionStr;
		if (Json->TryGetStringField(TEXT("action"), ActionStr))
		{
			if (ActionStr == TEXT("accept"))       Action = EAction::Accept;
			else if (ActionStr == TEXT("decline"))  Action = EAction::Decline;
			else if (ActionStr == TEXT("cancel"))   Action = EAction::Cancel;
		}

		if (Json->HasField(TEXT("content")))
		{
			Content = Json->GetObjectField(TEXT("content"));
		}

		return true;
	}
};

// ============================================================================
// JSON-RPC 2.0 Types
// ============================================================================

struct FJsonRpcRequest
{
	FString JsonRpc = TEXT("2.0");
	FString Method;
	TSharedPtr<FJsonObject> Params;
	TSharedPtr<FJsonValue> Id; // Can be string, number, or null

	bool ParseFromJson(const TSharedPtr<FJsonObject>& Json)
	{
		if (!Json.IsValid()) return false;

		Json->TryGetStringField(TEXT("jsonrpc"), JsonRpc);
		if (!Json->TryGetStringField(TEXT("method"), Method)) return false;

		if (Json->HasField(TEXT("params")))
		{
			Params = Json->GetObjectField(TEXT("params"));
		}

		if (Json->HasField(TEXT("id")))
		{
			Id = Json->TryGetField(TEXT("id"));
		}

		return true;
	}

	bool IsNotification() const { return !Id.IsValid(); }
};

struct FJsonRpcResponse
{
	static TSharedPtr<FJsonObject> MakeResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		if (Id.IsValid())
		{
			Obj->SetField(TEXT("id"), Id);
		}
		Obj->SetObjectField(TEXT("result"), Result);
		return Obj;
	}

	static TSharedPtr<FJsonObject> MakeError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		if (Id.IsValid())
		{
			Obj->SetField(TEXT("id"), Id);
		}
		else
		{
			Obj->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
		}

		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetNumberField(TEXT("code"), Code);
		ErrorObj->SetStringField(TEXT("message"), Message);
		Obj->SetObjectField(TEXT("error"), ErrorObj);
		return Obj;
	}

	// Standard JSON-RPC error codes
	static constexpr int32 ParseError = -32700;
	static constexpr int32 InvalidRequest = -32600;
	static constexpr int32 MethodNotFound = -32601;
	static constexpr int32 InvalidParams = -32602;
	static constexpr int32 InternalError = -32603;
};

// ============================================================================
// JSON Schema Builder (helper for tool definitions)
// ============================================================================

class FMCPSchemaBuilder
{
public:
	static TSharedPtr<FJsonObject> Begin()
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		return Schema;
	}

	static void AddString(TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false)
	{
		TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
		Prop->SetStringField(TEXT("type"), TEXT("string"));
		Prop->SetStringField(TEXT("description"), Description);
		Schema->GetObjectField(TEXT("properties"))->SetObjectField(Name, Prop);
		if (bRequired) AddRequired(Schema, Name);
	}

	static void AddNumber(TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false)
	{
		TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
		Prop->SetStringField(TEXT("type"), TEXT("number"));
		Prop->SetStringField(TEXT("description"), Description);
		Schema->GetObjectField(TEXT("properties"))->SetObjectField(Name, Prop);
		if (bRequired) AddRequired(Schema, Name);
	}

	static void AddInteger(TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false)
	{
		TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
		Prop->SetStringField(TEXT("type"), TEXT("integer"));
		Prop->SetStringField(TEXT("description"), Description);
		Schema->GetObjectField(TEXT("properties"))->SetObjectField(Name, Prop);
		if (bRequired) AddRequired(Schema, Name);
	}

	static void AddBoolean(TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false)
	{
		TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
		Prop->SetStringField(TEXT("type"), TEXT("boolean"));
		Prop->SetStringField(TEXT("description"), Description);
		Schema->GetObjectField(TEXT("properties"))->SetObjectField(Name, Prop);
		if (bRequired) AddRequired(Schema, Name);
	}

	static void AddEnum(TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, const TArray<FString>& Values, bool bRequired = false)
	{
		TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
		Prop->SetStringField(TEXT("type"), TEXT("string"));
		Prop->SetStringField(TEXT("description"), Description);

		TArray<TSharedPtr<FJsonValue>> EnumValues;
		for (const FString& Val : Values)
		{
			EnumValues.Add(MakeShared<FJsonValueString>(Val));
		}
		Prop->SetArrayField(TEXT("enum"), EnumValues);

		Schema->GetObjectField(TEXT("properties"))->SetObjectField(Name, Prop);
		if (bRequired) AddRequired(Schema, Name);
	}

	static void AddStringArray(TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false)
	{
		TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
		Prop->SetStringField(TEXT("type"), TEXT("array"));
		Prop->SetStringField(TEXT("description"), Description);

		TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
		Items->SetStringField(TEXT("type"), TEXT("string"));
		Prop->SetObjectField(TEXT("items"), Items);

		Schema->GetObjectField(TEXT("properties"))->SetObjectField(Name, Prop);
		if (bRequired) AddRequired(Schema, Name);
	}

	static void AddObject(TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, TSharedPtr<FJsonObject> SubSchema, bool bRequired = false)
	{
		SubSchema->SetStringField(TEXT("description"), Description);
		Schema->GetObjectField(TEXT("properties"))->SetObjectField(Name, SubSchema);
		if (bRequired) AddRequired(Schema, Name);
	}

private:
	static void AddRequired(TSharedPtr<FJsonObject>& Schema, const FString& Name)
	{
		TArray<TSharedPtr<FJsonValue>> Required;
		if (Schema->HasField(TEXT("required")))
		{
			Required = Schema->GetArrayField(TEXT("required"));
		}
		Required.Add(MakeShared<FJsonValueString>(Name));
		Schema->SetArrayField(TEXT("required"), Required);
	}
};

// ============================================================================
// Utility: JSON serialization helper
// ============================================================================

inline FString JsonToString(const TSharedPtr<FJsonObject>& Json)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
	return Output;
}

inline TSharedPtr<FJsonObject> StringToJson(const FString& Input)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Input);
	FJsonSerializer::Deserialize(Reader, Json);
	return Json;
}
