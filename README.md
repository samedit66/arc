<div align="center">

# Ꭿrc

A human-centered 🥰 Eiffel library and CLI for everyday archive tasks.

</div>

Arc is a small, void-safe Eiffel library built on libarchive. Use it to pack, unpack, inspect and verify archives from Eiffel or the command line.

## In a hurry?

```sh
# Pack a directory
arc pack backup.tar.gz ./src

# Keep the directory itself inside the archive
arc pack --keep-root backup.zip ./src

# List files
arc list backup.zip
arc list --tree backup.zip

# Unpack into a new directory named "backup"
arc unpack backup.zip

# Show archive information or check its contents
arc info backup.zip
arc verify backup.zip

# Create and unpack a password-protected ZIP
arc pack --password secret protected.zip ./src
arc unpack --password secret protected.zip
```

Add `--progress` to packing or unpacking for a per-file progress bar on stderr. Unknown file sizes use a spinner and byte counter.

Creation formats: `tar`, `tar.gz`, `tar.bz2`, `tar.xz`, `tar.zst`, `tar.lz4`, `zip` and `7z`. Arc normally chooses the format from the filename; use `--format` to override it.

Passwords enable AES-256 for ZIP creation. Other output formats reject passwords before creating a file. Reading supports the encryption methods available in the installed libarchive.

## Installation

Choose what you need:

| Use | Requirements |
|---|---|
| Run a built application | The executable and supported OS; no Eiffel, Python or Conan |
| Use the library with system libarchive | Eiffel/C compilers and libarchive development files |
| Use a prepared library package | Compatible Eiffel/C compilers; native dependencies are included |
| Prepare packages or build the application | Eiffel/C compilers, Python and Conan |

### Build tools with mise

The repository’s mise configuration installs Python 3.13.15 and Conan 2.33.0. Python runs the build and packaging scripts; Conan supplies libarchive and its native dependencies.

```sh
mise install
```

Install Gobo and a C toolchain separately. You can also install Python and Conan yourself and run the scripts directly.

Mise, Python and Conan are unnecessary when using system libarchive, consuming a prepared library package or running a built executable.

### Library with system libarchive

Use libarchive **3.8.7 or later**, including its development headers and libraries. Add Arc to your project:

```xml
<library name="arc" location="vendor/arc/arc.ecf"/>
```

Arc uses system libarchive by default. No preparation script is needed.

On **macOS**, a Homebrew installation can be selected with:

```sh
export ARC_LIBARCHIVE_PREFIX="$(brew --prefix libarchive)"
```

On **Linux**, install the development package supplied by your distribution. If it is outside the compiler’s usual search paths, set `ARC_LIBARCHIVE_PREFIX`.

On **Windows**, provide matching MSVC headers and libraries. A libarchive DLL requires shared CRT: select `ARC_SYSTEM_CRT=md` in the consumer’s ECF or compiler options. For static libarchive, select `ARC_SYSTEM_STATIC=true` and provide its dependencies; the caller and library must use compatible CRT settings.

### Library with Conan libarchive

Prepare static dependencies and create a library package:

```sh
mise run prepare
mise run package
```

Without mise:

```sh
python3 scripts/prepare.py
python3 scripts/package.py --skip-prepare
```

Unpack the resulting ZIP from `build/package` into `vendor/arc` and connect its `arc.ecf`. The package includes core sources, native libraries and their licenses. Consumers need neither Conan nor the CLI submodule.

Choose a package matching your OS, architecture and compiler. Windows packages use static CRT (`/MT`); Linux packages distinguish glibc and musl.

To use prepared dependencies directly from the checkout, select `ARC_NATIVE_MODE=conan` in your project configuration.

### Application with Conan libarchive

