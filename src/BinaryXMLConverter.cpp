/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#include "BinaryXMLConverter.h"

#include <utf8.h>
#include <vector>
#include <deque>
#include <algorithm>

#include "ProjectModel.h"

namespace
{

enum class FieldTypes : uint8_t
{
    FT_CharSize, // type, ID, value
    FT_StartTag, // type, ID
    FT_EndTag,   // type, ID
    FT_String,   // type, ID, string length, string
    FT_Int,      // type, ID, value
    FT_Bool,     // type, ID, value
    FT_Long,     // type, ID, value
    FT_LongLong, // type, ID, value
    FT_SizeT,    // type, ID, value
    FT_Float,    // type, ID, value, digits
    FT_Double,   // type, ID, value, digits
    FT_Data,     // type, string length, string
    FT_Raw,      // type, string length, string
    FT_Push,     // type only
    FT_Pop,      // type only
    FT_Name      // type, ID, name length, name
};

class Stream final
{
public:
    explicit Stream(const Buffer& buffer)
        : mBuffer(buffer)
        , mBufferSize(buffer.getSize())
    {
    }

    template<typename T> T read()
    {
        T result;

        if (sizeof(T) != mBuffer.read(result, mOffset))
            throw std::overflow_error(fmt::format("Unable to read {} bytes at offset {}", sizeof(T), mOffset));

        mOffset += sizeof(T);

        return result;
    }

    void setCharSize(size_t size)
    {
        mCharSize = size;
    }

    std::string readString(bool useInt = false)
    {
        if (mCharSize == 0)
            throw std::runtime_error("Char size is not set");

        const auto bytesCount =
            useInt ? read<uint32_t>() : uint32_t(read<uint16_t>());

        mTempData.reserve(bytesCount);

        if (bytesCount != mBuffer.read(mTempData.data(), mOffset, bytesCount))
        {
            throw std::overflow_error(fmt::format(
                "Unable to read {} bytes at offset {}", bytesCount, mOffset));
        }

        mOffset += bytesCount;

        if (mCharSize == 1)
        {
            return std::string(mTempData.data(), bytesCount);
        }
        else if (mCharSize == 2)
        {
            std::string result;

            const auto symbolsCount = bytesCount / 2;
            result.reserve(symbolsCount);

            const char16_t* data = reinterpret_cast<char16_t*>(mTempData.data());
            const char16_t* end = data + symbolsCount;

            utf8::utf16to8(data, end, std::back_inserter(result));

            return std::move(result);
        }
        else if (mCharSize == 4)
        {
            std::string result;

            const auto symbolsCount = bytesCount / 4;
            result.reserve(symbolsCount);

            const char32_t* data =
                reinterpret_cast<char32_t*>(mTempData.data());
            const char32_t* end = data + symbolsCount;

            utf8::utf32to8(data, end, std::back_inserter(result));

            return std::move(result);
        }

        throw std::runtime_error("Invalid char size");
    }

    void skip(size_t bytes)
    {
        const auto targetOffset = mOffset + bytes;

        if (targetOffset > mBufferSize)
        {
            throw std::overflow_error(fmt::format(
                "Unable to skip {} bytes at offset {}", bytes, mOffset));
        }

        mOffset = targetOffset;
    }

    void skipString(bool useInt = false)
    {
        const auto bytesCount =
            useInt ? read<uint32_t>() : uint32_t(read<uint16_t>());

        skip(bytesCount);
    }

    bool isEof() const noexcept
    {
        return mBufferSize == mOffset;
    }

private:
    const Buffer& mBuffer;

    std::vector<char> mTempData;

    size_t mOffset { 0 };
    size_t mBufferSize;

    size_t mCharSize { 0 };
};

class XMLConverter : public XMLHandler
{
public:
    XMLConverter()
        : mBuffer(std::make_unique<Buffer>())
    {
    }

    void HandleTagStart(
        std::string_view name, const AttributeList& attributes) override
    {
        if (mInTag)
            write(">\n");

        write(mSpaces);
        write("<");
        write(name);

        for (auto& attr : attributes)
        {
            write(" ");
            write(attr.Name);
            write("=\"");

            std::visit(
                [this](auto&& value) {
                    write(fmt::format("{}", value));
                },
                attr.Value);

            write("\"");
        }

        mLastTagName = name;
        mInTag = true;

        mSpaces.push_back(' ');
        mSpaces.push_back(' ');
    }

    void HandleTagEnd(std::string_view name) override
    {
        mSpaces.pop_back();
        mSpaces.pop_back();

        if (mLastTagName == name)
        {
            write(" />\n");
        }
        else
        {
            write(mSpaces);
            write("</");
            write(name);
            write(">\n");
        }

        mInTag = false;
    }

