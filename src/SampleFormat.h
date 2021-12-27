/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <string_view>
#include <cstdint>

enum class SampleFormat
{
    Int16 = 0x00020001,
    Int24 = 0x00040001,
    Float32 = 0x0004000F
};

SampleFormat SampleFormatFromString(std::string_view format);

uint32_t BytesPerSample(SampleFormat format);
