#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_copyemojimarkdown(Registry &registry);
QString emojiMarkdown(const EmojiInfo &emoji, bool copyUnicode);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestCopyEmojiMarkdown : public QObject
{
    Q_OBJECT

    static EmojiInfo custom(const QString &name, Id id, bool animated = false)
    {
        EmojiInfo e;
        e.name = name;
        e.customId = id;
        e.animated = animated;
        return e;
    }

    static EmojiInfo unicode()
    {
        EmojiInfo e;
        e.name = QStringLiteral("alien");
        e.surrogates = QStringLiteral("👽");
        return e;
    }

private slots:
    void markdownLikeUpstream()
    {
        QCOMPARE(emojiMarkdown(custom(QStringLiteral("blobcatcozy"), 1026533070955872337), true),
                 QStringLiteral("<:blobcatcozy:1026533070955872337>"));
        QCOMPARE(emojiMarkdown(custom(QStringLiteral("party"), 42, true), true), QStringLiteral("<a:party:42>"));
        QCOMPARE(emojiMarkdown(custom(QStringLiteral("cat~2"), 7), true), QStringLiteral("<:cat:7>"));
        QCOMPARE(emojiMarkdown(unicode(), true), QStringLiteral("👽"));
        QCOMPARE(emojiMarkdown(unicode(), false), QStringLiteral(":alien:"));
    }

    void copiesFromThePickerMenu()
    {
        SinglePluginHost h(&qord_register_copyemojimarkdown);
        QVERIFY(h.enableWithAllPermissions());
        EmojiContext ctx;
        ctx.accountId = 1;
        ctx.emoji = custom(QStringLiteral("blobcatcozy"), 1026533070955872337);
        const auto items = h.host->dispatchEmojiContextMenuItems(ctx);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("Copy Emoji Markdown"));
        QVERIFY(items[0].item.svg.contains("<svg"));
        h.host->dispatchEmojiContextMenuAction(items[0].pluginId, items[0].item.id, ctx);
        QCOMPARE(h.clipboard.text, QStringLiteral("<:blobcatcozy:1026533070955872337>"));
        QCOMPARE(h.ui.toasts.size(), 1);
        QCOMPARE(h.ui.toasts[0].first, QStringLiteral("Success! Copied emoji markdown."));

        h.host->settings(h.id)->setValue(QStringLiteral("copyUnicode"), false);
        ctx.emoji = unicode();
        h.host->dispatchEmojiContextMenuAction(items[0].pluginId, items[0].item.id, ctx);
        QCOMPARE(h.clipboard.text, QStringLiteral(":alien:"));
    }
};

QTEST_GUILESS_MAIN(TestCopyEmojiMarkdown)
#include "tst_copyemojimarkdown.moc"