    void HandleCharData(std::string_view data) override
    {
        static int charXMLCompatiblity[] = {

            /* 0x00 */ 0, 0, 0, 0,
            /* 0x04 */ 0, 0, 0, 0,
            /* 0x08 */ 0, 1, 1, 0,
            /* 0x0C */ 0, 1, 0, 0,
            /* 0x10 */ 0, 0, 0, 0,
            /* 0x14 */ 0, 0, 0, 0,
            /* 0x18 */ 0, 0, 0, 0,
            /* 0x1C */ 0, 0, 0, 0,
        };

        for (uint8_t c : data)
        {
            switch (c)
            {
            case '\'':
                write("&apos;");
                break;

            case '"':
                write("&quot;");
                break;

            case '&':
                write("&amp;");
                break;

            case '<':
                write("&lt;");
                break;

            case '>':
                write("&gt;");
                break;
            default:
                if (static_cast<uint8_t>(c) > 0x1F || charXMLCompatiblity[c] != 0)
                    mBuffer->append(&c, 1);
            }
        }
    }

    std::unique_ptr<Buffer> Consume()
    {
        return std::move(mBuffer);
    }

    void write(std::string_view data)
    {
        mBuffer->append(data.data(), data.size());
    }

private:
    std::unique_ptr<Buffer> mBuffer;
    std::string_view mLastTagName;

    std::string mSpaces;

    bool mInTag { false };
};

class IdsLookup final
{
public:
    void store(uint16_t index, std::string value)
    {
        const auto size = mIds.size();

        if (index == size)
            mIds.push_back(std::move(value));
        else
        {
            if ((index + 1) < size)
                mIds.resize(index);

            mIds[index] = std::move(value);
        }
    }

    std::string_view get(uint16_t index)
    {
        return mIds.at(index);
    }
private:
    std::vector<std::string> mIds;
};

class XMLHandlerHelper final
{
public:
    explicit XMLHandlerHelper(XMLHandler& handler) noexcept
        : mHandler(handler)
    {
    }

    ~XMLHandlerHelper()
    {
        if (mInTag)
        {
            emitEndTag(mCurrentTagName);
        }
    }

    void emitStartTag(const std::string_view& name)
    {
        if (mInTag)
            emitStartTag();

        mCurrentTagName = name;
        mInTag = true;
    }

    void emitEndTag(const std::string_view& name)
    {
        if (mInTag)
            emitStartTag();

        mHandler.HandleTagEnd(name);
    }

    void addAttr(const std::string_view& name, std::string value)
    {
        if (!mInTag)
        {
            throw std::runtime_error(fmt::format(
                "Attempt to write attribute {} outside of the tag context.",
                name));
        }

        mAttributes.emplace_back(name, CacheString(std::move(value)));
    }

    template <typename T> void addAttr(const std::string_view& name, T value)
    {
        if (!mInTag)
        {
            throw std::runtime_error(fmt::format(
                "Attempt to write attribute {} outside of the tag context.",
                name));
        }

        mAttributes.emplace_back(name, value);
    }

    void writeData(std::string value)
    {
        if (mInTag)
            emitStartTag();

        mHandler.HandleCharData(CacheString(std::move(value)));
    }

private:
    void emitStartTag()
    {
        mHandler.HandleTagStart(mCurrentTagName, mAttributes);

        mStringsCache.clear();
        mAttributes.clear();
        mInTag = false;
    }

    std::string_view CacheString(std::string string)
    {
        mStringsCache.emplace_back(std::move(string));
        return mStringsCache.back();
    }
    XMLHandler& mHandler;

    std::string_view mCurrentTagName;

    std::deque<std::string> mStringsCache;
    AttributeList mAttributes;

    bool mInTag { false };
};
}

void BinaryXMLConverter::Parse(const Buffer& buffer, XMLHandler& handler)
{
    Stream stream(buffer);
    IdsLookup lookup;
    XMLHandlerHelper helper(handler);

    FieldTypes prevOp;
    FieldTypes opCode {};

    while (!stream.isEof())
    {
        prevOp = opCode;
        opCode = stream.read<FieldTypes>();

        uint16_t id = 0;

        switch (opCode)
        {
        case FieldTypes::FT_CharSize:
            stream.setCharSize(stream.read<uint8_t>());
            break;
        case FieldTypes::FT_StartTag:
            helper.emitStartTag(lookup.get(stream.read<uint16_t>()));
            break;
        case FieldTypes::FT_EndTag:
            helper.emitEndTag(lookup.get(stream.read<uint16_t>()));
            break;
        case FieldTypes::FT_String:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.readString(true));
            break;
        case FieldTypes::FT_Int:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.read<int32_t>());
            break;
        case FieldTypes::FT_Bool:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.read<bool>());
            break;
        case FieldTypes::FT_Long:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.read<int32_t>());
            break;
        case FieldTypes::FT_LongLong:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.read<int64_t>());
            break;
        case FieldTypes::FT_SizeT:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.read<uint32_t>());
            break;
        case FieldTypes::FT_Float:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.read<float>());
            stream.skip(sizeof(uint32_t));
            break;
        case FieldTypes::FT_Double:
            id = stream.read<uint16_t>();
            helper.addAttr(lookup.get(id), stream.read<double>());
            stream.skip(sizeof(uint32_t));
            break;
        case FieldTypes::FT_Data:
            helper.writeData(stream.readString(true));
            break;
        case FieldTypes::FT_Name:
            id = stream.read<uint16_t>();
            lookup.store(id, stream.readString());
            break;
        case FieldTypes::FT_Raw:
            stream.skipString(true);
            break;
        default:
            throw std::runtime_error("Unsupported opcode");
        }
    }
}

