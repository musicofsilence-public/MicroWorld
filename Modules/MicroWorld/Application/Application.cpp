#include <MicroWorld/Application/Application.h>

namespace MicroWorld::Application
{

ERuntimeResult FApplication::BeginPlay(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	const ERuntimeResult BeginResult = Lifecycle.Begin();
	if (BeginResult != ERuntimeResult::Success)
	{
		return BeginResult;
	}

	LastUpdateMilliseconds = InNowMilliseconds;
	const ERuntimeResult ConsumerResult = OnBeginPlay(InNowMilliseconds);
	if (ConsumerResult != ERuntimeResult::Success)
	{
		OnBeginPlayFailed();
		Lifecycle.Fail();
		return ConsumerResult;
	}
	return ERuntimeResult::Success;
}

ERuntimeResult FApplication::Advance(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	const ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != ERuntimeResult::Success)
	{
		return PlayingResult;
	}
	if (InNowMilliseconds < LastUpdateMilliseconds)
	{
		return ERuntimeResult::NonMonotonicTime;
	}

	LastUpdateMilliseconds = InNowMilliseconds;
	return OnAdvance(InNowMilliseconds);
}

ERuntimeResult FApplication::EndPlay() noexcept
{
	if (Lifecycle.GetState() == ELifecycleState::Ended)
	{
		return ERuntimeResult::Success;
	}
	const ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != ERuntimeResult::Success)
	{
		return EndResult;
	}
	return OnEndPlay();
}

ERuntimeResult FApplication::OnBeginPlay(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	// OnConfigure runs before the engine begins so a subclass can spawn actors and
	// configure systems into a world that exists but has not yet started; either
	// failure short-circuits the engine begin and surfaces the first cause.
	const ERuntimeResult ConfigureResult = OnConfigure(Engine, InNowMilliseconds);
	if (ConfigureResult != ERuntimeResult::Success)
	{
		return ConfigureResult;
	}
	return Engine.BeginPlay(InNowMilliseconds);
}

ERuntimeResult FApplication::OnAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	return Engine.Tick(InNowMilliseconds);
}

ERuntimeResult FApplication::OnEndPlay() noexcept
{
	return Engine.EndPlay();
}

} // namespace MicroWorld::Application
