# Transport Conforms To The Model — Stage 1: Folder And Vocabulary

## Metadata

- **Concept**: `.claude/concepts/transport-conforms-to-model.md`
- **Stage**: 1 of 2. Stage 2 (collapsing the three layers into one `Device`
  contract) is a separate concept and is explicitly out of scope here.
- **Behaviour change**: none. No signature, no contract, no logic changes —
  identifiers, file paths and prose only.
- **Revision**: v2. v1 carried a "frozen list" of types predicted to die in
  Stage 2; a sceptic pass proved that prediction wrong in both directions and it
  is removed. See §3.

## 0. TL;DR

Retire `Net` from the codebase entirely and give `Transport/` a folder tree the
model explains. Three units, each ending on a green `ctest`. One rule governs the
rename: **no identifier keeps `Net` unless the word is foreign or genuine
English.**

## 1. Objective

The model calls the system Transport and its contract a Device. The code calls
them `Net` and `INetDriver`. Stage 1 closes the vocabulary and layout gap so that
Stage 2 — which changes real contracts — starts from names that already match the
model and reads as a design change rather than a rename.

## 2. Existing Context and Impact Radius

### The rule

Every identifier containing `Net` is renamed. Where a new name is not mechanically
obvious, it is fixed in the table below; everything else takes `Net` -> `Transport`
because these types live in the Transport system.

| Now | New | Why not mechanical |
| --- | --- | --- |
| `INetDriver` | `IDevice` | the model's word for the contract |
| `ENetResult` | `ETransportResult` | keyed on the system, matching the six sibling `E<Subject>Result` enums |
| `FNetAddress` | `FDeviceAddress` | the model's "address shape" |
| `FNetReceiveResult` | `FReceiveResult` | |
| `FNetDriverHandle` | `FDeviceHandle` | **lives in Networking**, not Transport — `Networking/NetSystem.h:75` |
| `TNetSystem` | `TNetworking` | the model's element is titled Networking |
| `FDefaultNetSystemTraits` | `FDefaultNetworkingTraits` | |
| `ENetMode` | `ENetworkMode` | owner's choice; survives inside `TNetworking`'s public API |
| `AddNetDriver` | `AddDevice` | follows `IDevice` |
| `MaxNetDrivers` | `MaxDevices` | traits member on `FDefaultNetworkingTraits` |
| `TNetHostSystem` | `THostPlaySystem` | it lives in **Engine**; a Transport-flavoured name there would be worse than the one it replaces |
| `TNetResult`, `TNet` (template params) | `TTransportResult`, `THost` | `Messaging/MessageChannelBinding.h` deliberately avoids naming Transport's types — keep that property |
| `MapNetSendResult` | `MapTransportSendResult` | 3 uses, not 2 |

Mechanical `Net` -> `Transport`, no decision needed: `TNetHost`, `TNetManager`,
`TNetPacketStorage`, `ENetHostState`, `FNetHostConfig`, `FNetPeerSlot`, `FNetHost`,
`FNetManager`, `FNetPacketStorage`, `ENetConsumerExitCode`, `RunNetConsumerProbe`,
`FNetSpawnedActor`, `FNetHostLifecycleActor`, and every local alias in tests and
examples (`FNet`, `FWorldNet`, `FNetFrame`, `FWorldNetSystem`,
`FWorldNetSystemTraits`, `NetPumpStats`, `NetAllocationCounters`, …).

### Protected — must NOT change

- **`NetifResult`** — an lwIP symbol, not ours.
- **`Network`, `Networking`** — real words and the system's own name.
- **`docs/UE5ConceptMap.md`** — prose naming *Unreal's* `NetDriver` and `UChannel`.
  That file exists to name foreign APIs; renaming them there makes it lie.

### Files

