# Qord

Qord is a native Discord client built on [Acheron](https://github.com/ouwou/acheron)
(C++ / Qt Widgets) that re-implements the behavior of
[Equicord](https://github.com/Equicord/Equicord) plugins as native plugins.
There is no Electron, Chromium, WebView or JavaScript runtime.

GPL-3.0, like Acheron and Equicord. Each plugin credits its original authors
in its manifest and source headers.

## Where things are

| Path | What |
|---|---|
| `src/Plugins/` | Plugin host library: manifests, permissions, settings, dispatch (no UI) |
| `src/Plugins/Client/`, `src/UI/Settings/PluginsPage.*` | Client glue and the Plugins settings page |
| `plugins/<category>/<id>/` | Native plugins (`plugin.json` + C++) |
| `tests/plugins/` | One Qt Test per plugin, plus host tests |
| `data/equicord/` | Pinned Equicord inventory, source version, status overrides |
| `docs/equicord-plugin-compatibility.md` | Generated status of every Equicord plugin |
| `docs/decisions.md`, `docs/performance.md`, `docs/building.md`, `docs/plugin-status.md` | Decisions, measurements, build steps, status rules |
| `tools/` | Inventory tools, Windows build and measurement scripts |
| `qord.cmake` | Everything Qord adds to Acheron's build (one `include()`) |

Statuses in the compatibility matrix are derived by
`tools/check_equicord_plugins.py` from manifests, tests and wiring in the
repository; see `docs/plugin-status.md`.
