// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatSecretStore.h"
#include "Agents/MCPChatProcessRunner.h"
#include "UnrealMCPChatModule.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AES.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
	#include <wincrypt.h>
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "MCPChatSecretStore"

namespace
{
	/** Key prefixes worth redacting even when we have never stored the value —
	 *  catches a key pasted into a prompt or echoed back in an error body. */
	const TCHAR* SecretPrefixes[] = {
		TEXT("sk-ant-"), TEXT("sk-proj-"), TEXT("sk-or-"), TEXT("sk-"),
		TEXT("gsk_"), TEXT("xai-"), TEXT("AIza"), TEXT("ghp_"), TEXT("github_pat_"),
	};

	FString StorageKeyFor(const FString& ProviderId)
	{
		return FString::Printf(TEXT("UnrealMCPChat.%s"), *ProviderId);
	}
}

FMCPChatSecretStore& FMCPChatSecretStore::Get()
{
	static FMCPChatSecretStore Instance;
	return Instance;
}

FString FMCPChatSecretStore::GetEnvironmentVariableName(const FString& ProviderId)
{
	// Match what users already have exported rather than inventing our own names.
	static const TMap<FString, FString> Known = {
		{ TEXT("anthropic"),  TEXT("ANTHROPIC_API_KEY") },
		{ TEXT("openai"),     TEXT("OPENAI_API_KEY") },
		{ TEXT("openrouter"), TEXT("OPENROUTER_API_KEY") },
		{ TEXT("groq"),       TEXT("GROQ_API_KEY") },
		{ TEXT("deepseek"),   TEXT("DEEPSEEK_API_KEY") },
		{ TEXT("mistral"),    TEXT("MISTRAL_API_KEY") },
		{ TEXT("xai"),        TEXT("XAI_API_KEY") },
		{ TEXT("google"),     TEXT("GEMINI_API_KEY") },
		{ TEXT("moonshot"),   TEXT("MOONSHOT_API_KEY") },
	};

	if (const FString* Found = Known.Find(ProviderId.ToLower()))
	{
		return *Found;
	}
	// Custom provider: PROVIDERNAME_API_KEY
	return FString::Printf(TEXT("%s_API_KEY"), *ProviderId.ToUpper().Replace(TEXT("-"), TEXT("_")));
}

// ============================================================================
// Read
// ============================================================================

bool FMCPChatSecretStore::GetSecret(const FString& ProviderId, FString& OutSecret,
	EMCPSecretSource* OutSource) const
{
	auto Remember = [this](const FString& Secret)
	{
		if (Secret.Len() >= 8)
		{
			FScopeLock ScopeLock(&Lock);
			KnownSecretValues.Add(Secret);
		}
	};

	// 1. Environment
	{
		const FString EnvValue = FPlatformMisc::GetEnvironmentVariable(*GetEnvironmentVariableName(ProviderId));
		if (!EnvValue.IsEmpty())
		{
			OutSecret = EnvValue;
			if (OutSource) { *OutSource = EMCPSecretSource::Environment; }
			Remember(OutSecret);
			return true;
		}
	}

	// 2. Session memory (set with bSessionOnly)
	{
		FScopeLock ScopeLock(&Lock);
		if (const FString* Found = SessionSecrets.Find(ProviderId))
		{
			OutSecret = *Found;
			if (OutSource) { *OutSource = EMCPSecretSource::SessionMemory; }
			return true;
		}
	}

	// 3. OS keychain
	if (IsKeychainAvailable() && KeychainGet(StorageKeyFor(ProviderId), OutSecret) && !OutSecret.IsEmpty())
	{
		if (OutSource) { *OutSource = EMCPSecretSource::Keychain; }
		Remember(OutSecret);
		return true;
	}

	// 4. Encrypted file
	if (FileGet(ProviderId, OutSecret) && !OutSecret.IsEmpty())
	{
		if (OutSource) { *OutSource = EMCPSecretSource::EncryptedFile; }
		Remember(OutSecret);
		return true;
	}

	OutSecret.Reset();
	if (OutSource) { *OutSource = EMCPSecretSource::None; }
	return false;
}

bool FMCPChatSecretStore::HasSecret(const FString& ProviderId) const
{
	FString Ignored;
	return GetSecret(ProviderId, Ignored);
}

// ============================================================================
// Write
// ============================================================================