The application depends on the [cli](https://github.com/samedit66/cli) and [progress_bar](https://github.com/Eiffel-Workshop/progress_bar) packages.

Initialize the CLI submodule, prepare dependencies and build:

```sh
git submodule update --init --recursive
mise run prepare
mise run build
./arc --help
```

Without mise:

```sh
python3 scripts/prepare.py --build
```

The build checks the resulting executable:

| OS | Application linkage |
|---|---|
| macOS | Static third-party dependencies; Apple system libraries remain dynamic |
| Windows | Static third-party dependencies and CRT (`/MT`); Windows system DLLs remain |
| Linux | Fully static executable, built in a native musl environment |

For Linux application builds:

```sh
python3 scripts/prepare.py --libc musl --build
```

The script does not turn a glibc environment into musl. Build inside a musl-based system or container.

Create an application ZIP:

```sh
python3 scripts/package.py --kind application
```

Copy `arc` or `arc.exe` onto `PATH`. No build tools or third-party shared libraries are needed to run it.

Gobo on macOS ARM64 is verified. The CI matrix builds and tests Gobo and EiffelStudio on Ubuntu, macOS ARM64 and Windows/MSVC. Linux/musl release builds require separate verification.

### Environment variables

Arc needs no runtime configuration variables. These variables affect building and dependency preparation:

| Variable | Purpose | Values or examples |
|---|---|---|
| `ARC_LIBARCHIVE_PREFIX` | System libarchive installation directory; adds `include` and, outside Windows, `lib` search paths | `/opt/homebrew/opt/libarchive`, `/usr/local`, `C:/libarchive` |
| `ARC_LIBARCHIVE_LIBRARY` | Exact system library file; replaces the default `-larchive` or `archive.lib` | `/opt/lib/libarchive.a`, `C:/libarchive/lib/archive.lib` |
| `ARC_LIBC` | Linux libc recorded by the Conan recipe | `glibc`, `musl`; the preparation script detects and sets it |
| `GOBO` | Gobo installation directory used by ECF library paths | `/opt/gobo` |
| `GOBO_EIFFEL` | Selects the Eiffel base-library configuration | `ge` for Gobo, `ise` for EiffelStudio; application scripts set `ge` |
| `GOBO_CC` | Gobo C backend; select MSVC for Windows Conan dependencies | `msc` on Windows, `gcc` on Linux/macOS |
| `ISE_LIBRARY` | EiffelStudio library directory | Your EiffelStudio library root |
| `CC` | C compiler used during dependency preparation | `clang`, `gcc`, an absolute compiler path |
| `CXX` | C++ compiler used during dependency preparation | `clang++`, `g++`, an absolute compiler path |
| `CONAN_HOME` | Conan cache directory | Mise uses `build/conan`; preparation selects `build/conan` or `build/conan-musl` |

On macOS, preparation defaults `CC` and `CXX` to `/usr/bin/clang` and `/usr/bin/clang++` when unset. Inspection subprocesses set `LC_ALL=C`, `LANG=C` and `VSLANG=1033` so tool output can be parsed consistently.

The following are **ECF configuration variables**, rather than path environment settings. With Gobo, pass them as `--variable=NAME=value`; for library consumers, select them in the root project configuration.

| Variable | Purpose | Values |
|---|---|---|
| `ARC_NATIVE_MODE` | Selects native dependencies | `system` by default; `conan` for a prepared checkout |
| `ARC_SYSTEM_CRT` | Windows CRT selection in system mode | `default` keeps the toolchain setting; `md` requests shared CRT |
| `ARC_SYSTEM_STATIC` | Declares static system libarchive on Windows | `false` by default; `true` for static linkage |
| `ARC_LIBC` | Selects Linux libc configuration; enables static application linkage for musl | `glibc` by default; `musl` |

Prepared library packages select their bundled dependencies automatically.

## Eiffel API

Add Arc to a project that already selects its Eiffel base library:

```xml
<library name="arc" location="vendor/arc/arc.ecf"/>
```

The examples below use `archive: ARC_ARCHIVE` and `PATH` arguments supplied by the caller.

### Unpack an archive?

Extract everything into a new directory:

```eiffel
create archive.make_open_read (input_path)
if not archive.has_failed then
	archive.extract_all (destination_path)
end
archive.close

if archive.has_failed then
	-- Report archive.last_error.
end
```

Or walk the entries yourself:

```eiffel
create archive.make_open_read (input_path)
from
until
	archive.after or archive.has_failed
loop
	-- Read archive.entry_name and entry_type.
	-- Read entry_size only when has_entry_size is True.
	-- Optionally call archive.extract_current (destination_path).
	archive.forth
end
archive.close
```

Advancing skips unread content. `extract_current` uses the exact destination you provide; `extract_all` validates archive names and maps them beneath a new extraction directory.

### Pack an archive?

```eiffel
create archive.make_open_write (output_path, {ARC_FORMATS}.Tar_gz)
if not archive.has_failed then
	archive.add_path (source_directory, "project")
	archive.add_path (readme_path, "README.txt")
	archive.pack
end
archive.close
```

`add_path` registers sources; `pack` reads them, writes the archive and closes it. Use an empty stored name to include a directory’s contents without its root.

Output must not already exist. Failed packing attempts to remove the incomplete archive; closing before `pack` cancels creation.

### Use a password?

```eiffel
create archive.make_open_write_with_password (
	output_path, {ARC_FORMATS}.Zip, "secret"
)
```

For reading, use `make_open_read_with_password (input_path, password)`. Passwords must be nonempty and contain no NUL characters.

### Inspect or verify?

```eiffel
create archive.make_open_read (input_path)
if not archive.has_failed then
	archive.inspect_remaining
end
archive.close

if not archive.has_failed and then attached archive.inspection as info then
	-- Use the immutable summary.
end
```

`inspect_remaining` collects format, filters, entry counts, declared sizes and encryption status. `verify_remaining` reads entry contents without extracting them. Both start at the current entry, so call them immediately after opening to cover the whole archive.

Verification checks what the format and libarchive support. For example, ordinary tar has no checksum for file contents.

### Progress, errors and resource ownership

Use `set_progress (callback)` before packing or extraction. The callback receives the entry name, bytes written and total size; zero means the total is unknown.

Check `has_failed` after operations and after `close`. `last_error` preserves the primary error and separate cleanup diagnostics. Close archives on early exits and in rescue clauses; native resources are not released by a GC finalizer.

Each archive object serves one operation. Reading is sequential; reopen the archive for another pass.

Symbolic links are preserved without traversal. Hardlinked source paths are packed independently; hardlink extraction is unsupported. Failed extraction can leave files already restored.

For block-level control, use `ARC_READER` and `ARC_WRITER`. See the [development guide](docs/development.md), [source map](docs/code.md) and [migration notes](docs/migration.md) for details.

## Mise commands

| Command | Action |
|---|---|
| `mise run prepare` | Prepare static dependencies and generate native ECF configuration |
| `mise run build` | Build and check the release application |
| `mise run package` | Create a library ZIP from prepared dependencies |
| `mise run test` | Build the application and run packaging, Eiffel and CLI checks |
| `mise run test-metadata` | Run packaging checks without Eiffel or native dependencies |
| `mise run test-native` | Build and run Eiffel probes and CLI integration checks |
| `mise run test-cli -- ./arc` | Check an existing executable |
| `mise run test-system` | Compile and test a library consumer with system libarchive |

Application packaging and package relocation checks are also available:

```sh
python3 scripts/package.py --kind application
python3 tests/check_archive.py --relocate build/package/PACKAGE-library.zip
python3 tests/check_archive.py --relocate build/package/PACKAGE-application.zip
```

Native dependency licenses are included in prepared packages. Arc currently has no repository-level license file.
