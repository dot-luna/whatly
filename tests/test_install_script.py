"""Verify the native installer with fake tools and a temporary HOME and checkout."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
DEPENDENCIES = (
    "git", "cmake", "ninja", "base-devel", "qt6-base", "qt6-webengine", "qt6-webchannel",
    "qt6-positioning", "qt6-svg", "qt6-tools", "qt6-wayland", "libx11", "libxcb",
    "tar", "gzip", "xdg-utils",
)


@unittest.skipIf(os.geteuid() == 0, "Native installation requires a real non-root user")
class NativeInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="whatly install '")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.home = self.root / "home with spaces"
        self.home.mkdir()
        self.checkout = self.root / "checkout"
        (self.checkout / "scripts").mkdir(parents=True)
        (self.checkout / ".git").mkdir()
        (self.checkout / "CMakeLists.txt").write_text("# Fixture source\n")
        self.source_desktop = self.checkout / "dist/linux/net.shakaran.whatly.desktop"
        self.source_desktop.parent.mkdir(parents=True)
        self.source_desktop.write_text(
            "[Desktop Entry]\nType=Application\nName=WhatsApp\nExec=whatly %u\n"
            "Actions=Chat;Settings;\n\n[Desktop Action Chat]\nName=Chat\nExec=whatly -n\n"
            "\n[Desktop Action Settings]\nName=Settings\nExec=whatly -s\n")
        shutil.copy2(REPO / "scripts/install.sh", self.checkout / "scripts/install.sh")
        shutil.copy2(REPO / "scripts/dependencies.sh", self.checkout / "scripts/dependencies.sh")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.installed = self.root / "installed"
        self.installed.mkdir()
        for package in DEPENDENCIES:
            (self.installed / package).touch()

        # No production package manager, compiler, Git, or desktop command is reachable.
        for name in ("bash", "readlink", "dirname", "mkdir", "cat", "mktemp", "chmod", "mv", "rm"):
            (self.bin / name).symlink_to(shutil.which(name))
        self.env = dict(os.environ, HOME=str(self.home), PATH=str(self.bin),
                        TEST_INSTALL_ROOT=str(self.root), BASH_ENV=str(self.root / "bash-env"),
                        NO_COLOR="1", LC_ALL="C.UTF-8")
        for name in ("TEST_OS_ID", "TEST_FAIL_PHASE", "TEST_CACHE_STATUS",
                     "TEST_SKIP_EXECUTABLE", "TEST_SKIP_PACKAGE_INSTALL", "CMAKE_BUILD_PARALLEL_LEVEL"):
            self.env.pop(name, None)
        (self.root / "bash-env").write_text('''
source() {
    if [[ "$1" == /etc/os-release ]]; then
        ID=${TEST_OS_ID:-arch}
        ID_LIKE=
    else
        builtin source "$@"
    fi
}
''')
        backend = f"#!{sys.executable}\n" + '''
import json, os, pathlib, sys
root = pathlib.Path(os.environ["TEST_INSTALL_ROOT"])
tool = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with (root / "calls.jsonl").open("a") as output:
    output.write(json.dumps([tool, *args]) + "\\n")
if tool == "sudo":
    if not args or args[0] != "pacman":
        sys.exit("Unexpected privileged command")
    os.execv(str(root / "bin/pacman"), args)
if tool == "pacman":
    if args == ["-Qq"]:
        print("\\n".join(sorted(path.name for path in (root / "installed").iterdir())))
        sys.exit(0)
    if args == ["-Qdtq"]:
        sys.exit(1)
    if args[:2] in (["-D", "--asexplicit"], ["-D", "--asdeps"]):
        sys.exit(0)
    if args[:2] == ["-Q", "--"]:
        sys.exit(0 if all((root / "installed" / name).exists() for name in args[2:]) else 1)
    if args[:2] != ["-Syu", "--needed"] or "--" not in args:
        sys.exit("Unexpected pacman command")
    if os.environ.get("TEST_FAIL_PHASE") == "packages":
        sys.exit(17)
    if not os.environ.get("TEST_SKIP_PACKAGE_INSTALL"):
        for name in args[args.index("--") + 1:]:
            (root / "installed" / name).touch()
elif tool == "git":
    if args[2:] != ["submodule", "update", "--init", "--recursive"]:
        sys.exit("Unexpected Git command")
    if os.environ.get("TEST_FAIL_PHASE") == "submodules":
        sys.exit(18)
    print("\\x1b[31mGit prepared café\\x1b[0m")
    print("Child locale: " + os.environ.get("LC_ALL", ""))
elif tool == "cmake":
    phase = "configure" if args[0] == "-S" else args[0].removeprefix("--")
    if os.environ.get("TEST_FAIL_PHASE") == phase:
        sys.exit(19)
    if phase == "configure":
        build = pathlib.Path(args[args.index("-B") + 1])
        build.mkdir(exist_ok=True)
        prefix = next(arg.split("=", 1)[1] for arg in args if arg.startswith("-DCMAKE_INSTALL_PREFIX="))
        (build / "prefix").write_text(prefix)
    elif phase == "install":
        prefix = pathlib.Path((pathlib.Path(args[1]) / "prefix").read_text())
        (prefix / "bin").mkdir(parents=True, exist_ok=True)
        if not os.environ.get("TEST_SKIP_EXECUTABLE"):
            binary = prefix / "bin/whatly"
            binary.write_text("#!/bin/sh\\nexit 0\\n")
            binary.chmod(0o755)
        (prefix / "share/applications").mkdir(parents=True, exist_ok=True)
        (prefix / "share/applications/net.shakaran.whatly.desktop").write_text(
            (root / "checkout/dist/linux/net.shakaran.whatly.desktop").read_text())
        (prefix / "share/icons/hicolor").mkdir(parents=True, exist_ok=True)
    elif phase != "build":
        sys.exit("Unexpected CMake command")
elif tool in ("update-desktop-database", "gtk-update-icon-cache"):
    sys.exit(int(os.environ.get("TEST_CACHE_STATUS", "0")))
else:
    sys.exit("Unexpected tool invocation: " + tool)
'''
        for name in ("sudo", "pacman", "git", "cmake", "ninja", "update-desktop-database", "gtk-update-icon-cache"):
            path = self.bin / name
            path.write_text(backend)
            path.chmod(0o755)

    def install(self, *arguments):
        return subprocess.run([str(self.bin / "bash"), str(self.checkout / "scripts/install.sh"), *arguments],
                              env=self.env, cwd=self.root, input="", text=True,
                              capture_output=True, timeout=10)

    def calls(self, tool=None):
        path = self.root / "calls.jsonl"
        if not path.exists():
            return []
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        return [row for row in rows if tool is None or row[0] == tool]

    def test_existing_dependencies_skip_sudo_and_install_release_to_local_prefix(self):
        result = self.install("--non-interactive")
        self.assertEqual(result.returncode, 0, result.stderr)
        prefix = self.home / ".local"
        self.assertTrue((prefix / "bin/whatly").is_file())
        self.assertEqual(self.calls("sudo"), [])
        self.assertEqual(self.calls("git"), [["git", "-C", str(self.checkout), "submodule", "update", "--init", "--recursive"]])
        configure, build, install = self.calls("cmake")
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", configure)
        self.assertIn("-DCMAKE_INSTALL_PREFIX=" + str(prefix), configure)
        self.assertEqual(configure[configure.index("-G") + 1], "Ninja")
        self.assertEqual(build[-2:], ["--parallel", "2"])
        self.assertEqual(install[1], "--install")
        self.assertIn("  Git prepared café", result.stdout)
        self.assertIn("Child locale: C.UTF-8", result.stdout)
        self.assertNotIn("\x1b", result.stdout)
        self.assertIn(f'Exec="{prefix}/bin/whatly" %u',
                      (prefix / "share/applications/net.shakaran.whatly.desktop").read_text())

    def test_missing_packages_are_installed_once_without_confirmation(self):
        missing = ("qt6-svg", "qt6-tools")
        for package in missing:
            (self.installed / package).unlink()
        result = self.install("--non-interactive")
        self.assertEqual(result.returncode, 0, result.stderr)
        operations = [row for row in self.calls("pacman") if "-Syu" in row]
        self.assertEqual(len(operations), 1)
        self.assertIn("--noconfirm", operations[0])
        self.assertEqual(operations[0][operations[0].index("--") + 1:], list(missing))
        self.assertEqual(self.calls("sudo")[0], ["sudo", *operations[0]])
        self.assertTrue(all(call[1] == "pacman" for call in self.calls("sudo")))
        calls = self.calls()
        cleanup = calls.index(["pacman", "-Qdtq"])
        install = next(index for index, call in enumerate(calls) if call[:2] == ["cmake", "--install"])
        self.assertGreater(cleanup, install)

    def test_skip_deps_supports_prepared_non_arch_system_and_custom_prefix(self):
        self.env["TEST_OS_ID"] = "debian"
        self.env["CMAKE_BUILD_PARALLEL_LEVEL"] = "3"
        prefix = self.root / "custom prefix"
        result = self.install("--skip-deps", "--prefix", str(prefix))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((prefix / "bin/whatly").is_file())
        self.assertEqual(self.calls("pacman"), [])
        self.assertEqual(self.calls("sudo"), [])
        self.assertEqual(self.calls("cmake")[1][-2:], ["--parallel", "3"])

    def test_unsupported_system_stops_before_package_or_build_work(self):
        self.env["TEST_OS_ID"] = "debian"
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--skip-deps", result.stderr)
        self.assertEqual(self.calls(), [])

    def test_package_failure_prevents_submodule_and_build_changes(self):
        (self.installed / "git").unlink()
        self.env["TEST_FAIL_PHASE"] = "packages"
        result = self.install("--non-interactive")
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertEqual(self.calls("git"), [])
        self.assertEqual(self.calls("cmake"), [])

    def test_failed_build_stops_before_install_and_preserves_status(self):
        (self.installed / "qt6-tools").unlink()
        self.env["TEST_FAIL_PHASE"] = "build"
        result = self.install("--non-interactive")
        self.assertEqual(result.returncode, 19, result.stderr)
        self.assertEqual(len(self.calls("cmake")), 2)
        self.assertNotIn("[OK]", result.stdout)
        self.assertFalse((self.home / ".local/bin/whatly").exists())
        self.assertNotIn(["pacman", "-Qdtq"], self.calls())

    def test_successful_package_command_must_actually_supply_dependencies(self):
        (self.installed / "git").unlink()
        self.env["TEST_SKIP_PACKAGE_INSTALL"] = "1"
        result = self.install("--non-interactive")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Required dependency is still missing", result.stderr)
        self.assertEqual(self.calls("git"), [])

    def test_missing_installed_binary_does_not_report_success(self):
        self.env["TEST_SKIP_EXECUTABLE"] = "1"
        result = self.install("--skip-deps")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("[OK]", result.stdout)
        self.assertEqual(self.calls("update-desktop-database"), [])

    def test_desktop_cache_failures_only_warn(self):
        self.env["TEST_CACHE_STATUS"] = "1"
        result = self.install("--skip-deps")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("[WARN]", result.stderr)
        self.assertNotIn("[ERROR]", result.stderr)
        self.assertIn("[OK]", result.stdout)

    def test_installed_desktop_uses_absolute_paths_and_preserves_actions(self):
        prefix = self.root / 'custom "quoted" $cash %value \\path `tick`'
        original = self.source_desktop.read_text()
        result = self.install("--skip-deps", "--prefix", str(prefix))
        self.assertEqual(result.returncode, 0, result.stderr)
        desktop = prefix / "share/applications/net.shakaran.whatly.desktop"
        lines = [line for line in desktop.read_text().splitlines() if line.startswith("Exec=")]
        self.assertEqual(len(lines), 3)
        self.assertTrue(lines[0].startswith('Exec=/usr/bin/env "'))
        self.assertTrue(lines[0].endswith('" %u'))
        self.assertTrue(lines[1].endswith('" -n'))
        self.assertTrue(lines[2].endswith('" -s'))
        self.assertIn(r'\\"quoted\\"', lines[0])
        self.assertIn(r'\\$cash', lines[0])
        self.assertIn('%%value', lines[0])
        self.assertIn(r'\\\\path', lines[0])
        self.assertIn(r'\\`tick\\`', lines[0])
        self.assertEqual(self.source_desktop.read_text(), original)

        # Validate against a real desktop-entry parser when the host provides it.
        validator = shutil.which("desktop-file-validate")
        if validator:
            result = subprocess.run([validator, str(desktop)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        try:
            import gi
            gi.require_version("GioUnix", "2.0")
            from gi.repository import GioUnix
        except (ImportError, ValueError):
            pass
        else:
            # Parse only: never launch the installed fixture or touch the desktop.
            app = GioUnix.DesktopAppInfo.new_from_filename(str(desktop))
            self.assertIsNotNone(app)
            self.assertEqual(app.get_executable(), "/usr/bin/env")

    def test_colored_tool_output_is_muted_without_changing_child_locale(self):
        self.env["LUNA_COLOR"] = "1"
        self.env.pop("NO_COLOR", None)
        result = self.install("--skip-deps")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("\x1b[90m  Git prepared café\x1b[0m", result.stdout)
        self.assertIn("Child locale: C.UTF-8", result.stdout)
        self.assertNotIn("\x1b[31mGit", result.stdout)

        self.env["NO_COLOR"] = "1"
        result = self.install("--skip-deps")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("\x1b", result.stdout)

    def test_help_and_bad_arguments_have_no_side_effects(self):
        self.assertEqual(self.install("--help").returncode, 0)
        for args in (("--unknown",), ("--prefix",), ("--prefix", "relative"), ("--prefix", ""),
                     ("--prefix", "/tmp/invalid=prefix"), ("--prefix", "/tmp/invalid\nprefix")):
            with self.subTest(arguments=args):
                self.assertNotEqual(self.install(*args).returncode, 0)
        self.env["CMAKE_BUILD_PARALLEL_LEVEL"] = "0"
        self.assertNotEqual(self.install().returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assertEqual(list(self.home.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
