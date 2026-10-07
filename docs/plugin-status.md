# How plugin statuses are derived

`tools/check_equicord_plugins.py` computes every status from evidence in the
repository. Nobody edits a status by hand except for the three listed below.

| Status | Evidence |
|---|---|
| Not Started | Nothing below applies |
| Analyzing / In Progress | `data/equicord/status.json` says so, with `notes`. Ignored (with a warning) once implementation evidence exists |
| Implemented | `plugins/<category>/<id>/plugin.json` names the inventory plugin, validates, and the directory has C++ sources |
| Tested | Implemented, plus `tests/plugins/tst_<id>.cpp` registered with `qord_add_plugin_test(<id>)`. With `--test-results` (ctest `--output-junit`), the `plugin_<id>` test must also have passed |
| Complete | Tested, plus: added with `qord_add_plugin(<id>)`; every hook in the manifest is dispatched from client code outside `src/Plugins` (see `HOOK_DISPATCH`); a `verifiedInClient` record in `status.json`; no open `gaps` in the manifest |
| Blocked | `status.json` sets `"status": "Blocked"` with a `reason` (at least 20 characters), and there is no manifest |

## status.json

```json
{
  "version": 1,
  "plugins": {
    "SomePlugin": { "status": "Blocked", "reason": "Why, specifically", "equivalent": "Native Qord feature that covers the responsibility, if any" },
    "Other": { "status": "Analyzing", "notes": "..." },
    "ClearURLs": { "verifiedInClient": { "date": "2026-10-07", "build": "<git sha>", "by": "<who>", "notes": "what was checked" } }
  }
}
```

`verifiedInClient` records a manual check in a logged-in client: the plugin
was enabled and its behavior was observed on real Discord traffic. Only add it
after doing that check, and say what was checked in `notes`.

## Gaps

A manifest's `gaps` lists upstream behavior that the port does not cover yet
(for example a UI surface Qord lacks). A plugin with open gaps can be Tested
but never Complete.
