#pragma once

#include <MicroWorld/Transport/Lora/E32Lora.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Example17
{
/** Identifies the three fixed payload boundaries exercised by the radio regression. */
enum class EPayloadRegressionCase : std::uint8_t
{
	Empty,
	Typical,
	Maximum
};

/** Keeps the typical case aligned with the existing origin-plus-counter volley frame. */
constexpr std::size_t TypicalPayloadBytes = 5;

/** Provides a repeatable maximum-frame pattern without storing mutable test data. */
constexpr std::uint8_t MaximumPayloadStartByte = 0xA5u;
constexpr std::uint8_t MaximumPayloadByteStep = 0x1Du;

/** Returns the exact byte boundary represented by a regression case. */
constexpr std::size_t PayloadRegressionByteCount(const EPayloadRegressionCase InCase) noexcept
{
	switch (InCase)
	{
		case EPayloadRegressionCase::Empty:
			return 0;
		case EPayloadRegressionCase::Typical:
			return TypicalPayloadBytes;
		case EPayloadRegressionCase::Maximum:
			return E32MaxPayloadBytes;
		default:
			return 0;
	}
}

/** Supplies stable trace labels so ESP32 and Pico logs name the same payload case. */
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

/** Produces one canonical byte so both boards can validate content without shared runtime state. */
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

/** Fills a fixed E32-sized buffer, preventing the regression payload from allocating or exceeding the radio contract. */
inline void FillCanonicalPayload(const EPayloadRegressionCase InCase, std::uint8_t (&OutPayload)[E32MaxPayloadBytes]) noexcept
{
	const std::size_t PayloadBytes = PayloadRegressionByteCount(InCase);
	for (std::size_t Index = 0; Index < PayloadBytes; ++Index)
	{
		OutPayload[Index] = CanonicalPayloadByte(InCase, Index);
	}
}

/** Checks received bytes against the fixed case shape and canonical contents before either board advances the protocol. */
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
