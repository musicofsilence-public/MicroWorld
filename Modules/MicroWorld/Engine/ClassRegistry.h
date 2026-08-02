#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ObjectResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Stores a fixed explicit class set owned by the application entry point with stable descriptor addresses.
 * Responsibilities: Validate and store descriptors without allocation or partial mutation, keep owned addresses stable so
 *   parent pointers and registry views survive, and reserve a low-id range for application descriptors.
 * Example:
 *   TClassRegistry<8> Registry;
 *   if (Registry.Register(Descriptor) == EObjectResult::Success) { Build(); }
 */
template<std::size_t MaxClasses>
class TClassRegistry final
{
public:
	/** Motivation: Reserves low IDs for application-authored explicit descriptors by starting automatic IDs in a named range. */
	static constexpr FTypeId FirstAutomaticTypeId = 0x80000000U;

	/**
	 * Motivation: Creates an empty registry whose owned descriptor addresses remain stable for the registry's lifetime.
	 * Responsibilities: Default-construct the storage without any registered descriptor.
	 */
	TClassRegistry() noexcept = default;

	/**
	 * Motivation: Preserves owned descriptor and parent addresses retained by registry views.
	 * Responsibilities: Reject copy construction so descriptor addresses never move.
	 */
	TClassRegistry(const TClassRegistry&) = delete;

	/**
	 * Motivation: Prevents reassigning descriptor identity behind existing stores and views.
	 * Responsibilities: Reject copy assignment so descriptor addresses never move.
	 */
	TClassRegistry& operator=(const TClassRegistry&) = delete;

	/**
	 * Motivation: Preserves owned descriptor addresses retained by registered child parents.
	 * Responsibilities: Reject move construction so descriptor addresses never move.
	 */
	TClassRegistry(TClassRegistry&&) = delete;

	/**
	 * Motivation: Prevents moving descriptor identity behind existing stores and views.
	 * Responsibilities: Reject move assignment so descriptor addresses never move.
	 */
	TClassRegistry& operator=(TClassRegistry&&) = delete;

	/**
	 * Motivation: Lets a caller register one fully validated descriptor without allocation or partial mutation.
	 * Responsibilities: Reject a malformed descriptor, a duplicate id, an unregistered or cyclic parent, and a full
	 *   registry, leaving state unchanged in every failure case.
	 */
	EObjectResult Register(const FClassDescriptor& InDescriptor) noexcept
	{
		if (!HasExplicitDescriptorIdentity(InDescriptor))
		{
			return EObjectResult::InvalidClassDescriptor;
		}
		if (Find(InDescriptor.TypeId) != nullptr)
		{
			return EObjectResult::DuplicateClass;
		}
		if (!HasValidParentChain(InDescriptor))
		{
			return EObjectResult::UnknownClass;
		}
		if (RegisteredClassCount >= MaxClasses)
		{
			return EObjectResult::CapacityExceeded;
		}

		RegisteredClasses[RegisteredClassCount] = InDescriptor;
		++RegisteredClassCount;
		return EObjectResult::Success;
	}

