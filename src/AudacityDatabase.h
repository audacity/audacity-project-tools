/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <filesystem>
#include <memory>

#include "SampleFormat.h"

struct RecoveryConfig final
{
    const std::filesystem::path BinaryPath;

    bool FreelistCorrupt;
    bool AllowRecoveryFromConstructor;
};

class AudacityDatabase final
{
public:
    explicit AudacityDatabase(
        const std::filesystem::path& path, RecoveryConfig recoveryConfig);

    void reopenReadonlyAsWritable();
    void recoverDatabase();

    bool hasAutosave();
    void dropAutosave();
    bool checkIntegrity();

    SQLite::Database& DB();

    std::filesystem::path getProjectPath() const;
    std::filesystem::path getCurrentPath() const;

    std::filesystem::path getDataPath() const;

    void extractSampleBlocks(SampleFormat format, int32_t sampleRate);
    void extractTrack(SampleFormat format, int32_t sampleRate, bool asStereo);

private:
    void removeOldFiles();

    std::unique_ptr<SQLite::Database> mDatabase;
    std::filesystem::path mProjectPath;
    std::filesystem::path mWritablePath;
    std::filesystem::path mDataPath;

    uint32_t mProjectVersion;

    RecoveryConfig mRecoveryConfig;

    bool mReadOnly { true };
    bool mRecoveredInConstructor { false };
};
