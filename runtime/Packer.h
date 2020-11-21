/* Copyright (c) 2016-2018 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <cassert>
#include <type_traits>

#include <cstddef>
#include <cstring>
#include <cstdint>

#include "Common.h"

#ifndef PACKER_H
#define PACKER_H

/**
 * This file contains a collection of pack/unpack functions that are used
 * by the LogCompressor/Decompressor to find smaller representation for various
 * primitive types and save/retrieve them to/from a char array. The current
 * implementation requires the caller to persist a special code generated by
 * pack() to be passed into unpack() and requires the user to explicitly specify
 * the type of the variable being unpack()-ed. This is achieved in NanoLog by
 * encoding the types directly into generated code that inflates log messages.
 *
 * IMPORTANT: The implementation of the pack/unpack functions will only
 * work on little-endian machines.
 *
 * ** Implementation **
 *
 * The current compression scheme used in the pack/unpack functions is to store
 * the fewest number of bytes needed to represent integer values. No compression
 * is performed for floating-point and string types which are stored verbatim
 * into the compressed stream.
 *
 * For the integer values, the algorithm will check the value of the integer
 * and determine how many bytes need to be stored. For example, a value of 127
 * will require one byte and a value of 257 will require two bytes. The number
 * of bytes needed along with a sign bit is stored in a 4-bit code called a
 * Nibble. This nibble is produced by the pack() function and needs to be passed
 * back into the unpack() function to interpret the compressed bytes.
 *
 * The value of the special code S indicates different things
 *      (a) S = 0                  => 16-byte value was encoded
 *      (b) S = [1, sizeof(T)]     => integer was represented in S bytes
 *      (c) S = [9, 8 + sizeof(T)) => integer was represented in S-8 bytes and
 *                                    a negation was performed on the integer
 *
 * TODO(syang0) Consider a packing scheme that can encode the special code
 * directly in the stream itself
 *
 * TODO(syang0) A common use case for logs is to log metrics, which tend to be
 * monotonically increasing (i.e. time alive, number of hits, etc), so would it
 * make more sense to start encoding number deltas instead of just the smallest
 * value.
 */

