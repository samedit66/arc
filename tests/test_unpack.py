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
    "traversal", "absolute", "link-parent", "missing-hard", "hard-cycle", "duplicate-file", "special",
])
def test_unsafe_archive_is_rejected(arc, make_tar, tmp_path, case):
    outside = tmp_path / "outside"
    outside.mkdir()
    sentinel = outside / "sentinel"
    sentinel.write_bytes(b"keep")
    records = {
        "traversal": [file_entry("../outside/sentinel", b"bad")],
        "absolute": [file_entry(sentinel.as_posix(), b"bad")],
        "link-parent": [link_entry("redirect", "."), file_entry("redirect/file", b"bad")],
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
    # Windows creation needs a file/directory target kind. Arc rejects
    # unresolved targets instead of guessing; POSIX allows dangling links.
    result = arc("unpack", archive, "output", success=os.name != "nt")
    if os.name == "nt":
        assert "Unknown Windows link target type" in result.stderr
        assert not (tmp_path / "output/dangling").is_symlink()
        return
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
@pytest.mark.parametrize("extended_prefix", [False, True])
def test_windows_long_unicode_paths(arc, tmp_path, extended_prefix):
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
    # Create the fixture with an extended path, then exercise both CLI
    # destination spellings. Arc must handle long paths in either form.
    destination = parent / "output"
    if not extended_prefix:
        destination = Path(str(destination).removeprefix("\\\\?\\"))
    arc("unpack", archive, destination)
    assert (parent / "output/текст").read_bytes() == b"long path"


@pytest.mark.skipif(os.name == "nt", reason="POSIX untyped symbolic links")
@pytest.mark.parametrize("suffix", ["tar", "zip"])
def test_symbolic_targets_preserved(arc, make_tar, tmp_path, suffix):
    import stat
    import zipfile

    outside = tmp_path / "outside"
    outside.mkdir()
    sentinel = outside / "sentinel"
    sentinel.write_bytes(b"keep")
    targets = {
        "absolute": sentinel.as_posix(), "external": "../outside/sentinel",
        "dangling": "missing", "self": "self", "a": "b", "b": "a",
        "through-file": "value/child", "spelling": "./missing/../value",
    }
    (outside / "existing").symlink_to("sentinel")
    targets["existing-link"] = "../outside/existing"
    if suffix == "tar":
        archive = make_tar([file_entry("value"), *[
            link_entry(name, target) for name, target in targets.items()
        ]])
    else:
        archive = tmp_path / "input.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("value", b"data")
            for name, target in targets.items():
                entry = zipfile.ZipInfo(name)
                entry.create_system = 3
                entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                output.writestr(entry, target)
    arc("unpack", archive, "output")
    for name, target in targets.items():
        assert os.readlink(tmp_path / "output" / name) == target
    assert sentinel.read_bytes() == b"keep"
    assert set(outside.iterdir()) == {sentinel, outside / "existing"}


@pytest.mark.parametrize("link_first", [True, False])
def test_external_link_cannot_be_parent(arc, make_tar, tmp_path, link_first):
    outside = tmp_path / "outside"
    outside.mkdir()
    sentinel = outside / "sentinel"
    sentinel.write_bytes(b"keep")
    records = [link_entry("redirect", "../outside"), file_entry("redirect/sentinel", b"bad")]
    archive = make_tar(records if link_first else records[::-1])
    arc("unpack", archive, "output", success=False)
    assert sentinel.read_bytes() == b"keep"
    assert set(outside.iterdir()) == {sentinel}


def test_existing_dangling_link_is_not_overwritten(arc, make_tar, tmp_path, symlinks_available):
    output = tmp_path / "output"
    output.mkdir()
    (output / "alias").symlink_to("missing")
    archive = make_tar([link_entry("alias", "value"), file_entry("value")])
    arc("unpack", archive, output, success=False)
    assert os.readlink(output / "alias") == "missing"


def test_internal_directory_symbolic_links(arc, make_tar, tmp_path, symlinks_available):
    archive = make_tar([
        link_entry("alias", "sub"), link_entry("chain", "alias"),
        link_entry("relative", "alias/../value"), file_entry("sub/child"),
        file_entry("value", b"safe"),
    ])
    arc("unpack", archive, "output")
    assert (tmp_path / "output/chain/child").read_bytes() == b"data"
    assert (tmp_path / "output/relative").read_bytes() == b"safe"


@pytest.mark.skipif(os.name != "nt", reason="Windows requires known link types")
@pytest.mark.parametrize("target", ["../outside", "/outside", "alias", "value/child"])
def test_windows_unknown_symbolic_kind(arc, make_tar, target):
    archive = make_tar([link_entry("alias", target), file_entry("value")])
    result = arc("unpack", archive, "output", success=False)
    assert "Unknown Windows link target type" in result.stderr


@pytest.mark.skipif(os.name != "nt", reason="Windows type inference uses archive records")
def test_windows_link_kind_does_not_use_existing_target(arc, make_tar, tmp_path):
    output = tmp_path / "output"
    output.mkdir()
    (output / "existing").write_bytes(b"keep")
    archive = make_tar([link_entry("alias", "existing")])
    result = arc("unpack", archive, output, success=False)
    assert "Unknown Windows link target type" in result.stderr
    assert (output / "existing").read_bytes() == b"keep"
    assert not (output / "alias").is_symlink()


@pytest.mark.parametrize("conflict", ["mode", "mtime"])
def test_conflicting_hardlink_metadata_is_rejected(arc, make_tar, conflict):
    target = file_entry("value", b"shared")
    alias = link_entry("alias", "value", hard=True)
    if conflict == "mode":
        alias[0].mode = target[0].mode ^ 0o100
    else:
        alias[0].pax_headers["mtime"] = "0.123456789"
    archive = make_tar([target, alias])
    result = arc("unpack", archive, "output", success=False)
    assert "Conflicting hard-link metadata" in result.stderr