```
Modules/MicroWorld/Transport/     18 files flat + Detail/
Modules/MicroWorld/Networking/    NetSystem.h
Modules/MicroWorld/Engine/        EngineSystem.h (TNetHostSystem)
Modules/MicroWorld/Messaging/     MessageChannelBinding.h
Modules/MicroWorld/Platform/      Esp32 (9), Host (2), Pico (1)
Modules/tests/                    Transport (13), Networking (2), Messaging, Core/consumer
Modules/benchmarks/               Transport/, Platform/Esp32/
examples/                         26 numbered + HostLifecycle + TwoNodeDemo
Modules/CMakeLists.txt            source lists name files directly
Modules/library.json, */library.json   PlatformIO manifests naming INetDriver
tools/CheckProfileMap.py          LOAD-BEARING, see §8
docs/architecture/model.c4        Networking contract names TNetSystem
```

## 3. Options Considered

| Option | Verdict |
| --- | --- |
| Freeze the types Stage 2 will delete, rename only survivors | **Rejected — this was v1.** It requires predicting Stage 2, and an adversarial pass found the prediction wrong three ways: `FNetReceiveResult` dies (a span carries its own length, so a callback contract has no out-param struct), while `ENetMode` and `FNetHostConfig` survive inside `TNetworking::AddNetDriver`'s signature. It also leaves ~500 `Net` uses standing, which is not what was asked. |
| Rename everything (**selected**) | Needs no prediction about Stage 2 and delivers the stated goal. The "wasted work" objection is nearly empty: this is one scripted substitution, so 500 extra occurrences cost the same as 100. Only the names in §2 needed thought, and those are now decided. |
| One commit for all of it | **Rejected.** 147 files of substitution buried under structural judgment; nothing separable to review. |
| Split by module | **Rejected.** A rename is atomic — intermediate states do not compile, so per-module units cannot each be verified. |

## 4. Selected Design

Three units, in order, each ending green.

1. **Vocabulary** — global substitution, the header renames it forces, CMake
   source lists, and the profile-map markers.
2. **Layout** — folder guides first, then the moves.
3. **Documents** — `AGENTS.md` files, `README`s, `CLAUDE.md`, `model.c4`.

Target tree after unit 2:

```
Modules/MicroWorld/Transport/
    Device.h  Device.cpp            the contract      (was NetDriver.*)
    DeviceAddress.h                                   (was NetAddress.h)
    TransportResult.h                                 (was NetResult.h)
    FrameCodec.h
    ByteReader.h  ByteWriter.h
    HostLoopback.h
    PacketDropDriver.h  PacketDropDriver.cpp
    TransportHost.h  TransportManager.h                (was NetHost/NetManager)
    TransportPacketStorage.h  TransportProtocol.h
    Lora/
        AGENTS.md
        E32Lora.h
        RadioE32Driver.h  RadioE32Driver.cpp
        E32LoraTransportState.cpp
        Detail/  AGENTS.md  E32LoraTransportState.h
    Wifi/
        AGENTS.md
        UdpAddressCodec.h
```

Two deletions from v1's tree, both because a sceptic pass showed they asserted
something false:

- **No `Testing/`.** `PacketDropDriver.cpp` is in the production source list
  (`Modules/CMakeLists.txt:139`) and `Modules/library.json` ships `+<Transport/>`
  recursively, so the folder would reach every consumer's firmware while claiming
  to be test-only — and example 25 uses it. Both files stay flat.
- **No `Wired/` or `Bluetooth/`.** No portable code exists for either; an empty
  directory asserts a device that does not.

## 5. Architecture

```mermaid
graph LR
    U1["Unit 1 · Vocabulary<br/>no Net identifiers remain"]
    U2["Unit 2 · Layout<br/>Lora/ and Wifi/ exist"]
    U3["Unit 3 · Documents<br/>guides and model agree"]

    G1["ctest green"]
    G2["ctest green"]
    G3["ctest + likec4 validate"]

    U1 --> G1 --> U2 --> G2 --> U3 --> G3

    classDef unit fill:#2a3340,stroke:#6a8aaa,color:#e0eaf5
    classDef gate fill:#2a3a2a,stroke:#6a8a6a,color:#e0f0e0
    class U1,U2,U3 unit
    class G1,G2,G3 gate
```