bool FMCPChatSecretStore::SetSecret(const FString& ProviderId, const FString& Secret, bool bSessionOnly)
{
	{
		FScopeLock ScopeLock(&Lock);
		KnownSecretValues.Add(Secret);
	}

	if (bSessionOnly)
	{
		FScopeLock ScopeLock(&Lock);
		SessionSecrets.Add(ProviderId, Secret);
		return true;
	}

	if (IsKeychainAvailable() && KeychainSet(StorageKeyFor(ProviderId), Secret))
	{
		return true;
	}

	if (FileSet(ProviderId, Secret))
	{
		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("Stored the %s key in an encrypted file — the OS keychain was unavailable. ")
			TEXT("This is weaker than a keychain: the file is readable by anything running as you."),
			*ProviderId);
		return true;
	}

	// Never silently lose a key: fall back to memory and say so.
	{
		FScopeLock ScopeLock(&Lock);
		SessionSecrets.Add(ProviderId, Secret);
	}
	UE_LOG(LogUnrealMCPChat, Warning,
		TEXT("Could not persist the %s key; keeping it for this editor session only."), *ProviderId);
	return false;
}

void FMCPChatSecretStore::ClearSecret(const FString& ProviderId)
{
	{
		FScopeLock ScopeLock(&Lock);
		SessionSecrets.Remove(ProviderId);
	}
	KeychainRemove(StorageKeyFor(ProviderId));
	FileRemove(ProviderId);
}

// ============================================================================
// Masking / redaction
// ============================================================================

FString FMCPChatSecretStore::MaskSecret(const FString& Secret)
{
	if (Secret.IsEmpty()) { return FString(); }
	if (Secret.Len() <= 12)
	{
		return FString::ChrN(Secret.Len(), TEXT('•'));
	}
	// Keep enough of each end that a user can tell two keys apart, no more.
	return FString::Printf(TEXT("%s…%s%s"),
		*Secret.Left(7), TEXT("••••••••"), *Secret.Right(4));
}

FText FMCPChatSecretStore::DescribeSource(EMCPSecretSource Source)
{
	switch (Source)
	{
	case EMCPSecretSource::Environment:   return LOCTEXT("SrcEnv", "environment variable");
	case EMCPSecretSource::Keychain:      return LOCTEXT("SrcKeychain", "OS keychain");
	case EMCPSecretSource::EncryptedFile: return LOCTEXT("SrcFile", "encrypted file");
	case EMCPSecretSource::SessionMemory: return LOCTEXT("SrcSession", "this session only");
	case EMCPSecretSource::None:
	default:                              return LOCTEXT("SrcNone", "not set");
	}
}

FString FMCPChatSecretStore::RedactSecrets(const FString& In) const
{
	if (In.IsEmpty()) { return In; }

	FString Out = In;

	// Exact values we know about first — catches a key echoed back verbatim.
	{
		FScopeLock ScopeLock(&Lock);
		for (const FString& Value : KnownSecretValues)
		{
			if (Value.Len() >= 8 && Out.Contains(Value))
			{
				Out.ReplaceInline(*Value, TEXT("***REDACTED***"), ESearchCase::CaseSensitive);
			}
		}
	}

	// Then anything that merely looks like a key.
	for (const TCHAR* Prefix : SecretPrefixes)
	{
		int32 Idx = 0;
		while ((Idx = Out.Find(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart, Idx)) != INDEX_NONE)
		{
			int32 End = Idx;
			while (End < Out.Len()
				&& (FChar::IsAlnum(Out[End]) || Out[End] == TEXT('-') || Out[End] == TEXT('_')))
			{
				++End;
			}
			if (End - Idx >= 12)
			{
				Out = Out.Left(Idx) + TEXT("***REDACTED***") + Out.Mid(End);
				Idx += 14;
			}
			else
			{
				Idx = End > Idx ? End : Idx + 1;
			}
		}
	}
	return Out;
}

// ============================================================================
// Keychain — per platform
// ============================================================================

