#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <cstdint>

namespace
{

using MicroWorld::AActor;
using MicroWorld::EEngineResult;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FActorComponentRegistry;
using MicroWorld::FActorComponentRegistryBase;
using MicroWorld::FObjectRootEntry;
using MicroWorld::FObjectSlotMetadata;
using MicroWorld::FObjectStore;
using MicroWorld::FObjectStoreStorage;
using MicroWorld::FWorldActorRegistry;
using MicroWorld::FWorldActorRegistryBase;
using MicroWorld::MakeClassDescriptor;
using MicroWorld::MakeClassRegistryView;
using MicroWorld::TClassRegistry;
using MicroWorld::TObjectPtr;
using MicroWorld::UActorComponent;
using MicroWorld::UWorld;
using MicroWorld::Tests::TEngineEnvironment;

/** A minimal component used by registration rejection tests. */
class FPlainComponent final : public UActorComponent
{
public:
	FPlainComponent() noexcept : UActorComponent() {}
};

/** A minimal actor used by registration rejection tests. */
class FPlainActor final : public AActor
{
public:
	explicit FPlainActor(FActorComponentRegistryBase Components) noexcept : AActor(std::move(Components)) {}
};

constexpr MicroWorld::FTypeId PlainActorTypeId{0x00020001u};
constexpr MicroWorld::FTypeId PlainComponentTypeId{0x00020002u};

/** Environment sized for registration tests with capacity for several actors and components. */
using FRegistrationEnvironment = TEngineEnvironment<256, 16, 16, 4>;

/** Builds a plain actor in the environment through its own derived descriptor. */
TObjectPtr<FPlainActor> MakePlainActor(FRegistrationEnvironment& Env, FActorComponentRegistryBase Components) noexcept
{
	return Env.CreateDerivedObject<FPlainActor>(PlainActorTypeId, "PlainActor", std::move(Components));
}

/** Builds a plain component in the environment through its own derived descriptor. */
TObjectPtr<FPlainComponent> MakePlainComponent(FRegistrationEnvironment& Env) noexcept
{
	return Env.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
}

/** Builds a fresh standalone store so cross-store tests can use a second owner. */
class FSecondStore final
{
public:
	FSecondStore() noexcept : Store(MakeStorage(), MakeClassRegistryView(Registry))
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
		(void)Registry.Register(MakeClassDescriptor<FPlainActor>(
			PlainActorTypeId, "PlainActor", Registry.Find(MicroWorld::AActorClassId), &MicroWorld::TraceManagedObjectReferences));
		(void)Registry.Register(MakeClassDescriptor<FPlainComponent>(
			PlainComponentTypeId, "PlainComponent", Registry.Find(MicroWorld::UActorComponentClassId), &MicroWorld::TraceManagedObjectReferences));
	}

	FObjectStore& GetStore() noexcept { return Store; }
	TClassRegistry<8>& GetRegistry() noexcept { return Registry; }

private:
	FObjectStoreStorage MakeStorage() noexcept
	{
		return FObjectStoreStorage{SlotBytes.data(), SlotBytes.size(), Slots.data(), SlotCount, 256, 16, Roots.data(), RootCapacity};
	}

	static constexpr std::uint32_t SlotCount{4};
	static constexpr std::uint32_t RootCapacity{4};
	alignas(16) std::array<std::byte, 256 * SlotCount> SlotBytes{};
	std::array<FObjectSlotMetadata, SlotCount> Slots{};
	std::array<FObjectRootEntry, RootCapacity> Roots{};
	TClassRegistry<8> Registry;
	FObjectStore Store;
};

/**
 * Proves registering the same actor twice and the same component twice are both
 * rejected as duplicates and leave the registry unchanged.
 */
