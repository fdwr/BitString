// Prints:
//
// Test against generated data:
// LE data 19-bit: 0,7FFFF,0,7FFFF,0,7FFFF,0,7FFFF,
//       as bytes: 00,00,F8,FF,3F,00,00,FE,FF,0F,00,80,FF,FF,03,00,E0,FF,FF,
// BE data 19-bit: 0,7FFFF,0,7FFFF,0,7FFFF,0,7FFFF,
//       as bytes: 00,00,1F,FF,FC,00,00,7F,FF,F0,00,01,FF,FF,C0,00,07,FF,FF,
// 
// Test against known constant data:
// LE data 12-bit: 321,654,987,CBA,
//       as bytes: 21,43,65,87,A9,CB,00,00,00,
// BE data 12-bit: 321,654,987,CBA,
//       as bytes: 32,16,54,98,7C,BA,00,00,00,
// 
// Test against written data:
// LE data 13-bit: 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F,
//       as bytes: 00,20,00,08,80,01,40,00,0A,80,01,38,00,08,20,01,28,80,05,C0,00,1A,80,03,78,00,
// BE data 13-bit: 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F,
//       as bytes: 00,00,00,40,04,00,30,02,00,14,00,C0,07,00,40,02,40,14,00,B0,06,00,34,01,C0,0F,
//
// Test with struct:
// a=321, b=7FFF, c=6,

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <bit> // C++23 std::byteswap
#include <span>
#include <new> // std::launder
#include <assert.h>
#include <stdexcept> // std::invalid_argument

