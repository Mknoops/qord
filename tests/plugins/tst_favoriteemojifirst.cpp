#include "PluginTestUtils.hpp"

#include "FavoriteEmojiFirst.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_favoriteemojifirst(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
EmojiInfo unicode(const QString &name, const QString &surrogates, bool favorite = false)
{
    EmojiInfo e;
    e.key = name;
    e.name = name;
    e.surrogates = surrogates;
    e.favorite = favorite;
    return e;
}

EmojiInfo custom(Id id, const QString &name, bool favorite = false, bool animated = false)
{
    EmojiInfo e;
    e.key = QString::number(id);
    e.name = name;
    e.customId = id;
    e.favorite = favorite;
    e.animated = animated;
    return e;
}

QStringList keys(const QList<EmojiInfo> &list)
{
    QStringList out;
    for (const auto &e : list)
        out.append(e.key);
    return out;
}

QStringList sorted(SinglePluginHost &h, const QString &query, QList<EmojiInfo> results)
{
    EmojiAutocompleteContext ctx;
    ctx.query = query;
    ctx.results = std::move(results);
    h.host->dispatchSortEmojiSuggestions(ctx);
    return keys(ctx.results);
}

void setAlias(SinglePluginHost &h, const EmojiInfo &emoji, const QString &alias)
{
    EmojiContext ctx;
    ctx.emoji = emoji;
    h.host->dispatchEmojiContextMenuAction(h.id, QStringLiteral("vc-emoji-alias-set"), ctx);
    QVERIFY(h.ui.pendingPrompt);
    h.ui.pendingPrompt(alias);
    h.ui.pendingPrompt = nullptr;
}
} // namespace

class TestFavoriteEmojiFirst : public QObject
{
    Q_OBJECT

private slots:
    void favoritesFirst()
    {
        SinglePluginHost h(qord_register_favoriteemojifirst);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(sorted(h, QStringLiteral("smi"),
                        { unicode("smile", "😄"), unicode("smiley", "😃", true), custom(5, "smirk"),
                          custom(6, "smile_cat", true) }),
                 (QStringList { "smiley", "6", "smile", "5" }));
    }

    void aliases()
    {
        SinglePluginHost h(qord_register_favoriteemojifirst);
        QVERIFY(h.enableWithAllPermissions());
        const EmojiInfo sob = unicode("sob", "😭");
        const EmojiInfo blob = custom(42, "blobcry", false, true);

        EmojiContext ctx;
        ctx.emoji = sob;
        QCOMPARE(h.host->dispatchEmojiContextMenuItems(ctx).value(0).item.label, QStringLiteral("Set alias"));
        setAlias(h, sob, QStringLiteral(":Cry:"));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Alias set for 😭."));
        QCOMPARE(h.host->dispatchEmojiContextMenuItems(ctx).value(0).item.label, QStringLiteral("Edit alias"));
        QCOMPARE(h.ui.promptInitial, QString());

        // Validation and duplicates
        setAlias(h, blob, QStringLiteral("x"));
        QVERIFY(h.ui.toasts.last().first.startsWith(QStringLiteral("Alias must be 2-32 characters")));
        setAlias(h, blob, QStringLiteral("cry"));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Duplicate alias"));
        setAlias(h, blob, QStringLiteral("sad"));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Alias set for :blobcry:."));
        // One alias per emoji: renaming replaces it
        setAlias(h, blob, QStringLiteral("sadder"));
        auto map = FavoriteEmojiFirstPlugin::aliases(*h.host->settings(h.id));
        QCOMPARE(map.keys(), (QStringList { "cry", "sadder" }));

        // An exact alias first, prefix aliases after favorites; aliased emojis
        // whose names don't match are added
        QCOMPARE(sorted(h, QStringLiteral("cry"), { unicode("crying_cat", "😿", true), unicode("cry", "😢") }),
                 (QStringList { "sob", "crying_cat", "cry" }));
        QCOMPARE(sorted(h, QStringLiteral("sa"), { unicode("sake", "🍶") }), (QStringList { "42", "sake" }));
        QCOMPARE(sorted(h, QStringLiteral(":"), { unicode("sake", "🍶") }), QStringList { "sake" });

        // Sending
        OutgoingMessage out;
        out.content = QStringLiteral("so :CRY: and :sadder: but :nope: and :ok:");
        h.host->dispatchBeforeSend(out);
        QCOMPARE(out.content, QStringLiteral("so 😭 and <a:blobcry:42> but :nope: and :ok:"));
    }
};

QTEST_GUILESS_MAIN(TestFavoriteEmojiFirst)
#include "tst_favoriteemojifirst.moc"
