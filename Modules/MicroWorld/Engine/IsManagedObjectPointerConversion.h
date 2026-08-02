#pragma once

#include <MicroWorld/Engine/Object.h>

#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

/**
 * Motivation: Enables pointer conversions only after both endpoints prove managed ancestry, so unmanaged types never
 *   silently become convertible through a generic helper.
 * Responsibilities: Report false unless both From and To derive from UObject, and never perform runtime narrowing.
 * Example:
 *   static_assert(!TIsManagedObjectPointerConversion<int, UObject>::value);
 */
template<typename From, typename To, typename = void>
struct TIsManagedObjectPointerConversion : std::false_type
{
};

/**
 * Motivation: Accepts an accessible derived-to-base or same-type conversion between managed types while rejecting
 *   narrowing and unmanaged endpoints.
 * Responsibilities: Report standard convertibility only when both endpoints are UObject-derived, with no reflection or
 *   runtime check.
 * Example:
 *   static_assert(TIsManagedObjectPointerConversion<UActorComponent, UObject>::value);
 */
template<typename From, typename To>
struct TIsManagedObjectPointerConversion<
	From,
	To,
	std::void_t<
		decltype(static_cast<UObject*>(std::declval<typename std::remove_cv<From>::type*>())),
		decltype(static_cast<UObject*>(std::declval<typename std::remove_cv<To>::type*>()))>> : std::is_convertible<From*, To*>
{
};

} // namespace MicroWorld::Engine
