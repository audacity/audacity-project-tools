/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#include "ProjectModel.h"

#include <algorithm>
#include <fmt/format.h>
#include <cmath>

#include "ProjectBlobReader.h"
#include "BinaryXMLConverter.h"

#include "WaveFile.h"

DeserializedNode::DeserializedNode(ProjectTreeNode* node)
    : mXMLNode(node)
{
}

WaveBlock::WaveBlock(ProjectTreeNode* node, Sequence* parent)
    : DeserializedNode(node)
    , mParent(parent)
    , mParentIndex(parent->mBlocks.size())
{
    for (const auto& attr : node->Attributes)
    {
        if (attr.Name == "start")
            GetAttributeValue(attr.Value, mStart);
        else if (attr.Name == "blockid")
            GetAttributeValue(attr.Value, mBlockId);
    }

    parent->mBlocks.push_back(this);
}

bool WaveBlock::isSilence() const noexcept
{
    return mBlockId < 0;
}

void WaveBlock::convertToSilence() noexcept
{
    mBlockId = -getLength();

    mXMLNode->setAttribute("blockid", mBlockId);
    mXMLNode->setAttribute("badblock", true);
}

int64_t WaveBlock::getBlockId() const noexcept
{
    return mBlockId;
}

int64_t WaveBlock::getStart() const noexcept
{
    return mStart;
}

int64_t WaveBlock::getLength() const noexcept
{
    const size_t blocksInSequence = mParent->mBlocks.size();
    const size_t nextBlock = mParentIndex + 1;

    if (nextBlock < blocksInSequence)
        return mParent->mBlocks[nextBlock]->mStart - mStart;
    else
        return mParent->mNumSamples - mStart;
}

Sequence* WaveBlock::getParent() const noexcept
{
    return mParent;
}

Sequence::Sequence(ProjectTreeNode* node, Clip* parent)
    : DeserializedNode(node)
    , mParent(parent)
    , mParentIndex(parent->mSequences.size())
{
    for (const auto& attr : node->Attributes)
    {
        if (attr.Name == "maxsamples")
            GetAttributeValue(attr.Value, mMaxSamples);
        else if (attr.Name == "numsamples")
            GetAttributeValue(attr.Value, mNumSamples);
        else if (attr.Name == "sampleformat")
            GetAttributeValue(attr.Value, mFormat);
    }

    parent->mSequences.push_back(this);
}

int32_t Sequence::getFormat() const noexcept
{
    return mFormat;
}

int32_t Sequence::getMaxSamples() const noexcept
{
    return mMaxSamples;
}

int32_t Sequence::getNumSamples() const noexcept
{
    return mNumSamples;
}

Sequence::Blocks::const_iterator Sequence::begin() const
{
    return mBlocks.begin();
}

Sequence::Blocks::const_iterator Sequence::end() const
{
    return mBlocks.end();
}

Clip::Clip(ProjectTreeNode* node, WaveTrack* parent)
    : DeserializedNode(node)
    , mParent(parent)
    , mParentIndex(parent->mClips.size())
{
    for (const auto& attr : node->Attributes)
    {
        if (attr.Name == "offset")
            GetAttributeValue(attr.Value, mOffset);
        else if (attr.Name == "trimLeft")
            GetAttributeValue(attr.Value, mTrimLeft);
        else if (attr.Name == "trimRight")
            GetAttributeValue(attr.Value, mTrimRight);
        else if (attr.Name == "name")
            GetAttributeValue(attr.Value, mName);
    }

    parent->mClips.push_back(this);
}

std::string_view Clip::getName() const
{
    return mName;
}

size_t Clip::getParentIndex() const
{
    return mParentIndex;
}

WaveTrack* Clip::getParent() const
{
    return mParent;
}

double Clip::getOffset() const
{
    return mOffset;
}

double Clip::getTrimLeft() const
{
    return mTrimLeft;
}

double Clip::getTrimRight() const
{
    return mTrimRight;
}

Clip::Sequences::const_iterator Clip::begin() const
{
    return mSequences.begin();
}

Clip::Sequences::const_iterator Clip::end() const
{
    return mSequences.end();
}

WaveTrack::WaveTrack(ProjectTreeNode* node, size_t index)
    : DeserializedNode(node)
    , mParentIndex(index)
{
    for (const auto& attr: node->Attributes)
    {
        if (attr.Name == "channel")
            GetAttributeValue(attr.Value, mChannel);
        else if (attr.Name == "linked")
            GetAttributeValue(attr.Value, mLinked);
        else if (attr.Name == "name")
            GetAttributeValue(attr.Value, mName);
        else if (attr.Name == "sampleformat")
            GetAttributeValue(attr.Value, mSampleFormat);
        else if (attr.Name == "rate")
            GetAttributeValue(attr.Value, mRate);
    }
}

