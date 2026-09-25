// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SVerticalBox;

DECLARE_DELEGATE_OneParam(FOnArgumentFormSubmitted, TSharedPtr<FJsonObject> /*Values*/);
DECLARE_DELEGATE(FOnArgumentFormCancelled);

/**
 * Phase 6 — the inline form behind `#tool` and `#workflow`.
 *
 * Generated from a JSON Schema, not hand-written per tool. There are 452 tools;
 * hand-writing forms is not a plan, and a generated form stays correct when a
 * tool's schema changes underneath it.
 *
 * Scope is deliberately narrow: strings, numbers, booleans, enums, and arrays of
 * strings. Anything more structured falls back to a raw-JSON box with validation,
 * which is honest — a nested-object editor built from a schema is a worse
 * experience than a text field for the rare tool that needs one.
 */
class SChatArgumentForm : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatArgumentForm) {}
		/** Title row: the tool or prompt name. */
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(FText, Description)
		/** JSON Schema `properties` + `required`. */
		SLATE_ARGUMENT(TSharedPtr<FJsonObject>, Schema)
		SLATE_EVENT(FOnArgumentFormSubmitted, OnSubmitted)
		SLATE_EVENT(FOnArgumentFormCancelled, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Build a schema from an MCP prompt's argument list, so prompts and tools share
	 *  one form implementation. */
	static TSharedPtr<FJsonObject> SchemaFromPromptArguments(const FString& PromptName);

private:
	struct FField
	{
		FString Name;
		FString Type;          // string|number|integer|boolean|array|object
		FString Description;
		bool    bRequired = false;
		TArray<FString> EnumValues;

		/** Current value, held as text and converted on submit. */
		FString TextValue;
		bool    bBoolValue = false;
	};

	void BuildFields(const TSharedPtr<FJsonObject>& Schema);
	TSharedRef<SWidget> BuildFieldRow(int32 FieldIndex);

	FReply OnSubmitClicked();
	FReply OnCancelClicked();

	/** @return false with a filled OutError when a required field is empty or a
	 *  number will not parse. Submitting a knowingly-invalid payload just moves the
	 *  error somewhere less useful. */
	bool Validate(FText& OutError) const;

	TSharedPtr<FJsonObject> BuildValues() const;

	TArray<FField>           Fields;
	TSharedPtr<SVerticalBox> FieldContainer;
	FText                    ErrorText;

	FOnArgumentFormSubmitted OnSubmitted;
	FOnArgumentFormCancelled OnCancelled;
};
