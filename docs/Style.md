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
