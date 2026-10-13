import os
import select
import subprocess
import sys
import time

import pytest


@pytest.mark.parametrize("no_color", ["", "0", "1"])
def test_redirected_messages_are_plain(arc, simple_tar, no_color):
    environment = {"NO_COLOR": no_color, "TERM": "xterm-256color"}
    packed = arc("pack", "copy.tar", simple_tar, env=environment)
    unpacked = arc("unpack", simple_tar, "restored", env=environment)
    verified = arc("verify", simple_tar, env=environment)
    assert packed.stdout == "Created copy.tar\n"
    assert unpacked.stdout == "Extracted to restored\n"
    assert verified.stdout == "Content readable; available integrity checks passed.\n"
    failed = arc("verify", "missing.tar", success=False, env=environment)
    usage = arc("pack", success=False, env=environment)
    for result in (packed, unpacked, verified, failed, usage):
        assert "\x1b" not in result.stdout + result.stderr
        assert not any(marker in result.stdout + result.stderr for marker in ("✅", "❌", "⚠"))
    assert failed.stderr.startswith("Error:")
    assert usage.stderr.startswith("Error: invalid arguments for pack.\nUsage:")


def terminal_run(binary, arguments, cwd, *, stream="stdout", no_color=None, term="xterm-256color", utf8=True):
    import pty

    environment = os.environ.copy()
    utf8_locale = "en_US.UTF-8" if sys.platform == "darwin" else "C.UTF-8"
    environment.update(TERM=term, LC_ALL=utf8_locale if utf8 else "C")
    environment.pop("NO_COLOR", None)
    if no_color is not None:
        environment["NO_COLOR"] = no_color
    master, slave = pty.openpty()
    process = None
    try:
        process = subprocess.Popen(
            [str(binary), *map(str, arguments)], cwd=cwd, env=environment,
            stdout=slave if stream == "stdout" else subprocess.PIPE,
            stderr=slave if stream == "stderr" else subprocess.PIPE,
        )
        # Drain while running: help can exceed the PTY buffer. Keep the slave
        # open until drained, since macOS discards unread data on its close.
        chunks = []
        deadline = time.monotonic() + 30
        while True:
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(process.args, 30)
            ready = select.select([master], [], [], 0.1)[0]
            if ready:
                chunks.append(os.read(master, 4096))
            elif process.poll() is not None:
                break
        stdout, stderr = process.communicate(timeout=max(0.1, deadline - time.monotonic()))
        result = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        return result, b"".join(chunks).decode("utf-8").replace("\r\n", "\n")
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.communicate()
        os.close(slave)
        os.close(master)


@pytest.mark.skipif(os.name == "nt", reason="POSIX pseudoterminal test")
@pytest.mark.parametrize("no_color", [None, "", "0", "1"])
@pytest.mark.parametrize("failure", [False, True])
def test_terminal_messages(arc_binary, simple_tar, tmp_path, no_color, failure):
    result, output = terminal_run(
        arc_binary, ["verify", "missing.tar" if failure else simple_tar], tmp_path,
        stream="stderr" if failure else "stdout", no_color=no_color,
    )
    assert result.returncode == (1 if failure else 0)
    assert (result.stdout if failure else result.stderr) == b""
    assert output.startswith("❌ " if failure else "✅ ")
    if no_color:
        assert "\x1b" not in output
    else:
        assert ("\x1b[31mError:\x1b[0m" if failure else "\x1b[32mContent readable\x1b[0m") in output
    assert output.count("\n") == 1


@pytest.mark.skipif(os.name == "nt", reason="POSIX pseudoterminal test")
@pytest.mark.parametrize("term,utf8", [("dumb", True), ("xterm", False)])
def test_terminal_fallback(arc_binary, simple_tar, tmp_path, term, utf8):
    result, output = terminal_run(arc_binary, ["verify", simple_tar], tmp_path, term=term, utf8=utf8)
    assert result.returncode == 0
    assert "✅" not in output
    if term == "dumb":
        assert "\x1b" not in output
    else:
        assert "\x1b[32m" in output


@pytest.mark.skipif(os.name == "nt", reason="POSIX pseudoterminal test")
@pytest.mark.parametrize("command", ["pack", "unpack"])
def test_terminal_success_paths_are_uncolored(arc_binary, simple_tar, tmp_path, command):
    arguments = ["pack", "copy.tar", simple_tar] if command == "pack" else ["unpack", simple_tar, "restored"]
    result, output = terminal_run(arc_binary, arguments, tmp_path)
    assert result.returncode == 0
    assert result.stderr == b""
    expected = "✅ \x1b[32mCreated\x1b[0m copy.tar\n" if command == "pack" else "✅ \x1b[32mExtracted to\x1b[0m restored\n"
    assert output == expected


@pytest.mark.skipif(os.name == "nt", reason="POSIX pseudoterminal test")
@pytest.mark.parametrize("command", ["list", "info", "--help", "--version"])
def test_terminal_data_and_help_are_plain(arc_binary, simple_tar, tmp_path, command):
    arguments = [command, simple_tar] if command in ("list", "info") else [command]
    result, output = terminal_run(arc_binary, arguments, tmp_path)
    assert result.returncode == 0
    assert output
    assert "\x1b" not in output
    assert not any(marker in output for marker in ("✅", "❌", "⚠"))


@pytest.mark.skipif(os.name == "nt", reason="POSIX pseudoterminal test")
def test_terminal_usage_error_keeps_help_plain(arc_binary, tmp_path):
    result, output = terminal_run(arc_binary, ["pack"], tmp_path, stream="stderr")
    assert result.returncode == 2
    assert result.stdout == b""
    assert output.startswith("❌ \x1b[31mError:\x1b[0m invalid arguments for pack.\nUsage:")
    assert output.count("\x1b") == 2
