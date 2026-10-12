"""Shared logic for Qord's Equicord inventory tooling.

Everything reported here is computed from files in the repository:
the pinned upstream inventory, plugin manifests, test sources, CMake
registrations, client wiring and data/equicord/status.json. Nothing is
hardcoded, so the numbers can't drift from the evidence.
"""

from __future__ import annotations

import collections
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT / "data" / "equicord"
INVENTORY_PATH = DATA_DIR / "plugins.json"
SOURCE_VERSION_PATH = DATA_DIR / "source-version.json"
STATUS_PATH = DATA_DIR / "status.json"
PLUGINS_DIR = ROOT / "plugins"
PLUGIN_TESTS_DIR = ROOT / "tests" / "plugins"
CLIENT_SRC_DIR = ROOT / "src"
DOCS_DIR = ROOT / "docs"
MATRIX_PATH = DOCS_DIR / "equicord-plugin-compatibility.md"
SOURCE_DOC_PATH = DOCS_DIR / "equicord-source-version.md"

# Status names, ordered from least to most progressed. Blocked is separate.
NOT_STARTED = "Not Started"
ANALYZING = "Analyzing"
IN_PROGRESS = "In Progress"
IMPLEMENTED = "Implemented"
TESTED = "Tested"
COMPLETE = "Complete"
BLOCKED = "Blocked"
ALL_STATUSES = [NOT_STARTED, ANALYZING, IN_PROGRESS, IMPLEMENTED, TESTED, COMPLETE, BLOCKED]
# Only these may be set by hand in status.json; the rest are derived.
OVERRIDE_STATUSES = {ANALYZING, IN_PROGRESS, BLOCKED}

INVENTORY_REQUIRED_FIELDS = {
    "name", "description", "filePath", "dirName", "tags", "enabledByDefault",
    "required", "hasPatches", "hasCommands", "isModified", "searchTerms", "authors",
}
INVENTORY_OPTIONAL_FIELDS = {"dependencies", "commands", "target"}

CATEGORIES = [
    "core", "appearance", "accessibility", "chat", "media", "notifications", "voice",
    "servers", "roles", "emotes", "commands", "privacy", "developer", "utility",
    "experimental",
]

# Must match Qord::Plugins::permissionFromString in src/Plugins/Permission.cpp
PERMISSIONS = {
    "modify_outgoing_messages",
    "modify_displayed_messages",
    "modify_message_links",
    "reply_mentions",
    "read_guild_members",
    "network",
    "add_ui_elements",
    "write_clipboard",
    "log_messages",
    "hide_messages",
    "read_relationships",
    "open_links",
    "control_typing",
    "trust_links",
    "control_notifications",
    "edit_messages",
    "mark_read",
    "read_presences",
    "read_voice_states",
    "read_reactions",
}

SETTING_TYPES = {"bool", "int", "string", "choice", "stringlist", "json"}

# Hook name -> call that must appear in client code (outside src/Plugins) for
# the hook to count as wired into the running client. Must match PluginHost.
HOOK_DISPATCH = {
    "onBeforeSend": "dispatchBeforeSend(",
    "onBeforeEdit": "dispatchBeforeEdit(",
    "shouldMentionOnReply": "dispatchShouldMentionOnReply(",
    "transformCopiedMessageLink": "dispatchTransformCopiedMessageLink(",
    "transformIncomingContent": "dispatchTransformIncomingContent(",
    "chatBarButtons": "dispatchChatBarButtons(",
    "userContextMenu": "dispatchUserContextMenuItems(",
    "messageContextMenu": "dispatchMessageContextMenuItems(",
    "messageHistory": "dispatchMessageDeleted(",
    "messageFilter": "dispatchShouldHideMessage(",
    "typing": "dispatchShouldSendTyping(",
    "linkWarnings": "dispatchShouldConfirmLink(",
    "notifications": "dispatchShouldNotify(",
    "authorNames": "dispatchAuthorDisplayName(",
    "channelContextMenu": "dispatchChannelContextMenuItems(",
    "serverListButtons": "dispatchServerListButtons(",
    "memberCounts": "dispatchMemberCountBadges(",
    "userDecorations": "dispatchNameDecorations(",
    "profileImages": "dispatchProfileImageClicked(",
    "voiceStates": "dispatchVoiceStateChanged(",
    "messageClicks": "dispatchMessageClicked(",
    "reactionUsers": "dispatchReactionUsers(",
}

