# Static Checks

Inherits `../AGENTS.md`.

## Architecture

Tools are read-only repository policy checks. The documentation scanner
enforces adjacent intent contracts for C++ type definitions, the folder scanner
ensures every non-generated package directory has local architectural guidance,
the dependency scanner enforces package ownership and inward portable includes,
and the profile-map scanner rejects unselected module evidence. Function and
state documentation remains a declaration-level review requirement because a
regex-only parser must not pretend to understand arbitrary C++. Production code
never imports or invokes these scripts.

`CheckFolderAgents.py` is a strict coverage check for the existing packages
when deliberately invoked. It is not a policy requiring every future package
subdirectory to add a local guide.

## Concepts

- Scans operate only under an explicit root.
- Generated build, PlatformIO, cache, and caller-specified directories are
  excluded so results describe maintained source.
- Diagnostics identify the file and declaration that violated policy.
- Dependency ownership is declared explicitly as `MODULE=PATH`; a package may
  not hide another module below its own manifest.
- Profile checks inspect archive, path, and public-symbol markers. Every
  profile requires positive Core archive evidence; Memory- and Object-selected
  profiles additionally require their adjacent package archives. The current
  managed profile includes Core, Memory, Object, and Engine.
- Each architectural checker owns a deterministic `--self-test` covering both
  an accepted input and the violation it is intended to block.
- Scripts return non-zero on failure so CMake or CI can use them as gates.

## Documentation and verification

Every Python function and module-level policy constant needs a purpose-focused
docstring or comment. Keep scans deterministic and side-effect free. Verify with
`python tools/CheckClassDocumentation.py --root Modules/Core --exclude build --exclude .pio --exclude __pycache__ --require-doxygen --max-sentences 3`
and
`python tools/CheckFolderAgents.py --root Modules/Core --require-file AGENTS.md --exclude build --exclude .pio --exclude __pycache__`.
Verify module boundaries with
`python tools/CheckDependencyBoundaries.py --package Core=Modules/Core`
and verify a built Core map with
`python tools/CheckProfileMap.py --map <linker-map> --profile Core`.
Use `--profile Memory` for Core+Memory, `--profile Object` for
Core+Memory+Object, and `--profile Managed` for
Core+Memory+Object+Engine.
Run each new checker with `--self-test` before trusting its repository result.
