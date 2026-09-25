// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 4 — API key storage (docs/02_ARCHITECTURE.md §8).
 *
 * Hard rules, all enforced here rather than left to callers:
 *   - No secret ever reaches a UDeveloperSettings property or any .ini file.
 *     The host plugin's own FalAIApiKey is a plain config FString and its tooltip
 *     warns users not to commit it; that is the trap we are not repeating.
 *   - Every string a backend logs goes through RedactSecrets() first.
 *   - The UI shows a masked preview and requires a second click to reveal.
 *
 * Resolution order (first hit wins), surfaced to the user so they know where a
 * key actually came from:
 *   1. Environment variable   — many users already export these; zero storage
 *   2. OS keychain            — DPAPI (Win) / Keychain (Mac) / libsecret (Linux)
 *   3. Encrypted file         — AES-256 under Saved/, honest about being weaker
 *   4. Session-only memory    — never persisted
 *
 * Local CLI agents need none of this: Claude Code, Codex and Gemini CLI carry
 * their own sign-in. That is a genuine advantage of the agent path.
 */

enum class EMCPSecretSource : uint8
{
	None,
	Environment,
	Keychain,
	EncryptedFile,
	SessionMemory,
};

class UNREALMCPCHAT_API FMCPChatSecretStore
{
public:
	static FMCPChatSecretStore& Get();

	/** ProviderId is the catalogue's provider key, e.g. "anthropic", "openai". */
	bool GetSecret(const FString& ProviderId, FString& OutSecret, EMCPSecretSource* OutSource = nullptr) const;
	bool HasSecret(const FString& ProviderId) const;

	/** Persist to the best available backing store. bSessionOnly keeps it in memory. */
	bool SetSecret(const FString& ProviderId, const FString& Secret, bool bSessionOnly = false);

	/** Remove from every writable store. Env vars are not ours to remove. */
	void ClearSecret(const FString& ProviderId);

	/** "sk-ant-…••••••••…4f2a" — safe to render, never the whole key. */
	static FString MaskSecret(const FString& Secret);

	/** Human-readable description of where a key came from, for the settings row. */
	static FText DescribeSource(EMCPSecretSource Source);

	/**
	 * Replace anything that looks like a credential with "***REDACTED***".
	 * Called on every backend log line. Catches known key prefixes AND any value
	 * currently in the store, so a key echoed back in an error body is caught too.
	 */
	FString RedactSecrets(const FString& In) const;

	/** Environment variable consulted for a provider (also shown in settings). */
	static FString GetEnvironmentVariableName(const FString& ProviderId);

private:
	FMCPChatSecretStore() = default;

	// Platform keychain — implemented per-platform, no-ops where unavailable.
	static bool KeychainGet(const FString& Key, FString& OutSecret);
	static bool KeychainSet(const FString& Key, const FString& Secret);
	static void KeychainRemove(const FString& Key);
	static bool IsKeychainAvailable();

	// Encrypted-file fallback.
	static FString GetEncryptedFilePath();
	bool FileGet(const FString& Key, FString& OutSecret) const;
	bool FileSet(const FString& Key, const FString& Secret);
	void FileRemove(const FString& Key);
	static TArray<uint8> DeriveFileKey();

	/** Session-only secrets, and a mirror of everything we have handed out, used
	 *  by RedactSecrets. Never serialised. */
	TMap<FString, FString> SessionSecrets;
	mutable TSet<FString>  KnownSecretValues;
	mutable FCriticalSection Lock;
};
