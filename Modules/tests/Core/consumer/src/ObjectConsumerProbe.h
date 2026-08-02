#pragma once

#include "MemoryConsumerProbe.h"

#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectRootEntry.h>
#include <MicroWorld/Engine/ObjectSlotMetadata.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStats.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>
#include <MicroWorld/Engine/WeakObjectPtr.h>
#include <MicroWorld/Core/Version.h>

#include <cstddef>
#include <cstdint>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 4);
static_assert(MicroWorld::Version.Patch == 0);

namespace MicroWorldConsumer
{

/**
 * Motivation: Stable process exit codes that identify the exact Core+Object public-API probe failure.
 * Responsibilities: Name each distinct object-API failure so the probe reports the exact broken step.
 * Example:
 *   EObjectConsumerExitCode Code = EObjectConsumerExitCode::Success;
 */
enum class EObjectConsumerExitCode : int
{
	Success = 0,					  ///< Motivation: Reports the probe observed every object API succeeding.
	ClassRegistrationFailed = 1,	  ///< Motivation: Names a class-descriptor registration the registry rejected.
	RegisteredDescriptorMissing = 2,  ///< Motivation: Names a lookup that did not return the just-registered descriptor.
	StoreConfigurationFailed = 3,	  ///< Motivation: Names an object store that rejected its caller-owned storage.
	ObjectCreationFailed = 4,		  ///< Motivation: Names an object construction that did not return a live pointer.
	StrongRootAcquireFailed = 5,	  ///< Motivation: Names a strong-root acquire that did not return a live pointer.
	RootedCollectionDidNotRetain = 6, ///< Motivation: Names a rooted collection that reclaimed a still-referenced object.
	ProbeOutcomeMismatch = 7,		  ///< Motivation: Names a final outcome check that did not match the expected state.
	MemoryProfileFailureOffset = 100, ///< Motivation: Offsets the nested memory-probe failure codes out of the object range.
};

/**
 * Motivation: Supplies one concrete managed type for downstream construction and collection.
 * Responsibilities: Derive UObject so a descriptor can construct and destroy one user object.
 * Example:
 *   const FClassDescriptor D = MakeClassDescriptor<FConsumerObject>(1, "ConsumerObject");
 */
class FConsumerObject final : public MicroWorld::Engine::UObject
{
public:
	/**
	 * Motivation: Makes exact descriptor-driven destruction publicly instantiable.
	 * Responsibilities: Keep the destructor defaulted so the descriptor's destroy path can call it.
	 */
	~FConsumerObject() noexcept override = default;
};

/** Motivation: One-slot object store configuration the probe exercises. */
inline constexpr std::uint32_t ProbeSlotCount = 1;
inline constexpr std::uint32_t ProbeRootCapacity = 1;
inline constexpr std::size_t ProbeSlotSizeBytes = 128;
inline constexpr std::size_t ProbeSlotAlignmentBytes = 16;
inline constexpr MicroWorld::Engine::FTypeId ConsumerObjectTypeId = 1;

} // namespace MicroWorldConsumer

/**
 * Motivation: Exercises representative Core+Object public APIs without platform I/O.
 * Responsibilities: Register, create, root, collect, and reclaim one managed object and report the first failure code.
 */
