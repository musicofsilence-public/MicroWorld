#include <MicroWorld/Application/Application.h>

namespace MicroWorld::Application
{

Core::ERuntimeResult FApplication::BeginPlay(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	const Core::ERuntimeResult BeginResult = Lifecycle.Begin();
	if (BeginResult != Core::ERuntimeResult::Success)
	{
		return BeginResult;
	}

	LastUpdateMilliseconds = InNowMilliseconds;
	const Core::ERuntimeResult ConsumerResult = OnBeginPlay(InNowMilliseconds);
	if (ConsumerResult != Core::ERuntimeResult::Success)
	{
		OnBeginPlayFailed();
		Lifecycle.Fail();
		return ConsumerResult;
	}
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult FApplication::Advance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	const Core::ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != Core::ERuntimeResult::Success)
	{
		return PlayingResult;
	}
	if (InNowMilliseconds < LastUpdateMilliseconds)
	{
		return Core::ERuntimeResult::NonMonotonicTime;
	}

	LastUpdateMilliseconds = InNowMilliseconds;
	return OnAdvance(InNowMilliseconds);
}

Core::ERuntimeResult FApplication::EndPlay() noexcept
{
	if (Lifecycle.GetState() == Core::ELifecycleState::Ended)
	{
		return Core::ERuntimeResult::Success;
	}
	const Core::ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != Core::ERuntimeResult::Success)
	{
		return EndResult;
	}
	return OnEndPlay();
}

Core::ERuntimeResult FApplication::OnBeginPlay(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	// Configuration runs before runtime begin so concrete dependencies can be prepared
	// without broadening the runtime contract. A configuration failure skips runtime
	// begin; otherwise the runtime's BeginPlay result is returned unchanged.
	const Core::ERuntimeResult ConfigureResult = OnConfigure(InNowMilliseconds);
	if (ConfigureResult != Core::ERuntimeResult::Success)
	{
		return ConfigureResult;
	}
	return EngineRuntime.BeginPlay(InNowMilliseconds);
}

Core::ERuntimeResult FApplication::OnAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	return EngineRuntime.Tick(InNowMilliseconds);
}

Core::ERuntimeResult FApplication::OnEndPlay() noexcept
{
	return EngineRuntime.EndPlay();
}

} // namespace MicroWorld::Application
