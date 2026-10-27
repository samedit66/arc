"""Package the Eiffel library with static dependencies, or a verified application."""

import argparse
from collections.abc import Sequence
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

from native_ecf import package_name
from prepare import check_binary, prepare_dependencies


NAMESPACE = "http://www.eiffel.com/developers/xml/configuration-1-23-0"


def library_ecf(source: bytes, settings: dict[str, str]) -> bytes:
    """Select bundled dependencies and remove application-only targets from a library ECF."""
    tree = ET.fromstring(source)
    tag = "{" + NAMESPACE + "}"
    target = tree.find(f"{tag}target[@name='arc']")
    if target is None:
        raise ValueError("Missing arc library target")
    for child in list(tree):
        if child.tag == tag + "target" and child is not target:
            tree.remove(child)
    for child in list(target):
        condition = child.find(tag + "condition")
        if child.tag == tag + "library" and child.get("name") == "native":
            if condition is not None:
                child.remove(condition)
        elif condition is not None and any(item.get("name") == "ARC_NATIVE_MODE" for item in condition):
            target.remove(child)
    for name, value in (("ARC_NATIVE_MODE", "conan"), ("ARC_LIBC", settings.get("libc", "glibc"))):
        variable = target.find(f"{tag}variable[@name='{name}']")
        if variable is None:
            raise ValueError(f"Missing ECF variable: {name}")
        variable.set("value", value)
    ET.register_namespace("", NAMESPACE)
    ET.indent(tree, space="    ")
    return ET.tostring(tree, encoding="utf-8", xml_declaration=True)


def create_package(root: str | Path, kind: str = "library", binary: Path | None = None) -> Path:
    """Validate prepared dependencies and package the library or an inspected executable.

    Library consumers need neither Conan nor CLI sources. Application packaging
    verifies architecture and imports before creating or replacing its staging area.
    """
    if kind not in {"library", "application"}:
        raise ValueError(f"Unknown package kind: {kind}")
    root = Path(root).resolve()
    native = root / "build/native"
    if not (native / "manifest.json").is_file() or not (native / "native.ecf").is_file():
        raise SystemExit("Run mise run prepare before packaging")
    manifest = json.loads((native / "manifest.json").read_text(encoding="utf-8"))
    settings = manifest["settings"]
    name = package_name(settings)
    if manifest.get("package_name") != name:
        raise SystemExit("Package manifest does not match its target settings. Run prepare again")
    if settings["os"] == "Windows" and settings.get("compiler.runtime") != "static":
        raise SystemExit("Conan packages require static MSVC runtime (/MT); prepare again")
    target = ET.parse(native / "native.ecf").find(f"{{{NAMESPACE}}}target")
    if target is None:
        raise SystemExit("Native ECF has no target. Run prepare again")
    prefix = "$ECF_CONFIG_PATH/"
    for element in target:
        location = element.get("location", "")
        if location.startswith(prefix):
            path = (native / location[len(prefix):]).resolve()
            if not path.is_relative_to(native.resolve()) or not path.exists():
                raise SystemExit(f"Missing or external native dependency: {location}")
    if kind == "application":
        if settings["build_type"] != "Release" or settings.get("compiler.runtime_type", "Release") != "Release":
            raise SystemExit("Application packages require Release native dependencies and runtime")
        binary = (binary or root / ("arc.exe" if settings["os"] == "Windows" else "arc")).resolve()
        check_binary(binary, settings)
        ecf = None
    else:
        ecf = library_ecf((root / "arc.ecf").read_bytes(), settings)
    name += "-" + kind
    destination = root / "build/package" / name / "arc"
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    if kind == "library":
        (destination / "arc.ecf").write_bytes(ecf)
        shutil.copytree(root / "src/core", destination / "src/core")
        shutil.copytree(root / "docs", destination / "docs")
        source_map = destination / "docs/code.md"
        if source_map.is_file():
            text = source_map.read_text(encoding="utf-8")
            if "## Library" in text and "## CLI" in text:
                text = "# Library source map\n\nThis package contains the core library only.\n\n" + text.split("## Library", 1)[1].split("## CLI", 1)[0]
                source_map.write_text(text, encoding="utf-8")
        readme = (root / "README.md").read_text(encoding="utf-8")
        api = readme.split("## Eiffel API", 1)[-1].split("## Packaging and verification", 1)[0]
        (destination / "README.md").write_text(
            "# Arc Eiffel library\n\nConnect `arc.ecf` from your consumer ECF. "
            "This ABI-specific package uses its bundled static dependencies. "
            "Keep the package together when moving it. Eiffel and compatible C compilers "
            "are required; Python, Conan, mise and CLI sources are not. "
            "See `build/native/manifest.json` for platform/runtime details. "
            "Source-checkout build instructions in docs are for development, not installation.\n\n"
            "## Eiffel API\n" + api, encoding="utf-8")
        shutil.copy2(root / "conan.lock", destination / "conan.lock")
        deployed = destination / "build/native"
        deployed.mkdir(parents=True)
        for filename in ("native.ecf", "manifest.json"):
            shutil.copy2(native / filename, deployed / filename)
        shutil.copytree(native / "full_deploy/host", deployed / "full_deploy/host")
    else:
        shutil.copy2(binary, destination / binary.name)
        shutil.copy2(native / "manifest.json", destination / "manifest.json")
        (destination / "README.txt").write_text(
            "Arc application\n\nCopy the executable to a directory on PATH.\n"
            "Eiffel, Python and Conan are not required to run it.\n"
            "See manifest.json for the native platform and dependency versions.\n", encoding="utf-8")
        progress_license = destination / "licenses/progress_bar/LICENSE"
        progress_license.parent.mkdir(parents=True)
        shutil.copy2(root / "vendor/progress_bar/LICENSE", progress_license)
        host = native / "full_deploy/host"
        for licenses in host.rglob("licenses"):
            if licenses.is_dir():
                shutil.copytree(licenses, destination / "licenses" / licenses.relative_to(host))
    if (root / "LICENSE").is_file():
        shutil.copy2(root / "LICENSE", destination / "LICENSE")
    archive = shutil.make_archive(str(root / "build/package" / name), "zip", destination.parent, "arc")
    return Path(archive)


def main(arguments: Sequence[str] | None = None) -> None:
    """Optionally prepare dependencies, then package a library or verified executable."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("library", "application"), default="library")
    parser.add_argument("--binary", type=Path, help="Application executable; default is arc or arc.exe")
    parser.add_argument("--skip-prepare", action="store_true", help="Use the existing native deployment")
    parser.add_argument("--profile", help="Conan host profile")
    parser.add_argument("--build-profile", help="Conan build profile")
    parser.add_argument("--no-remote", action="store_true", help="Use cached dependencies only")
    parser.add_argument("--libc", choices=("glibc", "musl"), help="Require this native Linux libc")
    args = parser.parse_args(arguments)
    if args.binary and args.kind != "application":
        parser.error("--binary requires --kind application")
    root = Path(__file__).resolve().parents[1]
    if not args.skip_prepare and args.kind == "library":
        prepare_dependencies(root, args.profile, args.build_profile, args.no_remote, args.libc)
    print(create_package(root, args.kind, args.binary))


if __name__ == "__main__":
    main()
