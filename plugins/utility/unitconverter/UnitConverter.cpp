/*
 * Qord, a native Discord client
 * Port of Equicord's UnitConverter plugin (src/equicordplugins/unitConverter)
 * Original: Copyright (c) Vendicated and contributors, author sadan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

#include <QHash>
#include <QRegularExpression>

#include <cmath>
#include <functional>

namespace Qord {
namespace Plugins {

namespace {

QString fixed(double value, int decimals = 2)
{
    return QString::number(value, 'f', decimals);
}

double num(const QRegularExpressionMatch &m, int group)
{
    return m.captured(group).toDouble();
}

// One of upstream's rules: what to find and what to write instead.
// `skip` stands in for a lookbehind PCRE2 can't express (the text before a
// match decides whether it counts).
struct Rule
{
    QRegularExpression regex;
    std::function<QString(const QRegularExpressionMatch &)> convert;
    std::function<bool(const QString &before)> skip = {};
};

QString applyRule(const QString &text, const Rule &rule)
{
    QString out;
    qsizetype last = 0;
    auto it = rule.regex.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        if (rule.skip && rule.skip(text.left(m.capturedStart())))
            out += m.captured(0);
        else
            out += rule.convert(m);
        last = m.capturedEnd();
    }
    return out + text.mid(last);
}

constexpr auto CI = QRegularExpression::CaseInsensitiveOption;

// Imperial units, converted to metric (for people who use metric)
const QList<Rule> &imperialRules()
{
    static const QList<Rule> rules = {
        // Fahrenheit
        { QRegularExpression(QStringLiteral("(-?\\d+(?:\\.\\d+)?)°?(f)(?!\\w)"), CI),
          [](const auto &m) { return fixed((num(m, 1) - 32) * 5 / 9) + QStringLiteral("°C"); } },
        // 5'11"
        { QRegularExpression(QStringLiteral("(\\d+)(') ?(\\d+(?:\\.\\d+)?)(\"|'')?")),
          [](const auto &m) { return fixed(num(m, 1) / 3.281 + num(m, 3) / 39.37) + QStringLiteral("m"); } },
        // 6 feet
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) *(f(ee)?t)(?! *\\d)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 3.281) + QStringLiteral("m"); } },
        // 12 inches, unless it follows "5 ft" (feet and inches below)
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) *(in(?:ches?)?)"), CI),
          [](const auto &m) { return fixed(num(m, 1) * 2.54) + QStringLiteral("cm"); },
          [](const QString &before) {
              static const QRegularExpression feet(QStringLiteral("\\d+ *(?:f(?:ee|oo)?t) *$"), CI);
              return feet.match(before).hasMatch();
          } },
        // 5 ft 11 in
        { QRegularExpression(QStringLiteral("(\\d+) *(f(?:ee|oo)?t) *(\\d+(?:\\.\\d+)?) *(in(?:ches?)?)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 3.281 + num(m, 3) / 39.37) + QStringLiteral("m"); } },
        // 10 lbs
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) *(lbs?|pounds?)(?! ?\\d)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 2.205) + QStringLiteral("kg"); } },
        // 5 lb 3 oz
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) *(lbs?|pounds?) *(\\d+(?:\\.\\d+)?) *(ozs?|ounces?)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 2.205 + num(m, 3) / 35.274) + QStringLiteral("kg"); } },
        // 8 oz
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(ounces?|oz)"), CI),
          [](const auto &m) { return fixed(num(m, 1) * 28.35) + QStringLiteral("g"); } },
        // 60 mph
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(m(?:p|/)h)"), CI),
          [](const auto &m) { return fixed(num(m, 1) * 1.609) + QStringLiteral("km/h"); } },
    };
    return rules;
}

// Metric units, converted to imperial (for people who use imperial)
const QList<Rule> &metricRules()
{
    static const QList<Rule> rules = {
        // Celsius
        { QRegularExpression(QStringLiteral("(-?\\d+(?:\\.\\d+)?)\\s?°?c(?!\\w)"), CI),
          [](const auto &m) { return fixed(num(m, 1) * 9 / 5 + 32) + QStringLiteral("°F"); } },
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(cm|centimeters?)(?!\\w)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 2.54) + QStringLiteral("in"); } },
        // Speeds before distances, so "50 km/h" isn't read as "50 km"
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(km/h|kmph|kph|kilometers?/?h)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 1.609) + QStringLiteral("mph"); } },
        // Meters as feet and inches
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(m|meters?)(?!\\w)"), CI),
          [](const auto &m) {
              const double feet = std::round(num(m, 1) * 3.281 * 100) / 100;
              if (feet == std::floor(feet))
                  return QString::number(qint64(feet)) + QStringLiteral("ft");
              const double whole = std::floor(feet);
              return QString::number(qint64(whole)) + QStringLiteral("ft") + fixed((feet - whole) * 12) + QStringLiteral("in");
          } },
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(km|kilometers?|kms?)(?!\\w)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 1.609) + QStringLiteral("mi"); } },
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(grams?|g)"), CI),
          [](const auto &m) { return fixed(num(m, 1) / 28.35) + QStringLiteral("oz(s)"); } },
        { QRegularExpression(QStringLiteral("(\\d+(?:\\.\\d+)?) ?(kg|kilo(?:gram)?s?)"), CI),
          [](const auto &m) { return fixed(num(m, 1) * 2.205) + QStringLiteral("lb(s)"); } },
    };
    return rules;
}

} // namespace

// The message with units in the other system: metric ones become imperial
// for people who use imperial, and the other way round
QString convertUnits(const QString &message, bool toImperial)
{
    QString out = message;
    for (const Rule &rule : toImperial ? metricRules() : imperialRules())
        out = applyRule(out, rule);
    return out;
}

namespace {

// "Convert Units" on a message shows it with converted units underneath
class UnitConverterPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageContextMenu | Hook::MessageAccessories; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override
    {
        context = nullptr;
        conversions.clear();
    }

    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override
    {
        if (message.content.isEmpty())
            return {};
        return { { QStringLiteral("convert"), QStringLiteral("Convert Units") } };
    }

    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override
    {
        if (!context || id != QLatin1String("convert"))
            return;
        const bool toImperial = context->settings().stringValue(QStringLiteral("myUnits")) != QLatin1String("metric");
        conversions.insert(message.messageId, convertUnits(message.content, toImperial));
        refresh(message);
    }

    std::optional<MessageAccessory> messageAccessory(const MessageContext &message) override
    {
        const auto it = conversions.constFind(message.messageId);
        if (it == conversions.constEnd())
            return std::nullopt;
        MessageAccessory accessory;
        accessory.content = *it;
        accessory.links = { { QStringLiteral("dismiss"), QStringLiteral("Dismiss"), {} } };
        return accessory;
    }

    void onMessageAccessoryAction(const QString &id, const MessageContext &message) override
    {
        if (id == QLatin1String("dismiss") && conversions.remove(message.messageId))
            refresh(message);
    }

private:
    void refresh(const MessageContext &message)
    {
        if (MessageService *messages = context ? context->messages() : nullptr)
            messages->refreshMessage(message.accountId, message.channelId, message.messageId);
    }

    PluginContext *context = nullptr;
    QHash<Id, QString> conversions; // by message
};
} // namespace

QByteArray qord_manifest_unitconverter();

void qord_register_unitconverter(Registry &registry)
{
    registry.add({ QStringLiteral("unitconverter/plugin.json"), [] { return std::make_unique<UnitConverterPlugin>(); },
                   qord_manifest_unitconverter() });
}

} // namespace Plugins
} // namespace Qord
