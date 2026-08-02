#pragma once

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Engine
{

class FReferenceCollector;
class UObject;

/** Motivation: Identifies one explicitly registered managed class without C++ RTTI. */
using FTypeId = std::uint32_t;

/**
 * Motivation: Provides one writable zero-initialized byte for each exact managed C++ type so identical-code folding
 *   cannot merge their addresses in optimized builds.
 * Responsibilities: Hold a process-local address that stays a valid per-type token.
 * Example:
 *   const void* Token = ManagedObjectTypeToken<FMyType>();
 */
template<typename T>
inline std::uint8_t ManagedObjectTypeTokenStorage = 0;

/**
 * Motivation: Returns exact process-local C++ type identity without RTTI or initialization.
 * Responsibilities: Return the address of the per-type writable token storage.
 */
template<typename T>
const void* ManagedObjectTypeToken() noexcept
{
	return &ManagedObjectTypeTokenStorage<T>;
}

/** Motivation: Visits every traced outgoing reference owned by one managed object. */
using FTraceObjectReferences = void (*)(UObject&, FReferenceCollector&) noexcept;

/** Motivation: Invokes the exact managed object's destructor without public delete access. */
using FDestroyManagedObject = void (*)(UObject&) noexcept;

/**
 * Motivation: Describes construction layout, ancestry, tracing, and exact destruction for a managed class without RTTI.
 * Responsibilities: Carry a stable type id, parent, layout, tracer, and destructor; answer finite ancestry queries; and
 *   detect a cyclic parent chain without trusting caller-supplied structure.
 * Example:
 *   if (Descriptor.IsChildOf(ActorDescriptor)) { TreatAsActor(); }
 */
struct FClassDescriptor
{
	/** Motivation: Provides stable local class identity within one explicit registry. */
	FTypeId TypeId{0};

	/** Motivation: Supports diagnostics only and never acts as a stable or wire identifier. */
	const char* DiagnosticName{nullptr};

	/** Motivation: Defines explicit no-RTTI ancestry and must already be registered. */
	const FClassDescriptor* Parent{nullptr};

	/** Motivation: Defines the exact placement-construction extent required by this class. */
	std::size_t SizeBytes{0};

	/** Motivation: Defines the power-of-two alignment required by this class. */
	std::size_t AlignmentBytes{0};

	/** Motivation: Enumerates outgoing managed references; null means the class owns none. */
	FTraceObjectReferences TraceReferences{nullptr};

	/** Motivation: Provides exact destruction and is mandatory for every registered class. */
	FDestroyManagedObject Destroy{nullptr};

	/** Motivation: Binds layout and callbacks to one exact C++ type without RTTI. */
	const void* TypeToken{nullptr};

	/**
	 * Motivation: Lets a caller test finite explicit descriptor ancestry without C++ RTTI or cyclic traversal.
	 * Responsibilities: Reject an acyclic but absent relationship, returning false rather than following a cyclic chain.
	 */
	bool IsChildOf(const FClassDescriptor& InCandidateParent) const noexcept
	{
		if (!HasAcyclicAncestry())
		{
			return false;
		}
		return AncestryContains(InCandidateParent);
	}

	/**
	 * Motivation: Lets a caller confirm the Parent chain is loop-free so the ancestry walk always terminates without RTTI.
	 * Responsibilities: Run a Floyd cycle check and report false when the two probes meet.
	 */
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

	/**
	 * Motivation: Lets IsChildOf walk the finite Parent chain to test for a candidate ancestor.
	 * Responsibilities: Report true only when the chain reaches the candidate parent.
	 */
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

} // namespace MicroWorld::Engine
