# Static Checks

Inherits `../AGENTS.md`.

## Architecture

Tools are read-only repository policy checks. The documentation scanner
enforces adjacent intent contracts for C++ type definitions, the folder scanner
ensures every non-generated package directory has local architectural guidance,
the dependency scanner enforces package ownership and inward portable includes,
the profile-map scanner rejects unselected module evidence, the namespace scanner
holds each source to the system namespace its folder names, and the formatting
scanner rejects C++ sources that drift from the tracked clang-format policy.
Function and state documentation remains a declaration-level review requirement
because a regex-only parser must not pretend to understand arbitrary C++.
Production code never imports or invokes these scripts.

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
  profile requires positive Core archive evidence; Object- and Transport-selected
  profiles additionally require their adjacent system archives. The current
  managed profile includes Core, Engine (with the folded Object), and the
  Engine archive covers both.
- Formatting checks delegate to `clang-format --dry-run --Werror` with the
  repo style file passed explicitly as `--style=file:<root>/clang-format`; the
  file has no leading dot, so a bare `--style=file` would fall back to LLVM
  style and falsely flag every file. The file set is tracked `*.h`/`*.cpp`
  under `Modules/` (PlatformEsp32 sources included), discovered via
  `git ls-files` when available.
- Namespace checks assert the namespace each source must open, from a literal
  map of system folders plus the three declared Transport leaves, and reject any
  `using namespace` in a library source. The directive is rejected because it
  re-exports a whole namespace into the enclosing one — in a header, into every
  consumer — which restores the flat namespace ADR 0006 removed.
- Each architectural checker owns a deterministic `--self-test` covering both
  an accepted input and the violation it is intended to block.
- Scripts return non-zero on failure so CMake or CI can use them as gates.

## Documentation and verification

Every Python function and module-level policy constant needs a purpose-focused
docstring or comment. Keep scans deterministic and side-effect free. Verify with
`python tools/CheckClassDocumentation.py --root Modules/MicroWorld/Core --require-doxygen --max-sentences 3`
and
`python tools/CheckFolderAgents.py --root Modules/MicroWorld/Core --require-file AGENTS.md`.

`CheckClassDocumentation.py` and `CheckFolderAgents.py` each skip generated trees
and tool metadata through their own `DEFAULT_EXCLUDED_DIRECTORY_NAMES`, so no
`--exclude` chain belongs in a normal invocation. Matching is by exact directory
name, so a build tree the defaults do not anticipate enters the scan loudly; add
the name to that constant, or pass `--exclude` for a one-off. Both carry a
`--self-test` that pins the case where an excluded name sits in an *ancestor* of
the scan root, which once made either checker report a pass over zero files.
Verify module boundaries with
`python tools/CheckDependencyBoundaries.py --package Core=Modules/MicroWorld/Core`
and verify a built Core map with
`python tools/CheckProfileMap.py --map <linker-map> --profile Core`.
Use `--profile Object` for Core+Object, and `--profile Managed` for
Core+Object+Engine. (Memory folded into Core; there is no Memory profile.)
Run each new checker with `--self-test` before trusting its repository result.
Verify clang-format conformance with
`python tools/CheckFormatting.py --root <repo-root>` (it also runs as the
ctest step `microworld_format_check`).
Verify the namespace contract with `python tools/CheckNamespaces.py` (it also runs
as the ctest step `microworld_namespace_check`).
