/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <memory>
#include <string>
#include <SQLiteCpp/SQLiteCpp.h>

#include "Buffer.h"

std::unique_ptr<Buffer> ReadProjectBlob(SQLite::Database& db, const std::string& table);
