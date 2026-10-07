"""Tests for the inventory tooling. Run: python -m unittest discover -s tools/tests"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
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

    def test_hand_set_derived_status_is_rejected(self):
        # evaluate() flags this; derive_status ignores it
        status, _ = self.derive(self.rec(override={"status": "Complete"}))
        self.assertEqual(status, inv.NOT_STARTED)


class RepositoryIntegrity(unittest.TestCase):
    """The real repository must pass the checker without integrity errors."""

    def test_repository_has_no_integrity_errors(self):
        _records, errors, _warnings, ctx = inv.evaluate()
        self.assertEqual([str(e) for e in errors], [])
        self.assertEqual(ctx["stats"]["total"], len(json.loads(inv.INVENTORY_PATH.read_text(encoding="utf-8"))))


if __name__ == "__main__":
    unittest.main()
