from pathlib import Path
import json
import os
import shlex
import shutil
import subprocess
import sys
import textwrap

import pytest


VALIDATOR = Path(__file__).resolve().parents[1] / "scripts/check-release-tag.sh"


def git(repo, *args):
    return subprocess.check_output(["git", *args], cwd=repo, text=True).strip()


@pytest.fixture
def release_repo(tmp_path):
    git(tmp_path, "init", "-b", "main")
    git(tmp_path, "config", "user.name", "Release test")
    git(tmp_path, "config", "user.email", "release@example.invalid")
    git(tmp_path, "config", "commit.gpgsign", "false")
    git(tmp_path, "config", "tag.gpgsign", "false")
    (tmp_path / "VERSION").write_text("0.1.0\n", newline="\n")
    git(tmp_path, "add", "VERSION")
    git(tmp_path, "commit", "-m", "Initial version")
    commit = git(tmp_path, "rev-parse", "HEAD")
    git(tmp_path, "update-ref", "refs/remotes/origin/main", commit)
    return tmp_path, commit


def validate(repo, tag, commit):
    # Windows command-line parsing can discard a newline in an unquoted
    # argument. Keep the test input intact through the environment boundary.
    return subprocess.run(
        [os.environ.get("MISE_BASH_PATH", "bash"), "-c",
         'exec "$BASH" "$1" "$ARC_TEST_RELEASE_TAG" "$2"',
         "release-test", VALIDATOR.as_posix(), commit],
        cwd=repo, text=True, capture_output=True,
        env={**os.environ, "ARC_TEST_RELEASE_TAG": tag},
    )


@pytest.mark.parametrize("annotated", [False, True])
def test_release_tag_matches_committed_version(release_repo, annotated):
    repo, commit = release_repo
    args = ["tag", "v0.1.0"]
    if annotated:
        args += ["-a", "-m", "Release 0.1.0"]
    git(repo, *args)
    # The working tree and newer main versions cannot change a tagged version.
    (repo / "VERSION").write_text("0.2.0\n", newline="\n")
    git(repo, "commit", "-am", "Next version")
    git(repo, "update-ref", "refs/remotes/origin/main", "HEAD")
    result = validate(repo, "v0.1.0", commit)
    assert result.returncode == 0, result.stderr
    assert f"Validated v0.1.0 at {commit}" in result.stdout


@pytest.mark.parametrize("tag", [
    "0.1.0", "v01.1.0", "v0.01.0", "v0.1.00", "v0.1", "v0.1.0-rc.1",
    "v0.1.0+build.1", "v0.1.0;echo", "v0.1.0\n",
])
def test_release_rejects_malformed_tag(release_repo, tag):
    repo, commit = release_repo
    result = validate(repo, tag, commit)
    assert result.returncode != 0
    assert "Expected a stable tag" in result.stderr


def test_release_rejects_version_mismatch(release_repo):
    repo, commit = release_repo
    git(repo, "tag", "v0.2.0")
    result = validate(repo, "v0.2.0", commit)
    assert result.returncode != 0
    assert "disagrees with VERSION" in result.stderr


def test_release_rejects_commit_outside_main(release_repo):
    repo, _ = release_repo
    git(repo, "checkout", "-b", "unmerged")
    git(repo, "commit", "--allow-empty", "-m", "Unmerged work")
    commit = git(repo, "rev-parse", "HEAD")
    git(repo, "tag", "v0.1.0")
    result = validate(repo, "v0.1.0", commit)
    assert result.returncode != 0
    assert "must belong to main history" in result.stderr


def test_release_rejects_moved_tag(release_repo):
    repo, commit = release_repo
    git(repo, "tag", "v0.1.0")
    git(repo, "commit", "--allow-empty", "-m", "Another commit")
    git(repo, "tag", "-f", "v0.1.0")
    result = validate(repo, "v0.1.0", commit)
    assert result.returncode != 0
    assert "no longer points to the checked commit" in result.stderr


def test_release_rejects_missing_tag(release_repo):
    repo, commit = release_repo
    assert validate(repo, "v0.1.0", commit).returncode != 0


