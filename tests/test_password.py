import struct
import zipfile
from pathlib import Path

import pyzipper
import pytest

from support import assert_tree_matches

PASSWORD = "пароль-secret"


@pytest.fixture
def aes_zip(tmp_path):
    path = tmp_path / "aes.zip"
    with pyzipper.AESZipFile(path, "w", compression=zipfile.ZIP_DEFLATED,
                            encryption=pyzipper.WZ_AES) as archive:
        archive.setpassword(PASSWORD.encode())
        archive.setencryption(pyzipper.WZ_AES, nbits=256)
        archive.writestr("docs/value", b"payload" * 20000)
        archive.writestr("safe", b"selected")
        archive.writestr("zero", b"")
    return path


def test_independent_reader_accepts_created_aes256(arc, source_tree, tmp_path):
    path = tmp_path / "protected.zip"
    arc("pack", "--password", PASSWORD, path, source_tree)
    with pyzipper.AESZipFile(path) as archive:
        archive.setpassword(PASSWORD.encode())
        for entry in archive.infolist():
            if not entry.is_dir():
                if entry.file_size:
                    assert entry.flag_bits & 1
                    assert entry.wz_aes_strength == 3
                else:
                    assert not entry.flag_bits & 1
                assert archive.read(entry) == (source_tree / entry.filename).read_bytes()
        archive.setpassword(b"wrong")
        with pytest.raises(RuntimeError):
            archive.read("plain.txt")
    arc("unpack", "--password", PASSWORD, path, "output")
    assert_tree_matches(source_tree, tmp_path / "output")
    arc("verify", "--password", PASSWORD, path)


@pytest.mark.parametrize("bits", [128, 192, 256])
def test_independent_aes_input(arc, tmp_path, bits):
    path = tmp_path / "input.zip"
    with pyzipper.AESZipFile(path, "w", encryption=pyzipper.WZ_AES) as archive:
        archive.setpassword(PASSWORD.encode())
        archive.setencryption(pyzipper.WZ_AES, nbits=bits)
        archive.writestr("value", b"payload")
    arc("unpack", "--password", PASSWORD, path, "output")
    assert (tmp_path / "output/value").read_bytes() == b"payload"
    arc("verify", "--password", PASSWORD, path)


@pytest.mark.parametrize("command", ["verify", "unpack"])
@pytest.mark.parametrize("password,diagnostic", [(None, "password required"), ("wrong", "password rejected")])
def test_password_errors(arc, aes_zip, command, password, diagnostic):
    args = [] if password is None else ["--password", password]
    result = arc(command, *args, aes_zip, success=False)
    assert diagnostic in result.stderr.lower()
    assert PASSWORD not in result.stderr


def test_encrypted_selection_and_metadata(arc, aes_zip, tmp_path):
    arc("unpack", "--entry", "safe", "--password", PASSWORD, aes_zip, "selected")
    assert (tmp_path / "selected/safe").read_bytes() == b"selected"
    assert not (tmp_path / "selected/docs").exists()
    for command in ["list", "info"]:
        arc(command, "--password", PASSWORD, aes_zip)
        # Metadata alone does not authenticate a password or decrypt ordinary bodies.
        arc(command, aes_zip)
    arc("unpack", "--password", PASSWORD, aes_zip, "all")
    assert (tmp_path / "all/docs/value").read_bytes() == b"payload" * 20000
    assert (tmp_path / "all/zero").read_bytes() == b""


@pytest.mark.parametrize("command", ["pack", "unpack", "verify", "list", "info"])
def test_empty_password_is_invalid(arc, aes_zip, tmp_path, command):
    if command == "pack":
        args = ["output.zip", aes_zip]
    else:
        args = [aes_zip]
    result = arc(command, "--password", "", *args, success=False)
    assert "nonempty" in result.stderr
    assert not (tmp_path / "output.zip").exists()
    assert not (tmp_path / "aes").exists()


@pytest.mark.parametrize("suffix", ["tar", "tar.gz", "7z"])
def test_unsupported_creation_leaves_no_output(arc, source_tree, tmp_path, suffix):
    path = tmp_path / f"protected.{suffix}"
    result = arc("pack", "--password", PASSWORD, path, source_tree, success=False)
    assert "requires ZIP" in result.stderr
    assert not path.exists()


def test_damaged_encrypted_body_fails(arc, aes_zip):
    data = bytearray(aes_zip.read_bytes())
    start = 30 + struct.unpack_from("<H", data, 26)[0] + struct.unpack_from("<H", data, 28)[0]
    data[start + 20] ^= 1  # AES salt + password verifier precede ciphertext.
    aes_zip.write_bytes(data)
    arc("verify", "--password", PASSWORD, aes_zip, success=False)


@pytest.mark.parametrize("command", ["pack", "unpack", "verify", "list", "info"])
def test_password_cli_rejects_missing_or_repeated_option(arc, command):
    arc(command, "--password", success=False)
    arc(command, "--password", "one", "--password", "two", "archive.zip", success=False)


def test_zipcrypto_fixture(arc, tmp_path):
    path = Path(__file__).parent / "fixtures/password-zipcrypto.zip"
    arc("verify", "--password", "secret", path)
    arc("unpack", "--password", "secret", path, "output")
    assert (tmp_path / "output/value").read_bytes() == b"zipcrypto payload\n"
    arc("verify", path, success=False)
    arc("verify", "--password", "wrong", path, success=False)