MW_TEST_CASE(EngineDuplicateActorAndComponentRegistrationRejected)
{
	FRegistrationEnvironment Env{};
	FActorComponentRegistry<2> ActorComponents;
	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryBase WorldActorsView = WorldActors.MakeView();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env, ActorComponents.MakeView());
	const TObjectPtr<FPlainComponent> Component = MakePlainComponent(Env);

	const EEngineResult FirstActor = World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	const EEngineResult DuplicateActor = World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	const EEngineResult FirstComponent = Actor.Get()->RegisterComponent(Component);
	const EEngineResult DuplicateComponent = Actor.Get()->RegisterComponent(Component);

	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstActor, "The first actor registration should succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, DuplicateActor, "A second registration of the same actor is a duplicate");
	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstComponent, "The first component registration should succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, DuplicateComponent, "A second registration of the same component is a duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "Duplicate actor registration must not advance the live count");
	MW_EXPECT_EQ(Test, std::size_t{1}, ActorComponents.GetCount(), "Duplicate component registration must not advance the live count");
}

/**
 * Proves full-capacity and zero-capacity registries reject registration without
 * partially mutating the registry or the candidate's parent link.
 */
MW_TEST_CASE(EngineFullAndZeroCapacityRegistrationRejected)
{
	FRegistrationEnvironment Env{};

	// Full-capacity actor registry: capacity one, second registration rejected.
	FActorComponentRegistry<1> ActorComponents;
	FWorldActorRegistry<1> OneActorWorld;
	FWorldActorRegistryBase OneActorView = OneActorWorld.MakeView();
	const TObjectPtr<UWorld> OneActorWorldObj = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(OneActorView));
	const TObjectPtr<FPlainActor> ActorA = MakePlainActor(Env, ActorComponents.MakeView());
	const TObjectPtr<FPlainActor> ActorB = MakePlainActor(Env, ActorComponents.MakeView());
	const EEngineResult FirstActor = OneActorWorldObj.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});
	const EEngineResult FullActor = OneActorWorldObj.Get()->RegisterActor(TObjectPtr<AActor>{ActorB});

	// Zero-capacity actor registry: first registration rejected as full.
	FWorldActorRegistry<0> ZeroActorWorld;
	FWorldActorRegistryBase ZeroActorView = ZeroActorWorld.MakeView();
	const TObjectPtr<UWorld> ZeroActorWorldObj = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(ZeroActorView));
	const EEngineResult ZeroActor = ZeroActorWorldObj.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});

	// Full-capacity component registry: capacity one, second registration rejected.
	FActorComponentRegistry<1> OneComponentActor;
	FWorldActorRegistry<1> WorldActors;
	FWorldActorRegistryBase WorldActorsView = WorldActors.MakeView();
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FPlainActor> HostActor = MakePlainActor(Env, OneComponentActor.MakeView());
	const TObjectPtr<FPlainComponent> ComponentA = MakePlainComponent(Env);
	const TObjectPtr<FPlainComponent> ComponentB = MakePlainComponent(Env);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{HostActor});
	const EEngineResult FirstComponent = HostActor.Get()->RegisterComponent(ComponentA);
	const EEngineResult FullComponent = HostActor.Get()->RegisterComponent(ComponentB);

	// Zero-capacity component registry: first registration rejected as full.
	FActorComponentRegistry<0> ZeroComponentActor;
	const TObjectPtr<FPlainActor> ZeroHostActor = MakePlainActor(Env, ZeroComponentActor.MakeView());
	const EEngineResult ZeroComponent = ZeroHostActor.Get()->RegisterComponent(ComponentA);

	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstActor, "The capacity-one actor registry accepts its first actor");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, FullActor, "A full actor registry rejects further registration");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, ZeroActor, "A zero-capacity actor registry rejects its first registration");
	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstComponent, "The capacity-one component registry accepts its first component");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, FullComponent, "A full component registry rejects further registration");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, ZeroComponent, "A zero-capacity component registry rejects its first registration");
	MW_EXPECT_TRUE(Test, !ActorB.Get()->HasAssignedWorld(), "A rejected full actor must not gain a world parent link");
}

