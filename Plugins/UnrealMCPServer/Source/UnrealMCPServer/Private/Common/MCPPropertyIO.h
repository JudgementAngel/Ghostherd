// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class FProperty;
class UObject;

/**
 * v4 Phase 1 — type-aware property import/export.
 *
 * v3 set properties exclusively through ImportText_Direct, which:
 *  - fails silently with no reason,
 *  - cannot take TArray/TMap/nested structs from real JSON values,
 *  - forced agents to learn UE's text syntax for every type.
 *
 * This wrapper accepts BOTH conventions — UE text syntax ("X=1,Y=2,Z=3") and
 * native JSON values ({"x":1,"y":2,"z":3}, arrays, nested objects) — and on
 * failure returns a message that names the property type and the expected
 * formats, so agents can self-correct.
 */
namespace MCPCommon
{
	/** Set Prop (inside Container's owner object/struct memory) from a JSON value.
	 *  Strings go through ImportText first (UE syntax), then JSON conversion;
	 *  all other JSON types use FJsonObjectConverter (handles arrays/maps/structs).
	 *  OutError explains the expected format on failure. */
	bool SetPropertyFromJson(FProperty* Prop, void* Container, UObject* OwnerForImportText,
		const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** Convenience for the common string-only call sites. */
	bool SetPropertyFromString(FProperty* Prop, void* Container, UObject* OwnerForImportText,
		const FString& Value, FString& OutError);

	/** Set a property that lives directly on a UObject, with full editor change
	 *  notification: Modify() + PreEditChange() + import + PostEditChangeProperty().
	 *  REQUIRED when the owner caches state derived from the property — e.g.
	 *  UStaticMeshComponent tracks KnownStaticMesh and fires an ensure ("StaticMesh
	 *  property overwritten without a call to NotifyIfStaticMeshChanged") if the
	 *  property is overwritten behind its back. Use for component templates, CDOs,
	 *  and asset objects; plain SetPropertyFromString is fine for raw struct memory. */
	bool SetObjectPropertyWithNotify(FProperty* Prop, UObject* Object,
		const FString& Value, FString& OutError);

	/** Export a property to a JSON value (null on unsupported). */
	TSharedPtr<FJsonValue> ExportPropertyToJson(FProperty* Prop, const void* Container);
}
