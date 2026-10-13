import os
import pathlib
import shutil
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parent
os.chdir(root)
build_dir = pathlib.Path(os.environ.get("FILEKIT_BUILD_DIR", str(pathlib.Path(tempfile.gettempdir()) / "filekit-build"))).resolve()
with tempfile.TemporaryDirectory(prefix="filekit-check-") as temporary:
    executable = pathlib.Path(temporary) / ("filekit-check.exe" if os.name == "nt" else "filekit-check")
    sanitize = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("ARC_SANITIZE", os.environ.get("FILEKIT_SANITIZE")) == "1" else []
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_FILE_OFFSET_BITS=64",
                    "-Wall", "-Wextra", "-Wpedantic", "-Werror", *sanitize,
                    "tests.c", str(build_dir / "libfilekit.a"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=temporary, check=True)
    # A failed rebuild must not report success using stale objects.
    for tool in ("CC", "AR"):
        failed_build = pathlib.Path(temporary) / tool
        failed_build.mkdir()
        for artifact in build_dir.iterdir():
            if artifact.suffix in (".o", ".a"):
                shutil.copyfile(artifact, failed_build / artifact.name)
        environment = dict(os.environ, **{tool: "false"})
        result = subprocess.run(["bash", "build.sh", str(failed_build)],
                                env=environment, capture_output=True, text=True)
        assert result.returncode != 0, (tool, result.stdout, result.stderr)
        assert "Built " not in result.stdout, (tool, result.stdout)
    print("Filekit build failure checks passed.")
