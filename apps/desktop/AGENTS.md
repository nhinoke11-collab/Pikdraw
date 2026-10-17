# desktop scope

Canvas is the main workspace. UI calls module services and document commands; modules cannot import UI. Keep internal diagnostics outside routine design flows.

Before implementing a feature, read `docs/design/ui-ux-standard.md` and complete its UI template in the feature spec: primary menu path/action IDs, toolbox/standard toolbar, contextual property bar, Properties section, field scopes/states, SVG icon, short labels, tooltip/focus help, status hints and usage help. Reuse one command/value across entry points; keep panel-opening actions distinct from document-editing actions. No new bar/panel or long text button without a documented placement reason. Old Cyber mockups do not override the current workspace rules. Report specification/mockup/runtime evidence separately.
The implementing agent analyzes the request and existing UI, authors the spec, decides routine design choices, self-checks them and continues coding within scope. Do not send the user a blank template or wait for user/coordinator approval of routine menu, layout, icon or help decisions. Record concise reasoning for delivery review; existing ownership/contract rules still apply.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
