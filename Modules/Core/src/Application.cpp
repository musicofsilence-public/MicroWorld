#include <MicroWorld/Application.h>

namespace MicroWorld
{

ERuntimeResult FApplication::BeginPlay(const TimePointMilliseconds NowMilliseconds) noexcept
{
	const ERuntimeResult BeginResult = Lifecycle.Begin();
	if (BeginResult != ERuntimeResult::Success)
	{
		return BeginResult;
	}

	LastUpdateMilliseconds = NowMilliseconds;
	const ERuntimeResult ConsumerResult = OnBeginPlay(NowMilliseconds);
	if (ConsumerResult != ERuntimeResult::Success)
	{
		OnBeginPlayFailed();
		Lifecycle.Fail();
		return ConsumerResult;
	}
	return ERuntimeResult::Success;
}

ERuntimeResult FApplication::Advance(const TimePointMilliseconds NowMilliseconds) noexcept
{
	const ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != ERuntimeResult::Success)
	{
		return PlayingResult;
	}
	if (NowMilliseconds < LastUpdateMilliseconds)
	{
		return ERuntimeResult::NonMonotonicTime;
	}

	LastUpdateMilliseconds = NowMilliseconds;
	return OnAdvance(NowMilliseconds);
}

ERuntimeResult FApplication::EndPlay() noexcept
{
	if (Lifecycle.State() == ELifecycleState::Ended)
	{
		return ERuntimeResult::Success;
	}
	const ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != ERuntimeResult::Success)
	{
		return EndResult;
	}
	OnEndPlay();
	return ERuntimeResult::Success;
}

} // namespace MicroWorld
