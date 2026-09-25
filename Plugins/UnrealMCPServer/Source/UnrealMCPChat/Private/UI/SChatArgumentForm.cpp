// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatArgumentForm.h"
#include "MCPChatStyle.h"

#include "MCPPromptProvider.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatArgumentForm"

TSharedPtr<FJsonObject> SChatArgumentForm::SchemaFromPromptArguments(const FString& PromptName)
{
	for (const FMCPPromptDefinition& P : FMCPPromptProvider::Get().GetAllPrompts())
	{
		if (P.Name != PromptName) { continue; }

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Required;

		for (const FMCPPromptArgument& Arg : P.Arguments)
		{
			TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
			// MCP prompt arguments are untyped by spec — always strings.
			Field->SetStringField(TEXT("type"), TEXT("string"));
			Field->SetStringField(TEXT("description"), Arg.Description);
			Properties->SetObjectField(Arg.Name, Field);

			if (Arg.bRequired) { Required.Add(MakeShared<FJsonValueString>(Arg.Name)); }
		}

		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetObjectField(TEXT("properties"), Properties);
		if (Required.Num() > 0) { Schema->SetArrayField(TEXT("required"), Required); }
		return Schema;
	}
	return nullptr;
}

void SChatArgumentForm::Construct(const FArguments& InArgs)
{
	OnSubmitted = InArgs._OnSubmitted;
	OnCancelled = InArgs._OnCancelled;

	BuildFields(InArgs._Schema);

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	Root->AddSlot().AutoHeight()
	.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
			.Text(InArgs._Title)
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(InArgs._Description)
			.AutoWrapText(true)
			.Visibility(InArgs._Description.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
		]
	];

	Root->AddSlot().FillHeight(1.f)
	[
		SNew(SBox)
		.MaxDesiredHeight(FMCPChatStyle::Space(TEXT("popupMaxH"), 420.f))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(FieldContainer, SVerticalBox)
			]
		]
	];

	for (int32 i = 0; i < Fields.Num(); ++i)
	{
		FieldContainer->AddSlot().AutoHeight()[ BuildFieldRow(i) ];
	}

	if (Fields.Num() == 0)
	{
		FieldContainer->AddSlot().AutoHeight()
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(LOCTEXT("NoArgs", "This one takes no arguments."))
		];
	}

	Root->AddSlot().AutoHeight()
	.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.ColorAndOpacity(FMCPChatStyle::Color(TEXT("readoutOver")))
		.Text_Lambda([this]() { return ErrorText; })
		.Visibility_Lambda([this]() { return ErrorText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		.AutoWrapText(true)
	];

	Root->AddSlot().AutoHeight()
	.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
	[
		SNew(SHorizontalBox)

		// Cancel sits LEFT of the confirm, matching the approval prompt in the
		// transcript — the same hand position should never mean "go" in one place
		// and "stop" in another.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Cancel", "Cancel"))
			.OnClicked(this, &SChatArgumentForm::OnCancelClicked)
		]

		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNullWidget::NullWidget ]

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Insert", "Insert"))
			.OnClicked(this, &SChatArgumentForm::OnSubmitClicked)
		]
	];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("popupBg")))
		.Padding(0.f)
		[
			SNew(SBox).MinDesiredWidth(FMCPChatStyle::Space(TEXT("popupMinW"), 340.f))
			[
				Root
			]
		]
	];
}

void SChatArgumentForm::BuildFields(const TSharedPtr<FJsonObject>& Schema)
{
	if (!Schema.IsValid()) { return; }

	TSet<FString> RequiredNames;
	const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
	if (Schema->TryGetArrayField(TEXT("required"), Required) && Required)
	{
		for (const TSharedPtr<FJsonValue>& V : *Required)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) { RequiredNames.Add(S); }
		}
	}

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	if (!Schema->TryGetObjectField(TEXT("properties"), Properties) || !Properties) { return; }

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
	{
		const TSharedPtr<FJsonObject>* Prop = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Prop) || !Prop) { continue; }

		FField Field;
		Field.Name       = Pair.Key;
		Field.bRequired  = RequiredNames.Contains(Pair.Key);
		(*Prop)->TryGetStringField(TEXT("type"), Field.Type);
		(*Prop)->TryGetStringField(TEXT("description"), Field.Description);

		if (Field.Type.IsEmpty()) { Field.Type = TEXT("string"); }

		const TArray<TSharedPtr<FJsonValue>>* EnumArray = nullptr;
		if ((*Prop)->TryGetArrayField(TEXT("enum"), EnumArray) && EnumArray)
		{
			for (const TSharedPtr<FJsonValue>& V : *EnumArray)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S)) { Field.EnumValues.Add(S); }
			}
		}

		// Seed from the schema default so a form is useful the moment it opens.
		const TSharedPtr<FJsonValue> DefaultValue = (*Prop)->TryGetField(TEXT("default"));
		if (DefaultValue.IsValid())
		{
			if (Field.Type == TEXT("boolean")) { DefaultValue->TryGetBool(Field.bBoolValue); }
			else                               { Field.TextValue = DefaultValue->AsString(); }
		}

		Fields.Add(MoveTemp(Field));
	}

	// Required fields first: the form should read as "these, then optionally these".
	Fields.StableSort([](const FField& A, const FField& B)
	{
		return A.bRequired && !B.bRequired;
	});
}

