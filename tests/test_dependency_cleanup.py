"""Check dependency cleanup against a fake pacman database and dependency graph."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
RUNTIME = ["qt6-base", "qt6-webengine", "qt6-webchannel", "qt6-positioning", "qt6-svg",
           "qt6-wayland", "libx11", "libxcb", "tar", "gzip", "xdg-utils"]
BUILD = ["git", "cmake", "ninja", "base-devel", "qt6-tools"]
DEPENDENCIES = {
    "qt6-base": ["old-shared"], "qt6-webengine": ["qt6-base", "runtime-shared"],
    "qt6-webchannel": ["qt6-base"], "qt6-positioning": ["qt6-base"],
    "qt6-svg": ["qt6-base"], "qt6-wayland": ["runtime-shared"],
    "libx11": ["old-shared"], "libxcb": ["runtime-shared"],
    "tar": [], "gzip": [], "xdg-utils": [], "git": ["old-shared"],
    "cmake": ["build-support", "runtime-shared"], "ninja": [],
    "base-devel": ["compiler", "tar", "gzip"], "qt6-tools": ["qt6-base", "translation-support"],
    "build-support": ["old-orphan", "build-chain"], "compiler": ["compiler-support"],
    "translation-support": [], "build-chain": [], "compiler-support": [],
    "runtime-shared": [], "old-shared": [], "old-orphan": [],
}


@unittest.skipUnless(shutil.which("bash"), "Bash is required")
class DependencyCleanupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="whatly cleanup '")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.state_file = self.root / "state.json"
        self.initial = {
            "old-explicit": {"reason": "explicit", "depends": ["old-shared"]},
            "old-shared": {"reason": "dependency", "depends": []},
            "old-orphan": {"reason": "dependency", "depends": []},
            "old-alone": {"reason": "dependency", "depends": []},
            "git": {"reason": "explicit", "depends": ["old-shared"]},
            "qt6-base": {"reason": "dependency", "depends": ["old-shared"]},
        }
        self.state_file.write_text(json.dumps({"packages": self.initial, "queries": 0}))
        (self.root / "repository.json").write_text(json.dumps(DEPENDENCIES))
        self.env = dict(os.environ, PATH=str(self.bin) + ":" + os.environ["PATH"],
                        TEST_ROOT=str(self.root), TEST_PYTHON=sys.executable)
        for name in ("BASH_ENV", "ENV", "TEST_NON_INTERACTIVE", "TEST_SYNC_FAIL", "TEST_REASON_FAIL",
                     "TEST_REMOVE_FAIL", "TEST_ORPHAN_FAIL", "TEST_ORPHAN_EMPTY", "TEST_QUERY_FAIL",
                     "TEST_OMIT_PACKAGE", "TEST_AFTER_SNAPSHOT", "TEST_BUILD_FAIL"):
            self.env.pop(name, None)

        # The only pacman and sudo executables reachable by the helper are these
        # fixtures. Their transactions mutate JSON, never the system package DB.
        backend = f"#!{sys.executable}\n" + '''
import json, os, pathlib, sys
root = pathlib.Path(os.environ["TEST_ROOT"])
tool = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with (root / "calls.jsonl").open("a") as output:
    output.write(json.dumps([tool, *args]) + "\\n")
if tool == "sudo":
    assert args and args[0] == "pacman"
    os.execv(str(root / "bin/pacman"), ["pacman", *args[1:]])
state_file = root / "state.json"
state = json.loads(state_file.read_text())
packages = state["packages"]
repository = json.loads((root / "repository.json").read_text())
def save():
    state_file.write_text(json.dumps(state))
if args == ["-Qq"]:
    state["queries"] += 1
    save()
    if str(state["queries"]) == os.environ.get("TEST_QUERY_FAIL"):
        sys.exit(41)
    print("\\n".join(sorted(packages)))
elif args and args[0] == "-Syu":
    if os.environ.get("TEST_SYNC_FAIL") == "1":
        sys.exit(42)
    targets = args[args.index("--") + 1:]
    assert "--needed" in args
    # Simulate pacman's dangerous global reason flag so tests catch regressions
    # that would turn preexisting upgraded explicit packages into dependencies.
    if "--asdeps" in args:
        for entry in packages.values():
            entry["reason"] = "dependency"
    def install(name):
        if name in packages or name == os.environ.get("TEST_OMIT_PACKAGE"):
            return
        for dependency in repository[name]:
            install(dependency)
        packages[name] = {"reason": "dependency", "depends": repository[name]}
    for target in targets:
        install(target)
        if target in packages:
            packages[target]["reason"] = "dependency" if "--asdeps" in args else "explicit"
    save()
elif args[:2] == ["-D", "--asdeps"]:
    if os.environ.get("TEST_REASON_FAIL") == "1":
        sys.exit(43)
    for package in args[args.index("--") + 1:]:
        assert package in packages
        packages[package]["reason"] = "dependency"
    save()
elif args == ["-Qdtq"]:
    if os.environ.get("TEST_ORPHAN_FAIL") == "1":
        sys.exit(44)
    if os.environ.get("TEST_ORPHAN_EMPTY") == "1":
        sys.exit(1)
    required = {dependency for entry in packages.values() for dependency in entry["depends"]}
    orphans = sorted(name for name, entry in packages.items()
                     if entry["reason"] == "dependency" and name not in required)
    if not orphans:
        sys.exit(1)
    print("\\n".join(orphans))
elif args and args[0] == "-R":
    if os.environ.get("TEST_REMOVE_FAIL") == "1":
        sys.exit(45)
    assert not any(flag.startswith("-R") and flag != "-R" for flag in args)
    targets = args[args.index("--") + 1:]
    required = {dependency for name, entry in packages.items() if name not in targets
                for dependency in entry["depends"]}
    assert not any(package in required for package in targets)
    for package in targets:
        del packages[package]
    save()
else:
    sys.exit("Unexpected fake pacman invocation: " + repr(args))
'''
        for name in ("sudo", "pacman"):
            path = self.bin / name
            path.write_text(backend)
            path.chmod(0o755)

        (self.root / "after-snapshot.py").write_text('''
import json, os, pathlib
path = pathlib.Path(os.environ["TEST_ROOT"]) / "state.json"
state = json.loads(path.read_text())
mode = os.environ.get("TEST_AFTER_SNAPSHOT")
if mode == "unrelated":
    state["packages"]["added-later"] = {"reason": "dependency", "depends": []}
elif mode == "shared":
    state["packages"]["other-app"] = {"reason": "explicit", "depends": ["cmake"]}
elif mode == "runtime-reason":
    state["packages"]["xdg-utils"]["reason"] = "dependency"
path.write_text(json.dumps(state))
''')

    def run_helper(self, mode="install-and-clean"):
        script = '''
set -euo pipefail
log() { printf '[%s] %s\\n' "$1" "${*:2}"; }
run() { "$@"; }
fail() { log ERROR "$*" >&2; exit 1; }
source() {
    if [[ "$1" == /etc/os-release ]]; then
        ID=arch
        ID_LIKE=
    else
        builtin source "$@"
    fi
}
source "$1"
non_interactive=${TEST_NON_INTERACTIVE:-true}
if [[ "$2" != cleanup-only ]]; then
    whatly_install_dependencies
    if [[ "${TEST_BUILD_FAIL:-}" == 1 ]]; then
        exit 37
    fi
    printf 'APP_INSTALLED\\n'
    "$TEST_PYTHON" "$TEST_ROOT/after-snapshot.py"
fi
whatly_cleanup_build_dependencies
printf 'SUCCESS\\n'
'''
        return subprocess.run(["bash", "-c", script, "dependency-test",
                               str(REPO / "scripts/dependencies.sh"), mode], env=self.env,
                              cwd=self.root, text=True, capture_output=True, timeout=10)

    def packages(self):
        return json.loads(self.state_file.read_text())["packages"]

    def calls(self, operation=None):
        path = self.root / "calls.jsonl"
        entries = [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []
        return [entry[1:] for entry in entries
                if entry[0] == "pacman" and (operation is None or entry[1] == operation)]

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("SUCCESS", result.stdout)

    def test_cleanup_removes_new_direct_and_transitive_build_dependencies_only(self):
        result = self.run_helper()
        self.assert_success(result)
        packages = self.packages()
        for name, original in self.initial.items():
            self.assertEqual(packages[name], original, name)
        for name in BUILD:
            if name not in self.initial:
                self.assertNotIn(name, packages)
        for name in ("build-support", "build-chain", "compiler", "compiler-support", "translation-support"):
            self.assertNotIn(name, packages)
        for name in RUNTIME:
            self.assertIn(name, packages)
            self.assertEqual(packages[name]["reason"], self.initial.get(name, {}).get("reason", "explicit"))
        self.assertIn("runtime-shared", packages)
        self.assertGreaterEqual(len(self.calls("-R")), 3)
        removed = {name for call in self.calls("-R") for name in call[call.index("--") + 1:]}
        self.assertFalse(removed.intersection(self.initial))
        self.assertFalse(removed.intersection(RUNTIME))

    def test_transaction_targets_only_missing_packages_and_preserves_upgrade_reasons(self):
        self.assert_success(self.run_helper())
        transaction, = self.calls("-Syu")
        self.assertEqual(transaction[:3], ["-Syu", "--needed", "--noconfirm"])
        self.assertEqual(set(transaction[transaction.index("--") + 1:]), set(RUNTIME + BUILD) - self.initial.keys())
        self.assertNotIn("--asdeps", transaction)
        reasons, = self.calls("-D")
        self.assertEqual(reasons[:3], ["-D", "--asdeps", "--"])
        self.assertEqual(set(reasons[3:]), set(BUILD) - self.initial.keys())
        self.assertEqual(self.packages()["old-explicit"]["reason"], "explicit")
        self.assertEqual(self.packages()["git"]["reason"], "explicit")

    def test_packages_added_after_transaction_snapshot_are_never_removed(self):
        self.env["TEST_AFTER_SNAPSHOT"] = "unrelated"
        self.assert_success(self.run_helper())
        self.assertIn("added-later", self.packages())
        self.assertFalse(any("added-later" in call for call in self.calls("-R")))

    def test_new_build_dependency_kept_when_another_app_now_uses_it(self):
        self.env["TEST_AFTER_SNAPSHOT"] = "shared"
        self.assert_success(self.run_helper())
        self.assertIn("cmake", self.packages())
        self.assertIn("build-support", self.packages())
        self.assertIn("other-app", self.packages())

    def test_runtime_roots_are_kept_even_if_reason_changes_during_build(self):
        self.env["TEST_AFTER_SNAPSHOT"] = "runtime-reason"
        self.assert_success(self.run_helper())
        self.assertIn("xdg-utils", self.packages())

    def test_prepared_dependencies_require_no_transaction_or_cleanup(self):
        packages = self.packages()
        for name, dependencies in DEPENDENCIES.items():
            packages.setdefault(name, {"reason": "explicit", "depends": dependencies})
        self.state_file.write_text(json.dumps({"packages": packages, "queries": 0}))
        self.assert_success(self.run_helper())
        self.assertEqual(self.packages(), packages)
        self.assertEqual(self.calls(), [["-Qq"]])

    def test_cleanup_without_dependency_install_is_noop(self):
        self.assert_success(self.run_helper("cleanup-only"))
        self.assertEqual(self.calls(), [])

    def test_interactive_and_noninteractive_flags_apply_to_install_and_remove(self):
        self.env["TEST_NON_INTERACTIVE"] = "false"
        self.assert_success(self.run_helper())
        self.assertTrue(self.calls("-R"))
        self.assertFalse(any("--noconfirm" in call for call in self.calls()))

    def test_install_or_reason_failure_stops_before_app_and_cleanup(self):
        for setting, code in (("TEST_SYNC_FAIL", 42), ("TEST_REASON_FAIL", 43)):
            with self.subTest(setting=setting):
                self.state_file.write_text(json.dumps({"packages": self.initial, "queries": 0}))
                self.env[setting] = "1"
                result = self.run_helper()
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                self.assertNotIn("APP_INSTALLED", result.stdout)
                self.assertEqual(self.calls("-R"), [])
                self.env.pop(setting)

    def test_snapshot_or_verification_failure_stops_before_cleanup(self):
        for setting, value in (("TEST_QUERY_FAIL", "1"), ("TEST_QUERY_FAIL", "2"),
                               ("TEST_OMIT_PACKAGE", "qt6-webengine")):
            with self.subTest(setting=setting, value=value):
                self.state_file.write_text(json.dumps({"packages": self.initial, "queries": 0}))
                self.env[setting] = value
                result = self.run_helper()
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn("APP_INSTALLED", result.stdout)
                self.assertEqual(self.calls("-R"), [])
                self.env.pop(setting)

    def test_cleanup_failures_warn_and_preserve_success(self):
        for setting in ("TEST_ORPHAN_FAIL", "TEST_REMOVE_FAIL"):
            with self.subTest(setting=setting):
                self.state_file.write_text(json.dumps({"packages": self.initial, "queries": 0}))
                (self.root / "calls.jsonl").unlink(missing_ok=True)
                self.env[setting] = "1"
                result = self.run_helper()
                self.assert_success(result)
                self.assertIn("[WARN]", result.stderr)
                self.assertIn("Whatly is installed", result.stderr)
                self.assertIn("cmake", self.packages())
                self.assertLessEqual(len(self.calls("-R")), 1)
                self.env.pop(setting)

    def test_empty_orphan_query_is_normal(self):
        self.env["TEST_ORPHAN_EMPTY"] = "1"
        result = self.run_helper()
        self.assert_success(result)
        self.assertEqual(self.calls("-R"), [])
        self.assertNotIn("cleanup was skipped", result.stderr)

    def test_failed_build_does_not_trigger_cleanup(self):
        self.env["TEST_BUILD_FAIL"] = "1"
        result = self.run_helper()
        self.assertEqual(result.returncode, 37, result.stdout + result.stderr)
        self.assertEqual(self.calls("-R"), [])
        self.assertIn("cmake", self.packages())


if __name__ == "__main__":
    unittest.main()
