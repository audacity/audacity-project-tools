/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#include "AudacityDatabase.h"

#include <string>
#include <iostream>
#include <vector>
#include <charconv>
#include <locale>

#include <boost/process.hpp>
#include <boost/filesystem.hpp>

#include <fmt/format.h>
#include "AudacityDatabase.h"

#include "WaveFile.h"

namespace
{

constexpr int64_t AudacityProjectID = 1096107097;
constexpr int64_t MaxSupportedVersion =
    (3 << 24) + (1 << 16) + (3 << 8) + 0; // 3.1.3.0

template<typename T> size_t readInt(const std::string& string, size_t offset, T& output)
{
    while (std::isspace(string[offset]))
        ++offset;

    const auto result = std::from_chars(
        string.data() + offset, string.data() + string.length(), output);

    if (result.ec != std::errc {})
    {
        throw std::runtime_error(fmt::format(
            "Failed to convert {} to integer", string.data() + offset));
    }

    return result.ptr - string.data();
}
}

AudacityDatabase::AudacityDatabase(const std::filesystem::path& path)
    : mProjectPath(path)
    , mWritablePath(path)
{
    mWritablePath.replace_extension("recovered.aup3");

    mDataPath = mProjectPath.parent_path() /
                std::filesystem::u8path(
                    fmt::format("{}_data", mProjectPath.stem().u8string()));

    // This will trow on error
    mDatabase = std::make_unique<SQLite::Database>(path.u8string());

    // Check if application_id is valid, but only log warning if it is not
    const int64_t appId =
        mDatabase->execAndGet("PRAGMA application_id;").getInt64();

    if (appId != AudacityProjectID)
    {
        fmt::print(
            "Unexpected application_id pragma: {}. Is it really an Audacity project?\n",
            appId);
    }

    mProjectVersion = mDatabase->execAndGet("PRAGMA user_version;").getUInt();

    fmt::print(
        "Project requires Audacity {}.{}.{}\n", (mProjectVersion >> 24) & 0xFF,
        (mProjectVersion >> 16) & 0xFF, (mProjectVersion >> 8) & 0xFF);

    if (mProjectVersion > MaxSupportedVersion)
        throw std::runtime_error("Unsupported project version");
}

void AudacityDatabase::reopenReadonlyAsWritable()
{
    if (!mReadOnly)
        return;

    fmt::print(
        "Reopening database in writable mode at path {}",
        mWritablePath.string());

    removeOldFiles();

    std::filesystem::copy_file(mProjectPath, mWritablePath);
    mDatabase = std::make_unique<SQLite::Database>(
        mWritablePath.string(), SQLite::OPEN_READWRITE);

    mReadOnly = false;
}

