// Out-of-class member definitions: wire frame codec, transport-result mapping, header
// encoding, and fixed-width little-endian byte primitives for TMessagingSystem<TTraits>.
// Included from MessagingSystem.h after the class body closes; never include this file
// directly.

template<typename TTraits>
EMessagingResult TMessagingSystem<TTraits>::MapTransportSendResult(const Core::ETransportResult InTransportResult) noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::EncodeFrameHeader(
	const FNameId InChannelNameId, const FNameId InMessageNameId, std::uint8_t* const OutFrameBytes) noexcept
{
	WriteNameIdLittleEndian(InChannelNameId, &OutFrameBytes[ChannelNameIdByteIndex]);
	WriteNameIdLittleEndian(InMessageNameId, &OutFrameBytes[MessageNameIdByteIndex]);
}

template<typename TTraits>
void TMessagingSystem<TTraits>::WriteUnsignedLittleEndian(
	const std::uint32_t InValue, std::uint8_t* const OutBytes, const std::size_t InByteCount) noexcept
{
	for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
	{
		const std::size_t BitShift = ByteOffset * BitsPerByte;
		OutBytes[ByteOffset] = static_cast<std::uint8_t>(InValue >> BitShift);
	}
}

template<typename TTraits>
std::uint32_t TMessagingSystem<TTraits>::ReadUnsignedLittleEndian(const std::uint8_t* const InBytes, const std::size_t InByteCount) noexcept
{
	std::uint32_t Value = 0;
	for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
	{
		const std::size_t BitShift = ByteOffset * BitsPerByte;
		Value |= static_cast<std::uint32_t>(InBytes[ByteOffset]) << BitShift;
	}

	return Value;
}

template<typename TTraits>
void TMessagingSystem<TTraits>::WriteNameIdLittleEndian(const FNameId InNameId, std::uint8_t* const OutBytes) noexcept
{
	WriteUnsignedLittleEndian(InNameId.Value, OutBytes, NameIdBytes);
}

template<typename TTraits>
FNameId TMessagingSystem<TTraits>::ReadNameIdLittleEndian(const std::uint8_t* const InBytes) noexcept
{
	return FNameId{ReadUnsignedLittleEndian(InBytes, NameIdBytes)};
}

template<typename TTraits>
void TMessagingSystem<TTraits>::CopyBytes(std::uint8_t* const OutDestination, const Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	for (std::size_t PayloadByteOffset = 0; PayloadByteOffset < InPayload.Size(); ++PayloadByteOffset)
	{
		OutDestination[PayloadByteOffset] = InPayload.Data()[PayloadByteOffset];
	}
}
