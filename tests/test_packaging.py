"""ECF and ZIP packaging checks using fixtures, without invoking native compilers."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import subprocess
import shutil
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from native_ecf import package_name, static_ecf
from package import create_package
from prepare import check_binary, dependency_violations, prepare_dependencies


NAMESPACE = {"e": "http://www.eiffel.com/developers/xml/configuration-1-23-0"}
SETTINGS = (
    {"os": "Macos", "arch": "armv8", "compiler": "apple-clang",
     "compiler.version": "21", "build_type": "Release"},
    {"os": "Linux", "arch": "x86_64", "compiler": "gcc",
     "compiler.version": "14", "build_type": "Release"},
    {"os": "Windows", "arch": "x86_64", "compiler": "msvc",
     "compiler.version": "194", "compiler.runtime": "static",
     "compiler.runtime_type": "Release", "build_type": "Release"},
)


def dependency(directory, compiler, name):
    include, libraries = directory / "include", directory / "lib"
    include.mkdir(parents=True)
    libraries.mkdir()
    filename = name + ".lib" if compiler == "msvc" else "lib" + name + ".a"
    (libraries / filename).write_bytes(b"static archive fixture")
    (include / "archive.h").write_text("/* Fixture header. */\n")
    licenses = directory / "licenses"
    licenses.mkdir()
    (licenses / "LICENSE").write_text("Fixture license\n")
    return {"includedirs": [str(include)], "libdirs": [str(libraries)], "libs": [name]}


class NativeMetadataTests(unittest.TestCase):
    def test_runtime_variants_have_distinct_package_names(self):
        windows = SETTINGS[2]
        variants = (
            windows,
            dict(windows, **{"compiler.runtime": "dynamic"}),
            dict(windows, **{"compiler.runtime_type": "Debug"}),
        )
        self.assertEqual(len({package_name(settings) for settings in variants}), 3)

    def test_ecf_preserves_link_order_and_platform_parameters(self):
        for settings in SETTINGS:
            with self.subTest(system=settings["os"]), tempfile.TemporaryDirectory(prefix="arc space ") as temporary:
                output = Path(temporary)
                compiler = settings["compiler"]
                archive = dependency(output / "full_deploy/host/archive", compiler, "archive")
                zlib = dependency(output / "full_deploy/host/zlib", compiler, "z")
                if compiler == "msvc":
                    archive["defines"] = ["LIBARCHIVE_STATIC"]
                    archive["system_libs"] = ["bcrypt", "bcrypt.lib"]
                    zlib["system_libs"] = ["bcrypt"]
                else:
                    archive["defines"] = ["XML_STATIC"]
                    archive["system_libs"] = ["pthread", "m"]
                    zlib["system_libs"] = ["pthread"]
                data = static_ecf([archive, zlib], output, compiler)
                self.assertNotIn(str(output).encode(), data)
                target = ET.fromstring(data).find("e:target", NAMESPACE)
                libraries = [element.attrib["location"] for element in target.findall("e:external_library", NAMESPACE)]
                self.assertIn("/archive/", libraries[0])
                self.assertIn("/zlib/", libraries[1])
                if compiler == "msvc":
                    self.assertTrue(libraries[0].endswith("archive.lib"))
                    self.assertEqual(libraries[2:], ["bcrypt.lib"])
                    self.assertEqual(target.find("e:external_cflag", NAMESPACE).get("value"), "/DLIBARCHIVE_STATIC")
                else:
                    self.assertTrue(libraries[0].endswith("libarchive.a"))
                    self.assertEqual(libraries[2:], ["-lpthread", "-lm"])

    def test_missing_static_library_is_not_replaced_by_a_shared_library(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            info = dependency(output / "dep", "gcc", "archive")
            (output / "dep/lib/libarchive.a").unlink()
            (output / "dep/lib/libarchive.so").write_bytes(b"shared fixture")
            with self.assertRaisesRegex(ValueError, "Static library not found"):
                static_ecf([info], output, "gcc")

    def test_paths_outside_the_deployment_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "deployment"
            output.mkdir()
            info = dependency(root / "external-cache", "gcc", "archive")
            with self.assertRaises(ValueError):
                static_ecf([info], output, "gcc")


class PackageTests(unittest.TestCase):
    def fixture(self, root, settings):
        native = root / "build/native"
        native.mkdir(parents=True)
        info = dependency(native / "full_deploy/host/archive", settings["compiler"], "archive")
        (native / "native.ecf").write_bytes(static_ecf([info], native, settings["compiler"], settings=settings))
        manifest = {"settings": settings, "package_name": package_name(settings)}
        (native / "manifest.json").write_text(json.dumps(manifest))
        (root / "src/core/c").mkdir(parents=True)
        (root / "src/core/arc_reader.e").write_bytes(b"unchanged source fixture")
        (root / "src/core/c/libarchive_wrapper.h").write_bytes(b"native primitive fixture")
        (root / "src/cli").mkdir()
        (root / "src/cli/arc_cli.e").write_bytes(b"CLI source fixture")
        (root / "src/cli/presentation").mkdir()
        (root / "src/cli/presentation/arc_entry_display.e").write_bytes(b"Presentation fixture")
        (root / "vendor/progress_bar").mkdir(parents=True)
        (root / "vendor/progress_bar/LICENSE").write_text("Progress bar license fixture\n")
        (root / "vendor/cli/src").mkdir(parents=True)
        (root / "vendor/cli/.git").write_text("gitdir: ../../.git/modules/vendor/cli\n")
        (root / "vendor/cli/cli.ecf").write_text("<fixture/>\n")
        (root / "vendor/cli/src/application_parser.e").write_text("CLI parser fixture\n")
        (root / "arc.ecf").write_bytes((Path(__file__).resolve().parents[1] / "arc.ecf").read_bytes())
        (root / "README.md").write_text("Fixture documentation\n")
        (root / "docs").mkdir()
        (root / "docs/code.md").write_text("Source map fixture\n")
        (root / "conan.lock").write_text("{}\n")
        cache = root / "build/conan"
        cache.mkdir()
        (cache / "must-not-ship").write_text("cache fixture")
        return native

    def test_archives_include_dependencies_licenses_and_unchanged_source(self):
        for settings in SETTINGS:
            with self.subTest(system=settings["os"]), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self.fixture(root, settings)
                archive = create_package(root)
                with zipfile.ZipFile(archive) as package:
                    self.assertEqual(package.read("arc/src/core/arc_reader.e"), b"unchanged source fixture")
                    self.assertEqual(package.read("arc/docs/code.md"), b"Source map fixture\n")
                    self.assertFalse(any(name.startswith("arc/src/cli/") for name in package.namelist()))
                    self.assertFalse(any(name.startswith("arc/vendor/") for name in package.namelist()))
                    config = ET.fromstring(package.read("arc/arc.ecf"))
                    self.assertIsNone(config.find("e:target[@name='cli']", NAMESPACE))
                    mode = config.find("e:target/e:variable[@name='ARC_NATIVE_MODE']", NAMESPACE)
                    self.assertEqual(mode.get("value"), "conan")
                    native = config.find("e:target/e:library[@name='native']", NAMESPACE)
                    self.assertIsNone(native.find("e:condition", NAMESPACE))
                    self.assertEqual(package.read("arc/src/core/c/libarchive_wrapper.h"), b"native primitive fixture")
                    self.assertNotIn("arc/vendor/cli/.git", package.namelist())
                    self.assertIn("arc/build/native/manifest.json", package.namelist())
                    self.assertTrue(any(name.endswith("licenses/LICENSE") for name in package.namelist()))
                    self.assertFalse(any("must-not-ship" in name for name in package.namelist()))
                    package.extractall(root / "consumer/vendor")
                self.assertEqual((root / "consumer/vendor/arc/docs/code.md").read_text(), "Source map fixture\n")
                copied = root / "consumer/vendor/arc/build/native"
                tree = ET.parse(copied / "native.ecf")
                location = tree.find("e:target/e:external_library", NAMESPACE).get("location")
                self.assertTrue((copied / location.removeprefix("$ECF_CONFIG_PATH/")).is_file())

    def test_library_package_does_not_require_cli(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root, SETTINGS[0])
            shutil.rmtree(root / "vendor")
            shutil.rmtree(root / "src/cli")
            self.assertTrue(create_package(root).is_file())

    def test_application_package_contains_only_runtime_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root, SETTINGS[0])
            binary = root / "arc"
            binary.write_bytes(b"executable fixture")
            with patch("package.check_binary") as inspect:
                archive = create_package(root, "application", binary)
            inspect.assert_called_once_with(binary.resolve(), SETTINGS[0])
            with zipfile.ZipFile(archive) as package:
                self.assertEqual(package.read("arc/arc"), b"executable fixture")
                self.assertIn("arc/manifest.json", package.namelist())
                self.assertTrue(any(name.endswith("licenses/LICENSE") for name in package.namelist()))
                self.assertIn("arc/licenses/progress_bar/LICENSE", package.namelist())
                self.assertFalse(any(name.startswith(("arc/src/", "arc/build/", "arc/vendor/"))
                                     for name in package.namelist()))

    def test_application_dependency_failure_does_not_create_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root, SETTINGS[0])
            with patch("package.check_binary", side_effect=SystemExit("Unexpected dependency")):
                with self.assertRaises(SystemExit):
                    create_package(root, "application")
            self.assertFalse((root / "build/package").exists())

    def test_mismatched_manifest_is_rejected_before_packaging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = self.fixture(root, SETTINGS[0])
            manifest = json.loads((native / "manifest.json").read_text())
            manifest["settings"]["arch"] = "x86_64"
            (native / "manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(SystemExit):
                create_package(root)
            self.assertFalse((root / "build/package").exists())

    def test_missing_deployed_library_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = self.fixture(root, SETTINGS[0])
            next(native.rglob("*.a")).unlink()
            with self.assertRaises(SystemExit):
                create_package(root)


class BuildContractTests(unittest.TestCase):
    def test_import_policy_rejects_third_party_and_dynamic_crt(self):
        self.assertEqual(dependency_violations("Macos", ["/usr/lib/libSystem.B.dylib"]), ())
        self.assertTrue(dependency_violations("Macos", ["/opt/homebrew/lib/libarchive.dylib"]))
        self.assertEqual(dependency_violations("Windows", ["KERNEL32.dll", "bcrypt.dll"]), ())
        for library in ("archive.dll", "VCRUNTIME140.dll", "ucrtbase.dll", "api-ms-win-crt-runtime-l1-1-0.dll"):
            self.assertTrue(dependency_violations("Windows", [library]))
        self.assertTrue(dependency_violations("Linux", ["libc.so.6"]))

    def test_libc_variants_have_distinct_package_names(self):
        linux = SETTINGS[1]
        self.assertNotEqual(package_name(dict(linux, libc="glibc")),
                            package_name(dict(linux, libc="musl")))

    def test_actual_inspection_parsing_rejects_dynamic_linux_and_windows_crt(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "arc"
            binary.touch()
            linux = dict(SETTINGS[1], libc="musl")
            report = "Machine: Advanced Micro Devices X86-64\n  INTERP 0x0\n"
            with patch("prepare.subprocess.run", return_value=subprocess.CompletedProcess([], 0, report)):
                with self.assertRaisesRegex(SystemExit, "interpreter"):
                    check_binary(binary, linux)
            report = "8664 machine (x64)\n    KERNEL32.dll\n    VCRUNTIME140.dll\n"
            with patch("prepare.subprocess.run", return_value=subprocess.CompletedProcess([], 0, report)):
                with self.assertRaisesRegex(SystemExit, "VCRUNTIME140"):
                    check_binary(binary, SETTINGS[2])

    def test_failed_deployment_swap_restores_previous_native(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = root / "build/native"
            native.mkdir(parents=True)
            (native / "sentinel").write_bytes(b"previous deployment")
            rename = Path.rename

            def fail_staging_swap(path, destination):
                if path.name.startswith(".native-") and path.name != ".native-previous":
                    raise OSError("staging rename failed")
                return rename(path, destination)

            with patch("prepare.sys.platform", "darwin"), patch("prepare.shutil.which", return_value="conan"), \
                 patch("prepare.subprocess.run"), patch.object(Path, "rename", fail_staging_swap):
                with self.assertRaisesRegex(OSError, "staging rename"):
                    prepare_dependencies(root, "host", "build")
            self.assertEqual((native / "sentinel").read_bytes(), b"previous deployment")
            self.assertFalse((root / "build/.native-previous").exists())

    def test_failed_prepare_preserves_native_deployment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = root / "build/native"
            native.mkdir(parents=True)
            (native / "sentinel").write_bytes(b"previous deployment")
            with patch("prepare.sys.platform", "darwin"), patch("prepare.shutil.which", return_value="conan"), \
                 patch("prepare.subprocess.run", side_effect=subprocess.CalledProcessError(1, "conan")):
                with self.assertRaises(subprocess.CalledProcessError):
                    prepare_dependencies(root, "host", "build")
            self.assertEqual((native / "sentinel").read_bytes(), b"previous deployment")


if __name__ == "__main__":
    unittest.main()