/**
 * Proves registration after BeginPlay is rejected as lifecycle-locked at every
 * level (world, actor) without changing registry state.
 */
MW_TEST_CASE(EngineRegistrationAfterBeginPlayRejected)
{
	FRegistrationEnvironment Env{};
	FActorComponentRegistry<2> ActorComponents;
	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryBase WorldActorsView = WorldActors.MakeView();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FPlainActor> ActorA = MakePlainActor(Env, ActorComponents.MakeView());
	const TObjectPtr<FPlainActor> ActorB = MakePlainActor(Env, ActorComponents.MakeView());
	const TObjectPtr<FPlainComponent> Component = MakePlainComponent(Env);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});
	(void)ActorA.Get()->RegisterComponent(Component);
	(void)World.Get()->BeginPlay(0);

	const EEngineResult ActorAfterBegin = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorB});
	const EEngineResult ComponentAfterBegin = ActorA.Get()->RegisterComponent(MakePlainComponent(Env));

	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, ActorAfterBegin, "Actor registration must close after BeginPlay");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, ComponentAfterBegin, "Component registration must close after BeginPlay");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "A lifecycle-locked world keeps its prior actor count");
	MW_EXPECT_EQ(Test, std::size_t{1}, ActorComponents.GetCount(), "A lifecycle-locked actor keeps its prior component count");
}

/**
 * Proves an actor already owned by one world is rejected by a second world.
 */
MW_TEST_CASE(EngineActorCrossOwnerRejected)
{
	FRegistrationEnvironment Env{};
	FActorComponentRegistry<2> ActorComponents;
	FWorldActorRegistry<2> WorldAActors;
	FWorldActorRegistry<2> WorldBActors;
	FWorldActorRegistryBase WorldAView = WorldAActors.MakeView();
	FWorldActorRegistryBase WorldBView = WorldBActors.MakeView();

	const TObjectPtr<UWorld> WorldA = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldAView));
	const TObjectPtr<UWorld> WorldB = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldBView));
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env, ActorComponents.MakeView());

	const EEngineResult FirstOwner = WorldA.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	const EEngineResult SecondOwner = WorldB.Get()->RegisterActor(TObjectPtr<AActor>{Actor});

	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstOwner, "The first world should accept the actor");
	MW_EXPECT_EQ(Test, EEngineResult::AlreadyOwned, SecondOwner, "A second world must reject an already-owned actor");
	MW_EXPECT_EQ(Test, std::size_t{0}, WorldBActors.GetCount(), "The rejected world must keep an empty registry");
}

/**
 * Proves a component already owned by one actor is rejected by a second actor.
 */
MW_TEST_CASE(EngineComponentCrossOwnerRejected)
{
	FRegistrationEnvironment Env{};
	FActorComponentRegistry<2> ActorAComponents;
	FActorComponentRegistry<2> ActorBComponents;
	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryBase WorldActorsView = WorldActors.MakeView();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FPlainActor> ActorA = MakePlainActor(Env, ActorAComponents.MakeView());
	const TObjectPtr<FPlainActor> ActorB = MakePlainActor(Env, ActorBComponents.MakeView());
	const TObjectPtr<FPlainComponent> Component = MakePlainComponent(Env);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{ActorB});

	const EEngineResult FirstOwner = ActorA.Get()->RegisterComponent(Component);
	const EEngineResult SecondOwner = ActorB.Get()->RegisterComponent(Component);

	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstOwner, "The first actor should accept the component");
	MW_EXPECT_EQ(Test, EEngineResult::AlreadyOwned, SecondOwner, "A second actor must reject an already-owned component");
	MW_EXPECT_EQ(Test, std::size_t{0}, ActorBComponents.GetCount(), "The rejected actor must keep an empty registry");
}

/**
 * Proves a managed reference that belongs to a different FObjectStore is
 * rejected as a cross-store relationship by world and actor registration.
 */
