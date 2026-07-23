#pragma once

// The one place the raw-slot lifetime ritual lives. Every fixed-capacity Memory
// container and smart pointer keeps aligned storage that holds bytes with NO
// object lifetime until it deliberately begins one. These three helpers are that
// begin / end / observe ritual:
//   - ConstructAt begins a ValueType lifetime in the storage (placement new).
//   - DestroyAt ends it (explicit destructor call); the bytes stay caller-owned.
//   - LaunderedPointer hands back a pointer the compiler treats as pointing at
//     the live object.
// Why LaunderedPointer needs std::launder: the storage was declared as some
// other type (an aligned_storage slot or a byte buffer), so a bare
// reinterpret_cast to ValueType* would let the optimizer assume no live
// ValueType is there, making a read through it undefined behavior. std::launder
// tells the compiler a ValueType really does live at these bytes -- the single
// subtle rule this header exists to state once.

#include <new>
#include <utility>

namespace MicroWorld::Detail
{

/**
 * Begins one `ValueType` lifetime in caller-owned raw storage.
 *
 * @tparam ValueType Type to construct; specify explicitly (it is not deduced).
 * @tparam ConstructorArgumentTypes Argument types forwarded to the constructor.
 * @param Storage Address of storage already sized and aligned for `ValueType`.
 * @param Arguments Constructor arguments forwarded to `ValueType`.
 * @return Pointer to the newly live object.
 */
template<typename ValueType, typename... ConstructorArgumentTypes>
ValueType* ConstructAt(void* const Storage, ConstructorArgumentTypes&&... Arguments) noexcept
{
	return ::new (Storage) ValueType(std::forward<ConstructorArgumentTypes>(Arguments)...);
}

/**
 * Ends the lifetime a prior `ConstructAt` began; the storage bytes stay caller-owned.
 *
 * @param Value Live object whose destructor to run.
 */
template<typename ValueType>
void DestroyAt(ValueType* const Value) noexcept
{
	Value->~ValueType();
}

/**
 * Resolves the live `ValueType` begun in raw storage (see the launder rule above).
 *
 * @tparam ValueType Type of the live object; specify explicitly.
 * @param Storage Address whose bytes hold a live `ValueType`.
 * @return Laundered pointer safe to read and write through.
 */
template<typename ValueType>
ValueType* LaunderedPointer(void* const Storage) noexcept
{
	return std::launder(reinterpret_cast<ValueType*>(Storage));
}

/**
 * Const overload: resolves a live read-only `ValueType` in raw storage.
 *
 * @tparam ValueType Type of the live object; specify explicitly.
 * @param Storage Address whose bytes hold a live `ValueType`.
 * @return Laundered read-only pointer.
 */
template<typename ValueType>
const ValueType* LaunderedPointer(const void* const Storage) noexcept
{
	return std::launder(reinterpret_cast<const ValueType*>(Storage));
}

} // namespace MicroWorld::Detail
