#include <MicroWorld/Messaging/MessagingSystem.h>

#include <MicroWorld/Core/IO/TransportResult.h>

namespace MicroWorld::Messaging
{

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

void FMessagingSystem::WriteUnsignedLittleEndian(const std::uint32_t InValue, std::uint8_t* const OutBytes, const std::size_t InByteCount) noexcept
{
	for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
	{
		const std::size_t BitShift = ByteOffset * BitsPerByte;
		OutBytes[ByteOffset] = static_cast<std::uint8_t>(InValue >> BitShift);
	}
}

std::uint32_t FMessagingSystem::ReadUnsignedLittleEndian(const std::uint8_t* const InBytes, const std::size_t InByteCount) noexcept
{
	std::uint32_t Value = 0;
	for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
	{
		const std::size_t BitShift = ByteOffset * BitsPerByte;
		Value |= static_cast<std::uint32_t>(InBytes[ByteOffset]) << BitShift;
	}

	return Value;
}

void FMessagingSystem::WriteNameIdLittleEndian(const FNameId InNameId, std::uint8_t* const OutBytes) noexcept
{
	WriteUnsignedLittleEndian(InNameId.Value, OutBytes, NameIdBytes);
}

FNameId FMessagingSystem::ReadNameIdLittleEndian(const std::uint8_t* const InBytes) noexcept
{
	return FNameId{ReadUnsignedLittleEndian(InBytes, NameIdBytes)};
}

void FMessagingSystem::CopyBytes(std::uint8_t* const OutDestination, const Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	for (std::size_t PayloadByteOffset = 0; PayloadByteOffset < InPayload.Size(); ++PayloadByteOffset)
	{
		OutDestination[PayloadByteOffset] = InPayload.Data()[PayloadByteOffset];
	}
}

} // namespace MicroWorld::Messaging
