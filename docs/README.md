# Arc documentation

The [project README](../README.md) covers the everyday stuff. Here are the parts
you'll want when embedding Arc, changing it, or wondering where the C version
still does more.

| Guide | What's inside |
| --- | --- |
| [Using the Eiffel library](library.md) | Public API, contracts, errors, passwords, progress and stream lifetime |
| [CLI](cli.md) | Commands, argument rules, output and current limitations |
| [Building and packaging](build.md) | Eiffel toolchains, Conan dependencies, platforms and relocatable ZIPs |
| [Implementation](design.md) | Module boundaries, native calls, ownership, paths and cleanup |
| [Testing](testing.md) | Compiler matrix, integration probes, fault injection and release checks |
| [Parity audit](parity.md) | Missing capabilities and deliberate differences from the original Arc |

Public Eiffel declarations and their contracts are authoritative. The audit
compares a fixed upstream revision, so it won't quietly change when upstream does.
