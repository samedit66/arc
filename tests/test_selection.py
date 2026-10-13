import os

import pytest

from support import directory_entry, fifo_entry, file_entry, link_entry


@pytest.fixture
def selection_archive(make_tar):
    return make_tar([
        file_entry("./docs/readme", b"readme"), file_entry("docs/sub/value", b"nested"),
        file_entry("docs-old/value", b"excluded"), file_entry("Case", b"upper"),
        file_entry("case", b"lower"), file_entry("text-текст", b"unicode"),
        file_entry("../selection-escape", b"bad"), fifo_entry("pipe"),
        file_entry("CON.txt", b"host-specific"),
    ])


def test_subtree_selection_normalizes_and_deduplicates(arc, selection_archive, tmp_path):
    arc("unpack", "--entry", "./docs/", "--entry", "docs/readme", "--entry", "docs", selection_archive, "output")
    destination = tmp_path / "output"
    assert (destination / "docs/readme").read_bytes() == b"readme"
    assert (destination / "docs/sub/value").read_bytes() == b"nested"
    assert {path.relative_to(destination).as_posix() for path in destination.rglob("*")} == {
        "docs", "docs/readme", "docs/sub", "docs/sub/value",
    }
    assert not (tmp_path / "selection-escape").exists()


@pytest.mark.parametrize("name,expected", [("case", b"lower"), ("text-текст", b"unicode")])
def test_exact_selection_is_case_sensitive_and_supports_unicode(arc, selection_archive, tmp_path, name, expected):
    arc("unpack", "--entry", name, selection_archive, "output")
    destination = tmp_path / "output"
    assert (destination / name).read_bytes() == expected
    assert len(list(destination.iterdir())) == 1


def test_selected_existing_file_is_preserved(arc, selection_archive, tmp_path):
    destination = tmp_path / "output"
    (destination / "docs").mkdir(parents=True)
    (destination / "docs/readme").write_bytes(b"keep")
    arc("unpack", "--entry", "docs", selection_archive, destination, success=False)
    assert (destination / "docs/readme").read_bytes() == b"keep"


def test_no_selection_match(arc, selection_archive, tmp_path):
    arc("unpack", "--entry", "absent", selection_archive, "output", success=False)
    destination = tmp_path / "output"
    assert not destination.exists() or not list(destination.iterdir())


def test_partial_selection_reports_failure_after_matching_entries(arc, selection_archive, tmp_path):
    arc("unpack", "--entry", "docs", "--entry", "absent", selection_archive, "output", success=False)
    assert (tmp_path / "output/docs/readme").read_bytes() == b"readme"


def test_invalid_selector_creates_no_destination(arc, selection_archive, tmp_path):
    arc("unpack", "--entry", "../bad", selection_archive, "output", success=False)
    assert not (tmp_path / "output").exists()


@pytest.mark.parametrize("selected", [("alias", "value"), ("alias",)])
def test_hardlink_target_must_be_selected(arc, make_tar, tmp_path, selected):
    archive = make_tar([link_entry("alias", "value", hard=True), file_entry("value", b"shared")])
    options = [argument for name in selected for argument in ("--entry", name)]
    arc("unpack", *options, archive, "output", success=len(selected) == 2)
    if len(selected) == 2:
        assert os.path.samefile(tmp_path / "output/alias", tmp_path / "output/value")
    else:
        assert not (tmp_path / "output/alias").exists()


@pytest.mark.parametrize("selected", [("a", "b", "c"), ("a", "c")])
def test_hardlink_chain_must_be_selected(arc, make_tar, tmp_path, selected):
    archive = make_tar([
        link_entry("a", "b", hard=True), link_entry("b", "c", hard=True), file_entry("c", b"chain"),
    ])
    options = [argument for name in selected for argument in ("--entry", name)]
    arc("unpack", *options, archive, "output", success=len(selected) == 3)
    if len(selected) == 3:
        assert os.path.samefile(tmp_path / "output/a", tmp_path / "output/c")
    else:
        assert not (tmp_path / "output/a").exists()


def test_selected_hardlink_cycle_is_rejected(arc, make_tar):
    archive = make_tar([link_entry("a", "b", hard=True), link_entry("b", "a", hard=True)])
    arc("unpack", "--entry", "a", "--entry", "b", archive, "output", success=False)


def test_selected_symbolic_link_can_be_dangling(arc, make_tar, tmp_path, symlinks_available):
    archive = make_tar([link_entry("alias", "target"), file_entry("target", b"not-selected")])
    arc("unpack", "--entry", "alias", archive, "output")
    assert (tmp_path / "output/alias").is_symlink()
    assert not (tmp_path / "output/target").exists()


def test_selected_unsafe_path_is_rejected(arc, make_tar, tmp_path):
    archive = make_tar([file_entry("docs/../outside", b"bad")])
    arc("unpack", "--entry", "docs", archive, "output", success=False)
    assert not (tmp_path / "outside").exists()
    assert not (tmp_path / "output/outside").exists()


def test_unselected_directory_metadata_is_not_applied(arc, make_tar, tmp_path):
    archive = make_tar([directory_entry("docs", mode=0, mtime=123), file_entry("docs/value", b"x")])
    arc("unpack", "--entry", "docs/value", archive, "output")
    directory = tmp_path / "output/docs"
    assert (directory / "value").read_bytes() == b"x"
    assert directory.stat().st_mtime != 123
    if os.name != "nt":
        assert directory.stat().st_mode & 0o700 != 0
