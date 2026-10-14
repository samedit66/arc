from pathlib import Path
import os
import subprocess

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
