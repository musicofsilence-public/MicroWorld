#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <MicroWorld/Engine/ReferenceCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>

namespace MicroWorld::Engine
{

std::size_t UWorld::PendingSpawnCount() const noexcept
{
	return Actors.GetPendingSpawnCount();
}

std::size_t UWorld::PendingDestroyCount() const noexcept
{
	return Actors.GetPendingDestroyCount();
}

void UWorld::VisitReferences(FReferenceCollector& InCollector) noexcept
{
	// Every registered actor is a traced downward edge. Pending-spawn actors are
	// also reachable so they survive collection until the barrier begins them;
	// pending-destroy actors are still in the live set until the barrier removes
	// them, so they need no separate edge here.
	VisitDeferredSpawnReferences(InCollector);
	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		InCollector.AddReferencedObject(Actors.At(Index));
	}
	for (std::size_t Index = 0; Index < Actors.GetPendingSpawnCount(); ++Index)
	{
		InCollector.AddReferencedObject(Actors.PendingSpawnAt(Index));
	}
}

void UWorld::VisitDeferredSpawnReferences(FReferenceCollector& InCollector) noexcept
{
	DeferredSpawns.VisitReferences(InCollector);
}

} // namespace MicroWorld::Engine
