// Prints:
//
//  Test array reading of generated bitmasks:
//      LE 19-bit data @0: 0,7FFFF,0,7FFFF,0,7FFFF,0,7FFFF
//               as bytes: 00,00,F8,FF,3F,00,00,FE,FF,0F,00,80,FF,FF,03,00,E0,FF,FF
//      BE 19-bit data @0: 0,7FFFF,0,7FFFF,0,7FFFF,0,7FFFF
//               as bytes: 00,00,1F,FF,FC,00,00,7F,FF,F0,00,01,FF,FF,C0,00,07,FF,FF
//
//  Test array reading of known constant data:
//      LE 12-bit data @0: 321,654,987,CBA
//               as bytes: 21,43,65,87,A9,CB
//      BE 12-bit data @0: 321,654,987,CBA
//               as bytes: 32,16,54,98,7C,BA
//
//  Test writing/reading array of increasing sequence:
//      LE 13-bit data @0: 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F
//               as bytes: 00,20,00,08,80,01,40,00,0A,80,01,38,00,08,20,01,28,80,05,C0,00,1A,80,03,78,00
//      BE 13-bit data @0: 0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F
//               as bytes: 00,00,00,40,04,00,30,02,00,14,00,C0,07,00,40,02,40,14,00,B0,06,00,34,01,C0,0F
//
//  Test reading/writing fields inside struct:
//       Read NE data: a=321, b=7FFF, c=6
//           as bytes: 21,E3,FF,6F
//      Write NE data: a=321, b=7FFF, c=6
//           as bytes: 21,E3,FF,6F
//
//  Test reading/writing float32 at unaligned offset:
//      32-bit LE data @5: 3.141593
//               as bytes: 60,FB,21,09,08
//      32-bit BE data @5: 3.141593
//               as bytes: 02,02,48,7E,D8

// Needs C++20.
#include <climits>
#include <stdint.h>
#include <stdio.h>
#include <bit> // std::endian
#include <span>
#include <assert.h>

#include "BitString.h"

void PrintBytes(std::span<uint8_t const> data)
{
    for (size_t i = 0, size = data.size(); i < size; ++i)
    {
        printf((i == 0) ? "%02X" : ",%02X", data[i]);
    }
}

void PrintBitStringElements(
    std::span<uint8_t const> data,
    size_t bitOffset,
    size_t bitSize,
    size_t elementCount,
    std::endian endianness
)
{
    for (size_t i = 0; i < elementCount; ++i)
    {
        size_t elementBitOffset = bitOffset + i * bitSize;
        uint32_t elementValue = ReadBitString(data, elementBitOffset, bitSize, endianness);
        printf((i == 0) ? "%X" : ",%X", elementValue);
    }
}

void PrintLeAndBeBitStringElements(
    std::span<uint8_t const> dataLe,
    std::span<uint8_t const> dataBe,
    size_t bitOffset,
    size_t bitSize,
    size_t elementCount
)
{
    assert(dataLe.size() == dataBe.size());

    printf("    LE %zu-bit data @%zu: ", bitSize, bitOffset);
    PrintBitStringElements(dataLe, bitOffset, bitSize, elementCount, std::endian::little); printf("\n");
    printf("             as bytes: "); PrintBytes(dataLe); printf("\n");

    printf("    BE %zu-bit data @%zu: ", bitSize, bitOffset);
    PrintBitStringElements(dataBe, bitOffset, bitSize, elementCount, std::endian::big); printf("\n");
    printf("             as bytes: "); PrintBytes(dataBe); printf("\n");
}

template <typename T>
auto wrapStructAsBytes(T& t) -> std::span<uint8_t>
{
    return {reinterpret_cast<uint8_t*>(&t), sizeof(t)};
};

