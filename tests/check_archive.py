"""Run the assertion-enabled archive facade probe in an isolated filesystem."""

from pathlib import Path
import argparse
import os
import shutil
import re
import io
import tarfile
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET


def build_and_check(root, compiler="ge"):
    """Build the CLI and assertion-enabled probes with the selected compiler, then run shared checks."""
    env = dict(os.environ, GOBO_EIFFEL=compiler)
    suffix = ".exe" if os.name == "nt" else ""
    targets = [("cli", root / "arc.ecf", "arc")]
    targets += [(target, root / "tests" / (target + ".ecf"), "arc_" + target)
                for target in ("demo", "checks", "package_checks", "archive_checks")]
    for target, config, name in targets:
        if compiler == "ge":
            command = ["gec", "--gelint", "--gc=boehm", "--variable=ARC_NATIVE_MODE=conan",
                       "--target=" + target, str(config)]
            if target == "cli":
                command.insert(1, "--finalize")
            subprocess.run(command, cwd=root, env=env, check=True)
        else:
            # EiffelStudio has no Gobo-style --variable switch. Generate a configuration
            # with explicit root variables and absolute source/dependency locations.
            namespace = "{http://www.eiffel.com/developers/xml/configuration-1-23-0}"
            generated = root / "build/ci"
            generated.mkdir(parents=True, exist_ok=True)
            for source in (root / "arc.ecf", config):
                tree = ET.parse(source)
                for element in tree.iter():
                    location = element.get("location")
                    if location:
                        if location.startswith("$ECF_CONFIG_PATH"):
                            element.set("location", location.replace("$ECF_CONFIG_PATH", str(source.parent)))
                        elif not location.startswith(("$", "-")):
                            resolved = (source.parent / location).resolve()
                            if resolved == root / "arc.ecf":
                                resolved = generated / "arc.ecf"
                            element.set("location", str(resolved))
                for item in tree.getroot().findall(namespace + "target"):
                    variable = item.find(namespace + "variable[@name='ARC_NATIVE_MODE']")
                    if variable is None:
                        variable = ET.SubElement(item, namespace + "variable", name="ARC_NATIVE_MODE")
                    variable.set("value", "conan")
                tree.write(generated / source.name, encoding="utf-8", xml_declaration=True)
            project = generated / name
            subprocess.run(["ec", "-batch", "-config", str(generated / config.name),
                            "-target", target, "-project_path", str(project),
                            "-finalize", "-keep", "-c_compile"], cwd=root, env=env, check=True)
            shutil.copy2(project / "EIFGENs" / target / "F_code" / (name + suffix), root / (name + suffix))
    suffix = ".exe" if os.name == "nt" else ""

    def run(name, arguments, directory):
        """Run a fresh assertion-enabled probe and retain failure diagnostics."""
        subprocess.run([str(root / (name + suffix)), *map(str, arguments)],
                       cwd=directory, env=env, check=True, timeout=60)

    profiles = ("tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst", "tar.lz4", "zip", "7z")
    with tempfile.TemporaryDirectory(prefix="arc-profiles-") as temporary:
        directory = Path(temporary).resolve()
        run("arc_demo", [], directory)
        for profile in profiles:
            work = directory / ("checks-" + profile)
            work.mkdir()
            (work / "second.txt").write_bytes(b"x" * 200000)
            run("arc_checks", [directory / ("example." + profile), work / "selected"], work)
    with tempfile.TemporaryDirectory(prefix="arc-package-profiles-") as temporary:
        directory = Path(temporary).resolve()
        source = directory / "source"
        (source / "nested").mkdir(parents=True)
        (source / "empty-dir").mkdir()
        (source / "first.txt").write_bytes(b"first file\n")
        (source / "nested/second.txt").write_bytes(b"x" * 200000)
        (source / "nested/empty.txt").touch()
        (source / "file-link").symlink_to("first.txt")
        (source / "dir-link").symlink_to("nested", target_is_directory=True)
        (source / "dangling-link").symlink_to("missing")
        for profile in profiles:
            work = directory / profile
            work.mkdir()
            run("arc_package_checks", [source, work / ("output." + profile), profile, work / "restored"], work)
    main([str(root / ("arc_archive_checks" + suffix))])
    subprocess.run([sys.executable, str(root / "tests/check_cli.py"), str(root / ("arc" + suffix))],
                   cwd=root, env=env, check=True)


def check_consumer(directory, repository):
    """Compile and execute a consumer using only its installed vendor library."""
    env = dict(os.environ, GOBO_EIFFEL="ge")
    env.pop("CONAN_HOME", None)
    suffix = ".exe" if os.name == "nt" else ""
    shutil.copy2(repository / "tests/arc_archive_checks.e", directory / "arc_archive_checks.e")
    source = (repository / "tests/archive_checks.ecf").read_text()
    (directory / "consumer.ecf").write_text(source.replace('location="../arc.ecf"',
                                                           'location="vendor/arc/arc.ecf"'))
    if os.name == "nt":
        subprocess.run(["gec", "--gelint", "consumer.ecf"], cwd=directory, env=env, check=True)
        executable = directory / ("arc_archive_checks" + suffix)
    else:
        # Drive the generated script directly: this Gobo build's gecc mangles Unicode include paths.
        subprocess.run(["gec", "--gelint", "--cc=no", "consumer.ecf"], cwd=directory, env=env, check=True)
        generated = directory / ".gobo"
        subprocess.run(["sh", "-e", "arc_archive_checks.sh"], cwd=generated, env=env, check=True)
        executable = generated / "arc_archive_checks"
    main([str(executable)])


