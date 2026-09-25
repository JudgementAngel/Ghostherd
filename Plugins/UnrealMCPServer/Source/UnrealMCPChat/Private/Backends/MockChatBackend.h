// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMCPChatBackend.h"
#include "Containers/Ticker.h"

/**
 * A scripted backend that replays a canned event list on a timer.
 *
 * This is the highest-leverage thing in Phase 2: it lets the transcript, the
 * message rows, the tool cards and (later) the composer be built and exercised
 * with zero network, zero subprocesses and zero API keys. It also makes the
 * streaming path deterministic, which is the only way to test scroll behaviour
 * and frame cost honestly.
 *
 * Ships in the product too — "Mock (offline demo)" is a valid pick in the model
 * dropdown, and it is how a user checks the panel works before spending money.
 */
class FMockChatBackend : public IMCPChatBackend
{
public:
	FMockChatBackend();
	virtual ~FMockChatBackend() override;

	//~ IMCPChatBackend
	virtual EChatBackendKind GetKind() const override { return EChatBackendKind::Mock; }
	virtual FString GetId() const override { return TEXT("mock"); }
	virtual FText   GetDisplayName() const override;
	virtual bool    IsAvailable(FText& OutReason) const override { return true; }
	virtual TArray<FChatModelInfo> GetModels() const override;

	virtual void StartSession(const FGuid& SessionId) override {}
	virtual void SendTurn(const FChatTurnRequest& Request) override;
	virtual void CancelTurn(const FGuid& SessionId) override;
	virtual void EndSession(const FGuid& SessionId) override;
	//~ End IMCPChatBackend

	/** Tokens emitted per second. Raise it to stress the transcript. */
	void SetTokensPerSecond(float InRate) { TokensPerSecond = FMath::Max(1.f, InRate); }

private:
	/** One queued event plus how long to wait before emitting it. */
	struct FScriptedStep
	{
		FChatStreamEvent Event;
		float            DelaySeconds = 0.f;
	};

	/** Choose a canned script from the user's text so the mock is useful for
	 *  demoing specific UI states (tools, errors, refusals, long output). */
	void BuildScript(const FChatTurnRequest& Request);
	void QueueTextAsDeltas(const FString& Text, const FGuid& MessageId);
	bool TickScript(float DeltaTime);

	TArray<FScriptedStep> Script;
	int32  ScriptCursor = 0;
	float  TimeUntilNextStep = 0.f;
	float  TokensPerSecond = 45.f;

	FGuid ActiveSessionId;
	FGuid ActiveMessageId;
	TSharedPtr<TAtomic<bool>> CancelFlag;

	FTSTicker::FDelegateHandle TickerHandle;
};
