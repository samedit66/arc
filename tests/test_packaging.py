"""ECF and ZIP packaging checks using fixtures, without invoking native compilers."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from native_ecf import package_name, static_ecf
from package import create_package


NAMESPACE = {"e": "http://www.eiffel.com/developers/xml/configuration-1-23-0"}
SETTINGS = (
    {"os": "Macos", "arch": "armv8", "compiler": "apple-clang",
     "compiler.version": "21", "build_type": "Release"},
    {"os": "Linux", "arch": "x86_64", "compiler": "gcc",
     "compiler.version": "14", "build_type": "Release"},
    {"os": "Windows", "arch": "x86_64", "compiler": "msvc",
     "compiler.version": "194", "compiler.runtime": "dynamic",
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
            dict(windows, **{"compiler.runtime": "static"}),
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
        (native / "native.ecf").write_bytes(static_ecf([info], native, settings["compiler"]))
        manifest = {"settings": settings, "package_name": package_name(settings)}
        (native / "manifest.json").write_text(json.dumps(manifest))
        (root / "src/core/c").mkdir(parents=True)
        (root / "src/core/archive.e").write_bytes(b"unchanged source fixture")
        (root / "src/core/c/arc_unpack.h").write_bytes(b"native primitive fixture")
        (root / "src/cli").mkdir()
        (root / "src/cli/arc_cli.e").write_bytes(b"CLI source fixture")
        (root / "vendor/cli/src").mkdir(parents=True)
        (root / "vendor/cli/cli.ecf").write_text("<fixture/>\n")
        (root / "vendor/cli/src/application_parser.e").write_text("CLI parser fixture\n")
        (root / "arc.ecf").write_text("<fixture/>\n")
        (root / "README.md").write_text("Fixture documentation\n")
        (root / "docs").mkdir()
        (root / "docs/design.md").write_text("Implementation notes fixture\n")
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
                    self.assertEqual(package.read("arc/src/core/archive.e"), b"unchanged source fixture")
                    self.assertEqual(package.read("arc/docs/design.md"), b"Implementation notes fixture\n")
                    self.assertEqual(package.read("arc/src/cli/arc_cli.e"), b"CLI source fixture")
                    self.assertEqual(package.read("arc/src/core/c/arc_unpack.h"), b"native primitive fixture")
                    self.assertIn("arc/vendor/cli/cli.ecf", package.namelist())
                    self.assertIn("arc/vendor/cli/src/application_parser.e", package.namelist())
                    self.assertIn("arc/build/native/manifest.json", package.namelist())
                    self.assertTrue(any(name.endswith("licenses/LICENSE") for name in package.namelist()))
                    self.assertFalse(any("must-not-ship" in name for name in package.namelist()))
                    package.extractall(root / "consumer/vendor")
                self.assertEqual((root / "consumer/vendor/arc/docs/design.md").read_text(), "Implementation notes fixture\n")
                copied = root / "consumer/vendor/arc/build/native"
                tree = ET.parse(copied / "native.ecf")
                location = tree.find("e:target/e:external_library", NAMESPACE).get("location")
                self.assertTrue((copied / location.removeprefix("$ECF_CONFIG_PATH/")).is_file())

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


if __name__ == "__main__":
    unittest.main()
