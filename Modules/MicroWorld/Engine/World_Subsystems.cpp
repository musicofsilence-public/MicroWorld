#include <MicroWorld/Engine/World.h>

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/WorldSubsystem.h>

namespace MicroWorld::Engine
{

EEngineResult UWorld::RegisterSubsystem(const TObjectPtr<UWorldSubsystem> InSubsystem) noexcept
{
	const EEngineResult Verdict = CheckSubsystemRegistrable(InSubsystem);
	if (Verdict != EEngineResult::Success)
	{
		return Verdict;
	}
	PublishSubsystem(InSubsystem);
	return EEngineResult::Success;
}

EEngineResult UWorld::CheckSubsystemRegistrable(const TObjectPtr<UWorldSubsystem> InSubsystem) const noexcept
{
	if (Lifecycle.GetState() != Core::ELifecycleState::Constructed)
	{
		return EEngineResult::LifecycleLocked;
	}
	FObjectStore* ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	if (ObjectStore->IsMutationLocked())
	{
		return EEngineResult::LifecycleLocked;
	}
	UWorldSubsystem* Resolved = InSubsystem.Get();
	if (Resolved == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	if (!InSubsystem.BelongsTo(*ObjectStore))
	{
		return EEngineResult::CrossStore;
	}
	if (!Subsystems.IsValid())
	{
		return EEngineResult::CapacityExceeded;
	}

	const void* CandidateTypeToken = Resolved->GetClassDescriptor().TypeToken;
	for (std::size_t Index = 0; Index < Subsystems.GetCount(); ++Index)
	{
		const TObjectPtr<UWorldSubsystem>& ExistingReference = Subsystems.At(Index);
		if (ExistingReference.Handle() == InSubsystem.Handle())
		{
			return EEngineResult::Duplicate;
		}
		UWorldSubsystem* Existing = ExistingReference.Get();
		if (Existing != nullptr && Existing->GetClassDescriptor().TypeToken == CandidateTypeToken)
		{
			return EEngineResult::Duplicate;
		}
	}
	if (Subsystems.GetCount() >= Subsystems.GetCapacity())
	{
		return EEngineResult::CapacityExceeded;
	}
	if (Resolved->HasAssignedWorld())
	{
		return EEngineResult::AlreadyOwned;
	}
	return EEngineResult::Success;
}

void UWorld::PublishSubsystem(const TObjectPtr<UWorldSubsystem> InSubsystem) noexcept
{
	UWorldSubsystem* Resolved = InSubsystem.Get();
	Resolved->AssignWorld(GetObjectHandle());
	Subsystems.Add(InSubsystem);
}

Core::ERuntimeResult UWorld::InitializeSubsystemsWithRollback() noexcept
{
	if (!Subsystems.IsValid())
	{
		return Core::ERuntimeResult::Success;
	}

	std::size_t InitializedCount = 0;
	for (std::size_t Index = 0; Index < Subsystems.GetCount(); ++Index)
	{
		UWorldSubsystem* Subsystem = Subsystems.At(Index).Get();
		const Core::ERuntimeResult Result = Subsystem != nullptr ? Subsystem->DispatchInitialize() : Core::ERuntimeResult::InvalidLifecycle;
		if (Result != Core::ERuntimeResult::Success)
		{
			for (std::size_t RollbackIndex = InitializedCount; RollbackIndex > 0; --RollbackIndex)
			{
				UWorldSubsystem* Initialized = Subsystems.At(RollbackIndex - 1).Get();
				if (Initialized != nullptr)
				{
					(void)Initialized->DispatchDeinitialize();
				}
			}
			return Result;
		}
		++InitializedCount;
	}
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UWorld::DeinitializeSubsystemsReverse() noexcept
{
	if (!Subsystems.IsValid())
	{
		return Core::ERuntimeResult::Success;
	}

	Core::ERuntimeResult FirstError = Core::ERuntimeResult::Success;
	for (std::size_t Index = Subsystems.GetCount(); Index > 0; --Index)
	{
		UWorldSubsystem* Subsystem = Subsystems.At(Index - 1).Get();
		const Core::ERuntimeResult Result = Subsystem != nullptr ? Subsystem->DispatchDeinitialize() : Core::ERuntimeResult::InvalidLifecycle;
		if (FirstError == Core::ERuntimeResult::Success && Result != Core::ERuntimeResult::Success)
		{
			FirstError = Result;
		}
	}
	return FirstError;
}

} // namespace MicroWorld::Engine