namespace
{
#if PLATFORM_MAC || PLATFORM_LINUX
	/**
	 * Phase 8 — the keychain via its command-line front end.
	 *
	 * WHY A SUBPROCESS AND NOT THE LIBRARY:
	 *   macOS  — Security.framework works, but linking it drags an Apple framework
	 *            into a plugin that otherwise has none, and the API is deprecated
	 *            in parts across OS versions.
	 *   Linux  — libsecret is not installed on every distro. Linking it would make
	 *            the plugin fail to load on machines where it is absent, which is a
	 *            far worse outcome than falling back to the encrypted file.
	 *
	 * `security` (macOS) and `secret-tool` (Linux) are the supported front ends for
	 * exactly this, are present wherever the keychain itself is usable, and cost one
	 * process spawn per key — which happens on the first request of a session, not
	 * per message.
	 *
	 * The secret is passed on stdin where the tool supports it, never as an argv
	 * element: process arguments are world-readable in `ps` on both platforms.
	 */
	bool RunKeychainTool(const FString& Executable, const FString& Arguments,
	                     const FString& StdInPayload, FString& OutStdOut)
	{
		const FString Resolved = FMCPChatProcessRunner::ResolveExecutable(Executable);
		if (Resolved.IsEmpty()) { return false; }

		// ---- Read path: nothing sensitive going in, so ExecProcess is enough. ----
		if (StdInPayload.IsEmpty())
		{
			int32 ReturnCode = -1;
			FString StdErr;
			const bool bRan = FPlatformProcess::ExecProcess(*Resolved, *Arguments,
				&ReturnCode, &OutStdOut, &StdErr);
			return bRan && ReturnCode == 0;
		}

		// ---- Write path: the secret goes over STDIN. ----
		// Not as an argv element (world-readable in `ps`) and not through a temp file
		// (however briefly, that is a plaintext key on disk). ExecProcess has no stdin,
		// so this drives CreateProc directly and waits — a keychain write happens when
		// the user presses Save, not on any hot path.
		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe, /*bWritePipeLocal*/ true))
		{
			return false;
		}

		uint32 ProcessId = 0;
		FProcHandle Handle = FPlatformProcess::CreateProc(
			*Resolved, *Arguments,
			/*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
			&ProcessId, /*PriorityModifier*/ 0, /*WorkingDirectory*/ nullptr,
			/*PipeWriteChild*/ nullptr, /*PipeReadChild*/ ReadPipe);

		if (!Handle.IsValid())
		{
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
			return false;
		}

		{
			FTCHARToUTF8 Converter(*StdInPayload);
			TArray<uint8> Bytes;
			Bytes.Reserve(Converter.Length() + 1);
			Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
			Bytes.Add(static_cast<uint8>('\n'));
			FPlatformProcess::WritePipe(WritePipe, Bytes.GetData(), Bytes.Num(), nullptr);
		}

		// Closing stdin is what tells the tool the password is complete; without it
		// `security -w` waits for a terminator that never comes.
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

		FPlatformProcess::WaitForProc(Handle);

		int32 ReturnCode = -1;
		FPlatformProcess::GetProcReturnCode(Handle, &ReturnCode);
		FPlatformProcess::CloseProc(Handle);

		return ReturnCode == 0;
	}

#endif // PLATFORM_MAC || PLATFORM_LINUX

	const TCHAR* KeychainServiceName = TEXT("UnrealMCPChat");
}

bool FMCPChatSecretStore::IsKeychainAvailable()
{
#if PLATFORM_WINDOWS
	return true;
#elif PLATFORM_MAC
	// `security` ships with macOS; if it is missing, something is very wrong and the
	// encrypted file is the right answer anyway.
	static const bool bAvailable = !FMCPChatProcessRunner::ResolveExecutable(TEXT("security")).IsEmpty();
	return bAvailable;
#elif PLATFORM_LINUX
	// Probed once. A headless build machine has no secret service, and asking every
	// time would spawn a process per key lookup.
	static const bool bAvailable = !FMCPChatProcessRunner::ResolveExecutable(TEXT("secret-tool")).IsEmpty();
	return bAvailable;
#else
	return false;
#endif
}

bool FMCPChatSecretStore::KeychainSet(const FString& Key, const FString& Secret)
{
#if PLATFORM_WINDOWS
	// DPAPI encrypts with the current user's credentials: another user on the same
	// machine cannot decrypt it, and it needs no key management from us.
	const FTCHARToUTF8 Utf8(*Secret);

	DATA_BLOB In;
	In.pbData = reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(Utf8.Get()));
	In.cbData = static_cast<DWORD>(Utf8.Length());

	DATA_BLOB Out{};
	if (!CryptProtectData(&In, L"UnrealMCPChat", nullptr, nullptr, nullptr,
		CRYPTPROTECT_UI_FORBIDDEN, &Out))
	{
		return false;
	}

	TArray<uint8> Encrypted;
	Encrypted.Append(Out.pbData, Out.cbData);
	LocalFree(Out.pbData);

	// DPAPI gives us ciphertext; where it lives is our choice. The registry via
	// SetStoredValue is per-user and convenient, and the payload is already
	// encrypted, so the weak storage location is not load-bearing.
	return FPlatformMisc::SetStoredValue(TEXT("UnrealMCPChat"), TEXT("Secrets"), Key,
		FBase64::Encode(Encrypted));

