// Needs C++20.
#include <stdint.h> // uint32_t
#include <bit>      // std::endian
#include <span>     // std::span

// Reads a contiguous series of bits from the given bit offset, returning as a uint.
// The caller can then bitcast the result to a more specific type, like float16.
// Works with LE or BE data on both LE and BE machines.
// Invalid bitOffset's outside data are discarded.
// Bit sizes larger than 32 are clamped.
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
);

// Writes a contiguous series of bits to the given bit offset.
// Works with LE or BE data or LE or BE machines.
// Invalid bitOffset's outside data are discarded.
// Bit sizes larger than 32 are clamped.
void WriteBitString(
    std::span<uint8_t> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset,
    size_t bitSize,
    std::endian endianness,
    uint32_t newValue
);

// Basically like the x86 bts instruction, except it can invert indices in bytes for BE.
// Invalid bitOffset's outside data are discarded.
void SetSingleBit(
    std::span<uint8_t> data, // Size limited to 500GB's on 32-bit systems.
    size_t bitOffset,
    bool reversedBitsInByte
);