int main()
{
    printf("Test array reading of generated bitmasks:\n");
    {
        uint8_t elementsLe[19] = {};
        uint8_t elementsBe[19] = {};
        constexpr size_t elementBitSize = 19;
        constexpr size_t elementCount = sizeof(elementsLe) * CHAR_BIT / elementBitSize;

        // Initialize with simple test pattern of alternating 0 runs and 1 runs.
        for (size_t bitOffset = 0, bitCount = sizeof(elementsLe) * CHAR_BIT; bitOffset < bitCount; ++bitOffset)
        {
            static_assert(sizeof(elementsBe) == sizeof(elementsLe));
            if ((bitOffset / elementBitSize) & 1)
            {
                SetSingleBit(elementsLe, bitOffset, false);
                SetSingleBit(elementsBe, bitOffset, true);
            }
        }

        PrintLeAndBeBitStringElements(elementsLe, elementsBe, 0, elementBitSize, elementCount);
    }
    printf("\n");

    printf("Test array reading of known constant data:\n");
    {
        const uint8_t elementsLe[] = {0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB};
        const uint8_t elementsBe[] = {0x32, 0x16, 0x54, 0x98, 0x7C, 0xBA};
        constexpr size_t elementBitSize = 12;
        constexpr size_t elementCount = sizeof(elementsLe) * CHAR_BIT / elementBitSize;

        PrintLeAndBeBitStringElements(elementsLe, elementsBe, 0, elementBitSize, elementCount);
    }
    printf("\n");

    printf("Test writing/reading array of increasing sequence:\n");
    {
        uint8_t elementsLe[26] = {};
        uint8_t elementsBe[26] = {};
        constexpr size_t elementBitSize = 13;
        constexpr size_t elementCount = sizeof(elementsLe) * CHAR_BIT / elementBitSize;

        // Initialize with simple test pattern of alternating 0 runs and 1 runs.
        for (size_t bitOffset = 0, bitCount = sizeof(elementsLe) * CHAR_BIT; bitOffset < bitCount; bitOffset += elementBitSize)
        {
            uint32_t value = uint32_t(bitOffset / elementBitSize);
            WriteBitString(/*inout*/ elementsLe, bitOffset, elementBitSize, std::endian::little, value);
            WriteBitString(/*inout*/ elementsBe, bitOffset, elementBitSize, std::endian::big,    value);
        }

        PrintLeAndBeBitStringElements(elementsLe, elementsBe, 0, elementBitSize, elementCount);
    }
    printf("\n");

    printf("Test reading/writing fields inside struct:\n");
    {
        // Note I haven't actually tested this case on a BE machine (I don't have any :b), but it *should* work. 🤞
        static_assert(std::endian::native == std::endian::little); // This has only been tested on an LE machine.

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
            .c = 0x6,
        };

        uint32_t aValue = ReadBitString(wrapStructAsBytes(testStruct), 0,     13, std::endian::native);
        uint32_t bValue = ReadBitString(wrapStructAsBytes(testStruct), 13,    15, std::endian::native);
        uint32_t cValue = ReadBitString(wrapStructAsBytes(testStruct), 13+15, 3,  std::endian::native);

        printf("     Read NE data: a=%X, b=%X, c=%X\n", aValue, bValue, cValue);
        printf("         as bytes: "); PrintBytes(wrapStructAsBytes(testStruct)); printf("\n");

        testStruct = {};
        WriteBitString(wrapStructAsBytes(testStruct), 0,     13, std::endian::native, 0x321);
        WriteBitString(wrapStructAsBytes(testStruct), 13,    15, std::endian::native, 0x7FFF);
        WriteBitString(wrapStructAsBytes(testStruct), 13+15, 3,  std::endian::native, 0x6);

        printf("    Write NE data: a=%X, b=%X, c=%X\n", testStruct.a, testStruct.b, testStruct.c);
        printf("         as bytes: "); PrintBytes(wrapStructAsBytes(testStruct)); printf("\n");
    }
    printf("\n");

    printf("Test reading/writing float32 at unaligned offset:\n");
    {
        uint8_t bufferLe[sizeof(float) + 1] = {};
        uint8_t bufferBe[sizeof(float) + 1] = {};
        constexpr size_t bitOffset = 5;
        constexpr size_t bitSize = sizeof(float) * CHAR_BIT;
        constexpr uint32_t piValue = std::bit_cast<uint32_t>(3.14159265358979323846f);
        float readbackValue;

        WriteBitString(bufferLe, bitOffset, bitSize, std::endian::little, piValue);
        WriteBitString(bufferBe, bitOffset, bitSize, std::endian::big,    piValue);

        readbackValue = std::bit_cast<float>(ReadBitString(bufferLe, bitOffset, bitSize, std::endian::little));
        printf("    %zu-bit LE data @%zu: %f\n", bitSize, bitOffset, readbackValue);
        printf("             as bytes: "); PrintBytes(bufferLe); printf("\n");

        readbackValue = std::bit_cast<float>(ReadBitString(bufferBe, bitOffset, bitSize, std::endian::big));
        printf("    %zu-bit BE data @%zu: %f\n", bitSize, bitOffset, readbackValue);
        printf("             as bytes: "); PrintBytes(bufferBe); printf("\n");
    }
    printf("\n");
}