std::string_view WaveTrack::getTrackName() const
{
    return mName;
}

int WaveTrack::getChannel() const
{
    return mChannel;
}

bool WaveTrack::isLinked() const
{
    return mLinked;
}

size_t WaveTrack::getParentIndex() const
{
    return mParentIndex;
}

int WaveTrack::getSampleFormat() const
{
    return mSampleFormat;
}

int WaveTrack::getSampleRate() const
{
    return mRate;
}

const std::vector<Clip*>& WaveTrack::getClips() const
{
    return mClips;
}

AudacityProject::AudacityProject(AudacityDatabase& db)
    : mDb(db)
{
    mParserState = std::make_unique<ParserState>();

    mFromAutosave = mDb.hasAutosave();

    auto blob = mFromAutosave ? ReadProjectBlob(db.DB(), "autosave") :
                                ReadProjectBlob(db.DB(), "project");

    BinaryXMLConverter::Parse(*blob, *this);

    mParserState = {};
}

AudacityProject::~AudacityProject()
{
}

std::set<int64_t> AudacityProject::validateBlocks() const
{
    std::set<int64_t> missingBlocks;

    for (const auto& block : mWaveBlocks)
    {
        if (block.isSilence())
            continue;

        if (missingBlocks.count(block.getBlockId()))
            continue;

        try
        {
            // Creating query every time is slower,
            // but can withstand error
            SQLite::Statement query(
                mDb.DB(),
                "SELECT sampleformat, summin, summax, sumrms, summary256, summary64k, samples FROM sampleblocks WHERE blockid = ?1;");

            query.bind(1, block.getBlockId());

            int rowsCount = 0;

            while (query.executeStep())
            {
                const int format = query.getColumn(0).getInt();

                if (block.getParent()->getFormat() != format)
                    throw std::runtime_error(fmt::format("Format mismatch for block {}\n", block.getBlockId()));

                ++rowsCount;
            }

            if (rowsCount == 0)
                throw std::runtime_error("Block not found");
        }
        catch (const std::exception& ex)
        {
            missingBlocks.emplace(block.getBlockId());
            fmt::print("Invalid block {}: {}\n", block.getBlockId(), ex.what());
        }

    }

    return missingBlocks;
}

std::set<int64_t> AudacityProject::fixupMissingBlocks()
{
    auto missingBlocks = validateBlocks();

    for (auto& block : mWaveBlocks)
    {
        if (missingBlocks.count(block.getBlockId()) == 0)
            continue;

        block.convertToSilence();
    }

    if (!missingBlocks.empty())
    {
        mReusableStringsCache.emplace_back("badblock");
        saveProject();
    }

    return missingBlocks;
}

void AudacityProject::saveProject()
{
    mDb.reopenReadonlyAsWritable();

    auto result = BinaryXMLConverter::SerializeProject(mReusableStringsCache, *mProjectNode);

    SQLite::Statement query(
        mDb.DB(),
        fmt::format(
            R"(INSERT OR REPLACE INTO {}(id, dict, doc) VALUES (1, ?1, ?2);)",
            mFromAutosave ? "autosave" : "project"));

    const auto dict = result.first->getLinearRepresentation();
    const auto doc = result.second->getLinearRepresentation();

    query.bind(1, dict.data(), dict.size());
    query.bind(2, doc.data(), doc.size());

    query.exec();
}

void AudacityProject::removeUnusedBlocks()
{
    // Read all the available blocks from the DB first

    SQLite::Statement readBlocksList(
        mDb.DB(), R"(SELECT blockid FROM sampleblocks)");

    std::set<int64_t> availableBlocks;

    while (readBlocksList.executeStep())
        availableBlocks.emplace(readBlocksList.getColumn(0).getInt64());

    readBlocksList.reset();

    std::set<int64_t> orphanedBlocks;

    for (const auto block : mWaveBlocks)
    {
        if (block.isSilence())
            continue;

        if (availableBlocks.count(block.getBlockId()) == 0)
            orphanedBlocks.emplace(block.getBlockId());
    }

    mDb.reopenReadonlyAsWritable();

    if (!orphanedBlocks.empty())
    {
        mDb.DB().exec("BEGIN;");

        for (auto blockId : orphanedBlocks)
        {
            mDb.DB().exec(fmt::format(
                "DELETE FROM sampleblocks WHERE blockid = {};", blockId));
        }

        mDb.DB().exec("COMMIT;");

        fmt::print("Removed {} orphaned blocks\n", orphanedBlocks.size());
    }

    mDb.DB().exec("VACUUM;");
}

