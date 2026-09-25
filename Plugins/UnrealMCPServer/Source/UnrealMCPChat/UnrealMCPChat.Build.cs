// Copyright StraySpark Studio 2026. All Rights Reserved.

using UnrealBuildTool;

public class UnrealMCPChat : ModuleRules
{
	public UnrealMCPChat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// The host module. Phase 0 exported FMCPTransactionManager,
			// FMCPWorkingSetStore and the registry's catalog/observer surface for us.
			"UnrealMCPServer",

			// MCPHttpServer.h (which the status bar reads for live server state)
			// includes HttpServerModule.h, so we need these on the include path even
			// though the chat module never serves HTTP itself.
			"HTTPServer",
			"HTTP",
			"Json",
			"JsonUtilities",

			// Editor shell
			"UnrealEd",
			"EditorFramework",
			"EditorSubsystem",
			"ToolMenus",
			"ToolWidgets",
			"WorkspaceMenuStructure",
			"LevelEditor",
			"Projects",

			// Settings
			"DeveloperSettings",
			"Settings",

			// Phase 1 — hot-reloadable themes need a file watcher.
			"DirectoryWatcher",

			// Phase 1 uses these only for paths/attachment plumbing that Phase 3+
			// builds on; kept here so the dependency set stops churning per phase.
			"ApplicationCore",
			"DesktopPlatform",
			"ImageWrapper",

			// Phase 3's markdown renderer resolves /Game/… links against the registry
			// and syncs the Content Browser; Phase 6's @Asset resolution and the
			// Content Browser drag-drop handler need the same two.
			"AssetRegistry",
			"ContentBrowser",
		});

		// Phase 4 — DPAPI (CryptProtectData / CryptUnprotectData) for the Windows
		// keychain path in FMCPChatSecretStore.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Crypt32.lib");
		}

		// Phase 1 has no exception-throwing surface of its own, but it links against
		// UnrealMCPServer which is built with exceptions on. Matching the setting
		// avoids a mismatched-EH-model warning on MSVC.
		bEnableExceptions = true;
		bUseUnity = true;
	}
}
