# Known limitations (5.0.0-beta)

- **Platform**: Win64 only. Mac and Linux are not included in this release.
- **Beta**: the release has not completed a multi-hour soak, a second vision-capable host has not been qualified for image ingestion, and the external generation provider path has not been exercised live. Behaviour on those paths is implemented and tested with mocks but not proven in the field.
- **Visual capture** needs a rendering editor. Headless or NullRHI processes report no visual backend and produce no images. Native OS dialogs (file pickers) cannot be captured. Readiness proves that the editor painted after an operation, not that shader compilation or deferred asset work has settled; frames carry warnings for those states.
- **Observation** is polling-based with bounded retry hints; the server does not push frames or resource updates.
- **Plans** cover actors (property, transform, rename, create, delete). Asset-level plans (rename, move, save) are not part of plan recovery. Recovery of a deleted actor restores class, label, transform and editable properties; runtime component hierarchies and references from other actors are not restored.
- **Blueprint and widget patches** support a validated vocabulary of variable, node, connection and hierarchy operations; unsupported node classes and arbitrary graph rewrites are explicit errors.
- **World Partition** descriptor listings reflect saved actor descriptors; an unsaved map reports none.
- **External generation** stores files under `Saved/MCPV5/Generated`; the legacy synchronous generation tools remain and block the editor while they run. Prefer `submit_generation_job`.
- **Python**: `execute_python` requires the Python bridge setting and Destructive scope.
- **Discovery counts** depend on enabled plugins; a project without the optional plugins registers fewer tools than the reference lists.
