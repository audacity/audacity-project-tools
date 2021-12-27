/*
 SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
 SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include <memory>
#include <utility>
#include <string>
#include <deque>

#include "Buffer.h"
#include "XMLHandler.h"

struct ProjectTreeNode;

class BinaryXMLConverter final
{
public:
    static void Parse(const Buffer& buffer, XMLHandler& handler);
    static std::unique_ptr<Buffer> ConvertToXML(const Buffer& buffer);

    static std::pair<std::unique_ptr<Buffer>, std::unique_ptr<Buffer>>
    SerializeProject(const std::deque<std::string>& names, const ProjectTreeNode& project);
};
