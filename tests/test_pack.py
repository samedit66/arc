import os
import stat
from pathlib import Path
import tarfile
import zipfile

import pytest

from support import assert_tree_matches


@pytest.mark.parametrize("suffix", ["tar", "tar.gz", "tar.bz2", "tar.xz", "zip"])
def test_python_reads_packed_contents(arc, source_tree, tmp_path, suffix):
    output = tmp_path / f"packed.{suffix}"
    arc("pack", output, source_tree)
    if suffix == "zip":
        with zipfile.ZipFile(output) as archive:
            names = set(archive.namelist())
            read = archive.read
            assert names == {"empty/", "sub/", "plain.txt", ".hidden", "sub/text-текст.txt", "zero"}
            assert archive.getinfo("sub/text-текст.txt").flag_bits & 0x800
            for path in source_tree.rglob("*"):
                if path.is_file():
                    assert read(path.relative_to(source_tree).as_posix()) == path.read_bytes()
    else:
        with tarfile.open(output) as archive:
            assert {entry.name.rstrip("/") for entry in archive} == {
                "empty", "sub", "plain.txt", ".hidden", "sub/text-текст.txt", "zero",
            }
            for path in source_tree.rglob("*"):
                if path.is_file():
                    assert archive.extractfile(path.relative_to(source_tree).as_posix()).read() == path.read_bytes()
            if os.name != "nt":
                assert archive.getmember("plain.txt").mode == 0o751


@pytest.mark.parametrize("suffix", ["tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst", "zip", "7z"])
def test_format_roundtrip(arc, source_tree, tmp_path, suffix):
    archive = tmp_path / f"roundtrip.{suffix}"
    destination = tmp_path / "output"
    arc("pack", archive, source_tree)
    arc("unpack", archive, destination)
    assert_tree_matches(source_tree, destination)
    if os.name != "nt":
        assert (destination / "plain.txt").stat().st_mode & 0o777 == 0o751
    arc("verify", archive)
    assert "plain.txt" in arc("list", archive).stdout
    assert "Files: 4" in arc("info", archive).stdout


@pytest.mark.parametrize("suffix", ["tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst", "zip", "7z"])
def test_existing_archive_is_preserved(arc, source_tree, tmp_path, suffix):
    archive = tmp_path / f"existing.{suffix}"
    archive.write_bytes(b"existing archive sentinel")
    arc("pack", archive, source_tree, success=False)
    assert archive.read_bytes() == b"existing archive sentinel"


@pytest.mark.parametrize("keep_root", [False, True])
def test_inferred_source(arc, source_tree, tmp_path, keep_root):
    arguments = ["--keep-root"] if keep_root else []
    arc("pack", *arguments, "source.tar")
    with tarfile.open(tmp_path / "source.tar") as archive:
        name = "source/plain.txt" if keep_root else "plain.txt"
        assert archive.extractfile(name).read() == (source_tree / "plain.txt").read_bytes()
        assert ("source" in archive.getnames()) == keep_root


def test_inferred_source_uses_working_directory(arc, source_tree, tmp_path):
    wrong = tmp_path / "archive-output/source"
    wrong.mkdir(parents=True)
    (wrong / "wrong.txt").write_bytes(b"wrong")
    arc("pack", "archive-output/source.tar.gz")
    with tarfile.open(tmp_path / "archive-output/source.tar.gz") as archive:
        assert "wrong.txt" not in archive.getnames()
        assert archive.extractfile("plain.txt").read() == (source_tree / "plain.txt").read_bytes()


def test_inferred_empty_source(arc, tmp_path):
    (tmp_path / "empty").mkdir()
    arc("pack", "empty.zip")
    with zipfile.ZipFile(tmp_path / "empty.zip") as archive:
        assert archive.namelist() == []


@pytest.mark.parametrize("name", ["missing.zip", "file-source.zip", ".zip", "..zip", "...zip"])
def test_invalid_inferred_source(arc, tmp_path, name):
    (tmp_path / "file-source").write_bytes(b"file")
    result = arc("pack", name, success=False)
    assert "specify sources explicitly" in result.stderr.lower()
    assert not (tmp_path / name).exists()


def test_inferred_symlink_source_is_rejected(arc, source_tree, tmp_path, symlinks_available):
    (tmp_path / "linked").symlink_to(source_tree, target_is_directory=True)
    arc("pack", "linked.zip", success=False)
    assert not (tmp_path / "linked.zip").exists()


def test_explicit_format_for_unknown_suffix(arc, source_tree, tmp_path):
    arc("pack", "--format", "zip", "unknown.bin", source_tree)
    with zipfile.ZipFile(tmp_path / "unknown.bin") as archive:
        assert archive.read(".hidden") == b"hidden"


def test_explicit_format_with_inferred_source(arc, tmp_path):
    source = tmp_path / "format-source.bin"
    source.mkdir()
    (source / "data").write_bytes(b"explicit format")
    (tmp_path / "outputs").mkdir()
    arc("pack", "--format", "zip", "outputs/format-source.bin")
    with zipfile.ZipFile(tmp_path / "outputs/format-source.bin") as archive:
        assert archive.read("data") == b"explicit format"


