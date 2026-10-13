import os
import stat
import zipfile

import pytest

from support import directory_entry, fifo_entry, file_entry, link_entry


@pytest.mark.parametrize("suffix,mode", [("tar", "w"), ("tar.gz", "w:gz"), ("tar.bz2", "w:bz2"), ("tar.xz", "w:xz"), ("zip", None)])
def test_extracts_python_archive(arc, make_tar, tmp_path, suffix, mode):
    if mode:
        archive = make_tar([
            directory_entry("empty"), directory_entry("sub"),
            file_entry("sub/текст", b"unicode", mode=0o751),
            file_entry(".hidden", b"hidden"), file_entry("zero", b""),
        ], f"input.{suffix}", mode=mode)
    else:
        archive = tmp_path / "input.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("empty/", b"")
            output.writestr("sub/текст", b"unicode")
            output.writestr(".hidden", b"hidden")
            output.writestr("zero", b"")
    destination = tmp_path / "output"
    arc("unpack", archive, destination)
    assert (destination / "empty").is_dir()
    assert (destination / "sub/текст").read_bytes() == b"unicode"
    assert (destination / ".hidden").read_bytes() == b"hidden"
    assert (destination / "zero").read_bytes() == b""
    if mode and os.name != "nt":
        assert stat.S_IMODE((destination / "sub/текст").stat().st_mode) == 0o751


@pytest.mark.parametrize("name,format_name,expected", [
    ("named.zip", None, "named"),
    ("wrapped.tar", None, "wrapped"),
    ("unknown.bin", "zip", "unknown.bin.extracted"),
])
def test_default_destination(arc, tmp_path, name, format_name, expected):
    archive = tmp_path / name
    if name.endswith("zip") or format_name == "zip":
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("value", b"data")
    else:
        from support import write_tar
        write_tar(archive, [file_entry("value")])
    arc("unpack", name)
    assert (tmp_path / expected / "value").read_bytes() == b"data"
    arc("unpack", name, success=False)
    assert (tmp_path / expected / "value").read_bytes() == b"data"


def test_existing_file_is_preserved(arc, simple_tar, tmp_path):
    destination = tmp_path / "output"
    destination.mkdir()
    (destination / "value").write_bytes(b"keep")
    arc("unpack", simple_tar, destination, success=False)
    assert (destination / "value").read_bytes() == b"keep"


def test_merge_preserves_existing_directory_metadata(arc, make_tar, tmp_path):
    destination = tmp_path / "merge"
    (destination / "sub").mkdir(parents=True)
    (destination / "unrelated").write_bytes(b"keep")
    directories = [destination, destination / "sub"]
    for directory in directories:
        if os.name != "nt":
            directory.chmod(0o700)
        os.utime(directory, (1000, 1000))
    before = [directory.stat() for directory in directories]
    archive = make_tar([
        directory_entry("sub", mode=0o755, mtime=2000),
        file_entry("new", b"x", mtime=2000),
        file_entry("sub/new", b"x", mtime=2000),
    ])
    arc("unpack", archive, destination)
    assert (destination / "unrelated").read_bytes() == b"keep"
    for directory, original in zip(directories, before):
        current = directory.stat()
        if os.name != "nt":
            assert stat.S_IMODE(current.st_mode) == stat.S_IMODE(original.st_mode)
            assert (current.st_uid, current.st_gid) == (original.st_uid, original.st_gid)
            assert current.st_mtime > 2000


def test_extract_into_current_directory(arc, simple_tar, tmp_path):
    arc("unpack", simple_tar.name, ".")
    assert (tmp_path / "value").read_bytes() == b"payload"
    assert simple_tar.is_file()


def test_forward_hardlink(arc, make_tar, tmp_path):
    archive = make_tar([link_entry("alias", "value", hard=True), file_entry("value", b"shared")])
    arc("unpack", archive, "output")
    assert os.path.samefile(tmp_path / "output/alias", tmp_path / "output/value")


def test_safe_symbolic_link_chain(arc, make_tar, tmp_path, symlinks_available):
    archive = make_tar([
        link_entry("sub/link", "../value"), link_entry("chain", "sub/link"),
        file_entry("value", b"safe"),
    ])
    arc("unpack", archive, "output")
    assert (tmp_path / "output/sub/link").is_symlink()
    assert (tmp_path / "output/chain").is_symlink()
    assert (tmp_path / "output/chain").read_bytes() == b"safe"


