# MicroWorld C++ Style

MicroWorld uses an embedded adaptation of UE5 naming without implying Unreal
compatibility.

| Kind | Rule | Example |
|---|---|---|
| Namespace | PascalCase product name | `MicroWorld` |
| Non-UObject class/struct | `F` prefix | `FApplication` |
| Class template | `T` prefix | `TStaticVector<uint32_t, 4>` |
| Enum | `E` prefix and PascalCase values | `ERuntimeResult::Success` |
| Scalar alias | No aggregate prefix; spell units | `TimePointMilliseconds` |
| Boolean | `b` prefix | `bShouldTick` |
| Public identifier | PascalCase | `SetTickInterval` |
| Public header | PascalCase, type-aligned | `TickFunction.h` |

`A` and `U` are reserved for real MicroWorld managed types with Object-store
identity, tracing, and lifecycle semantics. They do not imply Unreal
inheritance or source compatibility. Reflection vocabulary such as `UCLASS`,
`UPROPERTY`, and `GENERATED_BODY` remains forbidden.

Every complete class or class template has an adjacent one-to-three-sentence
`/** ... */` contract. Every function declaration, enumerator, configuration
field, and persistent/shared/state variable also has an adjacent
intent-focused Doxygen comment. State why the declaration exists, the ownership
or lifecycle boundary it protects, or the invariant it makes observable:

```cpp
/** Owns the bounded scheduling state for one independently tickable object. */
class FTickFunction final;

/** Rejects backward caller time before unsigned scheduling arithmetic can wrap. */
TimePointMilliseconds LastObservedMilliseconds{0};
```

Clear local variables document themselves through behavior-specific names. Add
a local comment only when the reason, safety constraint, or edge case cannot be
expressed in code; never write line-by-line narration such as “increment the
counter.” Comments explain intent and constraints rather than restating syntax.

Public code uses C++17, descriptive names, `const` values, `noexcept` where the
contract cannot fail through exceptions, early returns, fixed storage, and
bounded single-pass loops. Platform and product dependencies remain outside the
package.

All C/C++ package files use the tracked repository `clang-format` configuration.
Because the policy filename is `clang-format` rather than `.clang-format`, pass
it explicitly:

```sh
clang-format --style=file:clang-format -i <files>
clang-format --style=file:clang-format --dry-run --Werror <files>
```

## Simplicity rules

The rules below extend the conventions above. `CheckClassDocumentation.py
--require-doxygen` remains the gate for comment *presence* and the
three-sentence contract cap; this section defines the *content* that names
and comments should carry.

### Rule N — names state their whole role

- Spell names in full: `CalculateNextDueMilliseconds`, not `CalcNext`;
  `SourceNodeId`, not `SrcId`.
- Put the unit in the name of any scalar carrying one:
  `TickIntervalMilliseconds`, not `TickInterval`.
- Name booleans as a yes/no question with the `b` prefix: `bMustResetSchedule`.
- The codebase spells names out. The only abbreviations in use are
  established domain acronyms already present in the code, such as `Ipv4`,
  `UART`, `CRC`, and `Id`.

### Rule P — a function pointer is recognizable at every use site

`&Something` must read as taking the address of a *function*, never of an object.
A bare noun breaks that: `&Esp32LogSink` looked like an object's address, which is
what retired that name.

| Kind | Form | Examples |
| --- | --- | --- |
| The alias | `F<Verb>Function`, or a verb phrase | `FSleepFunction`, `FOutputDeviceFunction`, `FTestFunction`, `FTraceObjectReferences`, `FDestroyManagedObject` |
| A variable or member holding one | a bare verb — the alias already carries `Function` | `Invoke`, `MoveConstruct`, `Destroy`, `TraceReferences`, `WriteRecord` |
| A free function supplying one | a verb phrase | `SleepMilliseconds`, `WriteEsp32LogRecord`, `DestroyManagedObject<T>` |

Read the two suppliers in one `app_main` side by side; both announce themselves:

```cpp
MicroWorld::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);
static MicroWorld::TApplicationRunner<MicroWorld::FEsp32TimeSource> Runner{
	TimeSource, &MicroWorld::SleepMilliseconds, kFramePacingMilliseconds};
```