Dependency direction is untouched: `Core <- Transport` holds, Engine and Transport
still never see each other, and `CheckDependencyBoundaries.py` proves it after each
unit.

## 6. Implementation Steps

### Step 1 — Vocabulary

The contract keeps its exact shape. Transcribed from `Transport/NetDriver.h:51-88`
so there is no ambiguity about what must **not** change:

```cpp
// Modules/MicroWorld/Transport/Device.h            (was NetDriver.h)
namespace MicroWorld
{
class IDevice                                            // was INetDriver
{
public:
    virtual ETransportResult TrySend(                    // was ENetResult
        const FDeviceAddress& InTo,                      // was FNetAddress
        TSpan<const std::uint8_t> InPacket) noexcept = 0;

    virtual ETransportResult TryReceive(
        FDeviceAddress& OutFrom,                         // NOTE: first parameter
        TSpan<std::uint8_t> InDestination,
        FReceiveResult& OutResult) noexcept = 0;         // was FNetReceiveResult

    virtual void AdvanceTransmit() noexcept {}           // returns void, not a result

    virtual std::size_t MaxPacketBytes() const noexcept = 0;

protected:
    virtual ~IDevice() noexcept;                         // out of line, in Device.cpp
    // copy/move ctor and assignment stay deleted exactly as they are today
};
}
```

File renames:

| From | To |
| --- | --- |
| `Transport/NetResult.h` | `Transport/TransportResult.h` |
| `Transport/NetAddress.h` | `Transport/DeviceAddress.h` |
| `Transport/NetDriver.{h,cpp}` | `Transport/Device.{h,cpp}` |
| `Transport/NetHost.h` | `Transport/TransportHost.h` |
| `Transport/NetManager.h` | `Transport/TransportManager.h` |
| `Transport/NetPacketStorage.h` | `Transport/TransportPacketStorage.h` |
| `Transport/NetProtocol.h` | `Transport/TransportProtocol.h` |
| `Networking/NetSystem.h` | `Networking/Networking.h` |
| `tests/Networking/NetSystemTests.cpp` | `tests/Networking/NetworkingTests.cpp` |
| `tests/Networking/EngineNetHostTests.cpp` | `tests/Networking/EngineHostTests.cpp` |
| `tests/Transport/Net{Host,Manager,Protocol,Allocation*}` | `Transport{Host,Manager,Protocol,Allocation*}` |
| `tests/Core/consumer/src/NetConsumerProbe.h` | `TransportConsumerProbe.h` |
| `tests/Core/consumer/src/Net{Native,Esp32}Main.cpp` | `Transport{Native,Esp32}Main.cpp` |

#### Implementer Context

> **Script it.** 147 files. Whole-word matching, **longest identifier first**, or
> shorter patterns corrupt longer ones (`FNetDriverHandle` before `FNetDriver`
> before `FNet`). Never substitute a bare `Net`.
>
> **Scope: `*.h`, `*.cpp`, `*.md`, `*.txt`, `*.py`, `*.json`, `*.c4` only.**
> Explicitly exclude:
> - `docs/diagrams/*.svg` and `*.mmd` — generated renders. Editing an SVG
>   desynchronises its text from its geometry, and re-export is banned.
> - `Modules/benchmarks/**/Results/*.md` — dated measurement records, including
>   lines recording which headers compiled clean on a given date. Rewriting them
>   falsifies evidence.
> - `.claude/` — concept and plan files quote the old names on purpose.
>
> **`tools/CheckProfileMap.py` needs a hand edit, not a substitution.** Its
> `MODULE_MARKERS` at `:63-75` are **casefolded** — `"inetdriver"`, `"tnetsystem"`,
> `"fnetmanager"`, `"/microworld/net/"` — so a whole-word rename never touches them.
> They are *negative* checks, so if they go stale the gate keeps passing while it
> silently stops detecting leakage. Update them to `idevice`, `tnetworking`,
> `ftransportmanager`, `/microworld/transport/`. Also `:278` embeds the object name
> `libmicroworld_transport:NetDriver.obj`.
>
> **`Messaging/MessageChannelBinding.h`** has a comment explaining why it avoids
> naming Transport's result enum. Update the names inside it; keep the explanation.
>
> **Re-run the formatter afterwards.** `ENetResult` -> `ETransportResult` is six
> characters wider and will re-wrap lines near the 150-column limit. The command
> must be `clang-format -i --style=file:<repo>/clang-format` — the repo config has
> **no leading dot**, so plain `clang-format -i` silently applies LLVM style.
> `CheckFormatting.py` only scans `git ls-files`, so stage the renames first.

