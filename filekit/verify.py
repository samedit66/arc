import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent
build_dir = pathlib.Path(os.environ.get("FILEKIT_BUILD_DIR", root / "build")).resolve()
subprocess.run(["ctest", "--test-dir", str(build_dir), "--output-on-failure",
                "--no-tests=error"], check=True)
with tempfile.TemporaryDirectory(prefix="filekit-check-") as temporary:
    for tool in ("CC", "AR"):
        failed_build = pathlib.Path(temporary) / tool
        failed_build.mkdir()
        # A stale library must not turn a failed configure/build into success.
        (failed_build / "libfilekit.a").write_bytes((build_dir / "libfilekit.a").read_bytes())
        environment = dict(os.environ, **{tool: "false"})
        result = subprocess.run(["bash", "build.sh", str(failed_build)], cwd=root,
                                env=environment, capture_output=True, text=True)
        assert result.returncode != 0, (tool, result.stdout, result.stderr)
    print("Filekit build failure checks passed.")