Adding `Function` to an alias whose root word is borrowed from UE5 is worth the
six characters: a type that cannot be recognized as callable costs every reader
a jump to its declaration.

### Rule F — a function performs at most two logical actions

A function should read as one or two steps plus its guards; guard clauses
(early-return rejections) do not count toward the two actions. When a
function grows past that, extract each extra step into a named helper and
let the original become an orchestrator whose body names the steps in order.

### Rule W — comments explain *why*, not *what*

Write a comment only when the reason, safety constraint, or edge case is not
already visible in the code. Never narrate syntax (`// increment the
counter`); state the invariant a line protects or the boundary it honors.

### Reference files — already at the target bar; imitate them, cite them

When a review argues about style, cite one of these rather than re-deriving the
rule. Each file already meets Rules N, F, and W.

| Module | Exemplary files |
| --- | --- |
| Core | `Lifecycle.h` (the single best file in the repo), `TickFunction.h`, `Application.h` |
| Memory | `Containers/Span.h`, `Memory/MemoryResource.h` |
| Object | `ObjectHandle.h`, `Object.h` |
| Engine | `EngineStorage.h` (member-level ownership docs), `NetworkFrame.h`, the enums in `EngineResult.h` / `Timer.h` |
| Net | `ByteReader.h`, `ByteWriter.h` |
| Platform | `WinSockScope.h`, `HostTimeSource.h`, `Esp32TimeSource.h`, and the boundary documentation in `src/UdpSocketPlatformImplementation.h` |

### Worked example — `FTickFunction::Advance`

Before, one function performs four logical actions: validate lifecycle and
time, first-tick reset, cadence gate, and produce the tick. After, the
guards remain but each step is a named helper, and `Advance` reads as a
table of contents.

Before:

```cpp
FTickDecision FTickFunction::Advance(const TimePointMilliseconds NowMilliseconds) noexcept
{
	if (!bPlaying)
	{
		return FTickDecision::Rejected(ERuntimeResult::InvalidLifecycle);
	}
	if (NowMilliseconds < LastObservedMilliseconds)
	{
		return FTickDecision::Rejected(ERuntimeResult::NonMonotonicTime);
	}
	LastObservedMilliseconds = NowMilliseconds;
	if (!bEnabled)
	{
		return FTickDecision::NotDue();
	}
	if (bMustResetSchedule)
	{
		bMustResetSchedule = false;
		PreviousTickMilliseconds = NowMilliseconds;
		NextDueMilliseconds = CalculateNextDueMilliseconds(NowMilliseconds);
		return FTickDecision::Ticked(NowMilliseconds, 0);
	}
	if (IntervalMilliseconds != 0)
	{
		const bool bBeforeDeadline = NowMilliseconds < NextDueMilliseconds;
		const bool bAlreadyTicked = NowMilliseconds == PreviousTickMilliseconds;
		if (bBeforeDeadline || bAlreadyTicked)
		{
			return FTickDecision::NotDue();
		}
	}
	const DurationMilliseconds DeltaMilliseconds = CalculateDeltaMilliseconds(NowMilliseconds);
	PreviousTickMilliseconds = NowMilliseconds;
	NextDueMilliseconds = CalculateNextDueMilliseconds(NowMilliseconds);
	return FTickDecision::Ticked(NowMilliseconds, DeltaMilliseconds);
}
```

After (the real current code, `Modules/Core/src/TickFunction.cpp:52-77`):

```cpp
FTickDecision FTickFunction::Advance(const TimePointMilliseconds NowMilliseconds) noexcept
{
	if (!bPlaying)
	{
		return FTickDecision::Rejected(ERuntimeResult::InvalidLifecycle);
	}
	if (NowMilliseconds < LastObservedMilliseconds)
	{
		return FTickDecision::Rejected(ERuntimeResult::NonMonotonicTime);
	}
	LastObservedMilliseconds = NowMilliseconds;

	if (!bEnabled)
	{
		return FTickDecision::NotDue();
	}
	if (bMustResetSchedule)
	{
		return BeginResetSchedule(NowMilliseconds);
	}
	if (!IsTickDueNow(NowMilliseconds))
	{
		return FTickDecision::NotDue();
	}
	return ProduceDueTick(NowMilliseconds);
}
```
