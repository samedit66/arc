"""Package Eiffel source with the prepared static native dependencies for its target ABI."""

from collections.abc import Sequence
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

from native_ecf import package_name
from prepare import main as prepare_dependencies


def create_package(root: str | Path) -> Path:
    """Validate a prepared deployment and return the path of its library ZIP.

    Copy source, docs, ECF, native dependencies, licenses, and metadata from root.
    Replace this target's staging directory and ZIP on repeat packaging.
    Inconsistent metadata or missing native paths exit before staging begins;
    filesystem and malformed-input errors propagate to the caller.
    """
    root = Path(root).resolve()
    native = root / "build/native"
    if not (native / "manifest.json").is_file() or not (native / "native.ecf").is_file():
        raise SystemExit("Run mise run prepare before packaging.")
    manifest = json.loads((native / "manifest.json").read_text(encoding="utf-8"))
    name = package_name(manifest["settings"])
    if manifest.get("package_name") != name:
        raise SystemExit("Package manifest does not match its target settings. Run prepare again.")
    namespace = {"e": "http://www.eiffel.com/developers/xml/configuration-1-23-0"}
    target = ET.parse(native / "native.ecf").find("e:target", namespace)
    if target is None:
        raise SystemExit("Native ECF has no target. Run prepare again.")
    prefix = "$ECF_CONFIG_PATH/"
    for element in target:
        location = element.get("location", "")
        if location.startswith(prefix):
            path = (native / location[len(prefix):]).resolve()
            if not path.is_relative_to(native.resolve()) or not path.exists():
                raise SystemExit(f"Missing or external native dependency: {location}")
    destination = root / "build/package" / name / "arc"
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    shutil.copy2(root / "arc.ecf", destination / "arc.ecf")
    shutil.copytree(root / "src", destination / "src")
    shutil.copytree(root / "docs", destination / "docs")
    shutil.copytree(root / "vendor/cli", destination / "vendor/cli")
    deployed = destination / "build/native"
    deployed.mkdir(parents=True)
    for filename in ("native.ecf", "manifest.json"):
        shutil.copy2(native / filename, deployed / filename)
    shutil.copytree(native / "full_deploy/host", deployed / "full_deploy/host")
    for filename in ("README.md", "conan.lock"):
        shutil.copy2(root / filename, destination / filename)
    archive = shutil.make_archive(str(root / "build/package" / name),
                                  "zip", destination.parent, "arc")
    return Path(archive)


def main(arguments: Sequence[str] | None = None) -> None:
    """Prepare dependencies with the supplied CLI arguments, then create the ZIP.

    None selects sys.argv. Forward profile and offline options to preparation;
    print the archive path only after packaging succeeds.
    """
    prepare_dependencies(arguments)
    print(create_package(Path(__file__).resolve().parents[1]))


if __name__ == "__main__":
    main()
