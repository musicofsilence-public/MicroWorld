#pragma once

#include <MicroWorld/Transport/Lora/E32Lora.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Example17
{
/**
 * Motivation: Names the three fixed payload boundaries the radio regression exercises, so both
 *   boards can speak about the same wire shapes by name rather than restating byte counts.
 * Responsibilities: Distinguish the empty, typical, and maximum payload cases and nothing else.
 * Example:
 *   const std::size_t Bytes = PayloadRegressionByteCount(EPayloadRegressionCase::Typical);
 */
enum class EPayloadRegressionCase : std::uint8_t
{
	Empty,	 ///< Motivation: Zero-byte frame; exercises the radio's empty-payload boundary.
	Typical, ///< Motivation: Five-byte frame; matches the origin-plus-counter volley shape.
	Maximum	 ///< Motivation: Full-size frame; exercises the radio's maximum-payload boundary.
};

/** Motivation: Keeps the typical case aligned with the existing origin-plus-counter volley frame. */
constexpr std::size_t TypicalPayloadBytes = 5;

/** Motivation: Provides a repeatable maximum-frame pattern without storing mutable test data. */
constexpr std::uint8_t MaximumPayloadStartByte = 0xA5u;
constexpr std::uint8_t MaximumPayloadByteStep = 0x1Du;

/**
 * Motivation: Lets both boards ask the shared helper for a case's exact byte boundary, so the wire
 *   contract is stated once instead of restated at each send and receive.
 * Responsibilities: Return zero, five, or the radio maximum for the matching case.
 */
constexpr std::size_t PayloadRegressionByteCount(const EPayloadRegressionCase InCase) noexcept
{
	switch (InCase)
	{
		case EPayloadRegressionCase::Empty:
			return 0;
		case EPayloadRegressionCase::Typical:
			return TypicalPayloadBytes;
		case EPayloadRegressionCase::Maximum:
			return MicroWorld::Transport::E32MaxPayloadBytes;
		default:
			return 0;
	}
}

/**
 * Motivation: Lets ESP32 and Pico logs name the same payload case, so the two boards' serial traces
 *   line up without each restating the label text.
 * Responsibilities: Return one stable label string per case and an "unknown" default.
 */
constexpr const char* PayloadRegressionLabel(const EPayloadRegressionCase InCase) noexcept
{
	switch (InCase)
	{
		case EPayloadRegressionCase::Empty:
			return "empty";
		case EPayloadRegressionCase::Typical:
			return "typical";
		case EPayloadRegressionCase::Maximum:
			return "maximum";
		default:
			return "unknown";
	}
}

/**
 * Motivation: Lets both boards derive one canonical byte so payload content can be validated without
 *   sharing mutable runtime state across the radio link.
 * Responsibilities: Return the fixed pattern byte for a case and index, deterministically.
 */
constexpr std::uint8_t CanonicalPayloadByte(const EPayloadRegressionCase InCase, const std::size_t InIndex) noexcept
{
	switch (InCase)
	{
		case EPayloadRegressionCase::Typical:
			return (InIndex == 0 || InIndex == TypicalPayloadBytes - 1) ? 1u : 0u;
		case EPayloadRegressionCase::Maximum:
			return static_cast<std::uint8_t>(MaximumPayloadStartByte + (MaximumPayloadByteStep * InIndex));
		case EPayloadRegressionCase::Empty:
		default:
			return 0;
	}
}

/**
 * Motivation: Lets one side fill a fixed E32-sized buffer so the regression payload never allocates
 *   or exceeds the radio contract.
 * Responsibilities: Write the canonical byte pattern up to the case's byte count and leave the rest.
 */
inline void FillCanonicalPayload(const EPayloadRegressionCase InCase, std::uint8_t (&OutPayload)[MicroWorld::Transport::E32MaxPayloadBytes]) noexcept
{
	const std::size_t PayloadBytes = PayloadRegressionByteCount(InCase);
	for (std::size_t Index = 0; Index < PayloadBytes; ++Index)
	{
		OutPayload[Index] = CanonicalPayloadByte(InCase, Index);
	}
}

/**
 * Motivation: Lets either board reject a frame that does not match the fixed case shape before the
 *   protocol advances on bad radio content.
 * Responsibilities: Confirm the byte count matches the case and that each byte equals the canonical pattern.
 */
inline bool IsCanonicalPayload(const EPayloadRegressionCase InCase, const std::uint8_t* const InPayload, const std::size_t InPayloadBytes) noexcept
{
	const std::size_t ExpectedPayloadBytes = PayloadRegressionByteCount(InCase);
	if (InPayloadBytes != ExpectedPayloadBytes)
	{
		return false;
	}
	if (ExpectedPayloadBytes == 0)
	{
		return true;
	}
	if (InPayload == nullptr)
	{
		return false;
	}

	for (std::size_t Index = 0; Index < ExpectedPayloadBytes; ++Index)
	{
		if (InPayload[Index] != CanonicalPayloadByte(InCase, Index))
		{
			return false;
		}
	}
	return true;
}
} // namespace MicroWorld::Example17