namespace BufferUtils {

/**
 * Packs two 4-bit nibbles into one byte. This is used to pack the special
 * codes returned by pack() in the compressed log.
 */
struct TwoNibbles {
    uint8_t first:4;
    uint8_t second:4;
} __attribute__((packed));

/**
 * Given an unsigned integer and a char array, find the fewest number of
 * bytes needed to represent the integer, copy that many bytes into the
 * char array, and bump the char array pointer.
 *
 * \param[in/out] buffer
 *      char array pointer used to store the compressed value and bump
 * \param val
 *      Unsigned integer to pack into the buffer
 *
 * \return
 *      Special 4-bit value indicating how the primitive was packed
 */
template<typename T>
inline typename std::enable_if<std::is_integral<T>::value &&
                                !std::is_signed<T>::value, int>::type
pack(char **buffer, T val) {
    // Binary search for the smallest container. It is also worth noting that
    // with -O3, the compiler will strip out extraneous if-statements based on T
    // For example, if T is uint16_t, it would only leave the t < 1U<<8 check

    //TODO(syang0) Is this too costly vs. a simple for loop?
    int numBytes;
    if (val < (1UL << 8)) {
            numBytes = 1;
    } else if (val < (1UL << 16)) {
        numBytes = 2;
    } else if (val < (1UL << 24)) {
        numBytes = 3;
    } else if (val < (1UL << 32)) {
        numBytes = 4;
    } else if (val < (1UL << 40)) {
        numBytes = 5;
    } else if (val < (1UL << 48)) {
        numBytes = 6;
    } else if (val < (1UL << 56)) {
        numBytes = 7;
    } else {
        numBytes = 8;
    }

    // Although we store the entire value here, we take advantage of the fact
    // that x86-64 is little-endian (storing the least significant bits first)
    // and lop off the rest by only partially incrementing the buffer pointer
    std::memcpy(*buffer, &val, sizeof(T));
    *buffer += numBytes;

    return numBytes;
}

/**
 * Below are a series of pack functions that take in a signed integer,
 * test to see if the value will be smaller if negated, and then invoke
 * the unsigned version of the pack() function above.
 *
 * \param[in/out] buffer
 *      char array to copy the value into and bump
 * \param val
 *      Unsigned integer to pack into the buffer
 *
 * \return
 *      Special 4-bit value indicating how the primitive was packed
 */
inline int
pack(char **buffer, int32_t val)
{
    if (val >= 0 || val <= int32_t(-(1<<24)))
        return pack<uint32_t>(buffer, static_cast<uint32_t>(val));
    else
        return 8 + pack<uint32_t>(buffer, static_cast<uint32_t>(-val));
}

inline int
pack(char **buffer, int64_t val)
{
    if (val >= 0 || val <= int64_t(-(1LL<<56)))
        return pack<uint64_t>(buffer, static_cast<uint64_t>(val));
    else
        return 8 + pack<uint64_t>(buffer, static_cast<uint64_t>(-val));
}

//TODO(syang0) we should measure the performance of doing it this way
// vs taking both the negated and non-negated versions and encoding the smaller
inline int
pack(char **buffer, long long int val)
{
    if (val >= 0 || val <= int64_t(-(1LL<<56)))
        return pack<uint64_t>(buffer, static_cast<uint64_t>(val));
    else
        return 8 + pack<uint64_t>(buffer, static_cast<uint64_t>(-val));
}

// The following pack functions that specialize on smaller signed types don't
// make sense in the context of NanoLog since printf doesn't allow the
// specification of an int16_t or an int8_t. This means we wouldn't have the
// type information to unpack them properly based purely on the format string.
#if 0
    inline int
pack(char **buffer, int8_t val)
{
    **buffer = val;
    (*buffer)++;
    return 1;
}

inline int
pack(char **buffer, int16_t val)
{
    if (val >= 0 || val <= int16_t(-(1<<8)))
        return pack<uint16_t>(buffer, static_cast<uint16_t>(val));
    else
        return 8 + pack<uint16_t>(buffer, static_cast<uint16_t>(-val));
}
#endif