MW_TEST_CASE(EngineCrossStoreRegistrationRejected)
{
	FRegistrationEnvironment EnvA{};
	FSecondStore StoreBOwner{};
	FObjectStore& StoreB = StoreBOwner.GetStore();

	FWorldActorRegistry<2> WorldActorsA;
	FWorldActorRegistryBase WorldActorsAView = WorldActorsA.MakeView();
	FActorComponentRegistry<2> ActorComponentsA;
	FActorComponentRegistry<2> ActorComponentsB;
	FWorldActorRegistry<2> WorldActorsB;
	FWorldActorRegistryBase WorldActorsBView = WorldActorsB.MakeView();

	const TObjectPtr<UWorld> WorldA = EnvA.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsAView));
	const TObjectPtr<UWorld> WorldB = EnvA.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsBView));
	// Build an actor in StoreB and try to register it with worlds in StoreA.
	const TObjectPtr<FPlainActor> ForeignActor =
		StoreB.NewObject<FPlainActor>(*StoreBOwner.GetRegistry().Find(PlainActorTypeId), ActorComponentsB.MakeView()).Object;
	const TObjectPtr<FPlainComponent> ForeignComponent =
		StoreB.NewObject<FPlainComponent>(*StoreBOwner.GetRegistry().Find(PlainComponentTypeId)).Object;
	const TObjectPtr<FPlainActor> LocalActor = MakePlainActor(EnvA, ActorComponentsA.MakeView());
	(void)WorldA.Get()->RegisterActor(TObjectPtr<AActor>{LocalActor});

	const EEngineResult ForeignActorRegistration = WorldB.Get()->RegisterActor(TObjectPtr<AActor>{ForeignActor});
	const EEngineResult ForeignComponentRegistration = LocalActor.Get()->RegisterComponent(ForeignComponent);

	MW_EXPECT_EQ(Test, EEngineResult::CrossStore, ForeignActorRegistration, "A foreign-store actor must be rejected");
	MW_EXPECT_EQ(Test, EEngineResult::CrossStore, ForeignComponentRegistration, "A foreign-store component must be rejected");
}

/**
 * Proves a registration rejected for any reason leaves no partial state: the
 * registry count and the candidate's parent link are unchanged.
 */
MW_TEST_CASE(EngineRejectedRegistrationLeavesNoPartialState)
{
	FRegistrationEnvironment Env{};
	FActorComponentRegistry<2> ActorComponents;
	FWorldActorRegistry<1> WorldActors;
	FWorldActorRegistryBase WorldActorsView = WorldActors.MakeView();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FPlainActor> ActorA = MakePlainActor(Env, ActorComponents.MakeView());
	const TObjectPtr<FPlainActor> ActorB = MakePlainActor(Env, ActorComponents.MakeView());
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});

	// Capacity is now exhausted; the rejected ActorB must keep no parent link.
	const EEngineResult Rejected = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorB});

	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, Rejected, "The world at capacity rejects the second actor");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "The rejected registration leaves the count unchanged");
	MW_EXPECT_TRUE(Test, !ActorB.Get()->HasAssignedWorld(), "The rejected actor keeps no world parent link");
}

/**
 * Proves empty and reclaimed actor/component references are rejected explicitly
 * and cannot publish either a registry entry or a parent link.
 */
