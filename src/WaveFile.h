
/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <filesystem>
#include <cstdint>

#include "Buffer.h"

#include "SampleFormat.h"

class WaveFile final
{
public:
    WaveFile(
        const std::filesystem::path& path, SampleFormat fmt, uint32_t sampleRate,
        uint16_t numChannels);
    ~WaveFile();

    void writeBlock(const void* data, size_t blockSize, uint16_t channel);

    void writeFile();
private:
    const std::filesystem::path& mPath;

    SampleFormat mFmt;
    uint32_t mSampleRate;
    uint16_t mNumChannels;

    std::vector<Buffer> mChannels;
};
