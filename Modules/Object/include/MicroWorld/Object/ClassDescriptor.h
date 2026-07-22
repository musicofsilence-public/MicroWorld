#pragma once

#include <MicroWorld/Object/ObjectHandle.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld
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
	bool IsChildOf(const FClassDescriptor& CandidateParent) const noexcept
	{
		const FClassDescriptor* SlowDescriptor = this;
		const FClassDescriptor* FastDescriptor = this;
		while (FastDescriptor != nullptr && FastDescriptor->Parent != nullptr)
		{
			SlowDescriptor = SlowDescriptor->Parent;
			FastDescriptor = FastDescriptor->Parent->Parent;
			if (SlowDescriptor == FastDescriptor)
			{
				return false;
			}
		}

		const FClassDescriptor* CurrentDescriptor = this;
		while (CurrentDescriptor != nullptr)
		{
			if (CurrentDescriptor == &CandidateParent)
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
	EObjectResult Register(const FClassDescriptor& Descriptor) noexcept
	{
		if (!HasValidLayout(Descriptor) || Descriptor.TypeId == 0 || Descriptor.Destroy == nullptr || Descriptor.TypeToken == nullptr)
		{
			return EObjectResult::InvalidClassDescriptor;
		}
		if (Find(Descriptor.TypeId) != nullptr)
		{
			return EObjectResult::DuplicateClass;
		}
		if (!HasValidParentChain(Descriptor))
		{
			return EObjectResult::UnknownClass;
		}
		if (RegisteredClassCount >= MaxClasses)
		{
			return EObjectResult::CapacityExceeded;
		}

		RegisteredClasses[RegisteredClassCount] = Descriptor;
		++RegisteredClassCount;
		return EObjectResult::Success;
	}

	/** Finds a registered descriptor by local type identifier without changing registry state. */
	const FClassDescriptor* Find(const FTypeId TypeId) const noexcept
	{
		for (std::size_t Index = 0; Index < RegisteredClassCount; ++Index)
		{
			if (RegisteredClasses[Index].TypeId == TypeId)
			{
				return &RegisteredClasses[Index];
			}
		}
		return nullptr;
	}

	/** Reports fixed registry occupancy for capacity planning and tests. */
	std::size_t ClassCount() const noexcept { return RegisteredClassCount; }

private:
	/** Rejects zero and non-power-of-two layout requirements before registration. */
	static bool HasValidLayout(const FClassDescriptor& Descriptor) noexcept
	{
		return Descriptor.SizeBytes > 0 && Descriptor.AlignmentBytes > 0 && (Descriptor.AlignmentBytes & (Descriptor.AlignmentBytes - 1U)) == 0;
	}

	/** Requires an already registered, finite parent chain that cannot include the candidate. */
	bool HasValidParentChain(const FClassDescriptor& Descriptor) const noexcept
	{
		if (Descriptor.Parent == nullptr)
		{
			return true;
		}
		if (Find(Descriptor.Parent->TypeId) != Descriptor.Parent)
		{
			return false;
		}

		const FClassDescriptor* Current = Descriptor.Parent;
		std::size_t VisitedDescriptors = 0;
		while (Current != nullptr)
		{
			if (Current == &Descriptor || VisitedDescriptors >= RegisteredClassCount)
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
};

} // namespace MicroWorld