### Step 2 — Layout

```
Transport/Lora/         <- E32Lora.h, RadioE32Driver.*, E32LoraTransportState.cpp
Transport/Lora/Detail/  <- E32LoraTransportState.h  (from Transport/Detail/)
Transport/Wifi/         <- UdpAddressCodec.h
```

#### Implementer Context

> **Write the folder guides BEFORE moving anything.** `CheckFolderAgents.py` runs
> inside ctest with `--root Modules/` (`Modules/CMakeLists.txt:543`) and requires
> an `AGENTS.md` in **every** directory, each carrying an architecture heading and
> a concepts heading. `Lora/`, `Lora/Detail/` and `Wifi/` all need one, or this
> unit ends red. The existing `Transport/Detail/AGENTS.md` moves to `Lora/Detail/`
> and must be rewritten to describe only the E32 state it now covers.
>
> Use `git mv` so history follows the file into Stage 2's diff.
>
> Grep each moved filename **repo-wide**. `UdpAddressCodec.h` is included by both
> UDP platform adapters and by their address headers — `Platform/Esp32/UdpAddress.h`,
> `Platform/Host/HostUdpDriver.h` and others outside Transport.
>
> `Modules/library.json` ships `+<Transport/>`, which recurses, so new subfolders
> need no manifest edit.

### Step 3 — Documents and the model

#### Implementer Context

> `docs/architecture/model.c4` — Networking's `contract` metadata reads
> *"TNetSystem, an IPlaySystem that owns net drivers and channels..."*. Both are
> retired words, and the model must not be the last place using vocabulary it
> removed.
>
> `Transport/AGENTS.md` and `Networking/AGENTS.md` describe the renamed types in
> prose, including a paragraph about the `Net` package folding into Transport that
> now reads oddly.
>
> `CLAUDE.md`, root `AGENTS.md`, `README.md`, `examples/README.md`, and the
> per-example `AGENTS.md` files.
>
> **Rename only.** Do not rewrite reasoning — a prose rewrite hides the rename in
> the diff.

## 7. Test Strategy

No new tests. Behaviour does not change, so the existing suite is the test.

| Gate | Covers |
| --- | --- |
| `ctest` — unit + behaviour tests | Core, Transport, Messaging, Networking, Platform/Host |
| `microworld_format_check` | formatting after the re-wrap |
| `microworld_folder_guides` | an `AGENTS.md` in every `Modules/` directory |
| `microworld_class_documentation` | doxygen on classes, and it scans `.md` too |
| `CheckDependencyBoundaries.py` | `Core <- Transport`, Engine still blind to Transport |
| `CheckProfileMap.py` | profile bundles — **only if its markers were updated** |
| `likec4 validate` + `CheckFolderAgents.py --root docs` | unit 3 only |

### What ctest does NOT cover — verify by hand

The build is **not** a complete check. `Modules/CMakeLists.txt` defines only
`microworld_platform_host` and an INTERFACE pico target, and compiles just two
examples (`HostLifecycle`, `TwoNodeDemo`). Uncompiled by ctest:

- **`Platform/Esp32/`** — ~10 files, ~200 `Net` uses.
- **The 26 numbered examples** — PlatformIO projects.
- **`tests/Core/consumer/`** — standalone CMake, not reached by `add_subdirectory`.

