/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#include <gflags/gflags.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <fmt/format.h>

#include <string_view>

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#   include <windows.h>
#   include <utf8.h>
#   include <vector>
#   include <deque>
#endif

#include "ProjectBlobReader.h"
#include "BinaryXMLConverter.h"
#include "AudacityDatabase.h"
#include "ProjectModel.h"

DEFINE_bool(drop_autosave, false, "Drop autosave table, if exists");
DEFINE_bool(extract_project, false, "Extract Audacity project as an XML file");
DEFINE_bool(check_integrity, false, "Check AUP3 integrity");

DEFINE_bool(compact, false, "Compact the project");

DEFINE_bool(recover_db, false, "Try to recover the project database");
DEFINE_bool(recover_project, false, "Try to recover the project database");

DEFINE_bool(extract_clips, false, "Try to extract clips from the AUP3");

DEFINE_bool(
    extract_sample_blocks, false, "Try to extract individual sample blocks");

DEFINE_bool(extract_as_mono_track, false, "Extract all available samples as a mono track");
DEFINE_bool(extract_as_stereo_track, false, "Extract all available samples as a stereo track");

DEFINE_int32(sample_rate, 44100, "Bitrate for the extracted samples (-extract_sample_blocks, -extract_as_mono_track, -extract_as_stereo_track). Deafult is 44100");

DEFINE_string(
    sample_format,
    "float",
    "Sample format for the extracted samples (-extract_sample_blocks, -extract_as_mono_track, -extract_as_stereo_track). Possible values are: int16, int24, float");

constexpr int64_t AudacityProjectID = 1096107097;
constexpr int64_t MaxSupportedVersion = (3 << 24) + (1 << 16) + (3 << 8) + 0; // 3.1.3.0

namespace
{
bool CanContinueInFailedState() noexcept
{
    return FLAGS_extract_project || FLAGS_recover_db || FLAGS_recover_project ||
           FLAGS_extract_clips || FLAGS_extract_sample_blocks ||
           FLAGS_extract_as_mono_track ||
           FLAGS_extract_as_stereo_track;
}

void ExtractProjectXML(
    SQLite::Database& db, const std::string& table, const std::filesystem::path& projectPath)
{
    fmt::print("Reading project from table {}\n", table);
    auto buffer = ReadProjectBlob(db, table);

    auto xmlText = BinaryXMLConverter::ConvertToXML(*buffer);

    std::filesystem::path xmlPath =
        projectPath.parent_path() /
        std::filesystem::u8path(fmt::format("{}.{}.xml", projectPath.filename().u8string(), table));

    std::fstream xmlFile(xmlPath, std::ios_base::out | std::ios::binary);

    constexpr size_t writeBufferSize = 8 * 1024;
    char writeBuffer[writeBufferSize];

    size_t offset = 0;
    size_t bytesRead = 0;

    while (0 !=
           (bytesRead = xmlText->read(writeBuffer, offset, writeBufferSize)))
    {
        xmlFile.write(writeBuffer, bytesRead);
        offset += bytesRead;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t *wArgv[])
#else
int main(int argc, char **argv)
#endif
{
#ifdef _WIN32
    std::deque<std::string> utf8Args;
    std::vector<char*> utf8ArgsPtrs;

    for (int i = 0; i < argc; ++i)
    {
        std::string utf8Arg;

        auto wArg = wArgv[i];

        utf8::utf16to8(wArg, wArg + wcslen(wArg), std::back_inserter(utf8Arg));

        utf8Args.emplace_back(std::move(utf8Arg));
        utf8ArgsPtrs.push_back(const_cast<char*>(utf8Args.back().c_str()));
    }

    char** argv = utf8ArgsPtrs.data();
#endif

    gflags::SetUsageMessage("[mode] path.aup3");

    if (argc <= 1)
    {
        gflags::ShowUsageWithFlags(argv[0]);
        return 1;
    }

    int argsLeft = argc;

    gflags::ParseCommandLineFlags(&argsLeft, &argv, true);

    if (argc == argsLeft || argsLeft == 1)
    {
        gflags::ShowUsageWithFlags(argv[0]);
        return 1;
    }

    std::filesystem::path projectPath = std::filesystem::u8path(argv[1]);

    try
    {
        AudacityDatabase projectDatabase(projectPath);

        if (FLAGS_drop_autosave)
        {
            projectDatabase.dropAutosave();
        }

        if (FLAGS_check_integrity)
        {
            if (!projectDatabase.checkIntegrity())
            {
                fmt::print(
                    "Integrity check for '{}' has failed.\n", projectPath.string());

                if (!CanContinueInFailedState())
                    return 3;
            }
            else
            {
                fmt::print("Database integrity check has passed\n");
            }
        }

        if (FLAGS_extract_project)
        {
            if (projectDatabase.hasAutosave())
                ExtractProjectXML(projectDatabase.DB(), "autosave", projectPath);

            ExtractProjectXML(projectDatabase.DB(), "project", projectPath);
        }

        if (FLAGS_recover_db)
        {
            projectDatabase.recoverDatabase(std::filesystem::u8path(argv[0]));
        }

        std::unique_ptr<AudacityProject> project;

        if (FLAGS_recover_project)
        {
            if (project == nullptr)
                project = std::make_unique<AudacityProject>(projectDatabase);

            project->fixupMissingBlocks();
        }

        if (FLAGS_compact)
        {
            if (project == nullptr)
                project = std::make_unique<AudacityProject>(projectDatabase);

            project->removeUnusedBlocks();
        }

        if (FLAGS_extract_clips)
        {
            if (project == nullptr)
                project = std::make_unique<AudacityProject>(projectDatabase);

            project->extractClips();
        }

        if (FLAGS_extract_sample_blocks)
        {
            projectDatabase.extractSampleBlocks(
                SampleFormatFromString(FLAGS_sample_format), FLAGS_sample_rate);
        }

        if (FLAGS_extract_as_mono_track)
        {
            projectDatabase.extractTrack(
                SampleFormatFromString(FLAGS_sample_format), FLAGS_sample_rate, false);
        }

        if (FLAGS_extract_as_stereo_track)
        {
            projectDatabase.extractTrack(
                SampleFormatFromString(FLAGS_sample_format), FLAGS_sample_rate, true);
        }
    }
    catch (const std::exception& ex)
    {
        fmt::print("{}\n", ex.what());
        return -1;
    }

    return 0;
}
