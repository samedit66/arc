import struct

import pytest

from support import fifo_entry, file_entry, link_entry


@pytest.fixture
def damaged_zip(zip_archive):
    data = bytearray(zip_archive.read_bytes())
    offset = 30 + struct.unpack_from("<H", data, 26)[0] + struct.unpack_from("<H", data, 28)[0]
    data[offset] ^= 1
    path = zip_archive.with_name("bad-crc.zip")
    path.write_bytes(data)
    return path


@pytest.fixture
def encrypted_flag_zip(zip_archive):
    data = bytearray(zip_archive.read_bytes())
    data[6] |= 1
    central = data.index(b"PK\x01\x02")
    data[central + 8] |= 1
    path = zip_archive.with_name("encrypted-flag.zip")
    path.write_bytes(data)
    return path


@pytest.mark.parametrize("kind", ["tar", "zip"])
def test_valid_archive(arc, simple_tar, zip_archive, kind):
    arc("verify", simple_tar if kind == "tar" else zip_archive)


@pytest.mark.parametrize("case", ["duplicate", "special", "external-link", "unsafe-path"])
def test_integrity_does_not_require_safe_extraction(arc, make_tar, case):
    records = {
        "duplicate": [file_entry("a", b"one"), file_entry("a", b"two")],
        "special": [fifo_entry("pipe")],
        "external-link": [link_entry("escape", "../outside")],
        "unsafe-path": [file_entry("../outside", b"bad")],
    }
    archive = make_tar(records[case])
    arc("verify", archive)


def test_crc_error_is_detected_without_modification(arc, damaged_zip, tmp_path):
    before = set(tmp_path.iterdir())
    original = damaged_zip.read_bytes()
    arc("verify", damaged_zip, success=False)
    assert set(tmp_path.iterdir()) == before
    assert damaged_zip.read_bytes() == original


def test_info_can_read_archive_with_damaged_body(arc, damaged_zip):
    assert "Files: 2" in arc("info", damaged_zip).stdout


def test_unselected_damaged_body_can_be_skipped(arc, damaged_zip, tmp_path):
    arc("unpack", "--entry", "safe", damaged_zip, "output")
    assert (tmp_path / "output/safe").read_bytes() == b"selected"
    assert not (tmp_path / "output/value").exists()


def test_selected_damaged_body_is_rejected(arc, damaged_zip):
    arc("unpack", "--entry", "value", damaged_zip, "output", success=False)


def test_encrypted_flag_is_rejected(arc, encrypted_flag_zip):
    # A synthetic header checks refusal; it does not exercise actual encryption.
    arc("verify", encrypted_flag_zip, success=False)


def test_unselected_encrypted_entry_can_be_skipped(arc, encrypted_flag_zip, tmp_path):
    arc("unpack", "--entry", "safe", encrypted_flag_zip, "output")
    assert (tmp_path / "output/safe").read_bytes() == b"selected"
    assert not (tmp_path / "output/value").exists()


@pytest.mark.parametrize("command", ["verify", "unpack"])
def test_broken_zip_header(arc, tmp_path, command):
    archive = tmp_path / "broken.zip"
    archive.write_bytes(b"PK\x03\x04broken")
    arc(command, archive, success=False)


def test_truncated_zip(arc, zip_archive):
    data = zip_archive.read_bytes()
    offset = 30 + struct.unpack_from("<H", data, 26)[0] + struct.unpack_from("<H", data, 28)[0]
    zip_archive.write_bytes(data[:offset + 3])
    arc("verify", zip_archive, success=False)


def test_tar_payload_changes_have_no_checksum(arc, simple_tar):
    data = bytearray(simple_tar.read_bytes())
    data[512] ^= 1
    simple_tar.write_bytes(data)
    arc("verify", simple_tar)


def test_truncated_tar(arc, simple_tar):
    simple_tar.write_bytes(simple_tar.read_bytes()[:515])
    arc("verify", simple_tar, success=False)


def test_invalid_tar_header(arc, make_tar):
    archive = make_tar([file_entry("first", b"x"), file_entry("second", b"y")])
    data = bytearray(archive.read_bytes())
    data[1024] ^= 1
    archive.write_bytes(data)
    arc("verify", archive, success=False)


def test_sparse_tar(arc, make_tar, tmp_path):
    entry, data = file_entry("sparse", b"headtail")
    entry.pax_headers = {
        "GNU.sparse.map": "0,4,4092,4", "GNU.sparse.size": "4096",
        "GNU.sparse.numblocks": "2", "GNU.sparse.name": "sparse",
    }
    archive = make_tar([(entry, data)])
    arc("verify", archive)
    arc("unpack", archive, "output")
    assert (tmp_path / "output/sparse").read_bytes() == b"head" + bytes(4088) + b"tail"


def test_cpio_hardlink_group(arc, tmp_path):
    def record(name, data, *, inode=1, links=2):
        name = name.encode() + b"\0"
        fields = (0, inode, 0o100644, 0, 0, links, 0)
        header = "070707" + "".join(f"{value:06o}" for value in fields)
        header += f"{0:011o}{len(name):06o}{len(data):011o}"
        return header.encode() + name + data

    archive = tmp_path / "links.cpio"
    archive.write_bytes(
        record("a", b"data") + record("b", b"") + record("TRAILER!!!", b"", inode=0, links=1)
    )
    arc("verify", archive)
    arc("list", archive, success=False)


def test_empty_tar(arc, make_tar):
    archive = make_tar([])
    arc("verify", archive)
    assert "Files: 0" in arc("info", archive).stdout


def test_large_entry_and_empty_file(arc, make_tar, tmp_path):
    data = bytes(index % 256 for index in range(131077))
    archive = make_tar([file_entry("big", data), file_entry("empty", b"")])
    arc("verify", archive)
    arc("unpack", archive, "output")
    assert (tmp_path / "output/big").read_bytes() == data
    assert (tmp_path / "output/empty").read_bytes() == b""