def check_relocation(archive):
    """Compile a library consumer or run the application from a project-built ZIP after relocation."""
    repository = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="arc relocated данные ") as temporary:
        directory = Path(temporary).resolve()
        with zipfile.ZipFile(archive) as bundle:
            bundle.extractall(directory / "vendor")
        installed = directory / "vendor/arc"
        for document in [installed / "README.md", *(installed / "docs").glob("*.md")]:
            if document.is_file():
                for link in re.findall(r"\]\(([^)]+)\)", document.read_text(encoding="utf-8")):
                    if "://" not in link and not link.startswith("#"):
                        assert (document.parent / link.split("#")[0]).exists(), (document, link)
        suffix = ".exe" if os.name == "nt" else ""
        env = dict(os.environ, GOBO_EIFFEL="ge")
        env.pop("CONAN_HOME", None)
        if (installed / "arc.ecf").is_file():
            check_consumer(directory, repository)
        else:
            binary = installed / ("arc" + suffix)
            if os.name != "nt":
                binary.chmod(0o755)
            env.pop("LD_LIBRARY_PATH", None)
            env.pop("DYLD_LIBRARY_PATH", None)
            subprocess.run([sys.executable, str(repository / "tests/check_cli.py"), str(binary)],
                           cwd=directory, env=env, check=True)
    print(f"PASS: relocated {archive}")


def check_system_library(repository):
    """Compile against a system libarchive from source-only core/ECF without any native deployment or CLI."""
    with tempfile.TemporaryDirectory(prefix="arc system данные ") as temporary:
        directory = Path(temporary).resolve()
        installed = directory / "vendor/arc"
        shutil.copytree(repository / "src/core", installed / "src/core")
        shutil.copy2(repository / "arc.ecf", installed / "arc.ecf")
        check_consumer(directory, repository)
    print("PASS: system libarchive consumer without Conan deployment or CLI")


def main(arguments):
    """Check facade outcomes independently of the probe's Eiffel assertions."""
    if not arguments:
        raise SystemExit("usage: check_archive.py EXECUTABLE [EXECUTABLE ...]")
    for argument in arguments:
        executable = Path(argument).resolve()
        with tempfile.TemporaryDirectory(prefix="arc-archive-") as temporary:
            root = Path(temporary)
            corrupt = root / "corrupt.zip"
            with zipfile.ZipFile(corrupt, "w") as output:
                output.writestr("file", b"payload with deliberately invalid CRC")
            data = bytearray(corrupt.read_bytes())
            data[data.index(b"payload with deliberately invalid CRC")] ^= 1
            corrupt.write_bytes(data)
            bad_header = root / "bad-header.tar"
            with tarfile.open(bad_header, "w") as output:
                for name in ("first", "second"):
                    entry = tarfile.TarInfo(name)
                    entry.size = 3
                    output.addfile(entry, io.BytesIO(b"abc"))
            data = bytearray(bad_header.read_bytes())
            data[1024 + 148] = ord("?")
            bad_header.write_bytes(data)
            result = subprocess.run([str(executable)], cwd=root, capture_output=True,
                                    text=True, encoding="utf-8", timeout=30)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert "PASS:" in result.stdout
            with zipfile.ZipFile(root / "registered.zip") as archive:
                assert archive.namelist() == ["first", "nested/later"]
                assert archive.read("first") == b"after registration"
                assert archive.read("nested/later") == b"x" * 150000
            assert (root / "selected.txt").read_bytes() == b"x" * 150000
            assert (root / "restored/first").read_bytes() == b"after registration"
            assert (root / "restored/nested/later").read_bytes() == b"x" * 150000
            for name in ("cancelled.tar", "aborted.zip", "failed.tar"):
                assert not (root / name).exists(), name
            print(result.stdout.strip())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", choices=("ge", "ise"), default="ge", help="Compiler for --build")
    parser.add_argument("--build", action="store_true", help="Rebuild and run all probes and CLI integration")
    parser.add_argument("--relocate", type=Path, help="Check a project-built library or application ZIP after relocation")
    parser.add_argument("--system", action="store_true", help="Check a source-only library against installed libarchive")
    parser.add_argument("executables", nargs="*")
    options = parser.parse_args()
    if options.system:
        check_system_library(Path(__file__).resolve().parents[1])
    elif options.relocate:
        check_relocation(options.relocate.resolve())
    elif options.build:
        build_and_check(Path(__file__).resolve().parents[1], options.compiler)
    else:
        main(options.executables)
