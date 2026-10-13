#!/usr/bin/env python3
"""Start a native port of an Equicord plugin.

    python3 tools/new_plugin.py BlockKeywords --hook messageFilter

creates, from the pinned Equicord inventory:

  plugins/<category>/<id>/plugin.json   the manifest (name, upstream path, authors, ...)
  plugins/<category>/<id>/<Name>.cpp    the plugin class, registered with the client
  tests/plugins/tst_<id>.cpp            a test to fill in

and adds the plugin and its test to the CMake lists. Hooks you pass with --hook
are declared in the manifest and in hooks(), and the permissions they need are
added for you; see src/Plugins/Plugin.hpp for each hook's functions.

The generated test is marked as a template: the plugin counts as Tested only
once you replace it with tests of what the plugin does.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qord_inventory as inv  # noqa: E402

PERMISSION_CPP = inv.ROOT / "src" / "Plugins" / "Permission.cpp"
PLUGINS_CMAKE = inv.PLUGINS_DIR / "CMakeLists.txt"
TESTS_CMAKE = inv.PLUGIN_TESTS_DIR / "CMakeLists.txt"

# Equicord tags -> Qord category directories, first match wins
TAG_CATEGORIES = {
    "Appearance": "appearance", "Customisation": "appearance", "Accessibility": "accessibility",
    "Chat": "chat", "Media": "media", "Notifications": "notifications", "Voice": "voice",
    "Servers": "servers", "Roles": "roles", "Emotes": "emotes", "Commands": "commands",
    "Privacy": "privacy", "Developers": "developer", "Utility": "utility", "Fun": "chat",
    "Friends": "utility", "Shortcuts": "utility", "Activity": "utility",
}


def plugin_id(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def class_name(name: str) -> str:
    base = re.sub(r"[^A-Za-z0-9]", "", name)
    return (base[0].upper() + base[1:]) if base else "New"


def hook_permissions() -> dict[str, str]:
    """hook name -> permission name, read from Permission.cpp's tables."""
    return inv.hook_permissions()


def hook_enum(name: str) -> str:
    """Hook::... for a manifest hook name ("onBeforeSend" -> BeforeSend)."""
    text = PERMISSION_CPP.read_text(encoding="utf-8")
    m = re.search(rf'\{{\s*Hook::(\w+),\s*"{re.escape(name)}"', text)
    if not m:
        raise SystemExit(f"error: unknown hook {name!r}; see src/Plugins/Permission.cpp")
    return m.group(1)


def insert_line(path: Path, prefix: str, line: str) -> None:
    """Add `line` after the last line starting with `prefix`."""
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    last = max(i for i, l in enumerate(lines) if l.startswith(prefix))
    # The call may span lines: insert after its closing parenthesis
    depth = 0
    for end in range(last, len(lines)):
        depth += lines[end].count("(") - lines[end].count(")")
        if depth <= 0:
            break
    lines.insert(end + 1, line + "\n")
    path.write_text("".join(lines), encoding="utf-8", newline="\n")


def render_cpp(name: str, cls: str, pid: str, upstream: str, authors: list[str], hooks: list[str]) -> str:
    hook_expr = " | ".join(f"Hook::{hook_enum(h)}" for h in hooks) or "{}"
    author_word = "author" if len(authors) == 1 else "authors"
    return f"""/*
 * Qord, a native Discord client
 * Port of Equicord's {name} plugin ({upstream})
 * Original: Copyright (c) Vendicated and contributors, {author_word} {", ".join(authors)}
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {{
namespace Plugins {{

namespace {{
// TODO: one line on what the plugin does, as the user sees it
class {cls}Plugin : public Plugin
{{
public:
    [[nodiscard]] Hooks hooks() const override {{ return {hook_expr}; }}

    void start(PluginContext &ctx) override {{ context = &ctx; }}
    void stop() override {{ context = nullptr; }}

    // Override the functions of the hooks above (see src/Plugins/Plugin.hpp),
    // and onSettingChanged() if the plugin has settings.

private:
    PluginContext *context = nullptr;
}};
}} // namespace

QByteArray qord_manifest_{pid}();

void qord_register_{pid}(Registry &registry)
{{
    registry.add({{ QStringLiteral("{pid}/plugin.json"), [] {{ return std::make_unique<{cls}Plugin>(); }},
                   qord_manifest_{pid}() }});
}}

}} // namespace Plugins
}} // namespace Qord
"""


