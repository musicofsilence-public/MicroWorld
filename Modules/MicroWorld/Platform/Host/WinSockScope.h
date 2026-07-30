#pragma once

namespace MicroWorld
{

/**
 * Reference-counted RAII guard for the host platform's socket stack lifetime.
 *
 * The first construction performs `WSAStartup` and the last destruction performs
 * `WSACleanup` on Windows; on POSIX both bodies are no-ops. The refcount is
 * intentionally not thread-safe: the MicroWorld engine drives the host on one
 * deterministic thread.
 */
class FWinSockScope final
{
public:
	/** Increments the shared refcount, performing `WSAStartup` on the first live scope. */
	FWinSockScope() noexcept;

	/** Decrements the shared refcount, performing `WSACleanup` when the last scope drops. */
	~FWinSockScope() noexcept;

	/** Prevents two drivers from sharing one refcount contribution through a copy. */
	FWinSockScope(const FWinSockScope&) = delete;

	/** Prevents two drivers from sharing one refcount contribution through an assignment. */
	FWinSockScope& operator=(const FWinSockScope&) = delete;
};

} // namespace MicroWorld
