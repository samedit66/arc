import io
import tarfile


def file_entry(name, data=b"data", *, mode=0o644, mtime=0):
    entry = tarfile.TarInfo(name)
    entry.size = len(data)
    entry.mode = mode
    entry.mtime = mtime
    return entry, data


def directory_entry(name, *, mode=0o755, mtime=0):
    entry = tarfile.TarInfo(name)
    entry.type = tarfile.DIRTYPE
    entry.mode = mode
    entry.mtime = mtime
    return entry, b""


def link_entry(name, target, *, hard=False):
    entry = tarfile.TarInfo(name)
    entry.type = tarfile.LNKTYPE if hard else tarfile.SYMTYPE
    entry.linkname = target
    entry.mode = 0o644
    return entry, b""


def fifo_entry(name):
    entry = tarfile.TarInfo(name)
    entry.type = tarfile.FIFOTYPE
    return entry, b""


def write_tar(path, entries, *, mode="w"):
    with tarfile.open(path, mode, format=tarfile.PAX_FORMAT) as archive:
        for entry, data in entries:
            archive.addfile(entry, io.BytesIO(data) if entry.size else None)
    return path


def assert_tree_matches(source, destination):
    expected = {path.relative_to(source) for path in source.rglob("*")}
    actual = {path.relative_to(destination) for path in destination.rglob("*")}
    assert actual == expected
    for relative in expected:
        original = source / relative
        extracted = destination / relative
        if original.is_dir():
            assert extracted.is_dir()
        else:
            assert extracted.read_bytes() == original.read_bytes(), relative
