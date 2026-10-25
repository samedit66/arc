# Building and verification

The examples and test application use `just` recipes for EiffelStudio and Gobo.
Build with either compiler, or use both to check portability.

## Compiler setup

For EiffelStudio, make `ec` available and set `ISE_LIBRARY` to the EiffelStudio
library directory. The recipes set `GOBO_EIFFEL=ise` and select EiffelBase.

For Gobo, make `gec` available and set `GOBO` to the Gobo installation directory.
The recipes set `GOBO_EIFFEL=ge` and select FreeELKS.

[cli.ecf](../cli.ecf) defines the library selection and void-safety settings.
The example ECF files contain the application targets.
Examples and tests mark the local `cli` library as `readonly="false"` so that
EiffelStudio recompiles library changes during incremental builds.

## Build the examples

From an example directory, run either recipe:

```sh
just gobo
just eiffelstudio
```

Executables are placed in `build/gobo/` or `build/eiffelstudio/` under that directory.
Their names match the example directories: `greet`, `text`, `faketar`, and `arc`.
See each [example description](../README.md#examples) for its interface and invocations.

## Run the checks

From the repository root, build all examples and the test application for the
compiler you want to check:

```sh
just --justfile examples/greet/justfile gobo
just --justfile examples/text/justfile gobo
just --justfile examples/faketar/justfile gobo
just --justfile examples/arc/justfile gobo
just --justfile tests/justfile gobo
python3 tests/check_examples.py gobo
```

Replace `gobo` with `eiffelstudio` to check the other compiler. With both sets of
executables built, run:

```sh
python3 tests/check_examples.py
```

The script executes the built programs and checks their output and exit codes.
It also runs the assertion-enabled test application for parser behavior, defaults,
input ownership, splitting, and default-command help. Missing executables are
reported as failures; the script does not build them or skip them.

## Static checks

With Gobo configured, run the analyzer against the test and arc example targets:

```sh
GOBO_EIFFEL=ge gelint --flat tests/cli_tests.ecf
GOBO_EIFFEL=ge gelint --flat examples/arc/arc.ecf
```

Use `git diff --check` to check whitespace in tracked changes.
