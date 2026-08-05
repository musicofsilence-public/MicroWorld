#include <MicroWorld/Messaging/MessagingSystem.h>

#include <MicroWorld/Core/IO/TransportResult.h>

namespace MicroWorld::Messaging
{

EMessagingResult FMessageWriter::WriteU8(const std::uint8_t InValue) noexcept
{
	if (Remaining() < sizeof(InValue))
	{
		return EMessagingResult::Full;
	}

	Buffer[Offset] = InValue;
	++Offset;
	return EMessagingResult::Success;
}

EMessagingResult FMessageWriter::WriteU16(const std::uint16_t InValue) noexcept
{
	if (Remaining() < sizeof(InValue))
	{
		return EMessagingResult::Full;
	}

	Buffer[Offset] = static_cast<std::uint8_t>(InValue);
	Buffer[Offset + 1] = static_cast<std::uint8_t>(InValue >> 8u);
	Offset += sizeof(InValue);
	return EMessagingResult::Success;
}

EMessagingResult FMessageWriter::WriteU32(const std::uint32_t InValue) noexcept
{
	if (Remaining() < sizeof(InValue))
	{
		return EMessagingResult::Full;
	}

	for (std::size_t ByteIndex = 0; ByteIndex < sizeof(InValue); ++ByteIndex)
	{
		Buffer[Offset + ByteIndex] = static_cast<std::uint8_t>(InValue >> (ByteIndex * 8u));
	}
	Offset += sizeof(InValue);
	return EMessagingResult::Success;
}

EMessagingResult FMessageWriter::WriteBytes(const Core::TSpan<const std::uint8_t> InBytes) noexcept
{
	if (Remaining() < InBytes.Size())
	{
		return EMessagingResult::Full;
	}

	for (std::size_t ByteIndex = 0; ByteIndex < InBytes.Size(); ++ByteIndex)
	{
		Buffer[Offset + ByteIndex] = InBytes[ByteIndex];
	}
	Offset += InBytes.Size();
	return EMessagingResult::Success;
}

EMessagingResult FMessageReader::ReadU8(std::uint8_t& OutValue) noexcept
{
	if (Remaining() < sizeof(OutValue))
	{
		return EMessagingResult::Invalid;
	}

	OutValue = Buffer[Offset];
	++Offset;
	return EMessagingResult::Success;
}

EMessagingResult FMessageReader::ReadU16(std::uint16_t& OutValue) noexcept
{
	if (Remaining() < sizeof(OutValue))
	{
		return EMessagingResult::Invalid;
	}

	OutValue = static_cast<std::uint16_t>(Buffer[Offset]) | (static_cast<std::uint16_t>(Buffer[Offset + 1]) << 8u);
	Offset += sizeof(OutValue);
	return EMessagingResult::Success;
}

EMessagingResult FMessageReader::ReadU32(std::uint32_t& OutValue) noexcept
{
	if (Remaining() < sizeof(OutValue))
	{
		return EMessagingResult::Invalid;
	}

	std::uint32_t Value = 0;
	for (std::size_t ByteIndex = 0; ByteIndex < sizeof(Value); ++ByteIndex)
	{
		Value |= static_cast<std::uint32_t>(Buffer[Offset + ByteIndex]) << (ByteIndex * 8u);
	}
	OutValue = Value;
	Offset += sizeof(OutValue);
	return EMessagingResult::Success;
}

EMessagingResult FMessageReader::ReadBytes(const Core::TSpan<std::uint8_t> OutBytes) noexcept
{
	if (Remaining() < OutBytes.Size())
	{
		return EMessagingResult::Invalid;
	}

	for (std::size_t ByteIndex = 0; ByteIndex < OutBytes.Size(); ++ByteIndex)
	{
		OutBytes[ByteIndex] = Buffer[Offset + ByteIndex];
	}
	Offset += OutBytes.Size();
	return EMessagingResult::Success;
}

EMessagingResult FMessagingSystem::MapTransportSendResult(const Core::ETransportResult InTransportResult) noexcept
{
	switch (InTransportResult)
	{
		case Core::ETransportResult::Success:
			return EMessagingResult::Success;
		case Core::ETransportResult::Full:
			return EMessagingResult::Full;
		case Core::ETransportResult::Invalid:
		case Core::ETransportResult::Unavailable:
			return EMessagingResult::Invalid;
	}

	return EMessagingResult::Invalid;
}

void FMessagingSystem::EncodeFrameHeader(const FNameId InChannelNameId, const FNameId InMessageNameId, std::uint8_t* const OutFrameBytes) noexcept
{
	WriteNameIdLittleEndian(InChannelNameId, &OutFrameBytes[ChannelNameIdByteIndex]);
	WriteNameIdLittleEndian(InMessageNameId, &OutFrameBytes[MessageNameIdByteIndex]);
}

void FMessagingSystem::WriteUnsignedLittleEndian(const std::uint64_t InValue, std::uint8_t* const OutBytes, const std::size_t InByteCount) noexcept
{
	for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
	{
		OutBytes[ByteOffset] = static_cast<std::uint8_t>(InValue >> (ByteOffset * BitsPerByte));
	}
}

std::uint64_t FMessagingSystem::ReadUnsignedLittleEndian(const std::uint8_t* const InBytes, const std::size_t InByteCount) noexcept
{
	std::uint64_t Value = 0;
	for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
	{
		Value |= static_cast<std::uint64_t>(InBytes[ByteOffset]) << (ByteOffset * BitsPerByte);
	}

	return Value;
}

void FMessagingSystem::WriteNameIdLittleEndian(const FNameId InNameId, std::uint8_t* const OutBytes) noexcept
{
	WriteUnsignedLittleEndian(InNameId.Value, OutBytes, NameIdBytes);
}

FNameId FMessagingSystem::ReadNameIdLittleEndian(const std::uint8_t* const InBytes) noexcept
{
	return FNameId{static_cast<std::uint32_t>(ReadUnsignedLittleEndian(InBytes, NameIdBytes))};
}

void FMessagingSystem::CopyBytes(std::uint8_t* const OutDestination, const Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	for (std::size_t PayloadByteOffset = 0; PayloadByteOffset < InPayload.Size(); ++PayloadByteOffset)
	{
		OutDestination[PayloadByteOffset] = InPayload.Data()[PayloadByteOffset];
	}
}

} // namespace MicroWorld::Messaging