def render_test(cls: str, pid: str) -> str:
    return f"""#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {{
void qord_register_{pid}(Registry &registry);
}}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class Test{cls} : public QObject
{{
    Q_OBJECT

private slots:
    void loadsAndEnables()
    {{
        SinglePluginHost h(&qord_register_{pid});
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
    }}

    // QORD_SCAFFOLD_TEST: replace this with tests of what the plugin does
    // (dispatch its hooks through h.host and check the fake services in h,
    // see PluginTestUtils.hpp), then delete this marker. Until then the
    // plugin doesn't count as Tested.
    void behavesLikeUpstream() {{ QSKIP("not written yet"); }}
}};

QTEST_GUILESS_MAIN(Test{cls})
#include "tst_{pid}.moc"
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="the plugin's Equicord name, as in docs/equicord-plugin-compatibility.md")
    ap.add_argument("--category", choices=inv.CATEGORIES, help="plugins/<category>/ (guessed from its tags)")
    ap.add_argument("--hook", action="append", default=[], help="a hook it implements (repeatable)")
    ap.add_argument("--permission", action="append", default=[], help="an extra permission it needs (repeatable)")
    ap.add_argument("--dry-run", action="store_true", help="print what would be created")
    args = ap.parse_args()

    entries = {e["name"]: e for e in inv.load_json(inv.INVENTORY_PATH)}
    entry = entries.get(args.name)
    if entry is None:
        close = [n for n in entries if n.lower() == args.name.lower()]
        hint = f" (did you mean {close[0]}?)" if close else ""
        print(f"error: {args.name!r} is not in the pinned Equicord inventory{hint}", file=sys.stderr)
        return 1
    name = entry["name"]
    pid = plugin_id(name)
    if any(inv.PLUGINS_DIR.glob(f"*/{pid}/plugin.json")):
        print(f"error: plugins/*/{pid} already exists", file=sys.stderr)
        return 1

    category = args.category or next((TAG_CATEGORIES[t] for t in entry["tags"] if t in TAG_CATEGORIES), "utility")
    needed = hook_permissions()
    for h in args.hook:
        if h not in needed:
            print(f"error: unknown hook {h!r}; known: {', '.join(sorted(needed))}", file=sys.stderr)
            return 1
    permissions = []
    for p in [needed[h] for h in args.hook] + args.permission:
        if p not in inv.PERMISSIONS:
            print(f"error: unknown permission {p!r}", file=sys.stderr)
            return 1
        if p not in permissions:
            permissions.append(p)

    authors = [a["name"] for a in entry["authors"]] or ["Vendicated"]
    categories = [category] + [c for c in (TAG_CATEGORIES.get(t) for t in entry["tags"]) if c and c != category]
    manifest = {
        "id": pid,
        "equicordName": name,
        "equicordPath": entry["filePath"],
        "name": name,
        "description": entry["description"],
        "summary": "TODO: what it does, in a few words, for the README",
        "categories": list(dict.fromkeys(categories)),
        "tags": entry["tags"],
        "authors": authors,
        "permissions": permissions,
        "hooks": args.hook,
        "settings": [],
        "enabledByDefault": False,
        "target": entry.get("target") or "",
        "dependencies": [],
        "commands": [],
        "notes": "TODO: how it behaves in Qord, and anything that differs from upstream.",
    }
    cls = class_name(name)
    plugin_dir = inv.PLUGINS_DIR / category / pid
    files = {
        plugin_dir / "plugin.json": json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        plugin_dir / f"{cls}.cpp": render_cpp(name, cls, pid, entry["filePath"], authors, args.hook),
        inv.PLUGIN_TESTS_DIR / f"tst_{pid}.cpp": render_test(cls, pid),
    }
    plugin_line = f"qord_add_plugin({pid} {category} SOURCES {cls}.cpp)"
    test_line = f"qord_add_plugin_test({pid})"

    if args.dry_run:
        for path, text in files.items():
            print(f"--- {path.relative_to(inv.ROOT).as_posix()}\n{text}")
        print(f"--- plugins/CMakeLists.txt += {plugin_line}\n--- tests/plugins/CMakeLists.txt += {test_line}")
        return 0

    plugin_dir.mkdir(parents=True)
    for path, text in files.items():
        path.write_text(text, encoding="utf-8", newline="\n")
    insert_line(PLUGINS_CMAKE, "qord_add_plugin(", plugin_line)
    insert_line(TESTS_CMAKE, "qord_add_plugin_test(", test_line)

    print(f"Created {name} as plugins/{category}/{pid}/ ({', '.join(permissions) or 'no permissions'}).")
    print(f"""
Next:
  1. Read upstream: https://github.com/Equicord/Equicord/tree/main/{entry['filePath']}
  2. Write the plugin in plugins/{category}/{pid}/{cls}.cpp and its settings in plugin.json
  3. Write tests/plugins/tst_{pid}.cpp (and remove its QORD_SCAFFOLD_TEST marker)
  4. Build and test: cmake --build build && ctest --test-dir build -R {pid}
  5. Try it against the mock server, then run tools/check_equicord_plugins.py --write-docs
See CONTRIBUTING.md and docs/writing-plugins.md.""")
    return 0


if __name__ == "__main__":
    sys.exit(main())
