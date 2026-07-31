#pragma once

namespace MicroWorld::Platform::Host
{

/**
 * Motivation: Lets each socket-bearing device bracket its lifetime with one RAII guard so the host platform's
 *   socket stack is brought up exactly once and torn down exactly once.
 * Responsibilities: Hold a reference count that performs WSAStartup on first construction and WSACleanup on last
 *   destruction on Windows (no-ops on POSIX); stay single-threaded because the engine drives the host on one
 *   deterministic thread.
 * Example:
 *   FWinSockScope SocketStack;
 *   // socket-bearing device lifetime here
 */
class FWinSockScope final
{
public:
	/**
	 * Motivation: Lets each new scope add its contribution to the shared socket-stack lifetime.
	 * Responsibilities: Increment the shared refcount and perform WSAStartup only on the first live scope.
	 */
	FWinSockScope() noexcept;

	/**
	 * Motivation: Lets a dropped scope remove its contribution to the shared socket-stack lifetime.
	 * Responsibilities: Decrement the shared refcount and perform WSACleanup only when the last scope drops.
	 */
	~FWinSockScope() noexcept;

	/**
	 * Motivation: Prevents two devices from sharing one refcount contribution through a copy.
	 * Responsibilities: Reject copy construction so each device value owns exactly one scope identity.
	 */
	FWinSockScope(const FWinSockScope&) = delete;

	/**
	 * Motivation: Prevents two devices from sharing one refcount contribution through an assignment.
	 * Responsibilities: Reject copy assignment so each device value owns exactly one scope identity.
	 */
	FWinSockScope& operator=(const FWinSockScope&) = delete;
};

} // namespace MicroWorld::Platform::Host
