/*
 * Qord, a native Discord client
 * Port of Equicord's Unindent plugin (src/plugins/unindent)
 * Original: Copyright (c) 2022 Vendicated and contributors, author Ven
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {
namespace Unindent {

// Tabs become 4 spaces, then the smallest leading indentation of non-blank
// lines is removed from every line.
QString unindent(QString text);

// Applies unindent() to the body of every multi-line ``` code block, keeping
// the opening line (with its language) and a closing ``` line. Inline code
// and single-line blocks are untouched.
QString unindentMessage(const QString &content);

// Display side (upstream patches the code block renderer): unindents the
// body of every fenced code block as Discord's parser reads it.
QString unindentRendered(const QString &content);

} // namespace Unindent

class UnindentPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::BeforeSend | Hook::BeforeEdit | Hook::TransformIncomingContent;
    }
    void onBeforeSend(OutgoingMessage &message) override { message.content = Unindent::unindentMessage(message.content); }
    void onBeforeEdit(OutgoingMessage &message) override { message.content = Unindent::unindentMessage(message.content); }
    void transformIncomingContent(IncomingMessage &message) override
    {
        message.content = Unindent::unindentRendered(message.content);
    }
};

} // namespace Plugins
} // namespace Qord
