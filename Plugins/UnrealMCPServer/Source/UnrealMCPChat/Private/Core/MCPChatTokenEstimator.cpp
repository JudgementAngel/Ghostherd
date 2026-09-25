// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Core/MCPChatTokenEstimator.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatToolBridge.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

int32 FMCPChatTokenEstimator::EstimateText(const FString& Text)
{
	if (Text.IsEmpty()) { return 0; }

	// Sample rather than scan: a 200 KB inlined file does not need every character
	// inspected to decide whether it is code, and this runs on every keystroke.
	const int32 SampleLen = FMath::Min(Text.Len(), 4096);
	int32 Codeish = 0;
	for (int32 i = 0; i < SampleLen; ++i)
	{
		const TCHAR C = Text[i];
		if (C == TEXT('{') || C == TEXT('}') || C == TEXT('(') || C == TEXT(')')
			|| C == TEXT(';') || C == TEXT('<') || C == TEXT('>') || C == TEXT('_')
			|| C == TEXT('=') || C == TEXT('/') || C == TEXT('\\'))
		{
			++Codeish;
		}
	}

	const float CodeRatio = SampleLen > 0 ? static_cast<float>(Codeish) / SampleLen : 0.f;
	// 4% punctuation is ordinary prose; 12%+ is source. Interpolate between the two
	// divisors so mixed content (a message with one code block) is not classified
	// all-or-nothing.
	const float Alpha = FMath::Clamp((CodeRatio - 0.04f) / 0.08f, 0.f, 1.f);
	const float CharsPerToken = FMath::Lerp(3.7f, 2.2f, Alpha);

	return FMath::Max(1, FMath::CeilToInt(Text.Len() / CharsPerToken));
}

int32 FMCPChatTokenEstimator::EstimateJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) { return 0; }

	FString Serialised;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialised);
	FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

	// JSON is all punctuation and short keys — closer to 2.2 than the sampler would
	// guess for a small object, so skip the heuristic and use the code divisor.
	return FMath::Max(1, FMath::CeilToInt(Serialised.Len() / 2.2f));
}

int32 FMCPChatTokenEstimator::EstimateImage(int32 Width, int32 Height)
{
	if (Width <= 0 || Height <= 0)
	{
		// A typical editor screenshot. Better a plausible number than a zero that
		// implies images are free.
		return 1100;
	}
	return FMath::CeilToInt((static_cast<double>(Width) * Height) / 750.0);
}

int32 FMCPChatTokenEstimator::EstimateMessage(const FChatMessage& Message)
{
	int32 Total = 0;
	for (const FChatContentBlock& B : Message.Blocks)
	{
		switch (B.Type)
		{
		case FChatContentBlock::EType::Text:
		case FChatContentBlock::EType::ContextRef:
		case FChatContentBlock::EType::Refusal:
			Total += EstimateText(B.Text);
			break;

		case FChatContentBlock::EType::Image:
			// Dimensions are not stored on the block; the typical-screenshot fallback
			// applies. Under-counting here is the safer direction — the readout is a
			// warning, not a bill.
			Total += EstimateImage(0, 0);
			break;

		case FChatContentBlock::EType::File:
			Total += EstimateText(B.Text);
			break;

		case FChatContentBlock::EType::ToolCall:
			Total += EstimateText(B.ToolName) + EstimateJson(B.ToolArgs) + EstimateText(B.ToolResultText);
			break;

		case FChatContentBlock::EType::Thinking:
			// Thinking is not echoed back to the provider (its signature cannot be
			// reconstructed), so it costs nothing on the NEXT request.
			break;

		default:
			break;
		}
	}
	return Total;
}

FMCPChatTokenEstimator::FEstimate FMCPChatTokenEstimator::ForOutgoing(
	const FChatSessionPtr& Session, const FString& DraftText,
	const TArray<FChatAttachmentPtr>& Attachments)
{
	FEstimate E;

	if (Session.IsValid())
	{
		for (const FChatMessagePtr& M : Session->BuildActivePath())
		{
			if (M.IsValid()) { E.HistoryTokens += EstimateMessage(*M); }
		}

		// Pinned context rides along on every turn — a real, recurring cost that the
		// user has no other way to see.
		for (const FString& Pinned : Session->PinnedContext)
		{
			// Cannot resolve here (that touches the world on every keystroke); charge a
			// nominal amount so pinning several heavy contexts is visible.
			E.HistoryTokens += 400;
			(void)Pinned;
		}

		E.ToolSchemaTokens = FMCPChatToolBridge::Get().EstimateSchemaTokens(Session->Params.bCatalogToolExposure);
	}

	E.DraftTokens = EstimateText(DraftText);

	for (const FChatAttachmentPtr& A : Attachments)
	{
		if (!A.IsValid() || !A->IsValidForSend()) { continue; }

		if (A->IsImage())
		{
			E.AttachmentTokens += EstimateImage(A->Width, A->Height);
		}
		else if (A->IsContext())
		{
			// Unresolved until send. A flat charge keeps the readout honest about
			// "adding context is not free" without doing the work on every keystroke.
			E.AttachmentTokens += 400;
		}
		else
		{
			E.AttachmentTokens += FMath::CeilToInt(A->SizeBytes / 3.7f);
		}
	}

	E.Total = E.HistoryTokens + E.DraftTokens + E.AttachmentTokens + E.ToolSchemaTokens;

	if (Session.IsValid())
	{
		if (const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Session->ModelId))
		{
			E.ContextTokens = Model->ContextTokens;
			if (Model->ContextTokens > 0)
			{
				E.ContextFraction = static_cast<float>(E.Total) / Model->ContextTokens;
			}

			// Input price only: the output length is unknown before the turn runs, and
			// inventing one would make the estimate feel arbitrary when it lands wrong.
			E.CostUsd = (E.Total / 1'000'000.0) * Model->PriceInPerMillion;
		}
	}

	return E;
}

FString FMCPChatTokenEstimator::FormatTokens(int32 Tokens)
{
	if (Tokens < 1000)     { return FString::Printf(TEXT("%d"), Tokens); }
	if (Tokens < 100'000)  { return FString::Printf(TEXT("%.1fk"), Tokens / 1000.0); }
	return FString::Printf(TEXT("%dk"), Tokens / 1000);
}

FString FMCPChatTokenEstimator::FormatCost(double Usd)
{
	if (Usd <= 0.0)   { return TEXT("$0.00"); }
	if (Usd < 0.01)   { return TEXT("<$0.01"); }
	if (Usd < 10.0)   { return FString::Printf(TEXT("$%.2f"), Usd); }
	return FString::Printf(TEXT("$%.0f"), Usd);
}
