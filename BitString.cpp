// Prints:
//
//  Test against generated data:
//  LE data 19-bit: 0,7FFFF,0,7FFFF,0,7FFFF,0,7FFFF,
//  BE data 19-bit: 0,7FFFF,0,7FFFF,0,7FFFF,0,7FFFF,
//
//  Test against known constant data:
//  LE data 12-bit: 321,654,987,CBA,
//  BE data 12-bit: 321,654,987,CBA,
//
//  Test against written data:
//  LE data 21-bit: 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F,
//  BE data 21-bit: 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F,

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <bit> // C++23 std::byteswap
#include <span>
#include <new> // std::launder
#include <assert.h>
#include <stdexcept> // std::invalid_argument

// Reads a contiguous series of bits from the given bit offset,
// returning as a uint.
//
// Works with LE or BE data or LE or BE machines.
//
// Example:
//      uint32_t v = ReadBitString(data, 42, 13, std::endian::big);
//      // v now holds 13-bits read starting at relative bit offset 42.
uint32_t ReadBitString(
    std::span<uint8_t const> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset, // Must be within data.
    size_t bitSize, // Must be <= 32
    std::endian endianness // little or big
)
{
    using DataType = uint32_t; // Needs some work for even larger types.
    static_assert(std::endian::native == std::endian::little); // This has only been tested on an LE machine.
    const bool isBeData = (endianness == std::endian::big);
    const bool isBeHardware = (std::endian::native == std::endian::big);
    const bool endiannessMatchesHardware = (isBeData == isBeHardware);

    if (bitSize > sizeof(DataType) * CHAR_BIT)
    {
        throw std::invalid_argument("Bit size must be 32 or less");
    }

    union
    {
        uint64_t asUint64;
        uint8_t asBytes[sizeof(uint64_t)]; // Includes extra bytes (beyond DataType) in case bits misaligned.
    } value = {};
    assert(sizeof(value) * CHAR_BIT >= sizeof(DataType) * 2);

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

    // Mask and shift the value.
    const uint32_t bitOffset32Bit = static_cast<uint32_t>(bitOffset); // High bits don't matter anyway.
    #pragma warning(disable: 4146) // The unary minus is *intentional*.
    const uint32_t shiftAmount = (isBeData ? -(bitOffset32Bit + bitSize) : bitOffset32Bit) & 7;
    const uint64_t elementMask = (uint64_t(1) << bitSize) - 1; // Mask bits that are NOT the value.
    value.asUint64 = (*std::launder(&value.asUint64) >> shiftAmount) & elementMask;

    return static_cast<uint32_t>(value.asUint64);
}