	/**
	 * Motivation: Lets a caller find a registered descriptor by local type identifier without changing registry state.
	 * Responsibilities: Return the matching descriptor's address or null.
	 */
	const FClassDescriptor* Find(const FTypeId InTypeId) const noexcept
	{
		for (std::size_t Index = 0; Index < RegisteredClassCount; ++Index)
		{
			if (RegisteredClasses[Index].TypeId == InTypeId)
			{
				return &RegisteredClasses[Index];
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Lets a caller find a canonical descriptor by exact no-RTTI C++ type identity.
	 * Responsibilities: Return the matching descriptor's address or null for a null or unknown token.
	 */
	const FClassDescriptor* FindByTypeToken(const void* const InTypeToken) const noexcept
	{
		if (InTypeToken == nullptr)
		{
			return nullptr;
		}
		for (std::size_t Index = 0; Index < RegisteredClassCount; ++Index)
		{
			if (RegisteredClasses[Index].TypeToken == InTypeToken)
			{
				return &RegisteredClasses[Index];
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Lets a caller register a direct descriptor with a bounded local ID and receive its stable owned address.
	 * Responsibilities: Return an existing descriptor for a known type token, else assign a fresh automatic id and return
	 *   the new stable address, rejecting a malformed candidate, an unregistered parent, or a full registry.
	 */
	EObjectResult RegisterAutomatic(FClassDescriptor InCandidate, const FClassDescriptor*& OutDescriptor) noexcept
	{
		OutDescriptor = nullptr;
		if (const FClassDescriptor* const Existing = FindByTypeToken(InCandidate.TypeToken))
		{
			OutDescriptor = Existing;
			return EObjectResult::Success;
		}
		if (!HasUnassignedAutomaticIdentity(InCandidate))
		{
			return EObjectResult::InvalidClassDescriptor;
		}
		if (!HasValidParentChain(InCandidate))
		{
			return EObjectResult::UnknownClass;
		}
		if (RegisteredClassCount >= MaxClasses)
		{
			return EObjectResult::CapacityExceeded;
		}

		const FTypeId AutomaticTypeId = AllocateAutomaticTypeId();
		if (AutomaticTypeId == 0)
		{
			return EObjectResult::CapacityExceeded;
		}
		InCandidate.TypeId = AutomaticTypeId;
		RegisteredClasses[RegisteredClassCount] = InCandidate;
		OutDescriptor = &RegisteredClasses[RegisteredClassCount];
		++RegisteredClassCount;
		return EObjectResult::Success;
	}

	/**
	 * Motivation: Lets a caller report fixed registry occupancy for capacity planning and tests.
	 * Responsibilities: Return the count of successfully registered descriptors.
	 */
	std::size_t ClassCount() const noexcept { return RegisteredClassCount; }

private:
	/**
	 * Motivation: Lets RegisterAutomatic find an unused non-zero local automatic ID with at most MaxClasses probes.
	 * Responsibilities: Advance the next-id cursor, skip wrap-to-zero and collisions, and return zero when exhausted.
	 */
	FTypeId AllocateAutomaticTypeId() noexcept
	{
		for (std::size_t Probe = 0; Probe < MaxClasses; ++Probe)
		{
			const FTypeId Candidate = NextAutomaticTypeId;
			++NextAutomaticTypeId;
			if (NextAutomaticTypeId == 0)
			{
				NextAutomaticTypeId = FirstAutomaticTypeId;
			}
			if (Candidate != 0 && Find(Candidate) == nullptr)
			{
				return Candidate;
			}
		}
		return 0;
	}

	/**
	 * Motivation: Lets registration reject zero and non-power-of-two layout requirements before registry state changes.
	 * Responsibilities: Report true only when size and alignment are positive and alignment is a power of two.
	 */
	static bool HasValidLayout(const FClassDescriptor& InDescriptor) noexcept
	{
		return InDescriptor.SizeBytes > 0 && InDescriptor.AlignmentBytes > 0
			&& (InDescriptor.AlignmentBytes & (InDescriptor.AlignmentBytes - 1U)) == 0;
	}

	/**
	 * Motivation: Reports whether a descriptor carries the non-zero id, layout, and callables an explicit Register requires.
	 * Responsibilities: Check valid layout, a non-zero id, a destructor, and a type token together.
	 */
	static bool HasExplicitDescriptorIdentity(const FClassDescriptor& InDescriptor) noexcept
	{
		const bool bHasValidId = InDescriptor.TypeId != 0;
		const bool bHasDestructor = InDescriptor.Destroy != nullptr;
		const bool bHasTypeToken = InDescriptor.TypeToken != nullptr;
		return HasValidLayout(InDescriptor) && bHasValidId && bHasDestructor && bHasTypeToken;
	}

	/**
	 * Motivation: Reports whether a candidate is well-formed and carries no caller-assigned id so automatic allocation may assign one.
	 * Responsibilities: Check a zero id alongside valid layout, a destructor, and a type token.
	 */
	static bool HasUnassignedAutomaticIdentity(const FClassDescriptor& InDescriptor) noexcept
	{
		const bool bIdUnassigned = InDescriptor.TypeId == 0;
		const bool bHasDestructor = InDescriptor.Destroy != nullptr;
		const bool bHasTypeToken = InDescriptor.TypeToken != nullptr;
		return bIdUnassigned && HasValidLayout(InDescriptor) && bHasDestructor && bHasTypeToken;
	}

	/**
	 * Motivation: Lets registration reject a parent chain that is unregistered, cyclic, or self-referential.
	 * Responsibilities: Confirm the parent is the registry's own copy and that the chain terminates within the registry
	 *   without revisiting the candidate.
	 */
	bool HasValidParentChain(const FClassDescriptor& InDescriptor) const noexcept
	{
		if (InDescriptor.Parent == nullptr)
		{
			return true;
		}
		if (Find(InDescriptor.Parent->TypeId) != InDescriptor.Parent)
		{
			return false;
		}

		const FClassDescriptor* Current = InDescriptor.Parent;
		std::size_t VisitedDescriptors = 0;
		while (Current != nullptr)
		{
			// No valid ancestry is longer than the registry, so reaching
			// RegisteredClassCount visits (or re-reaching InDescriptor) means the
			// chain is cyclic or corrupt.
			if (Current == &InDescriptor || VisitedDescriptors >= RegisteredClassCount)
			{
				return false;
			}
			Current = Current->Parent;
			++VisitedDescriptors;
		}
		return true;
	}

	/** Motivation: Owns validated descriptors with fixed capacity and stable process-local addresses. */
	std::array<FClassDescriptor, MaxClasses> RegisteredClasses{};

	/** Motivation: Bounds registry scans to descriptors accepted by successful registration. */
	std::size_t RegisteredClassCount{0};

	/** Motivation: Starts automatic IDs in a named range while collision probes preserve local uniqueness. */
	FTypeId NextAutomaticTypeId{FirstAutomaticTypeId};
};

} // namespace MicroWorld::Engine
