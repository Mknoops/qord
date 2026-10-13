/*
 * Qord, a native Discord client
 * Port of Equicord's BlockKeywords plugin (src/equicordplugins/blockKeywords)
 * Original: Copyright (c) Vendicated and contributors, authors catcraft, secp192k1
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace Qord {
namespace Plugins {

// Upstream's comma-separated list: commas inside [...] and {n,m} belong to
// the pattern
QStringList splitKeywordPatterns(const QString &input)
{
    static const QRegularExpression brackets(QStringLiteral("\\[([^\\]]*)\\]"));
    static const QRegularExpression counts(QStringLiteral("\\{(\\d+,?\\d*|,\\d+)\\}"));
    QString protectedInput = input;
    for (const QRegularExpression *re : { &brackets, &counts }) {
        QString out;
        qsizetype last = 0;
        auto it = re->globalMatch(protectedInput);
        while (it.hasNext()) {
            const auto m = it.next();
            out += protectedInput.mid(last, m.capturedStart() - last);
            QString piece = m.captured(0);
            if (re == &brackets)
                piece.replace(QLatin1Char(','), QChar(0));
            else
                piece.replace(piece.indexOf(QLatin1Char(',')), 1, QChar(0));
            out += piece;
            last = m.capturedEnd();
        }
        protectedInput = out + protectedInput.mid(last);
    }
    QStringList patterns;
    for (QString part : protectedInput.split(QLatin1Char(','))) {
        part.replace(QChar(0), QLatin1Char(','));
        part = part.trimmed();
        if (!part.isEmpty())
            patterns.append(part);
    }
    return patterns;
}

// A keyword as upstream matches it: a whole word, or the pattern itself with
// "Use regex"
QRegularExpression keywordRegex(const QString &pattern, bool useRegex, bool caseSensitive)
{
    const QString source = useRegex ? pattern
                                    : QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(pattern));
    return QRegularExpression(source, caseSensitive ? QRegularExpression::NoPatternOption
                                                    : QRegularExpression::CaseInsensitiveOption);
}

namespace {

// Hides messages whose text or embed titles and descriptions match your
// keywords, as if their authors were blocked
class BlockKeywordsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageFilter; }

    void start(PluginContext &ctx) override
    {
        context = &ctx;
        compile();
    }
    void stop() override
    {
        context = nullptr;
        keywords.clear();
    }

    void onSettingChanged(const QString &key) override
    {
        Q_UNUSED(key);
        compile();
        if (MessageService *messages = context ? context->messages() : nullptr)
            messages->refreshFilters();
    }

    bool shouldHideMessage(const MessageContext &message) override
    {
        if (keywords.isEmpty())
            return false;
        if (matches(message.content))
            return true;
        if (!message.hasEmbeds || message.rawJson.isEmpty())
            return false;
        const QJsonArray embeds =
                QJsonDocument::fromJson(message.rawJson.toUtf8()).object().value(QStringLiteral("embeds")).toArray();
        for (const QJsonValue &v : embeds) {
            const QJsonObject embed = v.toObject();
            if (matches(embed.value(QStringLiteral("title")).toString())
                || matches(embed.value(QStringLiteral("description")).toString()))
                return true;
        }
        return false;
    }

    void onSettingAction(const QString &key) override
    {
        if (key != QLatin1String("regexHelper"))
            return;
        UiService *ui = context ? context->ui() : nullptr;
        if (!ui)
            return;
        std::weak_ptr<bool> weak = alive;
        ui->promptText(QStringLiteral("Regex Helper"), QStringLiteral("Input to test..."), lastTest, 0,
                       [this, weak](const QString &text) {
                           if (!weak.expired() && context)
                               showTest(text);
                       });
    }

private:
    bool matches(const QString &text) const
    {
        if (text.isEmpty())
            return false;
        return std::any_of(keywords.begin(), keywords.end(),
                           [&](const QRegularExpression &re) { return re.match(text).hasMatch(); });
    }

    void compile()
    {
        keywords.clear();
        alive = std::make_shared<bool>(true);
        if (!context)
            return;
        auto &settings = context->settings();
        const bool useRegex = settings.boolValue(QStringLiteral("useRegex"));
        const bool caseSensitive = settings.boolValue(QStringLiteral("caseSensitive"));
        for (const QString &pattern : splitKeywordPatterns(settings.stringValue(QStringLiteral("blockedWords")))) {
            QRegularExpression re = keywordRegex(pattern, useRegex, caseSensitive);
            if (re.isValid()) // upstream throws on an invalid one; skipping it keeps the rest working
                keywords.append(re);
        }
    }

    // Each pattern against the input: matches, doesn't, or why it's invalid
    void showTest(const QString &text)
    {
        lastTest = text;
        auto &settings = context->settings();
        const bool useRegex = settings.boolValue(QStringLiteral("useRegex"));
        const bool caseSensitive = settings.boolValue(QStringLiteral("caseSensitive"));
        QStringList lines;
        for (const QString &pattern : splitKeywordPatterns(settings.stringValue(QStringLiteral("blockedWords")))) {
            const QRegularExpression re = keywordRegex(pattern, useRegex, caseSensitive);
            if (!re.isValid())
                lines.append(QStringLiteral("⚠ %1 — %2").arg(pattern, re.errorString()));
            else
                lines.append(QStringLiteral("%1 %2").arg(re.match(text).hasMatch() ? QStringLiteral("✓")
                                                                                     : QStringLiteral("✗"),
                                                         pattern));
        }
        if (lines.isEmpty())
            lines.append(QStringLiteral("No patterns configured"));
        if (UiService *ui = context->ui())
            ui->showTextDialog(QStringLiteral("Regex Helper"),
                               { { QStringLiteral("Input"), text, false, {} },
                                 { QStringLiteral("Patterns"), lines.join(QLatin1Char('\n')), true, {} } });
    }

    PluginContext *context = nullptr;
    QList<QRegularExpression> keywords;
    QString lastTest;
    std::shared_ptr<bool> alive;
};
} // namespace

QByteArray qord_manifest_blockkeywords();

void qord_register_blockkeywords(Registry &registry)
{
    registry.add({ QStringLiteral("blockkeywords/plugin.json"), [] { return std::make_unique<BlockKeywordsPlugin>(); },
                   qord_manifest_blockkeywords() });
}

} // namespace Plugins
} // namespace Qord