MW_TEST_CASE(EngineEmptyAndStaleRegistrationRejectedWithoutPartialState)
{
	FRegistrationEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FActorComponentRegistry<2> HostComponents;
	FActorComponentRegistry<1> StaleActorComponents;
	FWorldActorRegistry<2> WorldActors;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> HostActor = MakePlainActor(Env, HostComponents.MakeView());
	const EEngineResult HostActorResult = World.Get()->RegisterActor(HostActor);

	const TObjectPtr<FPlainActor> StaleActor = MakePlainActor(Env, StaleActorComponents.MakeView());
	const EObjectResult MarkActorResult = Store.MarkPendingDestroy(StaleActor.Handle());
	const EObjectResult DestroyActorResult = Store.ApplyPendingDestroy(1).Result;
	const TObjectPtr<FPlainComponent> StaleComponent = MakePlainComponent(Env);
	const EObjectResult MarkComponentResult = Store.MarkPendingDestroy(StaleComponent.Handle());
	const EObjectResult DestroyComponentResult = Store.ApplyPendingDestroy(1).Result;

	const EEngineResult EmptyActorResult = World.Get()->RegisterActor(TObjectPtr<AActor>{});
	const EEngineResult StaleActorResult = World.Get()->RegisterActor(StaleActor);
	const EEngineResult EmptyComponentResult = HostActor.Get()->RegisterComponent(TObjectPtr<UActorComponent>{});
	const EEngineResult StaleComponentResult = HostActor.Get()->RegisterComponent(StaleComponent);

	MW_EXPECT_EQ(Test, EEngineResult::Success, HostActorResult, "Host actor setup succeeds");
	MW_EXPECT_EQ(Test, EObjectResult::Success, MarkActorResult, "Stale actor setup marks the candidate");
	MW_EXPECT_EQ(Test, EObjectResult::Success, DestroyActorResult, "Stale actor setup reclaims the candidate");
	MW_EXPECT_EQ(Test, EObjectResult::Success, MarkComponentResult, "Stale component setup marks the candidate");
	MW_EXPECT_EQ(Test, EObjectResult::Success, DestroyComponentResult, "Stale component setup reclaims the candidate");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, EmptyActorResult, "An empty actor reference is rejected explicitly");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, StaleActorResult, "A stale actor reference is rejected explicitly");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, EmptyComponentResult, "An empty component reference is rejected explicitly");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, StaleComponentResult, "A stale component reference is rejected explicitly");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "Rejected actors do not change the world registry");
	MW_EXPECT_EQ(Test, std::size_t{0}, HostComponents.GetCount(), "Rejected components do not change the actor registry");
}

/** Proves active collection blocks registration before either owner graph changes. */
MW_TEST_CASE(EngineRegistrationRejectedDuringActiveCollection)
{
	FRegistrationEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FActorComponentRegistry<2> HostComponents;
	FActorComponentRegistry<1> CandidateComponents;
	FWorldActorRegistry<2> WorldActors;
	std::array<MicroWorld::FObjectHandle, 16> Worklist{};
	MicroWorld::FGarbageCollector Collector{
		Store, MicroWorld::FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())}};

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> HostActor = MakePlainActor(Env, HostComponents.MakeView());
	const TObjectPtr<FPlainActor> CandidateActor = MakePlainActor(Env, CandidateComponents.MakeView());
	const TObjectPtr<FPlainComponent> CandidateComponent = MakePlainComponent(Env);
	const EEngineResult HostActorResult = World.Get()->RegisterActor(HostActor);
	const ERuntimeResult RequestResult = Collector.RequestCollection();

	const EEngineResult ActorResult = World.Get()->RegisterActor(CandidateActor);
	const EEngineResult ComponentResult = HostActor.Get()->RegisterComponent(CandidateComponent);

	MW_EXPECT_EQ(Test, EEngineResult::Success, HostActorResult, "Host actor setup succeeds before collection");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "Collection enters an active phase");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, ActorResult, "Active collection rejects actor registration");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, ComponentResult, "Active collection rejects component registration");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "Rejected actor registration leaves the world unchanged");
	MW_EXPECT_EQ(Test, std::size_t{0}, HostComponents.GetCount(), "Rejected component registration leaves the actor unchanged");
	MW_EXPECT_TRUE(Test, !CandidateActor.Get()->HasAssignedWorld(), "Rejected actor keeps no world parent");
	MW_EXPECT_TRUE(Test, !CandidateComponent.Get()->HasAssignedActor(), "Rejected component keeps no actor parent");
}

