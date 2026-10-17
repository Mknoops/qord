"""Tests for the inventory tooling. Run: python -m unittest discover -s tools/tests"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import new_plugin  # noqa: E402
import qord_inventory as inv  # noqa: E402
import update_equicord_inventory as upd  # noqa: E402


def entry(name, path=None, description="does a thing", **extra):
    e = {
        "name": name, "description": description, "filePath": path or f"src/plugins/{name[0].lower()}{name[1:]}",
        "dirName": name, "tags": ["Utility"], "enabledByDefault": False, "required": False,
        "hasPatches": False, "hasCommands": False, "isModified": False, "searchTerms": [],
        "authors": [{"name": "a", "id": "1"}],
    }
    e.update(extra)
    return e


class InventoryValidation(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(inv.validate_inventory([entry("A"), entry("B")]), [])

    def test_duplicates_and_missing_fields(self):
        bad = entry("A")
        del bad["authors"]
        issues = [str(i) for i in inv.validate_inventory([entry("A"), bad, entry("C", unknownField=1)])]
        self.assertTrue(any("duplicate" in i for i in issues))
        self.assertTrue(any("lacks" in i for i in issues))
        self.assertTrue(any("unknown fields" in i for i in issues))

    def test_not_a_list(self):
        self.assertTrue(inv.validate_inventory({"a": 1}))

    def test_stats_are_computed(self):
        entries = [entry("A", hasPatches=True), entry("B", path="src/plugins/_core/b.tsx", required=True),
                   entry("C", path="src/equicordplugins/c", target="desktop")]
        st = inv.inventory_stats(entries)
        self.assertEqual(st["total"], 3)
        self.assertEqual(st["has_patches"], 1)
        self.assertEqual(st["by_root"], {"src/equicordplugins": 1, "src/plugins": 1, "src/plugins/_core": 1})
        self.assertEqual(st["targets"]["desktop"], 1)


class InventoryDiff(unittest.TestCase):
    def test_added_removed_renamed_changed(self):
        old = [entry("Keep"), entry("Gone", path="src/plugins/gone", description="unique old text"),
               entry("OldName", path="src/plugins/moved"), entry("Changes", description="before")]
        new = [entry("Keep"), entry("NewName", path="src/plugins/moved"),
               entry("Changes", description="after", dependencies=["X"]),
               entry("Fresh", path="src/equicordplugins/fresh", description="totally different")]
        d = upd.diff_inventories(old, new)
        self.assertEqual(d["added"], ["Fresh"])
        self.assertEqual(d["removed"], ["Gone"])
        self.assertEqual([(a, b) for a, b, _ in d["renamed"]], [("OldName", "NewName")])
        fields = {(n, f) for n, f, _, _ in d["changed"]}
        self.assertIn(("Changes", "description"), fields)
        self.assertIn(("Changes", "dependencies"), fields)

    def test_similar_rename_detected(self):
        old = [entry("FooBar", path="src/plugins/fooBar", description="Shows foo next to bar")]
        new = [entry("FooBarPlus", path="src/plugins/fooBarPlus", description="Shows foo next to bar")]
        d = upd.diff_inventories(old, new)
        self.assertEqual(len(d["renamed"]), 1)

    def test_render_mentions_everything(self):
        d = upd.diff_inventories([entry("A")], [entry("B", path="src/x/y", description="zzz")])
        text = upd.render_diff(d, {"commit": "old"}, {"commit": "new", "equicordCommit": "e"})
        self.assertIn("New plugins (1)", text)
        self.assertIn("Removed plugins (1)", text)


class TestResults(unittest.TestCase):
    def test_junit_parsing(self):
        xml = """<?xml version="1.0"?><testsuite>
            <testcase name="plugin_a" status="run"/>
            <testcase name="plugin_b" status="fail"><failure message="x"/></testcase>
            <testcase name="plugin_c" status="notrun"><skipped/></testcase>
        </testsuite>"""
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "r.xml"
            p.write_text(xml)
            r = inv.load_test_results(p)
        self.assertEqual(r, {"plugin_a": True, "plugin_b": False, "plugin_c": False})


class StatusDerivation(unittest.TestCase):
    def rec(self, **kw):
        r = inv.PluginRecord(name="P", entry=entry("P"))
        for k, v in kw.items():
            setattr(r, k, v)
        return r

    def manifest(self, **kw):
        m = {"id": "p", "hooks": ["onBeforeSend"]}
        m.update(kw)
        return m

    def derive(self, r, have_results=False):
        errors, warnings = [], []
        inv.derive_status(r, errors, warnings, have_results)
        return r.status, errors

    def test_not_started_and_overrides(self):
        self.assertEqual(self.derive(self.rec())[0], inv.NOT_STARTED)
        self.assertEqual(self.derive(self.rec(override={"status": "Analyzing"}))[0], inv.ANALYZING)

    def test_blocked_needs_no_manifest(self):
        status, errors = self.derive(self.rec(override={"status": "Blocked", "reason": "x" * 30}))
        self.assertEqual(status, inv.BLOCKED)
        self.assertEqual(errors, [])
        status, errors = self.derive(self.rec(override={"status": "Blocked", "reason": "x" * 30},
                                              manifest=self.manifest(), has_source=True))
        self.assertTrue(errors)

    def test_manifest_without_source_is_not_implemented(self):
        self.assertEqual(self.derive(self.rec(manifest=self.manifest(), has_source=False))[0], inv.NOT_STARTED)

    def test_progression(self):
        base = dict(manifest=self.manifest(), has_source=True)
        self.assertEqual(self.derive(self.rec(**base))[0], inv.IMPLEMENTED)
        tested = dict(base, test_path=Path("t.cpp"), test_registered=True)
        self.assertEqual(self.derive(self.rec(**tested))[0], inv.TESTED)
        # Results supplied: a failing or missing run is not Tested
        self.assertEqual(self.derive(self.rec(**tested, test_passed=False), True)[0], inv.IMPLEMENTED)
        self.assertEqual(self.derive(self.rec(**tested, test_passed=None), True)[0], inv.IMPLEMENTED)
        self.assertEqual(self.derive(self.rec(**tested, test_passed=True), True)[0], inv.TESTED)
        # Complete needs wiring + a recorded verification + no gaps
        wired = dict(tested, hooks_wired=True)
        self.assertEqual(self.derive(self.rec(**wired))[0], inv.TESTED)
        verified = dict(wired, override={"verifiedInClient": {"date": "2026-10-07"}})
        self.assertEqual(self.derive(self.rec(**verified))[0], inv.COMPLETE)
        gapped = dict(verified, manifest=self.manifest(gaps=["render path"]))
        self.assertEqual(self.derive(self.rec(**gapped))[0], inv.TESTED)
        # Upstream behavior for a Discord feature Qord lacks entirely doesn't hold it back
        absent = dict(verified, manifest=self.manifest(notApplicable=["Qord has no soundboard to unlock"]))
        r = self.rec(**absent)
        self.assertEqual(self.derive(r)[0], inv.COMPLETE)
        self.assertIn("not applicable: Qord has no soundboard", r.notes)

    def test_scaffold_test_is_not_tested(self):
        with tempfile.TemporaryDirectory() as d:
            test = Path(d) / "tst_p.cpp"
            test.write_text("// " + inv.SCAFFOLD_MARKER + "\n", encoding="utf-8")
            r = self.rec(manifest=self.manifest(), has_source=True, test_path=test, test_registered=True)
            self.assertEqual(self.derive(r)[0], inv.IMPLEMENTED)
            self.assertIn("template", r.notes)
            test.write_text("// real tests\n", encoding="utf-8")
            r = self.rec(manifest=self.manifest(), has_source=True, test_path=test, test_registered=True)
            self.assertEqual(self.derive(r)[0], inv.TESTED)

    def test_hand_set_derived_status_is_rejected(self):
        # evaluate() flags this; derive_status ignores it
        status, _ = self.derive(self.rec(override={"status": "Complete"}))
        self.assertEqual(status, inv.NOT_STARTED)


class GeneratedReadme(unittest.TestCase):
    def records(self):
        def rec(name, status, **manifest):
            r = inv.PluginRecord(name=name, entry=entry(name))
            r.status = status
            if manifest:
                r.manifest = dict({"name": name, "permissions": []}, **manifest)
            return r
        return [rec("Zeta", inv.TESTED, summary="does z", permissions=["network"]),
                rec("Alpha", inv.COMPLETE, description="upstream text"),
                rec("Half", inv.IMPLEMENTED, summary="not yet"),
                rec("Todo", inv.NOT_STARTED), rec("Easy", inv.NOT_STARTED)]

    def ctx(self):
        return {"stats": {"total": 5}, "roadmap": [
            {"name": "Zeta", "effort": "small", "needs": "done already"},
            {"name": "Todo", "effort": "large", "needs": "a hook"},
            {"name": "Easy", "effort": "small", "goodFirst": True, "needs": "nothing"}]}

    def test_blocks_are_filled(self):
        text = "intro\n<!-- BEGIN GENERATED: plugin-table -->\nold\n<!-- END GENERATED: plugin-table -->\n" \
               "<!-- BEGIN GENERATED: up-next -->\n<!-- END GENERATED: up-next -->\n" \
               "<!-- BEGIN GENERATED: plugin-status -->\n<!-- END GENERATED: plugin-status -->\nend\n"
        out = inv.render_readme(self.records(), self.ctx(), text)
        self.assertNotIn("old", out)
        self.assertIn("These 2 native plugins", out)
        # Sorted by name; the summary wins over the upstream description
        self.assertLess(out.index("**Alpha** | upstream text | none"), out.index("**Zeta** | does z | network"))
        self.assertNotIn("Half", out)
        self.assertIn("**Up next:** Todo, Easy.", out)
        self.assertIn("**Good first plugins**", out)
        self.assertIn("2 of 5 ported and tested", out)
        # Idempotent
        self.assertEqual(inv.render_readme(self.records(), self.ctx(), out), out)

    def test_missing_block_is_an_error(self):
        with self.assertRaises(ValueError):
            inv.render_readme(self.records(), self.ctx(), "no markers")

    def test_roadmap_doc_skips_done_plugins(self):
        doc = inv.render_roadmap_doc(self.records(), self.ctx())
        self.assertIn("| 1 | Todo | large |  | a hook |", doc)
        self.assertIn("| 2 | Easy | small | yes | nothing |", doc)
        self.assertNotIn("Zeta", doc)

    def test_roadmap_validation(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "roadmap.json"
            path.write_text(json.dumps({"plugins": [
                {"name": "A", "effort": "small", "needs": "x"}, {"name": "A", "effort": "small", "needs": "x"},
                {"name": "Nope", "effort": "huge"}]}), encoding="utf-8")
            old = inv.ROADMAP_PATH
            inv.ROADMAP_PATH = path
            try:
                _, issues = inv.load_roadmap({"A": entry("A")})
            finally:
                inv.ROADMAP_PATH = old
        text = " ".join(str(i) for i in issues)
        for expected in ("listed twice", "not in the pinned inventory", "effort must be", "say what it needs"):
            self.assertIn(expected, text)


class NewPluginScaffold(unittest.TestCase):
    def test_names(self):
        self.assertEqual(new_plugin.plugin_id("Translate+"), "translate")
        self.assertEqual(new_plugin.plugin_id("USRBG"), "usrbg")
        self.assertEqual(new_plugin.class_name("betterRoleDot"), "BetterRoleDot")

    def test_hooks_and_permissions_come_from_the_source(self):
        needed = new_plugin.hook_permissions()
        self.assertEqual(needed["messageFilter"], "hide_messages")
        self.assertEqual(new_plugin.hook_enum("onBeforeSend"), "BeforeSend")
        self.assertEqual(set(needed), set(inv.HOOK_DISPATCH))

    def test_inserts_after_multiline_calls(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "CMakeLists.txt"
            path.write_text("qord_add_plugin(a x SOURCES A.cpp)\nqord_add_plugin(b x SOURCES B.cpp\n"
                            "    UI_SOURCES W.cpp)\n\n# tail\n", encoding="utf-8")
            new_plugin.insert_line(path, "qord_add_plugin(", "qord_add_plugin(c x SOURCES C.cpp)")
            self.assertEqual(path.read_text(encoding="utf-8").splitlines()[3], "qord_add_plugin(c x SOURCES C.cpp)")

    def test_every_hook_is_documented(self):
        self.assertEqual(set(inv.HOOK_DOCS), set(inv.HOOK_DISPATCH))
        self.assertTrue(all(inv.HOOK_DOCS.values()))

    def test_templates_carry_the_marker(self):
        self.assertIn(inv.SCAFFOLD_MARKER, new_plugin.render_test("Foo", "foo"))
        cpp = new_plugin.render_cpp("Foo", "Foo", "foo", "src/plugins/foo", ["a"], ["messageFilter"])
        self.assertIn("Hook::MessageFilter", cpp)
        self.assertIn("qord_register_foo", cpp)


class RepositoryIntegrity(unittest.TestCase):
    """The real repository must pass the checker without integrity errors."""

    def test_repository_has_no_integrity_errors(self):
        _records, errors, _warnings, ctx = inv.evaluate()
        self.assertEqual([str(e) for e in errors], [])
        self.assertEqual(ctx["stats"]["total"], len(json.loads(inv.INVENTORY_PATH.read_text(encoding="utf-8"))))

    def test_generated_docs_are_current(self):
        records, _errors, _warnings, ctx = inv.evaluate()
        stale = [p.relative_to(inv.ROOT).as_posix() for p in inv.docs_up_to_date(records, ctx)]
        self.assertEqual(stale, [], "run python3 tools/check_equicord_plugins.py --write-docs")


if __name__ == "__main__":
    unittest.main()