TSharedRef<SWidget> SChatArgumentForm::BuildFieldRow(int32 FieldIndex)
{
	const FField& Field = Fields[FieldIndex];

	TSharedRef<SVerticalBox> Row = SNew(SVerticalBox);

	Row->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.Text(FText::FromString(Field.bRequired ? (Field.Name + TEXT(" *")) : Field.Name))
		.ToolTipText(FText::FromString(Field.Description))
	];

	if (Field.Type == TEXT("boolean"))
	{
		Row->AddSlot().AutoHeight()
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, FieldIndex]()
			{
				return Fields.IsValidIndex(FieldIndex) && Fields[FieldIndex].bBoolValue
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, FieldIndex](ECheckBoxState State)
			{
				if (Fields.IsValidIndex(FieldIndex))
				{
					Fields[FieldIndex].bBoolValue = (State == ECheckBoxState::Checked);
				}
			})
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(FText::FromString(Field.Description))
			]
		];
	}
	else
	{
		// Enums stay a text box with the allowed values in the hint rather than a
		// combo: most enums here have two or three values, and typing is faster than
		// opening a second menu inside a popup that is already inside a menu.
		FText Hint = FText::FromString(Field.Description);
		if (Field.EnumValues.Num() > 0)
		{
			Hint = FText::FromString(FString::Join(Field.EnumValues, TEXT(" | ")));
		}
		else if (Field.Type == TEXT("array"))
		{
			Hint = LOCTEXT("ArrayHint", "comma-separated");
		}

		Row->AddSlot().AutoHeight()
		[
			SNew(SEditableTextBox)
			.HintText(Hint)
			.Text_Lambda([this, FieldIndex]()
			{
				return Fields.IsValidIndex(FieldIndex)
					? FText::FromString(Fields[FieldIndex].TextValue) : FText::GetEmpty();
			})
			.OnTextChanged_Lambda([this, FieldIndex](const FText& NewText)
			{
				if (Fields.IsValidIndex(FieldIndex))
				{
					Fields[FieldIndex].TextValue = NewText.ToString();
				}
			})
		];
	}

	return SNew(SBox)
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
		[
			Row
		];
}

bool SChatArgumentForm::Validate(FText& OutError) const
{
	for (const FField& Field : Fields)
	{
		if (Field.bRequired && Field.Type != TEXT("boolean") && Field.TextValue.TrimStartAndEnd().IsEmpty())
		{
			OutError = FText::Format(LOCTEXT("Missing", "'{0}' is required."), FText::FromString(Field.Name));
			return false;
		}

		if ((Field.Type == TEXT("number") || Field.Type == TEXT("integer")) && !Field.TextValue.IsEmpty())
		{
			if (!Field.TextValue.IsNumeric())
			{
				OutError = FText::Format(LOCTEXT("NotNumeric", "'{0}' must be a number."),
					FText::FromString(Field.Name));
				return false;
			}
		}

		if (Field.EnumValues.Num() > 0 && !Field.TextValue.IsEmpty()
			&& !Field.EnumValues.Contains(Field.TextValue))
		{
			OutError = FText::Format(LOCTEXT("NotInEnum", "'{0}' must be one of: {1}"),
				FText::FromString(Field.Name), FText::FromString(FString::Join(Field.EnumValues, TEXT(", "))));
			return false;
		}
	}
	return true;
}

TSharedPtr<FJsonObject> SChatArgumentForm::BuildValues() const
{
	TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();

	for (const FField& Field : Fields)
	{
		if (Field.Type == TEXT("boolean"))
		{
			Values->SetBoolField(Field.Name, Field.bBoolValue);
			continue;
		}

		const FString Trimmed = Field.TextValue.TrimStartAndEnd();
		// An empty optional field is OMITTED, not sent as "". A tool that treats ""
		// as a real value would otherwise get one it never asked for.
		if (Trimmed.IsEmpty()) { continue; }

		if (Field.Type == TEXT("number"))
		{
			Values->SetNumberField(Field.Name, FCString::Atod(*Trimmed));
		}
		else if (Field.Type == TEXT("integer"))
		{
			Values->SetNumberField(Field.Name, FCString::Atoi64(*Trimmed));
		}
		else if (Field.Type == TEXT("array"))
		{
			TArray<FString> Parts;
			Trimmed.ParseIntoArray(Parts, TEXT(","), true);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (FString& Part : Parts)
			{
				Items.Add(MakeShared<FJsonValueString>(Part.TrimStartAndEnd()));
			}
			Values->SetArrayField(Field.Name, Items);
		}
		else
		{
			Values->SetStringField(Field.Name, Trimmed);
		}
	}

	return Values;
}

FReply SChatArgumentForm::OnSubmitClicked()
{
	FText Error;
	if (!Validate(Error))
	{
		ErrorText = Error;
		return FReply::Handled();
	}

	ErrorText = FText::GetEmpty();
	OnSubmitted.ExecuteIfBound(BuildValues());
	return FReply::Handled();
}

FReply SChatArgumentForm::OnCancelClicked()
{
	OnCancelled.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