// Writes a contiguous series of bits to the given bit offset.
//
// Works with LE or BE data or LE or BE machines.
void WriteBitString(
    std::span<uint8_t> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset,
    size_t bitSize,
    std::endian endianness, // little or big
    uint32_t newValue
)
{
    using DataType = uint32_t; // Needs some work for even larger types.
    const bool isBeData = (endianness == std::endian::big);
    const bool isBeHardware = (std::endian::native == std::endian::big);
    const bool endiannessMatchesHardware = (endianness == std::endian::native);

    if (bitSize > sizeof(DataType) * CHAR_BIT)
    {
        throw std::invalid_argument("Bit size must be 32 or less");
    }

    union
    {
        uint64_t asUint64;
        uint8_t asBytes[sizeof(uint64_t)]; // Includes extra bytes (beyond DataType) in case bits misaligned.
    } value = {};

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

void PrintElementsOfGivenBitSize(
    std::span<uint8_t const> data,
    size_t elementCount,
    size_t elementBitSize,
    std::endian endianness
)
{
    for (size_t i = 0; i < elementCount; ++i)
    {
        size_t elementBitOffset = i * elementBitSize;
        uint32_t elementValue = ReadBitString(data, elementBitOffset, elementBitSize, endianness);
        printf("%X,", elementValue);
    }
}

void PrintElementsOfGivenBitSizeOld(
    std::span<uint8_t const> data,
    size_t elementCount,
    uint32_t elementBitSize,
    std::endian endianness
)
{
    // Limitations of this simple function:
    assert(elementBitSize <= 24); // Function supports 1-24 bit reads.

    const uint32_t elementBitMask = (1 << elementBitSize) - 1;
    bool isReversedEndian = (endianness == std::endian::big);

    for (size_t i = 0; i < elementCount; ++i)
    {
        if ((i + 1) * elementBitSize > data.size_bytes() * CHAR_BIT)
        {
            continue;
        }

        uint32_t elementValue = *reinterpret_cast<uint32_t const*>(&data[i * elementBitSize / CHAR_BIT]);
        uint32_t bitOffsetModulus = (i * elementBitSize) % CHAR_BIT;

        uint32_t rightShift = bitOffsetModulus;
        // Big endian requires some massaging on an x86.
        if (isReversedEndian)
        {
            rightShift = sizeof(elementValue) * CHAR_BIT - elementBitSize - bitOffsetModulus;
            elementValue = std::byteswap(elementValue);
        }

        elementValue = (elementValue >> rightShift) & elementBitMask;
        printf("%X,", elementValue);
    }
}

void SetBit(
    std::span<uint8_t> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset,
    bool reversedBitsInByte
)
{
    assert(bitOffset < data.size_bytes() * CHAR_BIT);
    constexpr uint32_t byteMask = CHAR_BIT - 1;
    bitOffset ^= reversedBitsInByte ? byteMask : 0;
    data[bitOffset / CHAR_BIT] |= 1 << (bitOffset & byteMask);
}

int main()
{
    printf("Test against generated data:\n");
    {
        std::uint8_t elementsLe[19] = {};
        std::uint8_t elementsBe[19] = {};
        constexpr size_t elementBitSize = 19;
        constexpr size_t elementCount = sizeof(elementsLe) * CHAR_BIT / elementBitSize;

        // Initialize with simple test pattern of alternating 0 runs and 1 runs.
        for (size_t bitOffset = 0, bitCount = sizeof(elementsLe) * CHAR_BIT; bitOffset < bitCount; ++bitOffset)
        {
            static_assert(sizeof(elementsBe) == sizeof(elementsLe));
            if ((bitOffset / elementBitSize) & 1)
            {
                SetBit(elementsLe, bitOffset, false);
                SetBit(elementsBe, bitOffset, true);
            }
        }

        printf("LE data %zu-bit: ", elementBitSize);
        //PrintElementsOfGivenBitSizeOld(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        PrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little);    printf("\n");

        printf("BE data %zu-bit: ", elementBitSize);
        //PrintElementsOfGivenBitSizeOld(elementsBe, elementCount, elementBitSize, std::endian::big);    printf("\n");
        PrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big);       printf("\n");
    }
    printf("\n");

    printf("Test against known constant data:\n");
    {
        constexpr size_t elementBitSize = 12;
        constexpr size_t elementCount = 4;
        constexpr size_t byteCount = (elementCount * elementBitSize + CHAR_BIT - 1) / CHAR_BIT;

        const std::uint8_t elementsLe[byteCount + 3] = {0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB};
        const std::uint8_t elementsBe[byteCount + 3] = {0x32, 0x16, 0x54, 0x98, 0x7C, 0xBA};

        printf("LE data %zu-bit: ", elementBitSize);
        //PrintElementsOfGivenBitSizeOld(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        PrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little);    printf("\n");

        printf("BE data %zu-bit: ", elementBitSize);
        //PrintElementsOfGivenBitSizeOld(elementsBe, elementCount, elementBitSize, std::endian::big);    printf("\n");
        PrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big);       printf("\n");
    }
    printf("\n");

    printf("Test against written data:\n");
    {
        std::uint8_t elementsLe[42] = {};
        std::uint8_t elementsBe[42] = {};
        constexpr size_t elementBitSize = 21;
        constexpr size_t elementCount = sizeof(elementsLe) * CHAR_BIT / elementBitSize;

        // Initialize with simple test pattern of alternating 0 runs and 1 runs.
        for (size_t bitOffset = 0, bitCount = sizeof(elementsLe) * CHAR_BIT; bitOffset < bitCount; bitOffset += elementBitSize)
        {
            uint32_t value = uint32_t(bitOffset / elementBitSize);
            WriteBitString(/*inout*/ elementsLe, bitOffset, elementBitSize, std::endian::little, value);
            WriteBitString(/*inout*/ elementsBe, bitOffset, elementBitSize, std::endian::big,    value);
        }

        printf("LE data %zu-bit: ", elementBitSize);
        //PrintElementsOfGivenBitSizeOld(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        PrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little);    printf("\n");

        printf("BE data %zu-bit: ", elementBitSize);
        //PrintElementsOfGivenBitSizeOld(elementsBe, elementCount, elementBitSize, std::endian::big);    printf("\n");
        PrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big);       printf("\n");
    }
    printf("\n");
}
