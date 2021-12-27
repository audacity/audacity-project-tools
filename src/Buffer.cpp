/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/


#include "Buffer.h"

#include <algorithm>
#include <cstring>

void Buffer::reset()
{
    mChunks.clear();
    mLastChunkOffset = {};
}

size_t Buffer::getSize() const noexcept
{
    if (mChunks.empty())
        return 0;

    return (mChunks.size() - 1) * BUFFER_SIZE + mLastChunkOffset;
}

bool Buffer::append(const void* data, size_t size)
{
    if (data == nullptr || size == 0)
        return false;

    const uint8_t* dataPtr = static_cast<const uint8_t*>(data);

    while (size > 0)
    {
        if (mChunks.empty() || mLastChunkOffset == BUFFER_SIZE)
        {
            mChunks.emplace_back();
            mLastChunkOffset = 0;
        }

        const size_t chunkSize = std::min(BUFFER_SIZE - mLastChunkOffset, size);

        void* ptr = mChunks.back().data() + mLastChunkOffset;

        std::memcpy(ptr, dataPtr, chunkSize);

        dataPtr += chunkSize;
        mLastChunkOffset += chunkSize;
        size -= chunkSize;
    }

    return true;
}

size_t
Buffer::read(void* data, size_t offset, size_t size) const noexcept
{
    if (data == nullptr || size == 0)
        return {};

    const size_t bufferSize = getSize();

    if (offset > bufferSize)
        return {};

    if (offset + size > bufferSize)
        size = bufferSize - offset;

    uint8_t* outPtr = static_cast<uint8_t*>(data);

    size_t bytesLeft = size;

    const size_t chunkIndex = offset / BUFFER_SIZE;
    offset = offset - BUFFER_SIZE * chunkIndex;

    auto chunk = mChunks.begin();
    std::advance(chunk, chunkIndex);

    while (bytesLeft > 0)
    {
        const size_t chunkSize = std::min(BUFFER_SIZE - offset, bytesLeft);
        const uint8_t* inPtr = chunk->data() + offset;

        std::memcpy(outPtr, inPtr, chunkSize);

        offset = 0;
        bytesLeft -= chunkSize;
        outPtr += chunkSize;
    }

    return size;
}

std::vector<uint8_t> Buffer::getLinearRepresentation() const
{
    size_t bytesLeft = getSize();

    std::vector<uint8_t> result;
    result.reserve(bytesLeft);

    for (const auto& chunk : mChunks)
    {
        const size_t chunkSize = std::min(BUFFER_SIZE, bytesLeft);

        result.insert(result.end(), chunk.begin(), chunk.begin() + chunkSize);

        bytesLeft -= chunkSize;
    }

    return result;
}
