#pragma once

#include "MemoryConsumerProbe.h"

#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Version.h>

#include <cstddef>
#include <cstdint>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 3);
static_assert(MicroWorld::Version.Patch == 0);

namespace MicroWorldConsumer
{

/** Stable process exit codes that identify the exact Core+Object public-API probe failure. */
enum class EObjectConsumerExitCode : int
{
	Success = 0,
	ClassRegistrationFailed = 1,
	RegisteredDescriptorMissing = 2,
	StoreConfigurationFailed = 3,
	ObjectCreationFailed = 4,
	StrongRootAcquireFailed = 5,
	RootedCollectionDidNotRetain = 6,
	ProbeOutcomeMismatch = 7,
	MemoryProfileFailureOffset = 100,
};

/** Supplies one concrete managed type for downstream construction and collection. */
class FConsumerObject final : public MicroWorld::UObject
{
public:
	/** Makes exact descriptor-driven destruction publicly instantiable. */
	~FConsumerObject() noexcept override = default;
};

/** One-slot object store configuration the probe exercises. */
inline constexpr std::uint32_t ProbeSlotCount = 1;
inline constexpr std::uint32_t ProbeRootCapacity = 1;
inline constexpr std::size_t ProbeSlotSizeBytes = 128;
inline constexpr std::size_t ProbeSlotAlignmentBytes = 16;
inline constexpr MicroWorld::FTypeId ConsumerObjectTypeId = 1;

} // namespace MicroWorldConsumer

/** Exercises representative Core+Object public APIs without platform I/O. */
inline int RunObjectConsumerProbe() noexcept
{
	using namespace MicroWorld;
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
