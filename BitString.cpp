// Needs C++20.
#include <stdint.h>
#include <climits>
#include <cstring>
#include <bit>          // std::endian
#include <span>         // std::span
#include <new>          // std::launder
#include <algorithm>
#include <assert.h>

#include "BitString.h"

uint32_t ReadBitString(
    std::span<uint8_t const> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset, // Must be within data.
    size_t bitSize, // Must be <= 32
    std::endian endianness
)
{
    using LargestDataType = uint32_t; // Needs some work for even larger types.
    const bool isBeData = (endianness == std::endian::big);
    const bool isBeHardware = (std::endian::native == std::endian::big);
    const bool endiannessMatchesHardware = (isBeData == isBeHardware);

    assert(bitSize <= sizeof(LargestDataType) * CHAR_BIT);
    bitSize = std::min(bitSize, sizeof(LargestDataType) * CHAR_BIT);

    union
    {
        uint64_t asUint64;
        uint8_t asBytes[sizeof(uint64_t)]; // Includes extra bytes (beyond LargestDataType) in case bits misaligned.
    } value = {};
    static_assert(sizeof(value) >= sizeof(LargestDataType) * 2);

    // Copy the data into aligned memory.
    // Note a more efficient approach could be to read the data in 2 x 32-bit word-aligned reads.
    // It would technically touch invalid memory on the trailing end (past data() + size()), but
    // it would be safe in practice because page sizes and cache lines on all relevant machines
    // are at least 8 bytes. Sticking with memcpy here for simplicity and safety.
    const size_t dataByteSize = data.size_bytes();
    const size_t dataByteOffsetBegin = std::min(bitOffset / CHAR_BIT, dataByteSize);
    const size_t dataByteOffsetEnd = std::min((bitOffset + bitSize + 7) / CHAR_BIT, dataByteSize);
    const size_t elementByteSize = dataByteOffsetEnd - dataByteOffsetBegin;
    const size_t endiannessAdjustment = isBeHardware ? sizeof(value.asBytes) - elementByteSize : 0;
    memcpy(value.asBytes + endiannessAdjustment, data.data() + dataByteOffsetBegin, elementByteSize);

    // Swap bytes if reading value on the opposite architecture, else nop.
    if (!endiannessMatchesHardware)
    {
        std::reverse(value.asBytes, value.asBytes + elementByteSize);
    }

    // Mask and shift the value.
    const uint32_t bitOffset32Bit = static_cast<uint32_t>(bitOffset); // High bits don't matter anyway.
    #pragma warning(disable: 4146) // The unary minus is *intentional*.
    const uint32_t shiftAmount = (isBeData ? -(bitOffset32Bit + bitSize) : bitOffset32Bit) & 7;
    const uint64_t elementMask = (uint64_t(1) << bitSize) - 1; // Mask bits that are NOT the value.
    value.asUint64 = (*std::launder(&value.asUint64) >> shiftAmount) & elementMask;

    return static_cast<uint32_t>(value.asUint64);
}

void WriteBitString(
    std::span<uint8_t> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset,
    size_t bitSize,
    std::endian endianness,
    uint32_t newValue
)
{
    using LargestDataType = uint32_t; // Needs some work for even larger types.
    const bool isBeData = (endianness == std::endian::big);
    const bool isBeHardware = (std::endian::native == std::endian::big);
    const bool endiannessMatchesHardware = (endianness == std::endian::native);

    assert(bitSize <= sizeof(LargestDataType) * CHAR_BIT);
    bitSize = std::min(bitSize, sizeof(LargestDataType) * CHAR_BIT);

    union
    {
        uint64_t asUint64;
        uint8_t asBytes[sizeof(uint64_t)]; // Includes extra bytes (beyond LargestDataType) in case bits misaligned.
    } value = {};
    static_assert(sizeof(value) >= sizeof(LargestDataType) * 2);

    // Copy the data into aligned memory.
    const size_t dataByteSize = data.size_bytes();
    const size_t dataByteOffsetBegin = std::min(bitOffset / CHAR_BIT, dataByteSize);
    const size_t dataByteOffsetEnd = std::min((bitOffset + bitSize + 7) / CHAR_BIT, dataByteSize);
    const size_t elementByteSize = dataByteOffsetEnd - dataByteOffsetBegin;
    const size_t endiannessAdjustment = isBeHardware ? sizeof(value.asBytes) - elementByteSize : 0;
    memcpy(value.asBytes + endiannessAdjustment, data.data() + dataByteOffsetBegin, elementByteSize);

    // Swap bytes if reading value on the opposite architecture, else nop.
    if (!endiannessMatchesHardware)
    {
        std::reverse(value.asBytes, value.asBytes + elementByteSize);
    }

    // Mask off the old value and shift the new value.
    const uint32_t bitOffset32Bit = static_cast<uint32_t>(bitOffset); // High bits don't matter anyway.
    #pragma warning(disable: 4146) // The unary minus is *intentional*.
    const uint32_t shiftAmount = (isBeData ? -(bitOffset32Bit + bitSize) : bitOffset32Bit) & 7;
    const uint64_t elementMask = ~(((uint64_t(1) << bitSize) - 1) << shiftAmount); // Mask bits that ARE the value.
    auto oldValue = *std::launder(&value.asUint64);
    value.asUint64 = (oldValue & elementMask) | (static_cast<uint64_t>(newValue) << shiftAmount);

    // Write back the new value.
    if (!endiannessMatchesHardware)
    {
        std::reverse(std::launder(value.asBytes), std::launder(value.asBytes) + elementByteSize);
    }
    assert(dataByteOffsetBegin <= data.size());
    assert(dataByteOffsetEnd <= data.size());
    memcpy(data.data() + dataByteOffsetBegin, value.asBytes + endiannessAdjustment, elementByteSize);
}

void SetSingleBit(
    std::span<uint8_t> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset,
    bool reversedBitsInByte
)
{
    size_t byteOffset = bitOffset / CHAR_BIT;
    assert(byteOffset < data.size_bytes());
    if (byteOffset < data.size_bytes())
    {
        constexpr uint32_t byteMask = CHAR_BIT - 1;
        bitOffset ^= reversedBitsInByte ? byteMask : 0;
        data[byteOffset] |= 1 << (bitOffset & byteMask);
    }
}
