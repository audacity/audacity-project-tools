
/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#include "WaveFile.h"

#include <algorithm>

#include <fmt/format.h>
#include <cstdio>

namespace
{
struct Header
{
    char Riff[4] = { 'R', 'I', 'F', 'F' };
    uint32_t ChunkSize { 36 };

    char Format[4] = { 'W', 'A', 'V', 'E' };

    char Subchunk1ID[4] = { 'f', 'm', 't', ' ' };
    uint32_t Subchunk1Size { 16 };
    uint16_t AudioFormat { 1 };
    uint16_t NumChannels { 1 };
    uint32_t SampleRate {};
    uint32_t ByteRate {};
    uint16_t BlockAlign {};
    uint16_t BitsPerSample {};

    char Subchunk2ID[4] = { 'd', 'a', 't', 'a' };
    uint32_t Subchunk2Size { };
};

static_assert(sizeof(Header) == 44);
} // namespace

WaveFile::WaveFile(
    const std::filesystem::path& path, SampleFormat fmt, uint32_t sampleRate, uint16_t numChannels)
    : mPath(path)
    , mFmt(fmt)
    , mSampleRate(sampleRate)
    , mNumChannels(numChannels)
{
    mChannels.resize(numChannels);
}

WaveFile::~WaveFile()
{
}

void WaveFile::writeBlock(const void* data, size_t blockSize, uint16_t channel)
{
    mChannels.at(channel).append(data, blockSize);
}

namespace
{
FILE* OpenFile(const std::filesystem::path& path)
{
    #ifdef _WIN32
        return _wfopen(path.native().c_str(), L"wb");
    #else
        return fopen(path.native().c_str(), "wb");
    #endif
}

void CloseFile(std::FILE* fp)
{
    std::fclose(fp);
}
}


void WaveFile::writeFile()
{
    std::unique_ptr<FILE, decltype(&CloseFile)> file(OpenFile(mPath), CloseFile);

    if (file == nullptr)
        throw std::runtime_error(
            fmt::format("Failed to open {} for writing", mPath.u8string()));

    const uint16_t bytesPerSample = BytesPerSample(mFmt);

    Header header;

    header.AudioFormat = mFmt == SampleFormat::Float32 ? 3 : 1;
    header.NumChannels = mNumChannels;
    header.SampleRate = mSampleRate;
    header.ByteRate = mSampleRate * mNumChannels * bytesPerSample;
    header.BlockAlign = mNumChannels * bytesPerSample;
    header.BitsPerSample = bytesPerSample * 8;

    auto it = std::max_element(
        mChannels.begin(), mChannels.end(),
        [](const auto& lhs, const auto& rhs)
        { return lhs.getSize() < rhs.getSize(); });

    const size_t dataSize = mNumChannels * it->getSize();

    header.ChunkSize += dataSize;
    header.Subchunk2Size = dataSize;

    if (sizeof(Header) != fwrite(&header, 1, sizeof(Header), file.get()))
        throw std::runtime_error("Failed to write WAV header");

    const auto maxSamples = it->getSize() / bytesPerSample;

    std::vector<uint8_t> sampleData(mNumChannels * bytesPerSample);

    for (size_t sampleIndex = 0; sampleIndex < maxSamples; ++sampleIndex)
    {
        const auto offset = sampleIndex * bytesPerSample;

        for (size_t channelIndex = 0; channelIndex < mNumChannels; ++channelIndex)
        {
            const auto& buffer = mChannels[channelIndex];

            // By definition, the buffer will have at least one
            // sample in this case
            const auto bufferSize = buffer.getSize();

            if (bufferSize >= offset)
                buffer.read(&sampleData[channelIndex * bytesPerSample], offset, bytesPerSample);
            else
                std::memset(&sampleData[channelIndex * bytesPerSample], 0, bytesPerSample);
        }

        if (sampleData.size() != fwrite(sampleData.data(), 1, sampleData.size(), file.get()))
            throw std::runtime_error("Failed to write sample to WAV file");

    }
}