std::unique_ptr<Buffer> BinaryXMLConverter::ConvertToXML(const Buffer& buffer)
{
    XMLConverter converter;

    Parse(buffer, converter);

    return converter.Consume();
}

namespace
{
template<typename StringLookup>
void WriteNode(
    const StringLookup& indexLookup, Buffer& buffer,
    const ProjectTreeNode& node)
{
    const uint16_t tagIndex = indexLookup(node.TagName);

    buffer.append(FieldTypes::FT_StartTag);
    buffer.append(tagIndex);

    for (const auto& attr : node.Attributes)
    {
        std::visit(
            [attrNameIndex = indexLookup(attr.Name), &buffer](auto&& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, bool>)
                {
                    buffer.append(FieldTypes::FT_Bool);
                    buffer.append(attrNameIndex);
                    buffer.append(value);
                }
                else if constexpr (std::is_same_v<T, int32_t>)
                {
                    buffer.append(FieldTypes::FT_Int);
                    buffer.append(attrNameIndex);
                    buffer.append(value);
                }
                else if constexpr (std::is_same_v<T, uint32_t>)
                {
                    buffer.append(FieldTypes::FT_SizeT);
                    buffer.append(attrNameIndex);
                    buffer.append(value);
                }
                else if constexpr (std::is_same_v<T, int64_t>)
                {
                    buffer.append(FieldTypes::FT_LongLong);
                    buffer.append(attrNameIndex);
                    buffer.append(value);
                }
                else if constexpr (std::is_same_v<T, size_t>)
                {
                    buffer.append(FieldTypes::FT_SizeT);
                    buffer.append(attrNameIndex);
                    buffer.append(uint32_t(value));
                }
                else if constexpr (std::is_same_v<T, float>)
                {
                    buffer.append(FieldTypes::FT_Float);
                    buffer.append(attrNameIndex);
                    buffer.append(value);
                    buffer.append(int32_t(7));
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    buffer.append(FieldTypes::FT_Double);
                    buffer.append(attrNameIndex);
                    buffer.append(value);
                    buffer.append(int32_t(19));
                }
                else if constexpr (std::is_same_v<T, std::string_view>)
                {
                    buffer.append(FieldTypes::FT_String);
                    buffer.append(attrNameIndex);
                    buffer.append(uint32_t(value.length()));
                    buffer.append(value.data(), value.length());
                }
            },
            attr.Value);
    }

    if (!node.Data.empty())
    {
        buffer.append(FieldTypes::FT_Data);
        buffer.append(uint32_t(node.Data.length()));
        buffer.append(node.Data.data(), node.Data.length());
    }

    for (const auto& child : node.Children)
        WriteNode(indexLookup, buffer, *child);

    buffer.append(FieldTypes::FT_EndTag);
    buffer.append(tagIndex);
}
}

std::pair<std::unique_ptr<Buffer>, std::unique_ptr<Buffer>>
BinaryXMLConverter::SerializeProject(
    const std::deque<std::string>& names, const ProjectTreeNode& project)
{
    std::pair<std::unique_ptr<Buffer>, std::unique_ptr<Buffer>> result = {
        std::make_unique<Buffer>(), std::make_unique<Buffer>()
    };

    // We write strings solely in UTF-8
    result.first->append(FieldTypes::FT_CharSize);
    result.first->append(uint8_t(1));

    uint16_t stringIndex = 0;

    for (const auto& name : names)
    {
        result.first->append(FieldTypes::FT_Name);
        result.first->append(uint16_t(stringIndex++));
        result.first->append(uint16_t(name.length()));
        result.first->append(name.data(), name.length());
    }

    auto getStringIndex = [&names](std::string_view name)
    {
        auto it = std::find(names.begin(), names.end(), name);

        if (it == names.end())
            throw std::logic_error(fmt::format("Name {} not found in the lookup", name));

        return uint16_t(std::distance(names.begin(), it));
    };

    WriteNode(getStringIndex, *result.second, project);

    return result;
}
