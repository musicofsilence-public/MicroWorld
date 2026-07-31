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

namespace MicroWorld::Core::RawStorage
{

/**
 * Motivation: Lets a fixed-capacity container or smart pointer begin one object lifetime in
 *   caller-owned raw storage without that storage ever implying a lifetime on its own.
 * Responsibilities: Forward every constructor argument via placement new into already sized and
 *   aligned storage and return a pointer to the newly live object.
 */
template<typename ValueType, typename... ConstructorArgumentTypes>
ValueType* ConstructAt(void* const InStorage, ConstructorArgumentTypes&&... Arguments) noexcept
{
	return ::new (InStorage) ValueType(std::forward<ConstructorArgumentTypes>(Arguments)...);
}

/**
 * Motivation: Lets an owner end the lifetime a prior ConstructAt began while keeping the
 *   underlying bytes caller-owned for reuse.
 * Responsibilities: Run the live object's destructor exactly once without releasing storage.
 */
template<typename ValueType>
void DestroyAt(ValueType* const InValue) noexcept
{
	InValue->~ValueType();
}

/**
 * Motivation: Lets an owner read and write through the storage of a live object begun in raw
 *   storage, where a bare reinterpret_cast would be undefined behavior.
 * Responsibilities: Hand back a laundered pointer the compiler must treat as pointing at the
 *   live ValueType object.
 */
template<typename ValueType>
ValueType* LaunderedPointer(void* const InStorage) noexcept
{
	return std::launder(reinterpret_cast<ValueType*>(InStorage));
}

/**
 * Motivation: Lets a const caller resolve the live read-only ValueType begun in raw storage
 *   under the same launder rule as the mutable overload.
 * Responsibilities: Hand back a laundered const pointer safe to read through.
 */
template<typename ValueType>
const ValueType* LaunderedPointer(const void* const InStorage) noexcept
{
	return std::launder(reinterpret_cast<const ValueType*>(InStorage));
}

} // namespace MicroWorld::Core::RawStorage
