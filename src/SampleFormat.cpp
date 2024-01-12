/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#include "SampleFormat.h"

#include <exception>
#include <fmt/format.h>

SampleFormat SampleFormatFromString(std::string_view format)
{
    if (format == "int16")
        return SampleFormat::Int16;
    else if (format == "int24")
        return SampleFormat::Int24;
    else if (format == "float")
        return SampleFormat::Float32;
    else
        throw std::runtime_error(fmt::format("Unsupported format {}", format));
}

uint32_t BytesPerSample(SampleFormat format)
{
    switch (format)
    {
    case SampleFormat::Int16:
        return 2;
    case SampleFormat::Int24:
        return 3;
    case SampleFormat::Float32:
        return 4;
    default:
        throw std::runtime_error(fmt::format("Unsupported format {}", format));
    }
}

uint32_t DiskBytesPerSample(SampleFormat format)
{
    switch (format)
    {
    case SampleFormat::Int16:
        return 2;
    case SampleFormat::Int24:
        return 4;
    case SampleFormat::Float32:
        return 4;
    default:
        throw std::runtime_error(fmt::format("Unsupported format {}", format));
    }
}
