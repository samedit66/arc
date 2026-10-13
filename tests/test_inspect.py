import os
from pathlib import Path

import pytest

from support import directory_entry, file_entry, link_entry


@pytest.fixture
def known_archive(make_tar):
    return make_tar([
        directory_entry("sub"), file_entry("sub/value", b"payload"),
        file_entry("empty", b""), link_entry("alias", "sub/value", hard=True),
        link_entry("symbolic", "sub/value"),
    ])


def test_list_reports_known_entries(arc, known_archive):
    listing = arc("list", known_archive).stdout
    for name in ("sub", "sub/value", "empty", "alias", "symbolic"):
        assert name in listing
    assert "symlink" in listing


def test_info_reports_known_counts(arc, known_archive):
    info = arc("info", known_archive).stdout
    assert "Files: 2" in info
    assert "Hard links: 1" in info


def test_inspection_does_not_extract(arc, known_archive, tmp_path):
    before = set(tmp_path.iterdir())
    original = known_archive.read_bytes()
    arc("list", known_archive)
    arc("info", known_archive)
    assert set(tmp_path.iterdir()) == before
    assert known_archive.read_bytes() == original


def test_rar_inspection(arc):
    archive = Path(__file__).parent / "fixtures/sample.rar"
    assert "test.txt" in arc("list", archive).stdout
    assert "Files: 2" in arc("info", archive).stdout
    arc("verify", archive)


def test_rar_extraction(arc, tmp_path):
    archive = Path(__file__).parent / "fixtures/sample.rar"
    # This fixture contains a symlink; an unprivileged Windows host can refuse it.
    result = arc("unpack", archive, "output", success=None)
    if result.returncode:
        assert os.name == "nt" and "symbolic link" in result.stderr
    else:
        assert (tmp_path / "output/test.txt").read_bytes() == b"test text document\r\n"
        assert (tmp_path / "output/testlink").is_symlink()
