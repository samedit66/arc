# Command-line syntax

These rules define command selection and token parsing. See
[classes and usage](usage.md) for declarations and API examples.

## Command selection

`CLI_APPLICATION` compares registered command names with the first input tokens.
It selects the longest complete matching name by word count. Words are separated
by single spaces in command definitions and compared with whole input tokens.
For commands `remote` and `remote add`, input `remote add origin` selects
`remote add` and passes only `origin` to its parser. Input `remote list` selects
`remote` and passes `list`. A single quoted token `"remote add"` does not match
the two-word name.

A command with an empty name matches zero tokens. It is selected when no nonempty
name matches and receives the full input. This also applies to empty input. Without
a match, selection fails. A parsing error never retries a shorter name.

Matching starts at the first token; `--verbose remote add` does not select
`remote add`. Options of the empty command are not inherited by named commands.
To pass a command name as a positional value to the empty command, use
`-- remote add`. Parameter names do not affect routing; positional values may
collide with command words regardless of their parameter's name.

An application containing only one command with an empty name passes all tokens
to that command, without a command prefix. `CLI_APPLICATION` initializes the command interface, which adds a help
parameter when needed, as described below. A registered `CLI_HELP_COMMAND` is
an ordinary command and accepts multiword names.

## Automatic help

After the user-defined `interface`, `CLI_COMMAND.initialize_interface` adds a
`--help` flag with a help action unless a parameter named `help` already exists.
Its short alias is `-h` unless that alias is already assigned. Explicit definitions
take precedence. The automatic flag is stored in the command's parameter list and appears in
its generated help. An explicit parameter named `help` added before execution
replaces it. Assigning `h` to another parameter releases the automatic alias;
`--help` remains available.

`tool pack --help` and `tool pack -h` show detailed help for `pack`, including
multiword commands. An empty default command receives the same detailed help.
Without a selected command, standalone `tool --help` and `tool -h` show the
application overview.

Automatic help is recognized wherever a flag is allowed, including short groups
and after positional values. Once recognized, parsing stops, missing required
parameters are not checked, and the handler is not called. Earlier token errors
still fail. `--help=value` is an invalid flag value. A pending option still needs
its value: `--password --help` reports a missing password. `--password=--help` is
a literal value, and `-- --help` is positional input.

`CLI_PARSER.make` accepts all definitions through one constructor. Ordinary flags
store Boolean values; a parameter created with `CLI_PARAMETER.make_help` requests
help. `help_requested` reports this outcome separately from `is_successful` and
`has_error`. Only a successful parse exposes complete `values`. The parser does
not print help or exit; the application renders the selected command's help.
A directly supplied ordinary flag named `help` has no special behavior.

Command names must be unique and remain stable after registration. Nonempty
names use single spaces between words,
with no leading or trailing spaces, tabs, or line breaks. No command word may
begin with `-`. The application name is separate and must be nonempty.

## Parameter definitions

Required positional parameters precede optional ones. At most one positional list
is allowed, and it must be last. These restrictions are checked when a command
registers its parameters. Optional positions fill from left to right; a caller
cannot skip an earlier position while providing a later one. Values are never
reassigned to a later parameter because conversion to the current type failed.

Parameter names are unique within a command, cannot contain spaces or `=`, and
cannot start with `-`. Short aliases are unique single letters within a command.
Required parameters need explicit values. Required lists need at least one value.

## Named options

| Form | Example |
|---|---|
| Long flag | `--verbose` |
| Long option with one value | `--file archive.tar` |
| Long option with an inline value | `--file=archive.tar` |
| Short flag | `-v` |
| Short option with one value | `-f archive.tar` |
| Short option with an inline value | `-f=archive.tar` |
| Group of short flags | `-cvz` means `-c -v -z` |
| Group ending with a value option | `-cvzf archive.tar` |
| Group ending with an inline value | `-cvzf=archive.tar` |
| Repeated list option | `-X tmp -X cache` |

Every non-flag occurrence accepts exactly one value: either the suffix of an
option after `=`, or a separate following token. There are no options taking an
optional value or consuming several following tokens.

A separate token beginning with `-` and containing more than one character cannot
be an option's value. If such a token appears while a value is expected, parsing
fails with a missing-value diagnostic for the preceding option. This applies to
known options, unknown options, negative numbers, and the separator `--`.
For example, `--password --keep-root` reports a missing password.
An absent following token is also an error. A single `-` and an empty string are
accepted as separate values.

Use an inline value to pass a value beginning with `-`:
`--password=--keep-root`, `-f=-archive`, or `--count=-3`.
Only the first `=` separates the name and value: `--file=a=b` and `-f=a=b`
store `a=b`. `--file=` and `-f=` supply an explicit empty string. Empty integer
values fail conversion. Flags do not accept inline values, including an empty
one: `--verbose=true`, `-v=true`, and `-v=` are errors. Quoting a separate value
does not change token recognition.

An option accepting a value must be the final letter in its short group.
`-cvzf archive.tar` and `-cvzf=archive.tar` are valid. The inline value consumes
the entire suffix after `=`, so `-f=cvz` stores `cvz` without setting flags.
Attached values without `=` are unsupported: `-fcvz` and `-farchive.tar` are
errors. Long names must match exactly.

Only list options may repeat. Repeating a scalar option or flag, including `-vv`,
is an error. Long and short spellings refer to the same parameter, so
`--file a -f b` is also a repetition. Commas are ordinary characters: `-X a,b`
stores one value. See [explicit splitting](usage.md#explicit-splitting) for splitting parsed strings
inside a handler.

## Positionals and the separator

`--` ends option recognition when no option is waiting for a value. All subsequent
tokens are positional, including further `--` tokens. When a value is pending,
`--` instead causes a missing-value error. Use `--file=--` or `-f=--` to pass it as an option
value. A single `-` is an ordinary value.

Positional values fill the next declared position. An individual position advances
after one value; the final list accepts all remaining positional values. Options
may appear between positional values until `--` is encountered. A positional
token with no remaining position is an unexpected argument, even if options follow.

Negative positional integers require the separator: `tool -- -3`. Negative option
values require an inline form: `tool --count=-3` or `tool -n=-3`. No special numeric-alias
or negative-number recognition is performed.

## Defaults and completion

The parser first stores explicit values. At end of input it checks missing
required parameters and adds defaults for absent optional parameters. The first
explicit list value replaces its entire default; subsequent values append.
Flags default to `False` and become `True` when present.

A pending option without a value, a missing required parameter, an invalid type,
an unknown option, a forbidden repetition, or an unexpected positional value makes
the whole parse fail. The parser exposes a complete `CLI_COMMAND_INPUT` only on
success. A flag named `help` has no effect on these rules. Applications report
invalid input with exit code `2`; see [error handling](usage.md#errors).