#elif PLATFORM_MAC
	// -U updates in place; without it a second save fails with "already exists"
	// rather than replacing the key, and the user's new key silently does nothing.
	FString Out;
	return RunKeychainTool(TEXT("security"),
		FString::Printf(TEXT("add-generic-password -U -s \"%s\" -a \"%s\" -w"),
			KeychainServiceName, *Key),
		Secret, Out);

#elif PLATFORM_LINUX
	FString Out;
	return RunKeychainTool(TEXT("secret-tool"),
		FString::Printf(TEXT("store --label=\"%s: %s\" service %s account %s"),
			KeychainServiceName, *Key, KeychainServiceName, *Key),
		Secret, Out);

#else
	(void)Key; (void)Secret;
	return false;
#endif
}

bool FMCPChatSecretStore::KeychainGet(const FString& Key, FString& OutSecret)
{
#if PLATFORM_WINDOWS
	FString Stored;
	if (!FPlatformMisc::GetStoredValue(TEXT("UnrealMCPChat"), TEXT("Secrets"), Key, Stored)
		|| Stored.IsEmpty())
	{
		return false;
	}

	TArray<uint8> Encrypted;
	if (!FBase64::Decode(Stored, Encrypted)) { return false; }

	DATA_BLOB In;
	In.pbData = Encrypted.GetData();
	In.cbData = static_cast<DWORD>(Encrypted.Num());

	DATA_BLOB Out{};
	if (!CryptUnprotectData(&In, nullptr, nullptr, nullptr, nullptr,
		CRYPTPROTECT_UI_FORBIDDEN, &Out))
	{
		return false;
	}

	TArray<uint8> Plain;
	Plain.Append(Out.pbData, Out.cbData);
	Plain.Add(0);
	LocalFree(Out.pbData);

	OutSecret = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Plain.GetData())));
	return true;

#elif PLATFORM_MAC
	FString Out;
	if (!RunKeychainTool(TEXT("security"),
		FString::Printf(TEXT("find-generic-password -s \"%s\" -a \"%s\" -w"),
			KeychainServiceName, *Key),
		FString(), Out))
	{
		return false;
	}
	// -w prints the password and a newline. Keeping the newline would send a key
	// with a trailing \n in an Authorization header, which 401s in a way nobody
	// would guess from the message.
	OutSecret = Out.TrimStartAndEnd();
	return !OutSecret.IsEmpty();

#elif PLATFORM_LINUX
	FString Out;
	if (!RunKeychainTool(TEXT("secret-tool"),
		FString::Printf(TEXT("lookup service %s account %s"), KeychainServiceName, *Key),
		FString(), Out))
	{
		return false;
	}
	OutSecret = Out.TrimStartAndEnd();
	return !OutSecret.IsEmpty();

#else
	(void)Key; (void)OutSecret;
	return false;
#endif
}

void FMCPChatSecretStore::KeychainRemove(const FString& Key)
{
#if PLATFORM_WINDOWS
	FPlatformMisc::DeleteStoredValue(TEXT("UnrealMCPChat"), TEXT("Secrets"), Key);

#elif PLATFORM_MAC
	FString Out;
	RunKeychainTool(TEXT("security"),
		FString::Printf(TEXT("delete-generic-password -s \"%s\" -a \"%s\""),
			KeychainServiceName, *Key),
		FString(), Out);

#elif PLATFORM_LINUX
	FString Out;
	RunKeychainTool(TEXT("secret-tool"),
		FString::Printf(TEXT("clear service %s account %s"), KeychainServiceName, *Key),
		FString(), Out);

#else
	(void)Key;
#endif
}

// ============================================================================
// Encrypted-file fallback
// ============================================================================

FString FMCPChatSecretStore::GetEncryptedFilePath()
{
	// Under Saved/, which is git-ignored by every UE project template.
	return FPaths::ProjectSavedDir() / TEXT("UnrealMCPChat") / TEXT("credentials.bin");
}

