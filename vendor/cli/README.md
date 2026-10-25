<div align="center">

# cli

[![ISE Eiffel](https://img.shields.io/badge/toolchain-ISE%20Eiffel-17365D)](https://www.eiffel.com/)
[![Gobo Eiffel](https://img.shields.io/badge/toolchain-Gobo%20Eiffel-8B5A2B)](https://www.gobosoft.com/)

A small Eiffel library for defining command-line arguments, options, and flags. 
It is void-safe, uses ELKS classes, and supports EiffelStudio and Gobo.

Inspired by Python's [Cleo](https://github.com/python-poetry/cleo).

</div>

## Highlights

- 💅 Describe your commands. Let cli deal with the [argument drama](docs/usage.md#commands-and-parameters).
- 🎯 Typed values, explicit defaults. No stringly-typed jumpscares. Meet [CLI_COMMAND_INPUT](src/cli_command_input.e).
- 🗣️ Multiword commands. Because one word is sometimes not enough. See [arc](examples/arc/README.md).
- 📖 [`--help` included](docs/syntax.md#automatic-help). Your users can stop guessing.

## Installation

Clone the library into your project:

```sh
git clone https://github.com/samedit66/cli.git vendor/cli
```

Add it to your application's ECF target:

```xml
<library name="cli" location="vendor/cli/cli.ecf"/>
```

Use `GOBO_EIFFEL=ge` with `GOBO` for Gobo's FreeELKS. Otherwise, the library uses
EiffelBase through `ISE_LIBRARY`. See [building and verification](docs/development.md)
for compiler setup and build commands.

## Quick start

Define a command in `hello_command.e`:

```eiffel
class
    HELLO_COMMAND

inherit
    CLI_COMMAND

feature -- Definition

    name: STRING_32 = "hello"
            -- Command name used for selection.

    description: STRING_32 = "Greet someone"

    interface
        do
            add_optional_string_argument ("name", "Person to greet", "world")
        end

feature -- Execution

    handle (a_input: CLI_COMMAND_INPUT)
        do
            io.put_string_32 ({STRING_32} "Hello, " + a_input.string_parameter ("name") + "!%N")
        end

end
```

Register it in your application's creation routine:

```eiffel
make
    local
        application: CLI_APPLICATION
    do
        create application.make ("tool", "Greeting example")
        application.add_command (create {HELLO_COMMAND})
        application.run
    end
```

With an executable named `tool`:

```sh
./tool hello          # Hello, world!
./tool hello Alice    # Hello, Alice!
```

See [classes and usage](docs/usage.md) for parameter declarations, applications
without subcommands, help, and error handling.

## Examples

Each example includes source code, build instructions, and invocation examples.

| Example | Demonstrates |
|---|---|
| [greet](examples/greet/README.md) | A default command, string defaults, and short aliases |
| [text](examples/text/README.md) | Named commands, positional lists, and integer options |
| [faketar](examples/faketar/README.md) | Grouped short flags and repeated list options |
| [arc](examples/arc/README.md) | Root flags and multiword command names |

## Documentation

- [Classes and usage](docs/usage.md): responsibilities, API examples, and links to class contracts.
- [Command-line syntax](docs/syntax.md): command selection, accepted forms, defaults, and errors.
- [Building and verification](docs/development.md): compiler configuration, example builds, and checks.

## License

Licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
