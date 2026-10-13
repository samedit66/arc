"""Compile external consumers using pkg-config's shell-escaped flag lists."""
import os
from pathlib import Path
import shlex
import subprocess
import sys

work, root = map(Path, sys.argv[1:])


def flags(*arguments):
    return shlex.split(subprocess.check_output(["pkg-config", *arguments, "arc"], text=True))


cflags = flags("--cflags")
libraries = flags("--libs")
static_libraries = flags("--static", "--libs")
if sys.platform == "darwin":
    prefix = subprocess.check_output(["brew", "--prefix"], text=True).strip()
    static_libraries.insert(0, f"-L{prefix}/lib")
platform_libraries = ["-lgdi32"] if os.name == "nt" else []
for language, compiler in [("c", os.environ.get("CC", "cc")), ("cpp", os.environ.get("CXX", "c++"))]:
    source = work / f"main.{language}"
    source.write_bytes((root / "tests/consumer/main.c").read_bytes())
    executable = work / ("consumer.exe" if os.name == "nt" else "consumer")
    for header_first in [0, 1]:
        for link_flags in [libraries, static_libraries]:
            subprocess.run([compiler, *cflags, f"-DARC_HEADER_FIRST={header_first}", str(source),
                            *link_flags, *platform_libraries, "-o", str(executable)], check=True)
            subprocess.run([str(executable)], cwd=work, check=True)
            (work / "consumer.tar").unlink()