TArray<uint8> FMCPChatSecretStore::DeriveFileKey()
{
	// Machine + user derived, so copying the file to another machine is useless.
	// This is NOT a password-based KDF and is not claimed to be — it raises the bar
	// above plaintext, which is the honest scope of a fallback.
	// ComputerName() and UserName() already return const TCHAR*; the extra '*' was
	// dereferencing to a single character, which the format-string checker catches.
	const FString Material = FString::Printf(TEXT("UnrealMCPChat|%s|%s"),
		FPlatformProcess::ComputerName(), FPlatformProcess::UserName());

	// FSHA256Signature is a 32-byte POD with no hashing behind it — only FSHA1 has a
	// HashBuffer in Core. AES needs 32 bytes and SHA-1 gives 20, so two independently
	// salted digests are concatenated and truncated.
	//
	// This is NOT a password-based KDF and never claimed to be. Its job is to make the
	// fallback file useless on a different machine; the OS keychain above it is the
	// real protection.
	const FString FirstMaterial  = Material + TEXT("|k1");
	const FString SecondMaterial = Material + TEXT("|k2");

	const FTCHARToUTF8 FirstUtf8(*FirstMaterial);
	const FTCHARToUTF8 SecondUtf8(*SecondMaterial);

	uint8 FirstHash[20]  = {};
	uint8 SecondHash[20] = {};
	FSHA1::HashBuffer(FirstUtf8.Get(),  FirstUtf8.Length(),  FirstHash);
	FSHA1::HashBuffer(SecondUtf8.Get(), SecondUtf8.Length(), SecondHash);

	TArray<uint8> KeyBytes;
	KeyBytes.Reserve(FAES::FAESKey::KeySize);
	KeyBytes.Append(FirstHash, 20);
	KeyBytes.Append(SecondHash, FAES::FAESKey::KeySize - 20);
	return KeyBytes;
}

bool FMCPChatSecretStore::FileSet(const FString& Key, const FString& Secret)
{
	// Read-modify-write the whole map: a handful of keys, and one file is simpler
	// to reason about than one file per provider.
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	{
		TArray<uint8> Existing;
		if (FFileHelper::LoadFileToArray(Existing, *GetEncryptedFilePath()) && Existing.Num() > 0)
		{
			FAES::FAESKey AesKey;
			FMemory::Memcpy(AesKey.Key, DeriveFileKey().GetData(), FAES::FAESKey::KeySize);
			FAES::DecryptData(Existing.GetData(), Existing.Num(), AesKey);

			Existing.Add(0);
			const FString Json = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Existing.GetData())));
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			TSharedPtr<FJsonObject> Parsed;
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				Root = Parsed;
			}
		}
	}

	Root->SetStringField(Key, Secret);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	const FTCHARToUTF8 Utf8(*Out);
	TArray<uint8> Plain;
	Plain.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	// AES block alignment.
	while (Plain.Num() % FAES::AESBlockSize != 0) { Plain.Add(0); }

	FAES::FAESKey AesKey;
	FMemory::Memcpy(AesKey.Key, DeriveFileKey().GetData(), FAES::FAESKey::KeySize);
	FAES::EncryptData(Plain.GetData(), Plain.Num(), AesKey);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(GetEncryptedFilePath()), true);
	return FFileHelper::SaveArrayToFile(Plain, *GetEncryptedFilePath());
}

bool FMCPChatSecretStore::FileGet(const FString& Key, FString& OutSecret) const
{
	TArray<uint8> Encrypted;
	if (!FFileHelper::LoadFileToArray(Encrypted, *GetEncryptedFilePath()) || Encrypted.Num() == 0)
	{
		return false;
	}

	FAES::FAESKey AesKey;
	FMemory::Memcpy(AesKey.Key, DeriveFileKey().GetData(), FAES::FAESKey::KeySize);
	FAES::DecryptData(Encrypted.GetData(), Encrypted.Num(), AesKey);

	Encrypted.Add(0);
	const FString Json = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Encrypted.GetData())));

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		// Wrong machine, wrong user, or a corrupt file — all indistinguishable and
		// all mean "no key here".
		return false;
	}

	return Root->TryGetStringField(Key, OutSecret);
}

void FMCPChatSecretStore::FileRemove(const FString& Key)
{
	FString Existing;
	if (!FileGet(Key, Existing)) { return; }
	FileSet(Key, FString());
}

#undef LOCTEXT_NAMESPACE