A missed rename in those is a silent break, not a compile error. Before calling
Unit 1 or Unit 2 done, run a PlatformIO compile of the ESP32 consumer and of
examples 15, 16, 17, 24 and 25 — the five that between them touch UDP, LoRa,
`PacketDropDriver` and the Networking API.

## 8. Risks and Pitfalls

- **A loose `Net` pattern** corrupts `Networking`, `Network`, and `NetifResult`
  (lwIP, not ours). Whole-word, explicit list, longest-first.
- **`CheckProfileMap.py` going silently blind** — the worst failure here, because
  nothing reports it. Its markers are casefolded negatives; stale ones pass forever.
- **The ESP32 and example blind spots** above — the reason §7 has a manual section.
- **Formatter re-wrap noise** in the Unit 1 diff. Expected and unavoidable; it is
  why the formatter runs in the same unit rather than later.
- **Generated diagrams and benchmark results** must be excluded from substitution.
- **Doc counts go stale** — `Transport/AGENTS.md` and root `AGENTS.md` state file
  counts. Re-derive; do not assume.

## 9. Rollback Strategy

One commit per unit on a green tree, so `git revert <sha>` undoes any one
independently. No migrations, no generated artifacts, no external state.

## 10. Verification

After **each** unit:

```bash
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

The completeness check, which must return **zero** lines:

```bash
rg -n "Net" --glob '!.claude/**' --glob '!docs/diagrams/**' --glob '!**/Results/*.md' | rg -v "NetifResult|Network|docs/UE5ConceptMap.md"
```

Unit 3 additionally:

```bash
likec4 validate docs/architecture && python tools/CheckFolderAgents.py --root docs
```

A unit is not done until its own run is green. Reporting a unit complete on a red
build is the failure this section exists to prevent.

## 11. Task Breakdown

### Unit 1 — Vocabulary

- **1.1** Substitute every identifier per §2, whole-word, longest-first, within the
  extension scope and exclusions in §6 Step 1.
- **1.2** `git mv` the files in §6 Step 1 and fix every `#include` naming them.
- **1.3** Hand-edit `tools/CheckProfileMap.py` `MODULE_MARKERS` (casefolded) and
  the embedded object name at `:278`.
- **1.4** Update `Modules/CMakeLists.txt` source lists and its stale comments
  ("was Net", "the former Net package", "was Integration"), plus the `library.json`
  descriptions.
- **1.5** Re-run clang-format with `--style=file:<repo>/clang-format`.
- **Done when**: `ctest` green, the §10 completeness command returns zero lines,
  and PlatformIO compiles the ESP32 consumer plus examples 15/16/17/24/25.

### Unit 2 — Layout

- **2.1** Author `Lora/AGENTS.md`, `Lora/Detail/AGENTS.md`, `Wifi/AGENTS.md` —
  each with an architecture heading and a concepts heading — **before** moving.
- **2.2** `git mv` the LoRa and Wi-Fi files; rewrite the old `Detail/AGENTS.md`
  for its narrower new scope.
- **2.3** Fix include paths repo-wide for every moved header.
- **2.4** Update `Modules/CMakeLists.txt` paths.
- **Done when**: `ctest` green, `Transport/` matches §4, PlatformIO set compiles.

### Unit 3 — Documents and the model

- **3.1** `docs/architecture/model.c4` — Networking's contract text.
- **3.2** `Transport/AGENTS.md`, `Networking/AGENTS.md`, and the moved-folder guides.
- **3.3** `CLAUDE.md`, root `AGENTS.md`, `README.md`, `examples/README.md`, and the
  per-example `AGENTS.md` files.
- **Done when**: `likec4 validate` and both folder gates pass, §10 returns zero.

## 12. Plan History

- 2026-07-31: v1 written from the approved concept.
- 2026-07-31: v2 after an adversarial review. Frozen list removed (§3); folder
  guides added to Unit 2 before the moves; `CheckProfileMap.py` markers made an
  explicit task; the `IDevice` sketch corrected against the real header; the
  ctest coverage gap for ESP32 and examples written down; `Testing/` dropped;
  substitution scope and exclusions stated; formatter re-run made a step.
