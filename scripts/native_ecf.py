"""Translate deployed Conan metadata into a relocatable static Eiffel dependency library."""

from collections.abc import Mapping, Sequence
from pathlib import Path
import re
import xml.etree.ElementTree as ET


PLATFORMS = {"Windows": "windows", "Linux": "linux", "Macos": "macos"}


def crypto_options(system: str) -> dict[str, bool]:
    """Use the OS crypto backend where available, and OpenSSL on Linux."""
    if system not in PLATFORMS:
        raise ValueError(f"Unsupported operating system: {system}")
    return {"with_openssl": system == "Linux", "with_cng": system == "Windows"}


def package_name(settings: Mapping[str, str]) -> str:
    """Return a filesystem-safe package name identifying the target platform and ABI.

    Require OS, architecture, compiler/version, and build type settings. Include
    the MSVC runtime and minimum OS version when supplied. Reject unsupported
    operating systems and unsafe filename components.
    """
    if settings["os"] not in PLATFORMS:
        raise ValueError(f"Unsupported operating system: {settings['os']}")
    system = PLATFORMS[settings["os"]]
    architecture = {"armv8": "arm64"}.get(settings["arch"], settings["arch"])
    parts = ["arc", system, architecture,
             settings["compiler"] + settings["compiler.version"]]
    if settings.get("compiler.runtime"):
        runtime = {"dynamic": "md", "static": "mt"}[settings["compiler.runtime"]]
        if settings.get("compiler.runtime_type") == "Debug":
            runtime += "d"
        parts.append(runtime)
    if settings.get("os.version"):
        parts.append("min" + settings["os.version"])
    parts.append(settings["build_type"].lower())
    if any(not re.fullmatch(r"[A-Za-z0-9_.+-]+", part) for part in parts):
        raise ValueError("Unsafe platform or ABI identifier in package settings")
    return "-".join(parts)


def static_ecf(
    dependencies: Sequence[Mapping[str, Sequence[str]]],
    output: str | Path,
    compiler: str,
    uuid: str = "BD87421C-37CA-48B8-8BB9-5591AE642EFA",
) -> bytes:
    """Return UTF-8 ECF for deployed static dependencies without writing a file.

    Dependencies must be in dependent-before-dependency link order. Resolve
    paths relative to the deployment directory and reject escaping paths,
    missing static archives, or unsupported custom framework directories.
    """
    output = Path(output).resolve()
    namespace = "http://www.eiffel.com/developers/xml/configuration-1-23-0"
    ET.register_namespace("", namespace)
    system = ET.Element(f"{{{namespace}}}system", {
        "name": "arc_native", "uuid": uuid,
        "library_target": "native",
    })
    target = ET.SubElement(system, "target", {"name": "native"})
    ET.SubElement(target, "root", {"all_classes": "true"})
    externals: dict[str, list[str]] = {kind: [] for kind in (
        "external_include", "external_cflag", "external_library", "external_linker_flag",
    )}

    def add(kind: str, value: str) -> None:
        """Append an external value while preserving link order and repetitions."""
        externals[kind].append(value)

    def relative(path: str | Path) -> str:
        """Return an ECF-relative path, raising ValueError if it escapes output."""
        suffix = Path(path).resolve().relative_to(output).as_posix()
        return "$ECF_CONFIG_PATH/" + suffix

    msvc = compiler == "msvc"
    system_libraries: list[str] = []
    for info in dependencies:
        for directory in info.get("includedirs", []):
            add("external_include", relative(directory))
        for define in info.get("defines", []):
            add("external_cflag", ("/D" if msvc else "-D") + define)
        for flag in info.get("cflags", []):
            add("external_cflag", flag)
        for library in info.get("libs", []):
            filename = (library if library.endswith(".lib") else library + ".lib") if msvc else "lib" + library + ".a"
            candidates = [Path(directory) / filename for directory in info.get("libdirs", [])]
            archive = next((path for path in candidates if path.is_file()), None)
            if archive is None:
                raise ValueError(f"Static library not found: {filename}")
            add("external_library", relative(archive))
        for library in info.get("system_libs", []):
            flag = (library if library.endswith(".lib") else library + ".lib") if msvc else "-l" + library
            if flag not in system_libraries:
                system_libraries.append(flag)
        if info.get("frameworkdirs"):
            raise ValueError("Custom framework directories are not supported by the static ECF generator")
        for framework in info.get("frameworks", []):
            add("external_linker_flag", "-framework " + framework)
        for flag in info.get("exelinkflags", []):
            add("external_linker_flag", flag)
    for library in system_libraries:
        add("external_library", library)
    for kind, values in externals.items():
        attribute = "value" if kind.endswith("flag") else "location"
        for value in values:
            ET.SubElement(target, kind, {attribute: value})
    ET.indent(system, space="    ")
    return ET.tostring(system, encoding="UTF-8", xml_declaration=True)