@pytest.mark.parametrize("arguments", [
    ["--format", "tar", "bad.zip", "source"],
    ["unknown.bin", "source"],
    ["--format", "rar", "bad.rar", "source"],
    ["--keep-root", "bad.zip", "source", "source/zero"],
    ["duplicate.tar", "source/zero", "source/zero"],
])
def test_invalid_pack_does_not_create_output(arc, source_tree, tmp_path, arguments):
    before = set(tmp_path.iterdir())
    arc("pack", *arguments, success=False)
    assert set(tmp_path.iterdir()) == before


def test_multiple_sources(arc, source_tree, tmp_path):
    arc("pack", "multiple.tar", "source/sub", "source/plain.txt")
    with tarfile.open(tmp_path / "multiple.tar") as archive:
        assert set(archive.getnames()) == {"sub", "sub/text-текст.txt", "plain.txt"}


def test_dash_prefixed_inferred_directory(arc, tmp_path):
    source = tmp_path / "-directory"
    source.mkdir()
    (source / "data").write_bytes(b"dash")
    arc("pack", "--", "-directory.zip")
    with zipfile.ZipFile(tmp_path / "-directory.zip") as archive:
        assert archive.read("data") == b"dash"


def test_output_inside_source_is_excluded(arc, source_tree):
    arc("pack", "self.tar", ".", cwd=source_tree)
    with tarfile.open(source_tree / "self.tar") as archive:
        assert "self.tar" not in archive.getnames()


def test_unicode_under_c_locale(arc, source_tree, tmp_path):
    arc("pack", "locale.tar", source_tree, env={"LC_ALL": "C"})
    arc("unpack", "locale.tar", "output", env={"LC_ALL": "C"})
    assert_tree_matches(source_tree, tmp_path / "output")


def test_hardlinks_in_tar(arc, tmp_path):
    source = tmp_path / "hard-source"
    source.mkdir()
    (source / "a").write_bytes(b"shared")
    os.link(source / "a", source / "b")
    arc("pack", "hard.tar", source)
    with tarfile.open(tmp_path / "hard.tar") as archive:
        entries = archive.getmembers()
        assert sum(entry.islnk() for entry in entries) == 1
    assert "Hard links: 1" in arc("info", "hard.tar").stdout
    arc("unpack", "hard.tar", "output")
    assert os.path.samefile(tmp_path / "output/a", tmp_path / "output/b")


def test_zip_refuses_multiple_hardlinks(arc, tmp_path):
    source = tmp_path / "hard-source"
    source.mkdir()
    (source / "a").write_bytes(b"shared")
    os.link(source / "a", source / "b")
    arc("pack", "hard.zip", source, success=False)
    assert not (tmp_path / "hard.zip").exists()
    arc("pack", "single.zip", source / "a")
    with zipfile.ZipFile(tmp_path / "single.zip") as archive:
        assert archive.read("a") == b"shared"


@pytest.fixture
def link_output(tmp_path):
    destination = tmp_path / "output"
    yield destination
    # ISO can restore read-only directory modes. Change them only after assertions
    # so pytest can remove symlinks without following their targets.
    for directory, _, _ in os.walk(destination, followlinks=False):
        path = Path(directory)
        if not path.is_symlink():
            path.chmod(stat.S_IMODE(path.stat().st_mode) | 0o700)


@pytest.mark.parametrize("suffix,format_name", [("tar", None), ("zip", None), ("7z", None), ("iso", "iso9660")])
def test_symbolic_links_roundtrip(arc, tmp_path, symlinks_available, link_output, suffix, format_name):
    source = tmp_path / "links"
    (source / "sub").mkdir(parents=True)
    (source / "value").write_bytes(b"safe")
    (source / "sub/link").symlink_to("../value")
    (source / "chain").symlink_to("sub/link")
    options = ["--format", format_name] if format_name else []
    arc("pack", *options, f"linked.{suffix}", source)
    arc("unpack", f"linked.{suffix}", link_output)
    assert (tmp_path / "output/chain").is_symlink()
    assert (tmp_path / "output/chain").read_bytes() == b"safe"


@pytest.mark.parametrize("suffix", ["tar", "zip"])
def test_directory_symlink_is_not_traversed(arc, source_tree, tmp_path, symlinks_available, suffix):
    (tmp_path / "directory-link").symlink_to(source_tree / "sub", target_is_directory=True)
    arc("pack", f"linked.{suffix}", "directory-link/")
    listing = arc("list", f"linked.{suffix}").stdout
    assert "symlink" in listing
    assert "text-" not in listing


@pytest.mark.parametrize("name", ["source.TAR.GZ", "source.tgz"])
def test_compound_and_uppercase_suffixes(arc, source_tree, tmp_path, name):
    arc("pack", name)
    with tarfile.open(tmp_path / name) as archive:
        assert archive.extractfile("plain.txt").read() == (source_tree / "plain.txt").read_bytes()


def test_keep_root_with_explicit_directory(arc, source_tree, tmp_path):
    arc("pack", "--keep-root", "wrapped.tar", source_tree)
    with tarfile.open(tmp_path / "wrapped.tar") as archive:
        assert archive.extractfile("source/.hidden").read() == b"hidden"