void AudacityDatabase::recoverDatabase(const std::filesystem::path& binaryPath)
{
    mDatabase = {};
    removeOldFiles();

    auto recoveredDB = std::make_unique<SQLite::Database>(
        mWritablePath.u8string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

    recoveredDB->exec(R"(
    PRAGMA page_size = 65536;
    PRAGMA busy_timeout = 5000;
    PRAGMA locking_mode = EXCLUSIVE;
    PRAGMA synchronous = OFF;
    PRAGMA journal_mode = WAL;
    PRAGMA wal_autocheckpoint = 1000;
    VACUUM;)");

    using namespace boost::process;

    std::vector<boost::filesystem::path> paths = boost::this_process::path();
    paths.insert(paths.begin(), binaryPath.parent_path().generic_string());

    ipstream out_stream;
    ipstream err_stream;

    const auto sqlite3Binary = search_path("sqlite3", paths);

    fmt::print("Using '{}' for recovery\n", sqlite3Binary.generic_string());

    // detail::windows::build_args()

    child c(
        sqlite3Binary, args += { mProjectPath.string(), ".recover" },
        std_out > out_stream, std_err > err_stream);

    std::string line;

    int64_t recoveredSampleBlocks = 0;

    while (out_stream && std::getline(out_stream, line) && !line.empty())
    {
        if (line.find("BEGIN") != std::string::npos)
            continue;

        if (line.find("COMMIT") != std::string::npos)
            continue;

        if (line.find("lost_and_found") != std::string::npos)
        {
            if (line.find("CREATE") != std::string::npos)
                continue; // Skip the line

            if (line.find("INSERT") == std::string::npos)
                throw std::runtime_error(
                    "Unsupported lost_and_found query: " + line);

            /*
            INSERT INTO "lost_and_found" VALUES(99, 99, 8, 5735, NULL, 262159,
            0, 0, 0, X'...

            root_n n n_fields rowid blockid sampleformat summin summax sumrms
            summary256 summary64k samples 8        blkid NULL?
            */

            const size_t openingBracketPosition = line.find('(');

            // Skip the root_n and n

            const size_t firstComma =
                line.find(',', openingBracketPosition + 1);
            const size_t secondComma = line.find(',', firstComma + 1);

            // In case string is "malformed" we will fail horribly anyway, so we
            // do not check if the values really make any sense
            int colsCount = 0;

            const size_t colsEnd = readInt(line, secondComma + 1, colsCount);

            if (colsCount != 8)
                throw std::runtime_error("Unexpected lost_and_found structure");

            int rowId = 0;
            size_t formatPosition = std::string::npos;

            try
            {
                formatPosition = readInt(line, colsEnd + 1, rowId);
                formatPosition = line.find("NULL,", formatPosition) + 5;
            }
            catch (...)
            {
                formatPosition = line.find(" NULL,", colsEnd + 1);

                if (formatPosition != colsEnd + 1)
                    throw std::runtime_error(
                        "Unexpected token in SQLStatement");

                formatPosition = readInt(line, formatPosition + 5, rowId) + 1;
            }

            line = fmt::format(
                "INSERT OR REPLACE INTO sampleblocks (blockid, sampleformat, summin, summax, sumrms, summary256, summary64k, samples) VALUES({},{}",
                rowId, line.data() + formatPosition);

            ++recoveredSampleBlocks;
        }

        recoveredDB->exec(line);
    }

    c.wait();

    const auto result = c.exit_code();

    if (result != 0)
    {
        throw std::runtime_error(
            std::string(std::istreambuf_iterator<char>(err_stream), {}));
    }

    recoveredDB->exec(R"(
    PRAGMA locking_mode = NORMAL;
    PRAGMA synchronous = NORMAL;)");

    recoveredDB->exec(fmt::format("PRAGMA application_id = {};", AudacityProjectID));
    recoveredDB->exec(fmt::format("PRAGMA user_version = {};", mProjectVersion));

    recoveredDB->exec("VACUUM;");

    if (recoveredSampleBlocks > 0)
        fmt::print("Recovered {} sample blocks from the database\n", recoveredSampleBlocks);

    mDatabase = std::move(recoveredDB);
    mReadOnly = false;
}

bool AudacityDatabase::hasAutosave()
{
    return mDatabase->execAndGet("SELECT COUNT(1) FROM autosave;").getInt() > 0;
}

void AudacityDatabase::dropAutosave()
{
    if (hasAutosave())
    {
        reopenReadonlyAsWritable();
        mDatabase->exec("DELETE FROM autosave WHERE id = 1");
    }
}

bool AudacityDatabase::checkIntegrity()
{
    fmt::print("Checking database integrity.\n");

    try
    {
        SQLite::Statement integrityCheck(DB(), "PRAGMA integrity_check(10240);");

        while (integrityCheck.executeStep())
        {
            const std::string message = integrityCheck.getColumn(0);

            if (message == "ok")
                return true;
            else
                fmt::print("{}\n", message);
        }
    }
    catch (const std::exception& ex)
    {
        fmt::print("Exception while checking the integrity: {}", ex.what());
    }

    return false;
}

SQLite::Database& AudacityDatabase::DB()
{
    return *mDatabase;
}

std::filesystem::path AudacityDatabase::getProjectPath() const
{
    return mProjectPath;
}

std::filesystem::path AudacityDatabase::getCurrentPath() const
{
    return mReadOnly ? mProjectPath : mWritablePath;
}

std::filesystem::path AudacityDatabase::getDataPath() const
{
    return mDataPath;
}

void AudacityDatabase::extractSampleBlocks(
    SampleFormat format, int32_t sampleRate)
{
    constexpr size_t entriesPerDirectory = 32;

    size_t outerIndex = 0;
    size_t innerIndex = 0;
    size_t fileIndex = 0;

    auto makePath = [baseDirectory = mDataPath / "sampleblocks"](
                        size_t outerIndex, size_t innerIndex)
    {
        const auto path = baseDirectory / fmt::format("{:03}", outerIndex) /
                    fmt::format("{:02}", innerIndex);

        std::filesystem::create_directories(path);

        return path;
    };

    auto baseDirectory = makePath(outerIndex, innerIndex);

    SQLite::Statement stmt(*mDatabase, R"(SELECT blockid, samples FROM sampleblocks;)");

    while (stmt.executeStep())
    {
        const int64_t blockId = stmt.getColumn(0).getInt64();

        const void* data = stmt.getColumn(1).getBlob();
        const int64_t bytes = stmt.getColumn(1).getBytes();

        const auto wavePath = baseDirectory / fmt::format("{}.wav", blockId);

        WaveFile waveFile(wavePath, format, sampleRate, 1);

        waveFile.writeBlock(data, bytes, 0);

        waveFile.writeFile();

        ++fileIndex;

        if (fileIndex == entriesPerDirectory)
        {
            fileIndex = 0;

            ++innerIndex;

            if (innerIndex == entriesPerDirectory)
            {
                ++outerIndex;
                innerIndex = 0;
            }

            baseDirectory = makePath(outerIndex, innerIndex);
        }
    }
}

void AudacityDatabase::extractTrack(
    SampleFormat format, int32_t sampleRate, bool asStereo)
{
    std::filesystem::create_directories(mDataPath);

    const auto wavePath = mDataPath / (asStereo ? "stereo.wav" : "mono.wav");

    WaveFile waveFile(wavePath, format, sampleRate, asStereo ? 2 : 1);

    SQLite::Statement stmt(
        *mDatabase, R"(SELECT blockid, samples FROM sampleblocks;)");

    while (stmt.executeStep())
    {
        const int64_t blockId = stmt.getColumn(0).getInt64();

        const void* data = stmt.getColumn(1).getBlob();
        const int64_t bytes = stmt.getColumn(1).getBytes();

        waveFile.writeBlock(
            data, bytes, asStereo && (blockId % 2 == 0) ? 1 : 0);
    }

    waveFile.writeFile();
}

void AudacityDatabase::removeOldFiles()
{
    if (std::filesystem::exists(mWritablePath))
    {
        std::filesystem::remove(mWritablePath);

        auto walFile = mWritablePath;
        walFile.replace_extension("aup3-wal");

        if (std::filesystem::exists(walFile))
            std::filesystem::remove(walFile);

        auto shmFile = mWritablePath;
        shmFile.replace_extension("aup3-shm");

        if (std::filesystem::exists(shmFile))
            std::filesystem::remove(shmFile);
    }
}