inline int RunObjectConsumerProbe() noexcept
{
	using namespace MicroWorld::Core;
	using namespace MicroWorld::Engine;
	using MicroWorldConsumer::EObjectConsumerExitCode;
	using MicroWorldConsumer::FConsumerObject;

	const int MemoryProfileResult = RunMemoryConsumerProbe();
	if (MemoryProfileResult != 0)
	{
		return static_cast<int>(EObjectConsumerExitCode::MemoryProfileFailureOffset) + MemoryProfileResult;
	}

	TClassRegistry<MicroWorldConsumer::ProbeSlotCount> Registry;
	const FClassDescriptor Descriptor = MakeClassDescriptor<FConsumerObject>(MicroWorldConsumer::ConsumerObjectTypeId, "ConsumerObject");
	if (Registry.Register(Descriptor) != EObjectResult::Success)
	{
		return static_cast<int>(EObjectConsumerExitCode::ClassRegistrationFailed);
	}
	const FClassDescriptor* const RegisteredDescriptor = Registry.Find(Descriptor.TypeId);
	if (RegisteredDescriptor == nullptr)
	{
		return static_cast<int>(EObjectConsumerExitCode::RegisteredDescriptorMissing);
	}

	alignas(MicroWorldConsumer::ProbeSlotAlignmentBytes)
		std::byte SlotBytes[MicroWorldConsumer::ProbeSlotSizeBytes * MicroWorldConsumer::ProbeSlotCount]{};
	FObjectSlotMetadata Slots[MicroWorldConsumer::ProbeSlotCount]{};
	FObjectRootEntry Roots[MicroWorldConsumer::ProbeRootCapacity]{};
	FObjectStore Store(
		FObjectStoreStorage{
			SlotBytes,
			sizeof(SlotBytes),
			Slots,
			MicroWorldConsumer::ProbeSlotCount,
			MicroWorldConsumer::ProbeSlotSizeBytes,
			MicroWorldConsumer::ProbeSlotAlignmentBytes,
			Roots,
			MicroWorldConsumer::ProbeRootCapacity,
		},
		MakeClassRegistryView(Registry));
	if (Store.ConfigurationResult() != EObjectResult::Success)
	{
		return static_cast<int>(EObjectConsumerExitCode::StoreConfigurationFailed);
	}

	const TObjectCreationResult<FConsumerObject> Creation = Store.NewObject<FConsumerObject>(*RegisteredDescriptor);
	const bool bCreationSucceeded = Creation.Result == EObjectResult::Success && Creation.Object.Get() != nullptr;
	if (!bCreationSucceeded)
	{
		return static_cast<int>(EObjectConsumerExitCode::ObjectCreationFailed);
	}
	const TWeakObjectPtr<FConsumerObject> WeakObject(Creation.Object);
	TStrongObjectPointerResult<FConsumerObject> Root = Store.MakeStrongObjectPtr(Creation.Object);
	const bool bRootAcquired = Root.Result == EObjectResult::Success && Root.Pointer.Get() != nullptr;
	if (!bRootAcquired)
	{
		return static_cast<int>(EObjectConsumerExitCode::StrongRootAcquireFailed);
	}

	FObjectHandle Worklist[MicroWorldConsumer::ProbeSlotCount]{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist, MicroWorldConsumer::ProbeSlotCount});
	const FGarbageCollectionResult RootedCollection = Collector.CollectFull();
	const bool bRootedCollectionHeldObject =
		RootedCollection.Result == ERuntimeResult::Success && RootedCollection.bCycleComplete && RootedCollection.ObjectsReclaimed == 0;
	if (!bRootedCollectionHeldObject)
	{
		return static_cast<int>(EObjectConsumerExitCode::RootedCollectionDidNotRetain);
	}

	Root.Pointer.Reset();
	const FGarbageCollectionResult UnrootedCollection = Collector.CollectFull();
	const FObjectStoreStats FinalStats = Store.Stats();
	const bool bUnrootedReclaimed = UnrootedCollection.Result == ERuntimeResult::Success && UnrootedCollection.bCycleComplete
		&& UnrootedCollection.ObjectsReclaimed == MicroWorldConsumer::ProbeSlotCount;
	const bool bWeakExpired = WeakObject.IsExpired();
	const bool bStoreEmpty = FinalStats.OccupiedSlots == 0;
	const bool bProbeSucceeded = bUnrootedReclaimed && bWeakExpired && bStoreEmpty;
	return bProbeSucceeded ? static_cast<int>(EObjectConsumerExitCode::Success) : static_cast<int>(EObjectConsumerExitCode::ProbeOutcomeMismatch);
}
