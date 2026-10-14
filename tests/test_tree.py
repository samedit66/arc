from support import directory_entry, file_entry, link_entry


def test_tree_groups_unordered_paths_and_implicit_parents(arc, make_tar):
    archive = make_tar([
        file_entry("a/z/value"), file_entry("a.txt"), file_entry("a/b"),
        directory_entry("empty"), link_entry("link", "a/b"),
    ])
    result = arc("list", "--tree", archive)
    assert result.stdout == (
        "|-- a/\n  |-- b\n  |-- z/\n    |-- value\n"
        "|-- a.txt\n|-- empty/\n|-- link -> a/b\n"
    )
    assert not result.stderr


def test_tree_preserves_repeated_paths_and_escapes_names(arc, make_tar):
    archive = make_tar([file_entry("a"), file_entry("a"), file_entry("bad\x1b[2J\n")])
    result = arc("list", "--tree", archive)
    assert result.stdout.count("a [repeated path]") == 2
    assert "bad\\x1b[2J\\x0a" in result.stdout
    assert "\x1b" not in result.stdout


def test_tree_does_not_validate_extraction_safety(arc, make_tar):
    archive = make_tar([file_entry("../unsafe")])
    assert "unsafe" in arc("list", "--tree", archive).stdout