// Reads a contiguous series of bits from the given bit offset, returning as a uint.
// The caller can then bitcast the result to a more specific type, like float16.
// Works with LE or BE data on both LE and BE machines.
//
// Example:
//      uint32_t v = ReadBitString(data, 42, 13, std::endian::big);
//      // v now holds 13-bits read starting at relative bit offset 42.
//
// Explanation:
//      e.g. Given:
//      bitOffset = 13
//      bitSize = 15
//
//      (1) Determine the memory offsets/range in the source data:
//      
//      Absolute bit index: [0 1 2 3 4 5 6 7   8 9₁0₁1₁2₁3₁4₁5  ₁6₁7₁8₁9₂0₂1₂2₂3  ₂4₂5₂6₂7₂8₂9₃0₃1  ₃2₃3...
//      Byte:               [0              ] [1              ] [2              ] [3              ] [4.....
//      Bit in byte:        [0 1 2 3 4 5 6 7] [0 1 2 3 4 5 6 7] [0 1 2 3 4 5 6 7] [0 1 2 3 4 5 6 7] [0 1...
//                                             |         |                                 |        |
//                                             |         bitOffset=13                      |        |
//                                             |         <----------bitSize=15------------->        |
//                                             |                                                    |
//                                             dataByteOffsetBegin=1                                dataByteOffsetEnd=4
//                                             <-----------------elementByteSize=3------------------>
//
//      (2) Copy into value bytes (low bytes if LE, or adjusted to high bytes if BE):
//
//      On LE machines...
//      value.asBytes:      [0xxxxx][1xxxxx][2xxxxx][3     ][4     ][5     ][6     ][7     ]
//      value.asUint64:     xxxxxxxxxxxxxxxxxxxxxxx00000000000000000000000000000000000000000
//
//      On BE machines...
//      value.asBytes:      [0     ][1     ][2     ][3     ][4     ][5xxxxx][6xxxxx][7xxxxx]
//      value.asUint64:     00000000000000000000000000000000000000000xxxxxxxxxxxxxxxxxxxxxx
//
//      (4) Swap bytes if running on an architecture opposite to the data format (like BE on an LE machine):
//
//      value.asBytes:       [0xxxxx][1xxxxx][2xxxxx][3     ][4     ][5     ][6     ][7     ]
//                           <---------swap--------->
//      value.asBytes:       [2xxxxx][1xxxxx][0xxxxx][3     ][4     ][5     ][6     ][7     ]
//
//      (5) Shift and mask:
//      *note: visualized in LTR bitstream order 0123.. for simpler correspondence to byte order
//             whereas typical numbers would be RTL order ...3210.
//
//      result:             [00000xxxxxxxxxxxxxxx0000000000000000000000000000000000000000000]
//      shift low:          [xxxxxxxxxxxxxxx000000000000000000000000000000000000000000000000] x >>= 5
//      bitMask:            [111111111111111000000000000000000000000000000000000000000000000] 0x0000000000007FFF
//
//      Return result:      [xxxxxxxxxxxxxxx00000000000000000] bits 0-14 of uint32, with upper bits being 0.
//
uint32_t ReadBitString(
    std::span<uint8_t const> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset, // Must be within data.
    size_t bitSize, // Must be <= 32
    std::endian endianness
)
{
    using LargestDataType = uint32_t; // Needs some work for even larger types.
    static_assert(std::endian::native == std::endian::little); // This has only been tested on an LE machine.
    const bool isBeData = (endianness == std::endian::big);
    const bool isBeHardware = (std::endian::native == std::endian::big);
    const bool endiannessMatchesHardware = (isBeData == isBeHardware);

    if (bitSize > sizeof(LargestDataType) * CHAR_BIT)
    {
        throw std::invalid_argument("Bit size must be 32 or less");
    }

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

// Writes a contiguous series of bits to the given bit offset.
//
// Works with LE or BE data or LE or BE machines.
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

    if (bitSize > sizeof(LargestDataType) * CHAR_BIT)
    {
        throw std::invalid_argument("Bit size must be 32 or less");
    }

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

void OldPrintElementsOfGivenBitSize(
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

void PrintBytes(std::span<uint8_t const> data)
{
    for (auto d : data)
    {
        printf("%02X,", d);
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
        //OldPrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        PrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        printf("      as bytes: "); PrintBytes(elementsLe); printf("\n");

        printf("BE data %zu-bit: ", elementBitSize);
        //OldPrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big); printf("\n");
        PrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big); printf("\n");
        printf("      as bytes: "); PrintBytes(elementsBe); printf("\n");
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
        //OldPrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        PrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        printf("      as bytes: "); PrintBytes(elementsLe); printf("\n");

        printf("BE data %zu-bit: ", elementBitSize);
        //OldPrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big); printf("\n");
        PrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big); printf("\n");
        printf("      as bytes: "); PrintBytes(elementsBe); printf("\n");
    }
    printf("\n");

    printf("Test against written data:\n");
    {
        std::uint8_t elementsLe[26] = {};
        std::uint8_t elementsBe[26] = {};
        constexpr size_t elementBitSize = 13;
        constexpr size_t elementCount = sizeof(elementsLe) * CHAR_BIT / elementBitSize;

        // Initialize with simple test pattern of alternating 0 runs and 1 runs.
        for (size_t bitOffset = 0, bitCount = sizeof(elementsLe) * CHAR_BIT; bitOffset < bitCount; bitOffset += elementBitSize)
        {
            uint32_t value = uint32_t(bitOffset / elementBitSize);
            WriteBitString(/*inout*/ elementsLe, bitOffset, elementBitSize, std::endian::little, value);
            WriteBitString(/*inout*/ elementsBe, bitOffset, elementBitSize, std::endian::big,    value);
        }

        printf("LE data %zu-bit: ", elementBitSize);
        //OldPrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        PrintElementsOfGivenBitSize(elementsLe, elementCount, elementBitSize, std::endian::little); printf("\n");
        printf("      as bytes: "); PrintBytes(elementsLe); printf("\n");

        printf("BE data %zu-bit: ", elementBitSize);
        //OldPrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big); printf("\n");
        PrintElementsOfGivenBitSize(elementsBe, elementCount, elementBitSize, std::endian::big); printf("\n");
        printf("      as bytes: "); PrintBytes(elementsBe); printf("\n");
    }
    printf("\n");

    // Note I haven't actually tested this case on a BE machine (I don't have any :b), but it *should* work. 🤞
    printf("Test with struct:\n");
    {
        struct TestStruct
        {
            // On an LE machine, the bytes are laid out:
            // 
            //  Byte 0: a[bits: 0-7]
            //  Byte 1: a[bits: 8-12], b[bits: 0-2]
            //  Byte 2: b[bits: 3-10]
            //  Byte 3: b[bits: 11-14], c[bits: 0-3]
            //
            // On a BE machine (gcc anyway):
            //
            //  Byte 0: a[bits: 5-12]
            //  Byte 1: a[bits: 0-4], b[bits: 12-14]
            //  Byte 2: b[bits: 4-11]
            //  Byte 3: b[bits: 0-3], c[bits: 0-3]
            uint32_t a : 13;
            uint32_t b : 15;
            uint32_t c : 4;
        };
        TestStruct testStruct = {
            .a = 0x321,
            .b = 0x7FFF,
            .c = 0x6
        };
        auto wrapAsBytes = [](TestStruct& ts) -> std::span<uint8_t>
        {
            return { reinterpret_cast<uint8_t*>(&ts), sizeof(ts) };
        };

        uint32_t aValue = ReadBitString(wrapAsBytes(testStruct), 0,     13, std::endian::native);
        uint32_t bValue = ReadBitString(wrapAsBytes(testStruct), 13,    15, std::endian::native);
        uint32_t cValue = ReadBitString(wrapAsBytes(testStruct), 13+15, 3,  std::endian::native);

        printf("a=%X, b=%X, c=%X,", aValue, bValue, cValue);
    }
    printf("\n");
}
