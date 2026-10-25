from pathlib import Path
import json
import platform

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration

from scripts.native_ecf import PLATFORMS, crypto_options, package_name, static_ecf


class ArcDependencies(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    requires = "libarchive/3.8.7"
    default_options = {
        "*:shared": False,
        "libarchive/*:with_zlib": True,
        "libarchive/*:with_bzip2": True,
        "libarchive/*:with_lz4": True,
        "libarchive/*:with_lzma": True,
        "libarchive/*:with_zstd": True,
        "libarchive/*:with_iconv": True,
        "libarchive/*:with_expat": True,
        "libarchive/*:with_libxml2": False,
        "libarchive/*:with_acl": False,
        "libarchive/*:with_xattr": False,
    }

    def configure(self):
        system = str(self.settings.os)
        if system not in PLATFORMS:
            raise ConanInvalidConfiguration(f"Unsupported operating system: {system}")
        for name, enabled in crypto_options(system).items():
            setattr(self.options["libarchive"], name, enabled)

    def validate(self):
        system, compiler = str(self.settings.os), str(self.settings.compiler)
        supported = {"Windows": {"msvc"}, "Linux": {"gcc", "clang"}, "Macos": {"apple-clang"}}
        if compiler not in supported[system]:
            raise ConanInvalidConfiguration(f"Unsupported compiler for {system}: {compiler}")
        if str(self.settings.arch) not in {"x86_64", "armv8"}:
            raise ConanInvalidConfiguration("Supported architectures: x86_64 and armv8")
        for dependency in self.dependencies.host.values():
            if dependency.options.get_safe("shared", False):
                raise ConanInvalidConfiguration(f"Shared dependency is not supported: {dependency.ref}")

    def generate(self):
        """Generate ECF and package metadata after full_deploy has rewritten paths."""
        output = Path(self.generators_folder)
        dependencies = list(reversed(list(self.dependencies.host.topological_sort.values())))
        fields = ("includedirs", "libdirs", "libs", "defines", "cflags", "system_libs",
                  "frameworkdirs", "frameworks", "exelinkflags")
        infos = []
        for dependency in dependencies:
            info = dependency.cpp_info.aggregated_components()
            infos.append({name: list(getattr(info, name) or []) for name in fields})
        settings = {name: str(value) for name, value in self.settings.items()}
        (output / "native.ecf").write_bytes(static_ecf(infos, output, settings["compiler"]))
        backend = {"Macos": "CommonCrypto", "Windows": "CNG", "Linux": "OpenSSL"}[settings["os"]]
        manifest = {
            "package_name": package_name(settings),
            "settings": settings,
            "crypto_backend": backend,
            "build_host": platform.platform(),
            "dependencies": [
                {"reference": str(dependency.ref), "recipe_revision": dependency.ref.revision,
                 "package_id": dependency.pref.package_id, "package_revision": dependency.pref.revision,
                 "options": {name: str(value) for name, value in dependency.options.items()}}
                for dependency in dependencies
            ],
        }
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