void AudacityProject::extractClips() const
{
    const auto directory = mDb.getDataPath() / "clips";

    if (!std::filesystem::exists(directory))
        std::filesystem::create_directories(directory);

    for (const auto& clip : mClips)
    {
        const auto& track = *clip.getParent();

        const auto clipPath =
            directory /
            std::filesystem::u8path(fmt::format(
                "{}_{}_{}_{}.wav", track.getParentIndex(), track.getTrackName(),
                clip.getParentIndex(), clip.getName()));

        const auto format = SampleFormat(track.getSampleFormat());

        WaveFile waveFile(
            clipPath, format, track.getSampleRate(),
            1);

        const uint16_t bytesPerSample = BytesPerSample(format);

        std::vector<uint8_t> silence;

        for (auto sequence : clip)
        {
            silence.resize(sequence->getMaxSamples() * bytesPerSample);
            std::uninitialized_fill(silence.begin(), silence.end(), 0);

            const auto firstSample =
                llrint(clip.getTrimLeft() * track.getSampleRate());

            const auto lastSample =
                sequence->getNumSamples() - llrint(clip.getTrimRight() * track.getSampleRate());

            for (auto block : *sequence)
            {
                auto blockStart = block->getStart();
                auto blockLength = block->getLength();
                auto blockEnd = blockStart + blockLength;

                if (blockEnd <= firstSample || blockStart >= lastSample)
                    continue;

                blockStart = std::max<decltype(blockStart)>(blockStart, firstSample);
                blockEnd = std::min<decltype(blockEnd)>(blockEnd, lastSample);
                blockLength = blockEnd - blockStart;

                if (blockLength <= 0)
                    continue;

                const auto blockId = block->getBlockId();

                if (blockId < 0)
                {
                    waveFile.writeBlock(
                        silence.data(), blockLength * bytesPerSample, 0);
                }
                else
                {
                    SQLite::Statement stmt(mDb.DB(), R"(SELECT samples FROM sampleblocks WHERE blockid = ?1;)");

                    stmt.bind(1, blockId);

                    while (stmt.executeStep())
                    {
                        const void* blobData = stmt.getColumn(0).getBlob();
                        const int64_t blobSize = stmt.getColumn(0).getBytes();

                        if (blobSize < blockLength * bytesPerSample)
                            throw std::runtime_error(
                                fmt::format("Unexpected blob size for sample block {}",
                                blockId));

                        auto data = static_cast<const uint8_t*>(blobData) + (blockStart - block->getStart()) * bytesPerSample;

                        waveFile.writeBlock(
                            data, blockLength * bytesPerSample, 0);
                    }
                }
            }
        }

        waveFile.writeFile();
    }
}

namespace
{
std::string FormatTime(double seconds)
{
    if (seconds < 0)
        return fmt::format("{}", seconds);

    if (seconds > 60 * 60)
        return fmt::format(
            "{:02}:{:02}:{:02}.{:03}", (int)seconds / 3600, (int)seconds / 60 % 60,
            (int)seconds % 60, (int)(seconds * 1000) % 1000);
    else if (seconds > 60)
        return fmt::format(
            "{:02}:{:02}.{:03}", (int)seconds / 60, (int)seconds % 60,
            (int)(seconds * 1000) % 1000);
    else
        return fmt::format(
            "{:02}.{:03}", (int)seconds, (int)(seconds * 1000) % 1000);
}

struct BlockStatistics final
{
    size_t totalUsageCount {};
    size_t audibleUsageCount {};
};
}

void AudacityProject::printProjectStatistics() const
{
    std::unordered_map<int64_t, BlockStatistics> blocksStatistics;

    for (const auto& track : mWaveTracks)
    {
        fmt::print(
            "Track {}: {}\n", track.getParentIndex(), track.getTrackName());

        for (auto clip : track.getClips())
        {
            const int64_t firstSample =
                int64_t((clip->getTrimLeft()) * track.getSampleRate());

            const int64_t lastSampleOffset =
                int64_t(clip->getTrimRight() * track.getSampleRate());

            size_t numSamples = 0;

            for (auto sequence : *clip)
            {
                numSamples += sequence->getNumSamples();

                const int64_t lastSample = sequence->getNumSamples() - lastSampleOffset;

                for (const auto& block : *sequence)
                {
                    auto& blockStats = blocksStatistics[block->getBlockId()];

                    ++blockStats.totalUsageCount;

                    if ((block->getStart() + block->getLength()) >= firstSample &&
                        block->getStart() < lastSample)
                        ++blockStats.audibleUsageCount;
                }
            }

            const double totalClipTime =
                double(numSamples) / track.getSampleRate();

            const double trimmedClipTime =
                totalClipTime - clip->getTrimLeft() - clip->getTrimRight();
            
            fmt::print(
                "\tClip {}: '{}'.\n\t\tTotal samples {}\n\t\tTotal time: {}\n\t\tTrimmed time: {}\n\t\tTrimmed / Total: {:.4f}%\n",
                clip->getParentIndex(), clip->getName(), numSamples,
                FormatTime(totalClipTime), FormatTime(trimmedClipTime),
                trimmedClipTime / totalClipTime * 100.0);
        }
    }

    const auto silentBlocksCount = std::count_if(
        blocksStatistics.begin(), blocksStatistics.end(),
        [](const auto& p) { return p.second.audibleUsageCount == 0; });

    const auto unsharedBlocksCount = std::count_if(
        blocksStatistics.begin(), blocksStatistics.end(),
        [](const auto& p) { return p.second.totalUsageCount == 1; });

    const auto unsharedSilentBlocks = std::count_if(
        blocksStatistics.begin(), blocksStatistics.end(),
        [](const auto& p) {
            return p.second.audibleUsageCount == 0 &&
                   p.second.totalUsageCount == 1;
        });

    fmt::print(
        "Total blocks in project: {}\n\tSilent blocks count: {} ({:02.5}%)\nNot shared blocks count: {} ({:02.5}%)\n\tSilent blocks count: {} ({:02.5}%)\n",
        blocksStatistics.size(), silentBlocksCount,
        double(silentBlocksCount) / blocksStatistics.size() * 100.0,
        unsharedBlocksCount,
        double(unsharedBlocksCount) / blocksStatistics.size() * 100.0,
        unsharedSilentBlocks,
        double(unsharedSilentBlocks) / unsharedBlocksCount * 100.0);
}

