/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <variant>
#include <cstdint>
#include <vector>
#include <string_view>
#include <string>
#include <exception>
#include <fmt/format.h>
#include <charconv>

// Binary XML encodings that carry more than a plain value, kept to write them back unchanged
struct LongValue final { int32_t Value; };
struct FloatValue final { float Value; int32_t Digits; };
struct DoubleValue final { double Value; int32_t Digits; };

using AttributeValue = std::variant<bool, int32_t, uint32_t, int64_t, size_t, float, double, std::string_view, std::vector<uint8_t>, LongValue, FloatValue, DoubleValue>;

template<typename T>
constexpr bool IsWrappedValue = std::is_same_v<T, LongValue> ||
                                std::is_same_v<T, FloatValue> ||
                                std::is_same_v<T, DoubleValue>;

struct Attribute final
{
    Attribute() = default;
    Attribute(std::string_view name, AttributeValue value)
        : Name(std::move(name))
        , Value(std::move(value))
    {
    }

    std::string_view Name;
    AttributeValue Value;
};

using AttributeList = std::vector<Attribute>;

template<typename Ret>
void GetAttributeValue(const AttributeValue& attr, Ret& result)
{
    std::visit(
        [&result](auto&& arg) mutable
        {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (IsWrappedValue<T>)
            {
                GetAttributeValue(AttributeValue { arg.Value }, result);
            }
            else if constexpr (std::is_same_v<Ret, std::string>)
            {
                if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
                    result = fmt::format("(blob {} bytes)", arg.size());
                else
                    result = fmt::format("{}", arg);
            }
            else if constexpr (std::is_same_v<Ret, T>)
            {
                result = arg;
            }
            else if constexpr(std::is_convertible_v<Ret, T>)
            {
                result = static_cast<Ret>(arg);
            }
            else if constexpr (std::is_same_v<T, std::string_view>)
            {
                if constexpr (std::is_same_v<Ret, bool>)
                {
                    result = arg == "true" || arg == "1";
                }
                else if constexpr (std::is_floating_point_v<Ret>)
                {
                    std::string tmp(arg);
                    if constexpr (std::is_same_v<Ret, float>)
                        result = std::stof(tmp);
                    else
                        result = static_cast<Ret>(std::stod(tmp));
                }
                else
                {
                    auto convResult = std::from_chars(
                        arg.data(), arg.data() + arg.size(), result);

                    if (convResult.ec != std::errc {})
                        throw std::runtime_error("Incompatible attribute type");
                }
            }
            else
            {
                throw std::runtime_error("Incompatible attribute type");
            }
        },
        attr);
}

template <typename Ret>
Ret GetAttributeValue(const AttributeValue& attr)
{
    Ret result;
    GetAttributeValue(attr, result);
    return result;
}

class XMLHandler
{
public:
    virtual ~XMLHandler() = default;

    virtual void HandleTagStart(std::string_view name, const AttributeList& attributes) = 0;
    virtual void HandleTagEnd(std::string_view name) = 0;
    virtual void HandleCharData(std::string_view data) = 0;
    virtual void HandleRaw(std::string_view) {}
};