MANIFEST_REQUIRED = {
    "id", "equicordName", "equicordPath", "name", "description", "categories",
    "tags", "authors", "permissions", "settings", "enabledByDefault", "target",
    "dependencies", "commands", "hooks",
}

ID_RE = re.compile(r"^[a-z0-9][a-z0-9_]*$")


@dataclass
class Issue:
    kind: str
    message: str

    def __str__(self) -> str:
        return f"[{self.kind}] {self.message}"


@dataclass
class PluginRecord:
    name: str
    entry: dict
    manifest: dict | None = None
    manifest_path: Path | None = None
    has_source: bool = False
    test_path: Path | None = None
    test_registered: bool = False
    test_passed: bool | None = None  # None = no results supplied
    hooks_wired: bool = False
    unwired_hooks: list[str] = field(default_factory=list)
    override: dict | None = None
    status: str = NOT_STARTED
    notes: str = ""

    @property
    def equicord_path(self) -> str:
        return self.entry["filePath"]

    @property
    def qord_path(self) -> str:
        if self.manifest_path is None:
            return ""
        return self.manifest_path.parent.relative_to(ROOT).as_posix()


# ---------------------------------------------------------------- inventory

def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path):
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def validate_inventory(entries) -> list[Issue]:
    issues: list[Issue] = []
    if not isinstance(entries, list):
        return [Issue("inventory", "plugins.json is not a JSON array")]
    seen: dict[str, int] = {}
    for i, e in enumerate(entries):
        if not isinstance(e, dict):
            issues.append(Issue("inventory", f"entry {i} is not an object"))
            continue
        missing = INVENTORY_REQUIRED_FIELDS - e.keys()
        if missing:
            issues.append(Issue("inventory", f"entry {i} ({e.get('name')}) lacks {sorted(missing)}"))
        unknown = e.keys() - INVENTORY_REQUIRED_FIELDS - INVENTORY_OPTIONAL_FIELDS
        if unknown:
            issues.append(Issue("inventory", f"entry {e.get('name')} has unknown fields {sorted(unknown)}"))
        name = e.get("name")
        if name in seen:
            issues.append(Issue("inventory", f"duplicate plugin name {name!r}"))
        seen[name] = i
    return issues


def path_root(file_path: str) -> str:
    parts = file_path.split("/")
    if len(parts) > 2 and parts[2] == "_core":
        return "/".join(parts[:3])
    return "/".join(parts[:2])


def inventory_stats(entries: list[dict]) -> dict:
    return {
        "total": len(entries),
        "unique_names": len({e["name"] for e in entries}),
        "by_root": dict(sorted(collections.Counter(path_root(e["filePath"]) for e in entries).items())),
        "has_patches": sum(1 for e in entries if e["hasPatches"]),
        "has_commands": sum(1 for e in entries if e["hasCommands"]),
        "required": sum(1 for e in entries if e["required"]),
        "enabled_by_default": sum(1 for e in entries if e["enabledByDefault"]),
        "required_and_enabled_by_default": sum(1 for e in entries if e["required"] and e["enabledByDefault"]),
        "with_dependencies": sum(1 for e in entries if e.get("dependencies")),
        "targets": dict(collections.Counter(e.get("target") or "(none)" for e in entries).most_common()),
        "tags": dict(collections.Counter(t for e in entries for t in e["tags"]).most_common()),
    }


# ---------------------------------------------------------------- manifests

def find_manifests() -> list[Path]:
    if not PLUGINS_DIR.is_dir():
        return []
    return sorted(PLUGINS_DIR.glob("*/*/plugin.json"))


