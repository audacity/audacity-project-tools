/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#include "ProjectBlobReader.h"

#include <fmt/format.h>
#include <sqlite3.h>
#include <cstdint>
#include <algorithm>

class SQLiteBlob final
{
public:
    SQLiteBlob(
        SQLite::Database& db, const char* table, const char* column, int64_t rowId)
    {
        rowId = db.execAndGet(fmt::format("SELECT ROWID FROM main.{} WHERE ROWID = {}", table, rowId)).getInt64();

        const int rc = sqlite3_blob_open(
            db.getHandle(), "main", table, column, rowId, 0, &mBlob);

        if (rc != SQLITE_OK)
            throw SQLite::Exception(db.getHandle(), rc);

        mBlobSize = sqlite3_blob_bytes(mBlob);
    }

    ~SQLiteBlob()
    {
        sqlite3_blob_close(mBlob);
    }

    void readToBuffer(Buffer* outputBuffer)
    {
        constexpr size_t BUFFER_SIZE = 8 * 1024;

        uint8_t buffer[BUFFER_SIZE];

        size_t bytesLeft = mBlobSize;
        size_t offset = 0;

        while (bytesLeft > 0)
        {
            const size_t chunkSize = std::min(bytesLeft, BUFFER_SIZE);

            const int rc = sqlite3_blob_read(mBlob, buffer, chunkSize, offset);

            if (rc != SQLITE_OK)
                throw SQLite::Exception("Read failed", rc);

            outputBuffer->append(buffer, chunkSize);

            offset += chunkSize;
            bytesLeft -= chunkSize;
        }
    }

private:
    sqlite3_blob* mBlob { nullptr };

    size_t mBlobSize { 0 };
};

std::unique_ptr<Buffer>
ReadProjectBlob(SQLite::Database& db, const std::string& table, int64_t rowId)
{
    auto buffer = std::make_unique<Buffer>();

    {
        SQLiteBlob dict(db, table.c_str(), "dict", rowId);
        dict.readToBuffer(buffer.get());
    }

    {
        SQLiteBlob project(db, table.c_str(), "doc", rowId);
        project.readToBuffer(buffer.get());
    }

    return buffer;
}
