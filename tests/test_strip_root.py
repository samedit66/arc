import os

import pytest

from support import directory_entry, file_entry, link_entry


@pytest.mark.parametrize("mode", ["w", "w:gz"])
def test_strip_root_preserves_contents(arc, make_tar, tmp_path, mode):
    archive = make_tar([
        directory_entry("./project"), file_entry("./project/sub/value", b"hello"),
        directory_entry("project/empty"), file_entry("project/.hidden", b"hidden"),
    ], mode=mode)
    arc("unpack", "--strip-root", archive, "output")
    assert (tmp_path / "output/sub/value").read_bytes() == b"hello"
    assert (tmp_path / "output/.hidden").read_bytes() == b"hidden"
    assert (tmp_path / "output/empty").is_dir()
    assert not (tmp_path / "output/project").exists()


@pytest.mark.parametrize("entries", [
    [], [file_entry("value")], [file_entry("a/value"), file_entry("b/value")],
    [directory_entry("root"), file_entry("plain")], [file_entry("root/../escape")],
    [link_entry("root", "other")],
])
def test_invalid_root_is_rejected_before_destination(arc, make_tar, tmp_path, entries):
    archive = make_tar(entries)
    result = arc("unpack", "--strip-root", archive, "output", success=False)
    assert not (tmp_path / "output").exists()
    assert "Some files" not in result.stderr


def test_strip_empty_directory(arc, make_tar, tmp_path):
    archive = make_tar([directory_entry("empty")])
    arc("unpack", "--strip-root", archive, "output")
    assert list((tmp_path / "output").iterdir()) == []


def test_strip_selects_original_names_and_checks_all_roots(arc, make_tar, tmp_path):
    archive = make_tar([file_entry("root/a", b"a"), file_entry("root/b", b"b")])
    arc("unpack", "--strip-root", "--entry", "root/a", archive, "output")
    assert (tmp_path / "output/a").read_bytes() == b"a"
    assert not (tmp_path / "output/b").exists()
    mixed = make_tar([file_entry("root/a"), file_entry("other/b")], name="mixed.tar")
    arc("unpack", "--strip-root", "--entry", "root/a", mixed, "mixed-out", success=False)
    assert not (tmp_path / "mixed-out").exists()


def test_strip_preserves_hardlink_chain(arc, make_tar, tmp_path):
    archive = make_tar([
        link_entry("root/a", "root/b", hard=True),
        link_entry("root/b", "root/value", hard=True), file_entry("root/value"),
    ])
    arc("unpack", "--strip-root", archive, "output")
    assert os.path.samefile(tmp_path / "output/a", tmp_path / "output/value")
    assert os.path.samefile(tmp_path / "output/b", tmp_path / "output/value")


def test_strip_symlinks(arc, make_tar, tmp_path, symlinks_available):
    archive = make_tar([
        link_entry("root/sub/alias", "../value"), file_entry("root/value"),
        link_entry("root/alias", "value"),
    ])
    arc("unpack", "--strip-root", archive, "output")
    assert (tmp_path / "output/sub/alias").read_bytes() == b"data"
    assert os.readlink(tmp_path / "output/alias") == "value"


@pytest.mark.parametrize("hard,target", [(True, "other/value")])
def test_strip_rejects_links_outside_root(arc, make_tar, tmp_path, hard, target):
    archive = make_tar([file_entry("root/value"), link_entry("root/alias", target, hard=hard)])
    arc("unpack", "--strip-root", archive, "output", success=False)
    assert not (tmp_path / "output/alias").exists()
    assert not (tmp_path / "outside").exists()


def test_strip_does_not_overwrite_and_reports_context(arc, make_tar, tmp_path):
    archive = make_tar([file_entry("root/new"), file_entry("root/value")])
    output = tmp_path / "output"
    output.mkdir()
    (output / "value").write_bytes(b"keep")
    result = arc("unpack", "--strip-root", archive, "./output", success=False)
    assert (output / "value").read_bytes() == b"keep"
    assert "./output/value" in result.stderr
    assert "arc does not overwrite" in result.stderr
    assert "Choose another destination." in result.stderr
    assert "Some files may have been extracted" in result.stderr


def test_strip_missing_input_has_no_structure_hint(arc, tmp_path):
    result = arc("unpack", "--strip-root", "missing.tar", "output", success=False)
    assert "Cannot extract" in result.stderr
    assert "Extract without --strip-root" not in result.stderr
    assert not (tmp_path / "output").exists()


@pytest.mark.skipif(os.name == "nt", reason="POSIX untyped symbolic links")
def test_strip_preserves_external_symbolic_target(arc, make_tar, tmp_path):
    archive = make_tar([file_entry("root/value"), link_entry("root/alias", "../outside")])
    arc("unpack", "--strip-root", archive, "output")
    assert os.readlink(tmp_path / "output/alias") == "../outside"
    assert not (tmp_path / "outside").exists()