def validate_manifest(path: Path, m, inventory_by_name: dict[str, dict]) -> list[Issue]:
    rel = path.relative_to(ROOT).as_posix()
    issues: list[Issue] = []
    if not isinstance(m, dict):
        return [Issue("manifest", f"{rel}: not a JSON object")]
    missing = MANIFEST_REQUIRED - m.keys()
    if missing:
        issues.append(Issue("manifest", f"{rel}: missing fields {sorted(missing)}"))
    pid = m.get("id", "")
    if not isinstance(pid, str) or not ID_RE.match(pid):
        issues.append(Issue("manifest", f"{rel}: invalid id {pid!r}"))
    if path.parent.name != pid:
        issues.append(Issue("manifest", f"{rel}: directory name must equal id {pid!r}"))
    category_dir = path.parent.parent.name
    cats = m.get("categories") or []
    if category_dir not in CATEGORIES:
        issues.append(Issue("manifest", f"{rel}: unknown category directory {category_dir!r}"))
    if category_dir not in cats:
        issues.append(Issue("manifest", f"{rel}: categories must include its directory {category_dir!r}"))
    for c in cats:
        if c not in CATEGORIES:
            issues.append(Issue("manifest", f"{rel}: unknown category {c!r}"))
    for p in m.get("permissions") or []:
        if p not in PERMISSIONS:
            issues.append(Issue("manifest", f"{rel}: unknown permission {p!r}"))
    for h in m.get("hooks") or []:
        if h not in HOOK_DISPATCH:
            issues.append(Issue("manifest", f"{rel}: unknown hook {h!r}"))
    if not m.get("authors"):
        issues.append(Issue("manifest", f"{rel}: authors must credit the original plugin authors"))
    keys = set()
    for s in m.get("settings") or []:
        if not isinstance(s, dict) or "key" not in s or "type" not in s or "default" not in s or "label" not in s:
            issues.append(Issue("manifest", f"{rel}: setting needs key, type, default and label: {s!r}"))
            continue
        if s["type"] not in SETTING_TYPES:
            issues.append(Issue("manifest", f"{rel}: setting {s['key']} has unknown type {s['type']!r}"))
        if s["key"] in keys:
            issues.append(Issue("manifest", f"{rel}: duplicate setting key {s['key']!r}"))
        keys.add(s["key"])
        if s["type"] == "choice":
            values = [c.get("value") for c in s.get("choices", []) if isinstance(c, dict)]
            if not values or s["default"] not in values:
                issues.append(Issue("manifest", f"{rel}: choice setting {s['key']} needs choices containing its default"))

    name = m.get("equicordName")
    entry = inventory_by_name.get(name)
    if entry is None:
        issues.append(Issue("manifest", f"{rel}: equicordName {name!r} is not in the pinned inventory"))
    elif m.get("equicordPath") != entry["filePath"]:
        issues.append(Issue("manifest", f"{rel}: equicordPath {m.get('equicordPath')!r} != inventory {entry['filePath']!r}"))
    return issues


# -------------------------------------------------------------------- tests

def registered_plugin_tests() -> set[str]:
    cmake = PLUGIN_TESTS_DIR / "CMakeLists.txt"
    if not cmake.is_file():
        return set()
    text = cmake.read_text(encoding="utf-8")
    text = re.sub(r"#[^\n]*", "", text)
    return set(re.findall(r"qord_add_plugin_test\(\s*([a-z0-9_]+)", text))


def registered_plugins() -> set[str]:
    cmake = PLUGINS_DIR / "CMakeLists.txt"
    if not cmake.is_file():
        return set()
    text = re.sub(r"#[^\n]*", "", cmake.read_text(encoding="utf-8"))
    return set(re.findall(r"qord_add_plugin\(\s*([a-z0-9_]+)", text))


def load_test_results(path: Path) -> dict[str, bool]:
    """Parse a ctest --output-junit file into {test name: passed}."""
    results: dict[str, bool] = {}
    tree = ET.parse(path)
    for case in tree.iter("testcase"):
        name = case.get("name", "")
        failed = any(child.tag in ("failure", "error") for child in case)
        skipped = case.get("status") in ("notrun", "disabled") or any(c.tag == "skipped" for c in case)
        results[name] = not failed and not skipped
    return results