@pytest.mark.parametrize("case", [
    "traversal", "absolute", "external-link", "absolute-link", "link-parent",
    "link-cycle", "missing-hard", "hard-cycle", "duplicate-file", "special",
])
def test_unsafe_archive_is_rejected(arc, make_tar, tmp_path, case):
    outside = tmp_path / "outside"
    outside.mkdir()
    sentinel = outside / "sentinel"
    sentinel.write_bytes(b"keep")
    records = {
        "traversal": [file_entry("../outside/sentinel", b"bad")],
        "absolute": [file_entry(sentinel.as_posix(), b"bad")],
        "external-link": [link_entry("escape", "../outside/sentinel")],
        "absolute-link": [link_entry("escape", sentinel.as_posix())],
        "link-parent": [link_entry("redirect", "."), file_entry("redirect/file", b"bad")],
        "link-cycle": [link_entry("a", "b"), link_entry("b", "a")],
        "missing-hard": [link_entry("a", "absent", hard=True)],
        "hard-cycle": [link_entry("a", "b", hard=True), link_entry("b", "a", hard=True)],
        "duplicate-file": [file_entry("a", b"one"), file_entry("a", b"two")],
        "special": [fifo_entry("pipe")],
    }
    archive = make_tar(records[case])
    arc("unpack", archive, "destination", success=False)
    assert sentinel.read_bytes() == b"keep"
    assert set(outside.iterdir()) == {sentinel}


def test_hardlink_cannot_use_unrelated_existing_file(arc, make_tar, tmp_path):
    archive = make_tar([link_entry("alias", "value", hard=True)])
    destination = tmp_path / "output"
    destination.mkdir()
    (destination / "value").write_bytes(b"unrelated")
    arc("unpack", archive, destination, success=False)
    assert not (destination / "alias").exists()
    assert (destination / "value").read_bytes() == b"unrelated"


def test_existing_symlink_parent_is_rejected(arc, make_tar, tmp_path, symlinks_available):
    outside = tmp_path / "outside"
    outside.mkdir()
    (outside / "sentinel").write_bytes(b"keep")
    destination = tmp_path / "output"
    destination.mkdir()
    (destination / "sub").symlink_to(outside, target_is_directory=True)
    archive = make_tar([file_entry("sub/value", b"bad")])
    arc("unpack", archive, destination, success=False)
    assert not (outside / "value").exists()
    assert (outside / "sentinel").read_bytes() == b"keep"


def test_dangling_symlink(arc, make_tar, tmp_path, symlinks_available):
    archive = make_tar([link_entry("dangling", "missing")])
    arc("unpack", archive, "output")
    assert (tmp_path / "output/dangling").is_symlink()
    assert not (tmp_path / "output/dangling").exists()


@pytest.mark.skipif(os.name != "nt", reason="Windows path restrictions")
@pytest.mark.parametrize("name", ["CON.txt", "LPT¹", "trailing.", "stream:name", "bad?name"])
def test_windows_invalid_names(arc, make_tar, name):
    archive = make_tar([file_entry(name, b"bad")])
    arc("unpack", archive, "output", success=False)


def test_hardlink_to_directory_is_rejected(arc, make_tar):
    archive = make_tar([link_entry("alias", "directory", hard=True), directory_entry("directory")])
    arc("unpack", archive, "output", success=False)


@pytest.mark.skipif(os.name != "nt", reason="Windows extended path support")
def test_windows_long_unicode_paths(arc, tmp_path):
    parent = tmp_path / ("ю" * 30)
    for index in range(6):
        parent = parent / (f"nested-{index}" + "x" * 40)
    from pathlib import Path
    parent = Path("\\\\?\\" + str(parent))
    parent.mkdir(parents=True)
    source = parent / "source"
    source.mkdir()
    (source / "текст").write_bytes(b"long path")
    archive = parent / "тест.tar"
    arc("pack", archive, source)
    arc("verify", archive)
    arc("unpack", archive, parent / "output")
    assert (parent / "output/текст").read_bytes() == b"long path"
