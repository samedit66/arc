"""Compile native packing fault probe against the prepared POSIX deployment metadata."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def main():
    root = Path(__file__).resolve().parents[1]
    native = root / "build/native"
    namespace = {"e": "http://www.eiffel.com/developers/xml/configuration-1-23-0"}
    target = ET.parse(native / "native.ecf").find("e:target", namespace)
    arguments = shlex.split(os.environ.get("CC", "cc"))
    arguments += ["-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-UNDEBUG"]
    arguments += ["-I" + str(root / "src/core/c"), str(root / "tests/pack_native.c")]

    for element in target:
        tag = element.tag.rsplit("}", 1)[-1]
        location = element.get("location", "").replace("$ECF_CONFIG_PATH", str(native))
        if tag == "external_include":
            arguments.append("-I" + location)
        elif tag == "external_library":
            arguments.append(location)
        elif tag in ("external_cflag", "external_linker_flag"):
            arguments += shlex.split(element.get("value", ""))

    with tempfile.TemporaryDirectory() as temporary:
        executable = Path(temporary) / "pack_native"
        subprocess.run(arguments + ["-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

        print("Native packing failure and locale checks passed")


if __name__ == "__main__":
    main()
