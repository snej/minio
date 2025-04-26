//
//  mini/varint.hh
//
//  Copyright © 2021 Jens Alfke. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include "minio/base.hh"
#include <bit>
#include <concepts>
#include <cstdint>
#include <span>
#include <stdexcept>

ASSUME_NONNULL_BEGIN

namespace snej::minio::varint {

    /// Variable-length *unsigned* integer encoding.
    /// This is NOT the format used by Protobuf, Go, etc.
    ///
    /// - Numbers 0x00..0xF7 are encoded unchanged, in 1 byte.
    /// - Larger numbers are encoded as:
    ///   - a length byte with value 0xF7 + <byte-count>, then
    ///   - the number minus 0xF8, in big-endian form, with no leading zero bytes.
    ///
    /// For example:
    ///    00 -> 00
    ///    F7 -> F7
    ///    F8 -> F8 00
    ///    F9 -> F8 01
    ///    FF -> F8 08
    ///  0100 -> F8 09
    ///  01F7 -> F8 FF
    ///  01F8 -> F9 00 01
    ///
    /// A useful property is that encoded numbers can be compared lexicographically.

    namespace i {
        // The number of bytes needed to represent the number `n` in binary.
        template <std::unsigned_integral UNSIGNED>
        inline pure constexpr unsigned byte_width(UNSIGNED n) noexcept {
            if (n) [[likely]]
                return (std::bit_width(n) + 7) / 8;
            else
                return 1;
        }

        // Swaps a number to/from big-endian byte order.
        template <std::unsigned_integral UNSIGNED>
        inline pure constexpr UNSIGNED big_endian(UNSIGNED n) noexcept {
            static_assert(sizeof(n) >= 2 && sizeof(n) <= 8);
            if constexpr (std::endian::native == std::endian::little) {
                #ifdef __cpp_lib_byteswap
                    return std::byteswap(n);                // C++23 or later
                #else
                    if constexpr (sizeof(n) == 2)
                        return __builtin_bswap16(n);        // Clang & GCC support these
                    else if constexpr (sizeof(n) == 4)
                        return __builtin_bswap32(n);
                    else
                        return __builtin_bswap64(n);
                #endif
            } else {
                return n;
            }
        }

        // Writes a variable length big-endian int to memory.
        template <std::unsigned_integral UNSIGNED>
        inline void write_be(void* dst, UNSIGNED n, size_t byteCount) noexcept {
            n = big_endian(n);
            memcpy(dst, (uint8_t const*)&n + sizeof(n) - byteCount, byteCount);
        }

        // Reads a variable length big-endian int from memory.
        // Throws if the byte count is bigger than the result type.
        template <std::unsigned_integral UNSIGNED>
        inline UNSIGNED read_be(const void* src, size_t byteCount) {
            if (sizeof(UNSIGNED) < 8 && byteCount > sizeof(UNSIGNED)) [[unlikely]]
                throw std::runtime_error("varint is too big to decode");
            UNSIGNED n = 0;
            memcpy((uint8_t*)&n + sizeof(n) - byteCount, src, byteCount);
            return big_endian(n);
        }
    }


    /// Returns the maximum size of the varint encoding of a number of type UNSIGNED.
    template <std::unsigned_integral UNSIGNED>
    inline constexpr size_t maxSize() noexcept {
        return 1 + sizeof(UNSIGNED);
    }


    /// Returns the size in bytes of the varint encoding of `n`.
    template <std::unsigned_integral UNSIGNED>
    inline pure unsigned sizeOfInt(UNSIGNED n) noexcept {
        if (n <= 0xF7) [[likely]]
            return 1;
        else
            return 1 + i::byte_width(n - 0xF8);
   }


    /// Writes `n` as varint to `dst`; returns number of bytes written (1 to 9.)
    template <std::unsigned_integral UNSIGNED>
    inline unsigned putInt(void *dst_, UNSIGNED n) noexcept {
        auto dst = static_cast<uint8_t*>(dst_);
        if (n <= 0xF7) [[likely]] {
            *dst = uint8_t(n);
            return 1;
        } else {
            n -= 0xF8;
            // Write the byte count:
            unsigned byteCount = i::byte_width(n);
            *dst = uint8_t(0xF7 + byteCount);
            // Write the big-endian value:
            i::write_be(dst + 1, n, byteCount);
            return 1 + byteCount;
        }
    }


    /// Decodes varint from pointer `src`.
    template <std::unsigned_integral UNSIGNED>
    inline pure UNSIGNED getInt(const void* src_) {
        auto src = static_cast<const uint8_t*>(src_);
        if (uint8_t b = *src++; b <= 0xF7) [[likely]] {
            return b;
        } else {
            size_t byteCount = b - 0xF7;
            return i::read_be<UNSIGNED>(src, byteCount) + 0xF8;
        }
    }


    /// Decodes varint from span `src`. Throws if it would read past the end.
    template <std::unsigned_integral UNSIGNED>
    inline pure UNSIGNED getInt(std::span<uint8_t> src) {
        if (src.empty()) [[unlikely]]
            throw std::runtime_error("truncated varint");
        if (uint8_t b = src[0]; b <= 0xF7) [[likely]] {
            return b;
        } else {
            size_t byteCount = b - 0xF7;
            if (1 + byteCount > src.size()) [[unlikely]]
                throw std::runtime_error("truncated varint");
            return i::read_be<UNSIGNED>(&src[1], byteCount) + 0xF8;
        }
    }


    /// Decodes varint from pointer `src` (passed by reference);
    /// updates `src` to point to the first byte after the varint.
    template <std::unsigned_integral UNSIGNED>
    inline pure UNSIGNED readInt(const uint8_t* _Nonnull &src) {
        UNSIGNED n;
        if (uint8_t b = *src++; b <= 0xF7) [[likely]] {
            n = b;
        } else {
            int byteCount = b - 0xF7;
            n = i::read_be<UNSIGNED>(src, byteCount) + 0xF8;
            src += byteCount;
        }
        return n;
    }


    /// Given the first byte of a varint, returns the number of bytes remaining.
    inline pure size_t remainingSize(uint8_t firstByte) noexcept {
        return (firstByte <= 0xF7) ? 0 : (firstByte - 0xF7);
    }


    /// Given a pointer to a varint, returns its total number of bytes.
    inline size_t sizeOfIntAt(const void* addr) noexcept {
        return 1 + remainingSize(*static_cast<const uint8_t*>(addr));
    }


    /// Skips past a varint at `src`; returns the updated pointer to the next byte after it.
    inline pure const void* skipInt(const void *src) {
        return (const uint8_t*)src + sizeOfIntAt(src);
    }

}

ASSUME_NONNULL_END
