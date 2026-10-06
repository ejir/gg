#!/usr/bin/env python3
"""Validate the gg module registry without executing third-party module code.

The Lua runtime is used only with loadfile() to compile each package; it never
calls the returned chunk. The checks are intentionally conservative. They are
not a proof that arbitrary Lua is harmless; CODEOWNERS review is still required.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INDEX = ROOT / "modules" / "index.json"
HEADER = ROOT / "src" / "gg.h"
MAX_MODULE_BYTES = 512 * 1024
ALLOWED_PERMISSIONS = {"process", "network", "filesystem-write", "environment"}

FORBIDDEN = [
    (re.compile(r"\bloadstring\s*\("), "dynamic Lua loader loadstring()"),
    (re.compile(r"\bload\s*\("), "dynamic Lua loader load()"),
    (re.compile(r"\bstring\s*\.\s*dump\s*\("), "bytecode serialization"),
    (re.compile(r"\bdebug\s*\."), "debug library access"),
    (re.compile(r"\bpackage\s*\.\s*loadlib\s*\("), "native library loading"),
    (re.compile(r"(?:curl|wget)[^\n|]{0,240}\|\s*(?:sh|bash)\b", re.I),
     "download piped directly to a shell"),
    (re.compile(r"\bpowershell\b[^\n]{0,120}\s-enc(?:odedcommand)?\b", re.I),
     "encoded PowerShell command"),
]

SENSITIVE_FILES = re.compile(
    r"(?:\.ssh/(?:id_rsa|id_ed25519|authorized_keys)|"
    r"\.aws/credentials|\.config/gcloud/|Login Data|Cookies|"
    r"Local State|\bwallet\.dat\b)",
    re.I,
)
SECRET_PATTERNS = [
    re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
    re.compile(r"\bgh[pousr]_[A-Za-z0-9_]{30,}\b"),
    re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{20,}\b"),
]

PROCESS_APIS = re.compile(
    r"\b(?:os\s*\.\s*execute|io\s*\.\s*popen|"
    r"(?:gg|ctx)\s*\.\s*(?:exec|spawn|run|capture|sh))\s*\("
)
NETWORK_APIS = re.compile(
    r"\b(?:gg|ctx)\s*\.\s*download\s*\(|"
    r"\b(?:socket|http|https)\s*\.\s*[A-Za-z_]"
)
WRITE_APIS = re.compile(
    r"\b(?:gg|ctx)\s*\.\s*(?:write|rm|copy|move|mkdir|chmod_x)\s*\(|"
    r"\bio\s*\.\s*open\s*\([^\n]*,[^\n]*['\"](?:w|a)"
)
ENV_APIS = re.compile(r"\b(?:os|gg)\s*\.\s*getenv\s*\(")


def fail(message: str) -> None:
    print(f"module-security: ERROR: {message}", file=sys.stderr)


def read_pinned_index_hash() -> str:
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(r'^#define GG_MODULES_INDEX_SHA256 "([0-9a-f]{64})"$', text, re.M)
    if not match:
        raise ValueError("src/gg.h must define a 64-character GG_MODULES_INDEX_SHA256")
    return match.group(1)


def package_permissions(item: dict) -> set[str]:
    values = item.get("permissions")
    if not isinstance(values, list) or any(not isinstance(v, str) for v in values):
        raise ValueError(f"{item.get('name', '<unnamed>')}: permissions must be an array of strings")
    result = set(values)
    unknown = result - ALLOWED_PERMISSIONS
    if unknown:
        raise ValueError(f"{item.get('name')}: unknown permissions: {', '.join(sorted(unknown))}")
    if len(result) != len(values):
        raise ValueError(f"{item.get('name')}: duplicate permission")
    return result


def scan_source(item: dict, path: Path, text: str) -> None:
    name = item["name"]
    if len(text.encode("utf-8")) > MAX_MODULE_BYTES:
        raise ValueError(f"{name}: exceeds the {MAX_MODULE_BYTES}-byte package limit")
    if any(len(line.encode("utf-8")) > 4096 for line in text.splitlines()):
        raise ValueError(f"{name}: a line exceeds 4096 bytes (minified/obfuscated payloads are not accepted)")

    for pattern, description in FORBIDDEN:
        if pattern.search(text):
            raise ValueError(f"{name}: forbidden construct detected: {description}")
    if SENSITIVE_FILES.search(text):
        raise ValueError(f"{name}: references a credential/browser-wallet file path")
    for pattern in SECRET_PATTERNS:
        if pattern.search(text):
            raise ValueError(f"{name}: possible hard-coded secret detected")

    required: set[str] = set()
    if PROCESS_APIS.search(text):
        required.add("process")
    if NETWORK_APIS.search(text):
        required.add("network")
    if WRITE_APIS.search(text):
        required.add("filesystem-write")
    if ENV_APIS.search(text):
        required.add("environment")
    declared = package_permissions(item)
    missing = required - declared
    if missing:
        raise ValueError(f"{name}: API use requires declared permission(s): {', '.join(sorted(missing))}")
    extra = declared - required
    if extra:
        raise ValueError(f"{name}: unused permission(s) violate least-privilege policy: {', '.join(sorted(extra))}")


def lua_compile(binary: Path, source_path: Path) -> None:
    escaped = str(source_path).replace("\\", "\\\\").replace("'", "\\'")
    code = f"local f,e=loadfile('{escaped}'); if not f then io.stderr:write(e or 'Lua syntax error'); os.exit(1) end"
    result = subprocess.run([str(binary), "-e", code], cwd=ROOT,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()
        raise ValueError(f"{source_path.relative_to(ROOT)}: Lua syntax check failed: {detail}")


def check_immutable_versions(base_ref: str, catalog: dict, repo_root: Path = ROOT) -> None:
    exists = subprocess.run(["git", "cat-file", "-e", f"{base_ref}:modules/index.json"],
                            cwd=repo_root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if exists.returncode:
        return  # first registry introduction
    old_raw = subprocess.check_output(["git", "show", f"{base_ref}:modules/index.json"], cwd=repo_root)
    old = json.loads(old_raw.decode("utf-8"))
    old_packages = old.get("packages", [])
    new_packages = catalog.get("packages", [])
    old_by_key = {(p.get("name"), p.get("version")): p for p in old_packages}
    new_by_key = {(p.get("name"), p.get("version")): p for p in new_packages}
    removed = sorted(set(old_by_key) - set(new_by_key))
    if removed:
        keys = ", ".join(f"{name}@{version}" for name, version in removed)
        raise ValueError(f"published package versions cannot be removed: {keys}; revoke via a reviewed policy change")
    for key, old_item in old_by_key.items():
        if old_item != new_by_key[key]:
            raise ValueError(f"published package metadata is immutable: {key[0]}@{key[1]}; publish a new version")
    for key, item in new_by_key.items():
        if key in old_by_key:
            continue
        path = item.get("file", "")
        if path and subprocess.run(["git", "cat-file", "-e", f"{base_ref}:{path}"],
                                   cwd=repo_root, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL).returncode == 0:
            raise ValueError(f"new version path already existed in base: {path}; use a fresh immutable path")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gg", type=Path, help="gg host binary used only for Lua syntax compilation")
    parser.add_argument("--base-ref", help="reject edits/removals of already-published package versions")
    args = parser.parse_args()
    errors = 0
    try:
        raw = INDEX.read_bytes()
        pinned = read_pinned_index_hash()
        actual = hashlib.sha256(raw).hexdigest()
        if actual != pinned:
            raise ValueError(
                f"registry index hash mismatch: modules/index.json={actual}, "
                f"GG_MODULES_INDEX_SHA256={pinned}; update both in the same PR"
            )
        catalog = json.loads(raw.decode("utf-8"))
        if not isinstance(catalog, dict) or catalog.get("schema") != 1:
            raise ValueError("modules/index.json must use schema 1")
        packages = catalog.get("packages")
        if not isinstance(packages, list) or len(packages) > 200:
            raise ValueError("modules/index.json packages must be an array with at most 200 entries")
        if args.base_ref:
            check_immutable_versions(args.base_ref, catalog)

        seen = set()
        package_paths = set()
        for item in packages:
            if not isinstance(item, dict):
                raise ValueError("every package entry must be an object")
            name = item.get("name")
            version = item.get("version")
            description = item.get("description")
            expected_path = f"modules/{name}/{version}.lua"
            if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", name):
                raise ValueError(f"invalid package name: {name!r}")
            if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
                raise ValueError(f"{name}: versions must be stable numeric semver (x.y.z)")
            if not isinstance(description, str) or not description.strip() or len(description) > 240:
                raise ValueError(f"{name}: description must be 1..240 characters")
            if item.get("file") != expected_path:
                raise ValueError(f"{name}@{version}: file must be exactly {expected_path}")
            digest = item.get("sha256")
            if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
                raise ValueError(f"{name}@{version}: sha256 must be lowercase hex")
            if not isinstance(item.get("license"), str) or not item["license"].strip():
                raise ValueError(f"{name}@{version}: license is required")
            key = (name, version)
            if key in seen:
                raise ValueError(f"duplicate package {name}@{version}")
            seen.add(key)

            path = ROOT / expected_path
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"{expected_path}: missing or symlinked package file")
            resolved = path.resolve()
            if ROOT not in resolved.parents:
                raise ValueError(f"{expected_path}: package path escapes repository")
            package_paths.add(path.relative_to(ROOT).as_posix())
            source = path.read_bytes()
            source_hash = hashlib.sha256(source).hexdigest()
            if source_hash != digest:
                raise ValueError(f"{expected_path}: SHA-256 mismatch (index says {digest}, file is {source_hash})")
            text = source.decode("utf-8")
            scan_source(item, path, text)
            if args.gg:
                lua_compile(args.gg.resolve(), path.resolve())
            print(f"module-security: OK {name}@{version} sha256={digest}")

        modules_root = ROOT / "modules"
        allowed_files = package_paths | {"modules/index.json", "modules/README.md"}
        for path in modules_root.rglob("*"):
            if path.is_symlink():
                raise ValueError(f"symlinks are not allowed in modules/: {path.relative_to(ROOT)}")
            if path.is_file():
                rel = path.relative_to(ROOT).as_posix()
                if rel not in allowed_files:
                    raise ValueError(f"unreviewed/non-Lua file in modules/: {rel}")
        actual_files = {
            p.relative_to(ROOT).as_posix()
            for p in modules_root.rglob("*.lua")
            if p.is_file()
        }
        unlisted = sorted(actual_files - package_paths)
        if unlisted:
            raise ValueError("unlisted Lua files in modules/: " + ", ".join(unlisted))
        print(f"module-security: {len(packages)} package(s) validated; catalog pinned to {actual}")
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        fail(str(exc))
        errors += 1
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