@pytest.mark.skipif(os.name == "nt", reason="Publication job runs on Linux")
@pytest.mark.parametrize("mode", ["generated", "curated", "invalid", "published", "unrelated", "resume"])
def test_release_publication_title_and_draft_ownership(release_repo, mode):
    repo, commit = release_repo
    git(repo, "tag", "v0.1.0")
    (repo / "scripts").mkdir()
    shutil.copyfile(VALIDATOR, repo / "scripts/check-release-tag.sh")
    assets = repo / "release-assets"
    assets.mkdir()
    for name in ("arc-test.tar.gz", "arc-test.zip"):
        (assets / name).write_text("test package", newline="\n")
    title = "Arc v0.1.0 — Named Release"
    if mode != "generated":
        (repo / "release").mkdir()
        heading = title if mode == "invalid" else f"# {title}"
        (repo / "release/notes-0.1.0.md").write_text(f"{heading}\n\nCurated notes.\n", newline="\n")
    tools = repo / "tools"
    tools.mkdir()
    git_program = shutil.which("git")
    assert git_program
    # Keep tag/ancestry checks real; only the network fetch is suppressed.
    (tools / "git").write_text(
        '#!/bin/bash\nif [[ "$1" == fetch ]]; then exit 0; fi\n'
        f'exec {shlex.quote(git_program)} "$@"\n', newline="\n")
    (tools / "gh").write_text(f"#!{sys.executable}\n" + '''
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
mode = os.environ["ARC_RELEASE_TEST_MODE"]
record = {"args": args}
if "--notes-file" in args:
    record["body"] = Path(args[args.index("--notes-file") + 1]).read_text()
with open(os.environ["ARC_RELEASE_TEST_LOG"], "a") as stream:
    stream.write(json.dumps(record) + "\\n")
if args[:2] == ["release", "view"]:
    if mode not in ("published", "unrelated", "resume"):
        sys.exit(1)
    if "body" in args:
        print("unrelated draft" if mode == "unrelated" else
              "<!-- arc-release:" + os.environ["RELEASE_COMMIT"] + " -->")
    else:
        print("false\\tfalse" if mode == "published" else "true\\tfalse")
''', newline="\n")
    for tool in tools.iterdir():
        tool.chmod(0o755)
    log = repo / "gh.jsonl"
    workflow = (VALIDATOR.parents[1] / ".github/workflows/ci.yml").read_text()
    publish = textwrap.dedent(workflow.rsplit("        run: |\n", 1)[1])
    result = subprocess.run(["bash", "-euo", "pipefail", "-c", publish], cwd=repo,
                            text=True, capture_output=True, env={
                                **os.environ, "PATH": f"{tools}{os.pathsep}{os.environ['PATH']}",
                                "GITHUB_WORKSPACE": str(repo), "RUNNER_TEMP": str(repo),
                                "RELEASE_TAG": "v0.1.0", "RELEASE_COMMIT": commit,
                                "ARC_RELEASE_TEST_MODE": mode, "ARC_RELEASE_TEST_LOG": str(log),
                            })
    records = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
    mutations = [r for r in records if r["args"][:2] != ["release", "view"]]
    if mode in ("invalid", "unrelated"):
        assert result.returncode != 0
        assert not mutations
        return
    assert result.returncode == 0, result.stderr
    if mode == "published":
        assert not mutations
        return
    expected_title = "v0.1.0" if mode == "generated" else title
    edit = mutations[-1]["args"]
    assert edit[:3] == ["release", "edit", "v0.1.0"]
    assert edit[edit.index("--title") + 1] == expected_title
    assert "--draft=false" in edit
    first = mutations[0]
    assert first["args"][1] == ("upload" if mode == "resume" else "create")
    if mode == "curated":
        assert first["args"][first["args"].index("--title") + 1] == title
        assert first["body"] == f"\nCurated notes.\n\n<!-- arc-release:{commit} -->\n"
    elif mode == "generated":
        assert "--generate-notes" in first["args"]
        assert first["args"][first["args"].index("--notes") + 1] == f"<!-- arc-release:{commit} -->"


@pytest.mark.parametrize("relocated", [False, True])
def test_musl_release_overrides_inherited_system_compiler(tmp_path, relocated):
    root = VALIDATOR.parents[1]
    work = tmp_path / "build"
    vcpkg = work / "vcpkg"
    (vcpkg / ".git").mkdir(parents=True)
    for directory in ("linux", "asm", "asm-generic"):
        (work / "kernel-headers" / directory).mkdir(parents=True)
    tools = tmp_path / "tools"
    tools.mkdir()

    def script(path, source):
        path.write_text("#!/bin/sh\nset -eu\n" + source, newline="\n")
        path.chmod(0o755)

    script(tools / "git", "exit 0\n")
    script(tools / "musl-gcc", "exit 0\n")
    if relocated:
        (work / "arc").mkdir()
        (work / "arc/CMakeCache.txt").write_text(
            "VCPKG_TARGET_TRIPLET:STRING=x64-linux-musl\n"
            f"CMAKE_C_COMPILER:FILEPATH={(tools / 'musl-gcc').as_posix()}\n"
            "CMAKE_HOME_DIRECTORY:INTERNAL=/old/checkout\n"
            "CMAKE_CACHEFILE_DIR:INTERNAL=/old/checkout/build/arc\n",
            newline="\n",
        )
    script(vcpkg / "bootstrap-vcpkg.sh", "exit 0\n")
    script(vcpkg / "vcpkg", '[ "$CC" = musl-gcc ] || exit 7\n')
    script(tools / "cmake", '''
[ "$CC" = musl-gcc ] || exit 8
case "$1" in
    --build)
        mkdir -p "$ARC_RELEASE_BUILD_DIR/arc"
        printf '#!/bin/sh\\nprintf "arc %s\\\\n"\\n' "$ARC_TEST_VERSION" > "$ARC_RELEASE_BUILD_DIR/arc/arc"
        chmod 755 "$ARC_RELEASE_BUILD_DIR/arc/arc" ;;
    *) printf '%s\\n' "$@" > "$ARC_TEST_FLAGS" ;;
esac
''')
    script(tools / "readelf", '''
if [ "$1" = -hW ]; then printf '  Machine: Advanced Micro Devices X86-64\\n'; fi
''')
    flags = tmp_path / "flags"
    env = dict(os.environ, CC="gcc", ARC_RELEASE_BUILD_DIR=work.as_posix(),
               ARC_TEST_VERSION=(root / "VERSION").read_text().strip(),
               ARC_TEST_FLAGS=flags.as_posix(),
               PATH=str(tools) + os.pathsep + os.environ["PATH"])
    result = subprocess.run(
        [os.environ.get("MISE_BASH_PATH", "bash"),
         (root / "scripts/build-release.sh").as_posix(), "linux-x86_64"],
        env=env, capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "VCPKG_CHAINLOAD_TOOLCHAIN_FILE=" in flags.read_text()
    assert "musl-toolchain.cmake" in flags.read_text()
    assert ("--fresh" in flags.read_text()) == relocated