# ------------------------------------------------------------------- wiring

def client_sources_text() -> str:
    chunks = []
    for p in CLIENT_SRC_DIR.rglob("*"):
        if p.suffix not in (".cpp", ".hpp", ".h"):
            continue
        if "Plugins" in p.relative_to(CLIENT_SRC_DIR).parts[:1]:
            continue
        chunks.append(p.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(chunks)


# ------------------------------------------------------------------- status

def load_status_overrides() -> dict:
    if not STATUS_PATH.is_file():
        return {}
    data = load_json(STATUS_PATH)
    return data.get("plugins", {})


def evaluate(test_results_path: Path | None = None) -> tuple[list[PluginRecord], list[Issue], list[Issue], dict]:
    """Return (records, integrity errors, warnings, context)."""
    errors: list[Issue] = []
    warnings: list[Issue] = []

    entries = load_json(INVENTORY_PATH)
    errors += validate_inventory(entries)
    by_name = {e["name"]: e for e in entries}

    source_version = load_json(SOURCE_VERSION_PATH) if SOURCE_VERSION_PATH.is_file() else None
    if source_version is None:
        errors.append(Issue("inventory", "data/equicord/source-version.json is missing"))
    elif source_version.get("sha256") != sha256_file(INVENTORY_PATH):
        errors.append(Issue("inventory", "plugins.json does not match the SHA256 in source-version.json "
                                         "(change the inventory only through tools/update_equicord_inventory.py)"))

    records = {e["name"]: PluginRecord(name=e["name"], entry=e) for e in entries}

    # Manifests
    ids: dict[str, Path] = {}
    for path in find_manifests():
        try:
            m = load_json(path)
        except json.JSONDecodeError as ex:
            errors.append(Issue("manifest", f"{path.relative_to(ROOT).as_posix()}: invalid JSON: {ex}"))
            continue
        problems = validate_manifest(path, m, by_name)
        errors += problems
        pid = m.get("id") if isinstance(m, dict) else None
        if pid in ids:
            errors.append(Issue("manifest", f"duplicate plugin id {pid!r} in {path} and {ids[pid]}"))
            continue
        if pid:
            ids[pid] = path
        rec = records.get(m.get("equicordName")) if isinstance(m, dict) else None
        if rec is None:
            continue
        if rec.manifest is not None:
            errors.append(Issue("manifest", f"{rec.name} has more than one manifest"))
            continue
        rec.manifest = m
        rec.manifest_path = path
        rec.has_source = any(path.parent.glob("*.cpp"))

    # Tests
    registered_tests = registered_plugin_tests()
    registered_plugin_ids = registered_plugins()
    results = load_test_results(test_results_path) if test_results_path else None
    test_files = {p.stem[len("tst_"):]: p for p in PLUGIN_TESTS_DIR.glob("tst_*.cpp")} if PLUGIN_TESTS_DIR.is_dir() else {}
    host_tests = {k for k in test_files if k.startswith("host")}
    for tid in sorted(set(test_files) - set(ids) - host_tests):
        errors.append(Issue("test", f"tests/plugins/tst_{tid}.cpp has no matching plugin manifest"))
    for tid in sorted(registered_tests - set(test_files)):
        errors.append(Issue("test", f"qord_add_plugin_test({tid}) has no tests/plugins/tst_{tid}.cpp"))
    for pid in sorted(registered_plugin_ids - set(ids)):
        errors.append(Issue("manifest", f"qord_add_plugin({pid}) has no plugins/*/{pid}/plugin.json"))

    client_text = client_sources_text()
    overrides = load_status_overrides()
    for name in overrides:
        if name not in records:
            errors.append(Issue("status", f"status.json names {name!r}, which is not in the inventory"))

    for rec in records.values():
        ov = overrides.get(rec.name)
        rec.override = ov
        if ov is not None and ov.get("status") not in OVERRIDE_STATUSES | {None}:
            errors.append(Issue("status", f"status.json sets {rec.name} to {ov.get('status')!r}; "
                                          f"only {sorted(OVERRIDE_STATUSES)} may be set by hand"))
        if rec.manifest is not None:
            pid = rec.manifest["id"]
            rec.test_path = test_files.get(pid)
            rec.test_registered = pid in registered_tests
            if results is not None:
                rec.test_passed = results.get(f"plugin_{pid}")
            hooks = rec.manifest.get("hooks") or []
            rec.unwired_hooks = [h for h in hooks if HOOK_DISPATCH.get(h, "\0") not in client_text]
            rec.hooks_wired = pid in registered_plugin_ids and not rec.unwired_hooks
            if pid not in registered_plugin_ids:
                warnings.append(Issue("wiring", f"{rec.name}: plugin {pid} is not added to plugins/CMakeLists.txt"))
            if rec.test_path is None:
                warnings.append(Issue("test", f"{rec.name}: no tests/plugins/tst_{pid}.cpp"))
            elif not rec.test_registered:
                warnings.append(Issue("test", f"{rec.name}: tst_{pid}.cpp is not registered with qord_add_plugin_test"))
        derive_status(rec, errors, warnings, results is not None)

    ctx = {"entries": entries, "stats": inventory_stats(entries), "source_version": source_version,
           "test_results_supplied": results is not None}
    return list(records.values()), errors, warnings, ctx


def derive_status(rec: PluginRecord, errors: list[Issue], warnings: list[Issue], have_results: bool) -> None:
    ov = rec.override or {}
    ov_status = ov.get("status")
    notes = []

    if ov_status == BLOCKED:
        if rec.manifest is not None:
            errors.append(Issue("status", f"{rec.name} is Blocked in status.json but has a manifest"))
        rec.status = BLOCKED
        rec.notes = (ov.get("reason") or "").strip()
        if ov.get("equivalent"):
            rec.notes += f" Qord equivalent: {ov['equivalent']}"
        return

    if rec.manifest is None or not rec.has_source:
        if rec.manifest is not None and not rec.has_source:
            warnings.append(Issue("manifest", f"{rec.name}: manifest without C++ source does not count as implemented"))
        rec.status = ov_status if ov_status in (ANALYZING, IN_PROGRESS) else NOT_STARTED
        rec.notes = (ov.get("notes") or "").strip()
        return

    if ov_status in (ANALYZING, IN_PROGRESS):
        warnings.append(Issue("status", f"{rec.name}: status.json says {ov_status} but implementation evidence exists; ignoring"))

    status = IMPLEMENTED
    tested = rec.test_path is not None and rec.test_registered
    if tested and have_results:
        tested = rec.test_passed is True
        if rec.test_passed is not True:
            notes.append("test failed or did not run")
    if tested:
        status = TESTED
        verified = ov.get("verifiedInClient")
        gaps = rec.manifest.get("gaps") or []
        if rec.hooks_wired and verified and not gaps:
            status = COMPLETE
        elif rec.unwired_hooks:
            notes.append("unwired hooks: " + ", ".join(rec.unwired_hooks))
        elif rec.hooks_wired and not verified:
            notes.append("wired; not yet verified in the running client")
    gaps = rec.manifest.get("gaps") or []
    if gaps:
        notes.append("gaps: " + "; ".join(gaps))
    if rec.manifest.get("notes"):
        notes.insert(0, rec.manifest["notes"])
    rec.status = status
    rec.notes = " ".join(n if n.endswith(".") else n + "." for n in notes)


def summarize(records: list[PluginRecord]) -> dict:
    counts = collections.Counter(r.status for r in records)
    at_least_impl = sum(counts[s] for s in (IMPLEMENTED, TESTED, COMPLETE))
    return {
        "total": len(records),
        **{s: counts.get(s, 0) for s in ALL_STATUSES},
        "implemented_or_better": at_least_impl,
        "tested_or_better": counts.get(TESTED, 0) + counts.get(COMPLETE, 0),
        "missing": sum(1 for r in records if r.status in (NOT_STARTED, ANALYZING, IN_PROGRESS)),
        "without_metadata": sum(1 for r in records if r.manifest is None and r.status != BLOCKED),
        "without_tests": sum(1 for r in records if r.manifest is not None and not (r.test_path and r.test_registered)),
        "blocked_without_reason": sum(1 for r in records if r.status == BLOCKED and len(r.notes) < 20),
    }


# --------------------------------------------------------------------- docs

def _md_escape(s: str) -> str:
    return s.replace("|", "\\|").replace("\n", " ")


def render_matrix(records: list[PluginRecord], ctx: dict) -> str:
    sv = ctx["source_version"] or {}
    summary = summarize(records)
    lines = [
        "# Equicord plugin compatibility",
        "",
        "<!-- Generated by tools/check_equicord_plugins.py / tools/update_equicord_inventory.py. Do not edit. -->",
        "",
        f"Inventory: `{sv.get('repository', '?')}` @ `{sv.get('commit', '?')}` "
        f"(Equicord `{sv.get('equicordCommit', '?')}`), plugins.json SHA256 `{sv.get('sha256', '?')}`.",
        "",
        "Statuses are derived from evidence in the repository, see `docs/plugin-status.md`.",
        "",
        "| Status | Count |",
        "|---|---|",
    ]
    for s in ALL_STATUSES:
        lines.append(f"| {s} | {summary[s]} |")
    lines += [f"| **Total** | {summary['total']} |", "", "| Plugin | Equicord Path | Qord Path | Status | Notes |", "|---|---|---|---|---|"]
    for r in sorted(records, key=lambda r: r.name.lower()):
        qp = f"`{r.qord_path}`" if r.qord_path else ""
        lines.append(f"| {_md_escape(r.name)} | `{r.equicord_path}` | {qp} | {r.status} | {_md_escape(r.notes)} |")
    return "\n".join(lines) + "\n"


def render_source_doc(ctx: dict) -> str:
    sv = ctx["source_version"] or {}
    st = ctx["stats"]
    lines = [
        "# Equicord source version",
        "",
        "<!-- Generated by tools/update_equicord_inventory.py. Do not edit. -->",
        "",
        "| Field | Value |",
        "|---|---|",
        f"| Inventory repository | {sv.get('repository', '?')} |",
        f"| Inventory commit | `{sv.get('commit', '?')}` ({sv.get('commitDate', '?')}) |",
        f"| Equicord repository | {sv.get('equicordRepository', '?')} |",
        f"| Equicord commit | `{sv.get('equicordCommit', '?')}` |",
        f"| File | `{sv.get('file', 'plugins.json')}` |",
        f"| SHA256 | `{sv.get('sha256', '?')}` |",
        f"| Imported | {sv.get('importedAt', '?')} |",
        "",
        "## Inventory facts (computed)",
        "",
        f"- Plugins: {st['total']} ({st['unique_names']} unique names)",
        "- By path: " + ", ".join(f"{n} in `{k}`" for k, n in st["by_root"].items()),
        f"- `hasPatches`: {st['has_patches']}",
        f"- `hasCommands`: {st['has_commands']}",
        f"- `required`: {st['required']}; `enabledByDefault`: {st['enabled_by_default']}; both: {st['required_and_enabled_by_default']}",
        f"- With dependencies: {st['with_dependencies']}",
        "- Targets: " + ", ".join(f"{k}: {v}" for k, v in st["targets"].items()),
        "- Tags: " + ", ".join(f"{k} ({v})" for k, v in st["tags"].items()),
    ]
    return "\n".join(lines) + "\n"


def write_docs(records: list[PluginRecord], ctx: dict) -> list[Path]:
    DOCS_DIR.mkdir(exist_ok=True)
    out = []
    for path, text in ((MATRIX_PATH, render_matrix(records, ctx)), (SOURCE_DOC_PATH, render_source_doc(ctx))):
        path.write_text(text, encoding="utf-8", newline="\n")
        out.append(path)
    return out


def docs_up_to_date(records: list[PluginRecord], ctx: dict) -> list[Path]:
    stale = []
    for path, text in ((MATRIX_PATH, render_matrix(records, ctx)), (SOURCE_DOC_PATH, render_source_doc(ctx))):
        if not path.is_file() or path.read_text(encoding="utf-8") != text:
            stale.append(path)
    return stale
