// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTypes.h"
#include "MCPChatAttachments.h"

/**
 * Phase 6 — the composer's live "1.2k ⌁ $0.03" readout.
 *
 * This is an ESTIMATE and says so. Real tokenisation needs the provider's own
 * BPE tables, which we do not ship and cannot keep current across five providers.
 * What the readout has to be right about is the shape of the answer:
 *
 *   - is this request small, or is it most of the context window?
 *   - is this turn worth pennies or dollars?
 *
 * A ±15% error changes neither. After each turn the provider's real `usage` comes
 * back and the session's running total is reconciled against it, so the numbers
 * the user sees in history are exact even though the pre-send preview is not.
 */
class FMCPChatTokenEstimator
{
public:
	/**
	 * Characters per token.
	 *
	 * English prose sits near 3.7; code is denser in punctuation and tokenises
	 * worse, nearer 2.2. Using one constant for both under-counts a code-heavy
	 * request by a third, which is exactly when the warning matters, so the text
	 * is sampled for code-ish characters and the divisor picked per string.
	 */
	static int32 EstimateText(const FString& Text);

	/** A JSON payload — tool args, structured results. Punctuation-heavy. */
	static int32 EstimateJson(const TSharedPtr<FJsonObject>& Json);

	/**
	 * Images are billed by tile, not by byte. Anthropic bills roughly
	 * (width × height) / 750; a 1024×768 screenshot is ~1050 tokens. Unknown
	 * dimensions fall back to a typical screenshot rather than to zero — a zero
	 * would make the readout claim an image is free.
	 */
	static int32 EstimateImage(int32 Width, int32 Height);

	static int32 EstimateMessage(const FChatMessage& Message);

	struct FEstimate
	{
		int32 HistoryTokens = 0;
		int32 DraftTokens = 0;
		int32 AttachmentTokens = 0;
		int32 ToolSchemaTokens = 0;
		int32 Total = 0;

		double CostUsd = 0.0;

		/** Total ÷ the model's context window. Drives the amber/red states. */
		float ContextFraction = 0.f;
		int32 ContextTokens = 0;
	};

	/** What the NEXT request will carry, including what is still in the composer. */
	static FEstimate ForOutgoing(const FChatSessionPtr& Session,
	                             const FString& DraftText,
	                             const TArray<FChatAttachmentPtr>& Attachments);

	/** "1.2k" / "184k". */
	static FString FormatTokens(int32 Tokens);
	/** "$0.03" / "<$0.01". */
	static FString FormatCost(double Usd);

	/** Above this fraction of the window the readout turns amber, and above
	 *  RedFraction it turns red and offers Compact. */
	static constexpr float AmberFraction = 0.75f;
	static constexpr float RedFraction   = 0.90f;
};
