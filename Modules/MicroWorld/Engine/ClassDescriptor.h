#pragma once

#include <MicroWorld/Engine/ObjectHandle.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Engine
{

class FReferenceCollector;
class UObject;

/** Identifies one explicitly registered managed class without C++ RTTI. */
using FTypeId = std::uint32_t;

/**
 * Provides one writable zero-initialized byte for each exact managed C++ type.
 *
 * Writable objects cannot be merged by identical-code folding, so their
 * addresses remain valid process-local type tokens in optimized builds.
 */
template<typename T>
inline std::uint8_t ManagedObjectTypeTokenStorage = 0;

/** Returns exact process-local C++ type identity without RTTI or initialization. */
template<typename T>
const void* ManagedObjectTypeToken() noexcept
{
	return &ManagedObjectTypeTokenStorage<T>;
}

/** Visits every traced outgoing reference owned by one managed object. */
using FTraceObjectReferences = void (*)(UObject&, FReferenceCollector&) noexcept;

/** Invokes the exact managed object's destructor without public delete access. */
using FDestroyManagedObject = void (*)(UObject&) noexcept;

/** Describes construction layout, ancestry, tracing, and exact destruction. */
struct FClassDescriptor
{
	/** Provides stable local class identity within one explicit registry. */
	FTypeId TypeId{0};

	/** Supports diagnostics only and never acts as a stable or wire identifier. */
	const char* DiagnosticName{nullptr};

	/** Defines explicit no-RTTI ancestry and must already be registered. */
	const FClassDescriptor* Parent{nullptr};

	/** Defines the exact placement-construction extent required by this class. */
	std::size_t SizeBytes{0};

	/** Defines the power-of-two alignment required by this class. */
	std::size_t AlignmentBytes{0};

	/** Enumerates outgoing managed references; null means the class owns none. */
	FTraceObjectReferences TraceReferences{nullptr};

	/** Provides exact destruction and is mandatory for every registered class. */
	FDestroyManagedObject Destroy{nullptr};

	/** Binds layout and callbacks to one exact C++ type without RTTI. */
	const void* TypeToken{nullptr};

	/** Tests finite explicit descriptor ancestry without C++ RTTI or cyclic traversal. */
	bool IsChildOf(const FClassDescriptor& InCandidateParent) const noexcept
	{
		if (!HasAcyclicAncestry())
		{
			return false;
		}
		return AncestryContains(InCandidateParent);
	}

	/** Reports whether the Parent chain is loop-free so the ancestry walk always terminates without RTTI. */
	bool HasAcyclicAncestry() const noexcept
	{
		// Floyd cycle check: AncestryProbe advances one link, CycleDetectorProbe
		// two. A corrupted Parent chain that loops makes them meet.
		const FClassDescriptor* AncestryProbe = this;
		const FClassDescriptor* CycleDetectorProbe = this;
		while (CycleDetectorProbe != nullptr && CycleDetectorProbe->Parent != nullptr)
		{
			AncestryProbe = AncestryProbe->Parent;
			CycleDetectorProbe = CycleDetectorProbe->Parent->Parent;
			if (AncestryProbe == CycleDetectorProbe)
			{
				return false;
			}
		}
		return true;
	}

	/** Walks the finite Parent chain and reports whether it reaches the candidate ancestor. */
	bool AncestryContains(const FClassDescriptor& InCandidateParent) const noexcept
	{
		const FClassDescriptor* CurrentDescriptor = this;
		while (CurrentDescriptor != nullptr)
		{
			if (CurrentDescriptor == &InCandidateParent)
			{
				return true;
			}
			CurrentDescriptor = CurrentDescriptor->Parent;
		}
		return false;
	}
};

/** Stores a fixed explicit class set owned by the application composition root. */
template<std::size_t MaxClasses>
class TClassRegistry final
{
public:
	/** Reserves low IDs for application-authored explicit descriptors. */
	static constexpr FTypeId FirstAutomaticTypeId = 0x80000000U;
	/** Creates an empty registry whose owned descriptor addresses remain stable. */
	TClassRegistry() noexcept = default;

	/** Preserves owned descriptor and parent addresses retained by registry views. */
	TClassRegistry(const TClassRegistry&) = delete;

	/** Prevents reassigning descriptor identity behind existing stores and views. */
	TClassRegistry& operator=(const TClassRegistry&) = delete;

	/** Preserves owned descriptor addresses retained by registered child parents. */
	TClassRegistry(TClassRegistry&&) = delete;

	/** Prevents moving descriptor identity behind existing stores and views. */
	TClassRegistry& operator=(TClassRegistry&&) = delete;

	/** Registers one fully validated descriptor without allocation or partial mutation. */
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

	/** Finds a registered descriptor by local type identifier without changing registry state. */
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

	/** Finds a canonical descriptor by exact no-RTTI C++ type identity. */
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

	/** Registers a direct descriptor with a bounded local ID and returns its stable owned address. */
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

	/** Reports fixed registry occupancy for capacity planning and tests. */
	std::size_t ClassCount() const noexcept { return RegisteredClassCount; }

private:
	/** Finds an unused non-zero local automatic ID with at most MaxClasses probes. */
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
	/** Rejects zero and non-power-of-two layout requirements before registration. */
	static bool HasValidLayout(const FClassDescriptor& InDescriptor) noexcept
	{
		return InDescriptor.SizeBytes > 0 && InDescriptor.AlignmentBytes > 0
			&& (InDescriptor.AlignmentBytes & (InDescriptor.AlignmentBytes - 1U)) == 0;
	}

	/** Reports whether a descriptor carries the non-zero id, layout, and callables an explicit Register requires. */
	static bool HasExplicitDescriptorIdentity(const FClassDescriptor& InDescriptor) noexcept
	{
		const bool bHasValidId = InDescriptor.TypeId != 0;
		const bool bHasDestructor = InDescriptor.Destroy != nullptr;
		const bool bHasTypeToken = InDescriptor.TypeToken != nullptr;
		return HasValidLayout(InDescriptor) && bHasValidId && bHasDestructor && bHasTypeToken;
	}

	/** Reports whether a candidate is well-formed and carries no caller-assigned id (so automatic allocation may assign one). */
	static bool HasUnassignedAutomaticIdentity(const FClassDescriptor& InDescriptor) noexcept
	{
		const bool bIdUnassigned = InDescriptor.TypeId == 0;
		const bool bHasDestructor = InDescriptor.Destroy != nullptr;
		const bool bHasTypeToken = InDescriptor.TypeToken != nullptr;
		return bIdUnassigned && HasValidLayout(InDescriptor) && bHasDestructor && bHasTypeToken;
	}

	/** Requires an already registered, finite parent chain that cannot include the candidate. */
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

	/** Owns validated descriptors with fixed capacity and stable process-local addresses. */
	std::array<FClassDescriptor, MaxClasses> RegisteredClasses{};

	/** Bounds registry scans to descriptors accepted by successful registration. */
	std::size_t RegisteredClassCount{0};

	/** Starts automatic IDs in a named range while collision probes preserve local uniqueness. */
	FTypeId NextAutomaticTypeId{FirstAutomaticTypeId};
};

} // namespace MicroWorld::Engine
