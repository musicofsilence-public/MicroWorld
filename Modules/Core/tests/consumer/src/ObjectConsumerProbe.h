#pragma once

#include "MemoryConsumerProbe.h"

#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Version.h>

#include <array>
#include <cstddef>
#include <cstdint>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 2);
static_assert(MicroWorld::Version.Patch == 0);

namespace MicroWorldConsumer
{

/** Supplies one concrete managed type for downstream construction and collection. */
class FConsumerObject final : public MicroWorld::UObject
{
public:
	/** Makes exact descriptor-driven destruction publicly instantiable. */
	~FConsumerObject() noexcept override = default;
};

} // namespace MicroWorldConsumer

/** Exercises representative Core+Memory+Object public APIs without platform I/O. */
inline int RunObjectConsumerProbe() noexcept
{
	using namespace MicroWorld;
	using MicroWorldConsumer::FConsumerObject;

	const int MemoryProfileResult = RunMemoryConsumerProbe();
	if (MemoryProfileResult != 0)
	{
		return 10 + MemoryProfileResult;
	}

	constexpr std::uint32_t SlotCount = 1;
	constexpr std::uint32_t RootCapacity = 1;
	constexpr std::size_t SlotSizeBytes = 128;
	constexpr std::size_t SlotAlignmentBytes = 16;

	TClassRegistry<1> Registry;
	const FClassDescriptor Descriptor = MakeClassDescriptor<FConsumerObject>(1, "ConsumerObject");
	if (Registry.Register(Descriptor) != EObjectResult::Success)
	{
		return 1;
	}
	const FClassDescriptor* const RegisteredDescriptor = Registry.Find(Descriptor.TypeId);
	if (RegisteredDescriptor == nullptr)
	{
		return 2;
	}

	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotBytes{};
	std::array<FObjectSlotMetadata, SlotCount> Slots{};
	std::array<FObjectRootEntry, RootCapacity> Roots{};
	FObjectStore Store(
		FObjectStoreStorage{
			SlotBytes.data(),
			SlotBytes.size(),
			Slots.data(),
			SlotCount,
			SlotSizeBytes,
			SlotAlignmentBytes,
			Roots.data(),
			RootCapacity,
		},
		MakeClassRegistryView(Registry));
	if (Store.ConfigurationResult() != EObjectResult::Success)
	{
		return 3;
	}

	const TObjectCreationResult<FConsumerObject> Creation = Store.NewObject<FConsumerObject>(*RegisteredDescriptor);
	if (Creation.Result != EObjectResult::Success || Creation.Object.Get() == nullptr)
	{
		return 4;
	}
	const TWeakObjectPtr<FConsumerObject> WeakObject(Creation.Object);
	TStrongObjectPointerResult<FConsumerObject> Root = Store.MakeStrongObjectPtr(Creation.Object);
	if (Root.Result != EObjectResult::Success || Root.Pointer.Get() == nullptr)
	{
		return 5;
	}

	std::array<FObjectHandle, SlotCount> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), SlotCount});
	const FGarbageCollectionResult RootedCollection = Collector.CollectFull();
	if (RootedCollection.Result != ERuntimeResult::Success || !RootedCollection.bCycleComplete || RootedCollection.ObjectsReclaimed != 0)
	{
		return 6;
	}

	Root.Pointer.Reset();
	const FGarbageCollectionResult UnrootedCollection = Collector.CollectFull();
	const FObjectStoreStats FinalStats = Store.Stats();
	return UnrootedCollection.Result == ERuntimeResult::Success && UnrootedCollection.bCycleComplete && UnrootedCollection.ObjectsReclaimed == 1
			&& WeakObject.IsExpired() && FinalStats.OccupiedSlots == 0
		? 0
		: 7;
}
