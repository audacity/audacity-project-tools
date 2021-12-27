/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <deque>
#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>
#include <cstddef>

class Buffer final
{
public:
    static constexpr size_t BUFFER_SIZE = 1024 * 1024;

    void reset();

    size_t getSize() const noexcept;

    template<typename T>
    std::enable_if_t<std::is_trivial_v<T>, bool> append(T data)
    {
        constexpr auto size = sizeof(T);

        if (mChunks.empty() || (BUFFER_SIZE - mLastChunkOffset) < size)
            return append(&data, size);

        void* ptr = mChunks.back().data() + mLastChunkOffset;

        *static_cast<T*>(ptr) = data;
        mLastChunkOffset += size;

        return true;
    }

    bool append(const void* data, size_t size);

    template <typename T>
    std::enable_if_t<std::is_trivial_v<T>, size_t> read(T& data, size_t offset) const noexcept
    {
        constexpr auto size = sizeof(T);

        if (getSize() < (offset + size))
            return 0;

        const size_t chunkIndex = offset / BUFFER_SIZE;
        offset = offset - BUFFER_SIZE * chunkIndex;

        if (BUFFER_SIZE < (offset + size))
            return read(&data, offset, size);

        const void* ptr = mChunks[chunkIndex].data() + offset;

        data = *static_cast<const T*>(ptr);

        return size;
    }

    size_t read(void* data, size_t offset, size_t size) const noexcept;

    std::vector<uint8_t> getLinearRepresentation() const;

private:
    using Chunk = std::array<uint8_t, BUFFER_SIZE>;
    using ChunkList = std::deque<Chunk>;

    ChunkList mChunks;
    size_t mLastChunkOffset { 0 };
};