std::string_view AudacityProject::CacheString(std::string_view view, bool reuse)
{
    if (reuse)
    {
        auto it = std::find(mReusableStringsCache.begin(), mReusableStringsCache.end(), view);

        if (it != mReusableStringsCache.end())
            return *it;

        mReusableStringsCache.emplace_back(view);
        return mReusableStringsCache.back();
    }
    else
    {
        mStringCache.emplace_back(view);
        return mStringCache.back();
    }
}

using DeserializedNodeStackElement = std::variant<std::monostate, WaveBlock*, Sequence*, Clip*, WaveTrack*>;

struct AudacityProject::ParserState
{
    std::vector<ProjectTreeNode*> NodesStack;
    std::vector<DeserializedNodeStackElement> DeserializedNodeStack;
};

void AudacityProject::HandleTagStart(std::string_view name, const AttributeList& attributes)
{
    if (mParserState->NodesStack.empty())
    {
        mProjectNode = std::make_unique<ProjectTreeNode>();
        mParserState->NodesStack.push_back(mProjectNode.get());

        mProjectNode->ParentIndex = 0;
    }
    else
    {
        mParserState->NodesStack.back()->Children.push_back(
            std::make_unique<ProjectTreeNode>());

        auto node = mParserState->NodesStack.back()->Children.back().get();
        node->ParentIndex =
            mParserState->NodesStack.back()->Children.size() - 1;

        mParserState->NodesStack.push_back(node);
    }

    auto node = mParserState->NodesStack.back();

    node->TagName = CacheString(name, true);

    for (auto attr : attributes)
    {
        if (std::holds_alternative<std::string_view>(attr.Value))
            attr.Value = CacheString(std::get<std::string_view>(attr.Value), false);

        node->Attributes.emplace_back(CacheString(attr.Name, true), attr.Value);
    }

    if (name == "waveblock")
    {
        mWaveBlocks.emplace_back(
            node, std::get<Sequence*>(
                      mParserState->DeserializedNodeStack.back()));

        mParserState->DeserializedNodeStack.push_back(
            &mWaveBlocks.back());
    }
    else if (name == "sequence")
    {
        mSequences.emplace_back(
            node, std::get<Clip*>(
                      mParserState->DeserializedNodeStack.back()));

        mParserState->DeserializedNodeStack.push_back(
            &mSequences.back());
    }
    else if (name == "waveclip")
    {
        mClips.emplace_back(
            node, std::get<WaveTrack*>(
                      mParserState->DeserializedNodeStack.back()));

        mParserState->DeserializedNodeStack.push_back(&mClips.back());
    }
    else if (name == "wavetrack")
    {
        mWaveTracks.emplace_back(node, mWaveTracks.size());

        mParserState->DeserializedNodeStack.push_back(
            &mWaveTracks.back());
    }
    else
    {
        mParserState->DeserializedNodeStack.emplace_back();
    }
}

void AudacityProject::HandleTagEnd(std::string_view name)
{
    mParserState->NodesStack.pop_back();
    mParserState->DeserializedNodeStack.pop_back();
}

void AudacityProject::HandleCharData(std::string_view data)
{
    mParserState->NodesStack.back()->Data = std::string(data);
}

void ProjectTreeNode::setAttribute(std::string_view name, AttributeValue value)
{
    for (auto& attr : Attributes)
    {
        if (attr.Name == name)
        {
            attr.Value = value;
            return;
        }
    }

    Attributes.emplace_back(name, value);
}