    /**
 * Pointer specialization for the pack template that will copy the value
 * without compression.
 *
 * \param[in/out] buffer
 *      char array to copy the integer into and bump
 * \param val
 *      Unsigned integer to pack into the buffer
 *
 * \return - Special 4-bit value indicating how the primitive was packed
 */
template<typename T>
inline int
pack(char **in, T* pointer) {
    return pack<uint64_t>(in, reinterpret_cast<uint64_t>(pointer));
}

/**
 * Floating point specialization for the pack template that will copy the value
 * without compression.
 *
 * \param[in/out] buffer
 *      char array to copy the float into and bump
 * \param val
 *      Unsigned integer to pack into the buffer
 *
 * \return - Special 4-bit value indicating how the primitive was packed
 */
template<typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, int>::type
pack(char **buffer, T val) {
    std::memcpy(*buffer, &val, sizeof(T));
    *buffer += sizeof(T);
    return sizeof(T);
}

/**
 * Below are various unpack functions that will take in a data array pointer
 * and the special pack code, return the value originally pack()-ed and bump
 * the pointer to "consume" the pack()-ed value.
 *
 * \param in
 *      data array pointer to read the data back from and increment.
 * \param packResult
 *      special 4-bit code returned from pack()
 *
 * \return
 *      original full-width value before compression
 */

template<typename T>
inline typename std::enable_if<!std::is_floating_point<T>::value &&
                                !std::is_pointer<T>::value, T>::type
unpack(const char **in, uint8_t packResult)
{
    int64_t packed = 0;

    if (packResult <= 8) {
        memcpy(&packed, (*in), packResult);
        (*in) += packResult;
       return static_cast<T>(packed);
    }

    int bytes = packResult == 0 ? 16 : (0x0f & (packResult - 8));
    memcpy(&packed, (*in), bytes);
    (*in) += bytes;

    return static_cast<T>(-packed);
}

template<typename T>
inline typename std::enable_if<std::is_pointer<T>::value, T>::type
unpack(const char **in, uint8_t packNibble) {
    return (T*)(unpack<uint64_t>(in, packNibble));
}

template<typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type
unpack(const char **in, uint8_t packNibble) {
    if (packNibble == sizeof(float)) {
        float result;
        std::memcpy(&result, *in, sizeof(float));
        *in += sizeof(float);
        return result;
    }

    // Double case
    T result;
    int bytes = packNibble == 0 ? 16 : packNibble;
    std::memcpy(&result, (*in), bytes);
    (*in) += bytes;
    
    return result;
}

/**
 * Given a stream of nibbles, return the total number of bytes used to represent
 * the values encoded with the nibbles.
 *
 * \param nibbles
 *      The start of the nibbles
 * \param numNibbles
 *      Number of nibbles to process
 *
 * \return
 *      The number of bytes encoded used to encode the values
 */
inline static uint32_t
getSizeOfPackedValues(const TwoNibbles *nibbles, uint32_t numNibbles)
{
    uint32_t size = 0;
    for (uint32_t i = 0; i < numNibbles/2; ++i) {
        size += nibbles[i].first + nibbles[i].second;
        if (nibbles[i].first == 0)
            size += 16;
        if (nibbles[i].first > 0x8)
            size -= 8;
        if (nibbles[i].second == 0)
            size += 16;
        if (nibbles[i].second > 0x8)
            size -= 8;
    }

    if (numNibbles & 0x1) {
        size += nibbles[numNibbles / 2].first;
        if (nibbles[numNibbles/2].first == 0)
            size += 16;
        if (nibbles[numNibbles/2].first > 0x8)
            size -= 8;
    }

    return size;
}

/**
 * This class takes in a data stream of pack() Nibbles followed by pack()'ed
 * values as produced by the compressor and unpack()'s them one by one.
 */
class Nibbler {
PRIVATE:
    // Position in the nibble stream
    const TwoNibbles *nibblePosition;

    // Indicates whether whether to use the first nibble or second
    bool onFirstNibble;

    // Number of nibbles in this stream
    int numNibbles;

    // Position in the stream marking the next packed value
    const char *currPackedValue;

    // End of the last valid packed value
    const char *endOfValues;

public:
    /**
     * Nibbler Constructor
     *
     * \param nibbleStart
     *      Data stream consisting of the Nibbles followed by pack()ed values.
     * \param numNibbles
     *      Number of nibbles in the data stream
     */
    Nibbler(const char *nibbleStart, int numNibbles)
        : nibblePosition(reinterpret_cast<const TwoNibbles*>(nibbleStart))
        , onFirstNibble(true)
        , numNibbles(numNibbles)
        , currPackedValue(nibbleStart + (numNibbles + 1)/2)
        , endOfValues(nullptr)
    {
        endOfValues = nibbleStart
                             + (numNibbles + 1)/2
                             + getSizeOfPackedValues(nibblePosition, numNibbles);
    }

    /**
     * Returns the next pack()-ed value in the stream
     *
     * \tparam T
     *      Type of the value in the stream
     * \return
     *      Next pack()-ed value in the stream
     */
    template<typename T>
    T getNext() {
        assert(currPackedValue < endOfValues);

        uint8_t nibble = (onFirstNibble) ? nibblePosition->first
                                         : nibblePosition->second;

        T ret = unpack<T>(&currPackedValue, nibble);

        if (!onFirstNibble)
            ++nibblePosition;

        onFirstNibble = !onFirstNibble;

        return ret;
    }

    /**
     * Returns a pointer to to the first byte beyond the last pack()-ed value
     *
     * \return
     *      Pointer to the first byte beyond last pack()-ed value.
     */
    const char *
    getEndOfPackedArguments() {
        return endOfValues;
    }
};
} /* BufferUtils */

#endif /* PACKER_H */