/** Proves test-derived descriptors preserve their registered Engine inheritance. */
MW_TEST_CASE(EngineDerivedDescriptorsUseRegisteredEngineParents)
{
	FRegistrationEnvironment Env{};
	const MicroWorld::FClassDescriptor* const ActorBase = Env.FindDescriptor(MicroWorld::AActorClassId);
	const MicroWorld::FClassDescriptor* const ComponentBase = Env.FindDescriptor(MicroWorld::UActorComponentClassId);
	const EObjectResult ActorRegistration = Env.RegisterDerivedClass<FPlainActor>(PlainActorTypeId, "PlainActor");
	const EObjectResult ComponentRegistration = Env.RegisterDerivedClass<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	const MicroWorld::FClassDescriptor* const ActorDescriptor = Env.FindDescriptor(PlainActorTypeId);
	const MicroWorld::FClassDescriptor* const ComponentDescriptor = Env.FindDescriptor(PlainComponentTypeId);

	MW_EXPECT_EQ(Test, EObjectResult::Success, ActorRegistration, "Derived actor descriptor registers");
	MW_EXPECT_EQ(Test, EObjectResult::Success, ComponentRegistration, "Derived component descriptor registers");
	MW_EXPECT_EQ(Test, ActorBase, ActorDescriptor->Parent, "Derived actor names its registered AActor parent");
	MW_EXPECT_EQ(Test, ComponentBase, ComponentDescriptor->Parent, "Derived component names its registered UActorComponent parent");
	MW_EXPECT_TRUE(Test, ActorDescriptor->IsChildOf(*ActorBase), "Derived actor reports IsChildOf AActor");
	MW_EXPECT_TRUE(Test, ComponentDescriptor->IsChildOf(*ComponentBase), "Derived component reports IsChildOf UActorComponent");
	MW_EXPECT_TRUE(Test, !ActorDescriptor->IsChildOf(*ComponentBase), "Derived actor is not a component");
}

/** Proves reusing a one-shot registry lease fails observably before hooks run. */
MW_TEST_CASE(EngineReusedRegistryLeaseFailsBeginPlay)
{
	FRegistrationEnvironment Env{};
	FWorldActorRegistry<2> SharedWorldActors;
	FActorComponentRegistry<1> SharedActorComponents;
	FWorldActorRegistry<1> ValidWorldActors;

	const TObjectPtr<UWorld> FirstWorld = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, SharedWorldActors.MakeView());
	const TObjectPtr<UWorld> ReusedLeaseWorld = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, SharedWorldActors.MakeView());
	const TObjectPtr<UWorld> ActorHostWorld = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, ValidWorldActors.MakeView());
	const TObjectPtr<FPlainActor> FirstActor = MakePlainActor(Env, SharedActorComponents.MakeView());
	const TObjectPtr<FPlainActor> ReusedLeaseActor = MakePlainActor(Env, SharedActorComponents.MakeView());
	const EEngineResult ActorRegistration = ActorHostWorld.Get()->RegisterActor(ReusedLeaseActor);

	const ERuntimeResult WorldBeginResult = ReusedLeaseWorld.Get()->BeginPlay(0);
	const ERuntimeResult ActorBeginResult = ActorHostWorld.Get()->BeginPlay(0);

	MW_EXPECT_TRUE(Test, FirstWorld.Get() != nullptr, "The first world consumes the valid registry lease");
	MW_EXPECT_TRUE(Test, FirstActor.Get() != nullptr, "The first actor consumes the valid registry lease");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorRegistration, "An invalid child registry remains observable at begin");
	MW_EXPECT_EQ(Test, ERuntimeResult::CapacityExceeded, WorldBeginResult, "A world with a reused registry lease rejects BeginPlay");
	MW_EXPECT_EQ(Test, ERuntimeResult::CapacityExceeded, ActorBeginResult, "An actor with a reused registry lease rejects BeginPlay");
}

} // namespace
