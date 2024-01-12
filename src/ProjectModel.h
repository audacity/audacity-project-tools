/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <set>

#include "AudacityDatabase.h"
#include "XMLHandler.h"

struct ProjectTreeNode final
{
    std::string_view TagName;
    AttributeList Attributes;

    std::deque<std::unique_ptr<ProjectTreeNode>> Children;

    size_t ParentIndex { size_t(-1) };

    std::string Data;

    void setAttribute(std::string_view name, AttributeValue value);
};

class WaveBlock;
class Sequence;
class Clip;
class WaveTrack;

class DeserializedNode
{
public:
    virtual ~DeserializedNode() = default;

protected:
    explicit DeserializedNode(ProjectTreeNode* node);

    ProjectTreeNode* mXMLNode;
};

class WaveBlock final : public DeserializedNode
{
public:
    WaveBlock(ProjectTreeNode* node, Sequence* parent);

    bool isSilence() const noexcept;
    void convertToSilence() noexcept;

    void setBlockId(int64_t blockId) noexcept;
    void setStart(int64_t start) noexcept;

    int64_t getBlockId() const noexcept;
    int64_t getStart() const noexcept;
    int64_t getLength() const noexcept;

    Sequence* getParent() const noexcept;

private:
    Sequence* mParent;
    size_t mParentIndex;

    int64_t mStart;
    int64_t mBlockId;
};

class Sequence final : public DeserializedNode
{
public:
    using Blocks = std::vector<WaveBlock*>;

    Sequence(ProjectTreeNode* node, Clip* parent);

    int32_t getFormat() const noexcept;

    int32_t getMaxSamples() const noexcept;
    int32_t getNumSamples() const noexcept;

    Blocks::const_iterator begin() const;
    Blocks::const_iterator end() const;

private:
    Clip* mParent;
    size_t mParentIndex;

    int64_t mMaxSamples;
    int64_t mNumSamples;
    int32_t mFormat;

    Blocks mBlocks;

    friend class WaveBlock;
};

class Clip final : public DeserializedNode
{
public:
    using Sequences = std::vector<Sequence*>;

    Clip(ProjectTreeNode* node, WaveTrack* parent);

    std::string_view getName() const;

    size_t getParentIndex() const;
    WaveTrack* getParent() const;

    double getOffset() const;
    double getTrimLeft() const;
    double getTrimRight() const;

    Sequences::const_iterator begin() const;
    Sequences::const_iterator end() const;

private:
    WaveTrack* mParent;
    size_t mParentIndex;

    std::string_view mName;

    double mOffset;
    double mTrimLeft;
    double mTrimRight;

    Sequences mSequences;

    friend class Sequence;
};

class WaveTrack final : public DeserializedNode
{
public:
    WaveTrack(ProjectTreeNode* node, size_t index);

    std::string_view getTrackName() const;
    int getChannel() const;
    bool isLinked() const;

    size_t getParentIndex() const;

    int getSampleFormat() const;
    int getSampleRate() const;

    const std::vector<Clip*>& getClips() const;

private:
    size_t mParentIndex;

    std::string_view mName;

    int mSampleFormat;
    int mRate;
    int mChannel;
    bool mLinked;

    std::vector<Clip*> mClips;

    friend class Clip;
};

class AudacityProject final : public XMLHandler
{
public:
    AudacityProject(AudacityDatabase& db);
    ~AudacityProject();

    bool containsBlock(int64_t blockId) const;

    enum class BlockValidationResult
    {
        Ok,
        Missing,
        Invalid
    };

    int getRealBlockLength(const WaveBlock& block) const;

    BlockValidationResult validateBlock(const WaveBlock& block) const;

    std::set<int64_t> validateBlocks() const;

    std::set<int64_t> recoverProject();

    void saveProject();

    void removeUnusedBlocks();

    void extractClips() const;

    void printProjectStatistics() const;

private:
    std::string_view CacheString(std::string_view view, bool reuse);

    void HandleTagStart(std::string_view name, const AttributeList& attributes) override;
    void HandleTagEnd(std::string_view name) override;
    void HandleCharData(std::string_view data) override;

    AudacityDatabase& mDb;

    std::unique_ptr<ProjectTreeNode> mProjectNode;

    std::deque<std::string> mReusableStringsCache;
    std::deque<std::string> mStringCache;

    std::deque<WaveBlock> mWaveBlocks;
    std::deque<Sequence> mSequences;
    std::deque<Clip> mClips;
    std::deque<WaveTrack> mWaveTracks;

    struct ParserState;
    std::unique_ptr<ParserState> mParserState;

    bool mFromAutosave;
};
