#include "PluginTestUtils.hpp"

#include "Plugins/Manifest.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/PluginHost.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <stdexcept>

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {

QJsonObject baseManifest(const QString &id)
{
    QJsonObject m;
    m["id"] = id;
    m["equicordName"] = id;
    m["equicordPath"] = "src/plugins/" + id;
    m["name"] = id;
    m["description"] = "test plugin";
    m["categories"] = QJsonArray { "utility" };
    m["tags"] = QJsonArray { "Utility" };
    m["authors"] = QJsonArray { "someone" };
    m["permissions"] = QJsonArray {};
    m["hooks"] = QJsonArray {};
    m["settings"] = QJsonArray {};
    m["enabledByDefault"] = false;
    m["target"] = "";
    m["dependencies"] = QJsonArray {};
    m["commands"] = QJsonArray {};
    return m;
}

QByteArray toBytes(const QJsonObject &o)
{
    return QJsonDocument(o).toJson();
}

// Counts lifecycle events across instances.
struct Counters
{
    int constructed = 0;
    int destroyed = 0;
    int started = 0;
    int stopped = 0;
    int sends = 0;
    QStringList order;
};

class FakePlugin : public Plugin
{
public:
    FakePlugin(Counters *c, Hooks h, QString tag, bool throwOnStart = false, bool throwOnSend = false)
        : c(c), h(h), tag(std::move(tag)), throwOnStart(throwOnStart), throwOnSend(throwOnSend)
    {
        ++c->constructed;
    }
    ~FakePlugin() override { ++c->destroyed; }
    Hooks hooks() const override { return h; }
    void start(PluginContext &ctx) override
    {
        if (throwOnStart)
            throw std::runtime_error("boom");
        context = &ctx;
        ++c->started;
    }
    void stop() override { ++c->stopped; }
    void onBeforeSend(OutgoingMessage &m) override
    {
        ++c->sends;
        c->order.append(tag);
        if (throwOnSend)
            throw std::runtime_error("send boom");
        m.content += tag;
    }
    void onBeforeEdit(OutgoingMessage &m) override { m.content += "e" + tag; }
    std::optional<bool> shouldMentionOnReply(const ReplyContext &r) override
    {
        Q_UNUSED(r);
        return answer;
    }
    QString transformCopiedMessageLink(const QString &url) override { return url + tag; }
    void transformIncomingContent(IncomingMessage &m) override { m.content = m.content.toUpper(); }

    Counters *c;
    Hooks h;
    QString tag;
    bool throwOnStart;
    bool throwOnSend;
    std::optional<bool> answer;
    PluginContext *context = nullptr;
};

// Builds registries of fake plugins whose manifests live in a temp dir.
class Fixture
{
public:
    void add(const QJsonObject &manifest, std::function<std::unique_ptr<Plugin>()> factory)
    {
        const QString path = dir.filePath(manifest["id"].toString() + ".json");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot write %s", qPrintable(path));
        f.write(toBytes(manifest));
        f.close();
        registry.add({ path, std::move(factory) });
    }

    std::unique_ptr<PluginHost> makeHost()
    {
        Services s;
        s.network = &network;
        s.members = &members;
        auto host = std::make_unique<PluginHost>(registry, &storage, s, dir.filePath("data"));
        host->load();
        return host;
    }

    QTemporaryDir dir;
    Registry registry;
    MemoryStorage storage;
    FakeNetwork network;
    FakeMembers members;
};

QJsonObject sendManifest(const QString &id)
{
    QJsonObject m = baseManifest(id);
    m["permissions"] = QJsonArray { "modify_outgoing_messages" };
    m["hooks"] = QJsonArray { "onBeforeSend" };
    return m;
}

} // namespace

class TestPluginHostCore : public QObject
{
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- manifests
    void manifestParsesValid()
    {
        QJsonObject m = sendManifest("ok");
        QJsonObject setting { { "key", "flag" }, { "type", "bool" }, { "default", true }, { "label", "Flag" } };
        m["settings"] = QJsonArray { setting };
        PluginManifest out;
        QString error;
        QVERIFY2(PluginManifest::parse(toBytes(m), out, error), qPrintable(error));
        QCOMPARE(out.id, QStringLiteral("ok"));
        QVERIFY(out.permissions.testFlag(Permission::ModifyOutgoingMessages));
        QVERIFY(out.hooks.testFlag(Hook::BeforeSend));
        QCOMPARE(out.settings.size(), 1);
        QCOMPARE(out.settings[0].type, SettingType::Bool);
    }

    void manifestRejects_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expected");

        QJsonObject m = baseManifest("x");
        m.remove("authors");
        QTest::newRow("missing authors") << toBytes(m) << "authors";

        m = baseManifest("x");
        m["authors"] = QJsonArray {};
        QTest::newRow("empty authors") << toBytes(m) << "credit";

        m = baseManifest("Bad-Id");
        QTest::newRow("bad id") << toBytes(m) << "invalid plugin id";

        m = baseManifest("x");
        m["permissions"] = QJsonArray { "steal_tokens" };
        QTest::newRow("unknown permission") << toBytes(m) << "unknown permission";

        m = baseManifest("x");
        m["hooks"] = QJsonArray { "onBeforeSend" };
        QTest::newRow("hook without permission") << toBytes(m) << "needs permission";

        m = baseManifest("x");
        m["hooks"] = QJsonArray { "onRender" };
        QTest::newRow("unknown hook") << toBytes(m) << "unknown hook";

        m = baseManifest("x");
        m["settings"] = QJsonArray { QJsonObject { { "key", "n" }, { "type", "int" }, { "default", "five" }, { "label", "N" } } };
        QTest::newRow("bad default") << toBytes(m) << "default of setting n is invalid";

        m = baseManifest("x");
        m["settings"] = QJsonArray { QJsonObject { { "key", "c" }, { "type", "choice" }, { "default", "z" }, { "label", "C" },
                                                   { "choices", QJsonArray { QJsonObject { { "value", "a" }, { "label", "A" } } } } } };
        QTest::newRow("choice default") << toBytes(m) << "unknown choice";

        m = baseManifest("x");
        m["settings"] = QJsonArray { QJsonObject { { "key", "s" }, { "type", "string" }, { "label", "S" } } };
        QTest::newRow("no default") << toBytes(m) << "no default";

        m = baseManifest("x");
        QJsonObject s { { "key", "s" }, { "type", "string" }, { "default", "" }, { "label", "S" } };
        m["settings"] = QJsonArray { s, s };
        QTest::newRow("duplicate key") << toBytes(m) << "duplicate setting key";

        QTest::newRow("not json") << QByteArray("{nope") << "";
        QTest::newRow("not object") << QByteArray("[]") << "must be a JSON object";
    }

    void manifestRejects()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, expected);
        PluginManifest out;
        QString error;
        QVERIFY(!PluginManifest::parse(json, out, error));
        QVERIFY2(error.contains(expected), qPrintable(error));
    }

    // ----------------------------------------------------------- settings
    void settingsDefaultsValidationAndPersistence()
    {
        SettingsSchema schema;
        SettingSpec n;
        n.key = "n";
        n.type = SettingType::Int;
        n.defaultValue = 3;
        n.minimum = 1;
        n.maximum = 10;
        schema.append(n);
        SettingSpec c;
        c.key = "c";
        c.type = SettingType::Choice;
        c.defaultValue = "a";
        c.choices = { { "a", "A" }, { "b", "B" } };
        schema.append(c);
        SettingSpec l;
        l.key = "l";
        l.type = SettingType::StringList;
        l.defaultValue = QJsonArray { "x" };
        schema.append(l);

        MemoryStorage storage;
        PluginSettings settings("p", schema, &storage);
        settings.load();
        QCOMPARE(settings.intValue("n"), 3);
        QCOMPARE(settings.stringValue("c"), QStringLiteral("a"));
        QCOMPARE(settings.stringListValue("l"), QStringList { "x" });

        QStringList changed;
        settings.setChangeCallback([&](const QString &k) { changed.append(k); });
        QVERIFY(!settings.setValue("n", 11).isEmpty());
        QVERIFY(!settings.setValue("n", 2.5).isEmpty());
        QVERIFY(!settings.setValue("n", "4").isEmpty());
        QVERIFY(!settings.setValue("c", "z").isEmpty());
        QVERIFY(!settings.setValue("l", QJsonArray { 1 }).isEmpty());
        QVERIFY(!settings.setValue("missing", true).isEmpty());
        QVERIFY(changed.isEmpty());

        QVERIFY(settings.setValue("n", 7).isEmpty());
        QVERIFY(settings.setValue("c", "b").isEmpty());
        QVERIFY(settings.setValue("n", 7).isEmpty()); // unchanged: no callback
        QCOMPARE(changed, (QStringList { "n", "c" }));

        PluginSettings reloaded("p", schema, &storage);
        reloaded.load();
        QCOMPARE(reloaded.intValue("n"), 7);
        QCOMPARE(reloaded.stringValue("c"), QStringLiteral("b"));

        reloaded.reset("n");
        QCOMPARE(reloaded.intValue("n"), 3);
    }

    void settingsIgnoreCorruptAndInvalidStoredValues()
    {
        SettingsSchema schema;
        SettingSpec b;
        b.key = "b";
        b.type = SettingType::Bool;
        b.defaultValue = true;
        schema.append(b);
        MemoryStorage storage;
        storage.write("settings-p", "{\"b\": \"yes\", \"unknown\": 1}");
        PluginSettings s("p", schema, &storage);
        s.load();
        QCOMPARE(s.boolValue("b"), true); // invalid stored value falls back
        storage.write("settings-p", "not json");
        s.load();
        QCOMPARE(s.boolValue("b"), true);
    }

    // --------------------------------------------------------------- host
    void disabledPluginsAreNeverConstructed()
    {
        Fixture fx;
        Counters c;
        fx.add(sendManifest("a"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        auto host = fx.makeHost();
        QCOMPARE(c.constructed, 0);
        QVERIFY(!host->hasHook(Hook::BeforeSend));
        QVERIFY(!host->isEnabled("a"));

        OutgoingMessage m;
        m.content = "x";
        host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("x"));
    }

    void enableStartsAndDisableDestroys()
    {
        Fixture fx;
        Counters c;
        fx.add(sendManifest("a"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        auto host = fx.makeHost();
        host->setGranted("a", Permission::ModifyOutgoingMessages);
        QSignalSpy spy(host.get(), &PluginHost::pluginStateChanged);
        QVERIFY(host->setEnabled("a", true));
        QCOMPARE(c.constructed, 1);
        QCOMPARE(c.started, 1);
        QVERIFY(host->isRunning("a"));
        QVERIFY(host->hasHook(Hook::BeforeSend));
        QCOMPARE(spy.count(), 1);

        QVERIFY(host->setEnabled("a", false));
        QCOMPARE(c.stopped, 1);
        QCOMPARE(c.destroyed, 1);
        QVERIFY(!host->hasHook(Hook::BeforeSend));
    }

    void hooksNeedGrantedPermission()
    {
        Fixture fx;
        Counters c;
        fx.add(sendManifest("a"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        auto host = fx.makeHost();
        QVERIFY(host->setEnabled("a", true));
        QVERIFY(host->isRunning("a"));
        // Running, but the permission is not granted: not dispatched
        QVERIFY(!host->hasHook(Hook::BeforeSend));
        OutgoingMessage m;
        host->dispatchBeforeSend(m);
        QCOMPARE(c.sends, 0);

        host->setGranted("a", Permission::ModifyOutgoingMessages);
        QVERIFY(host->hasHook(Hook::BeforeSend));
        host->dispatchBeforeSend(m);
        QCOMPARE(c.sends, 1);

        // Grants are capped to what the manifest declares
        host->setGranted("a", Permission::ModifyOutgoingMessages | Permission::Network);
        QCOMPARE(host->granted("a"), Permissions(Permission::ModifyOutgoingMessages));
    }

    void contextOnlyExposesGrantedServices()
    {
        Fixture fx;
        Counters c;
        QJsonObject m = sendManifest("a");
        m["permissions"] = QJsonArray { "modify_outgoing_messages", "network", "read_guild_members" };
        FakePlugin *instance = nullptr;
        fx.add(m, [&] {
            auto p = std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a");
            instance = p.get();
            return p;
        });
        auto host = fx.makeHost();
        host->setGranted("a", Permission::ModifyOutgoingMessages);
        QVERIFY(host->setEnabled("a", true));
        QVERIFY(instance->context);
        QVERIFY(!instance->context->network());
        QVERIFY(!instance->context->members());

        host->setGranted("a", Permission::ModifyOutgoingMessages | Permission::Network | Permission::ReadGuildMembers);
        QVERIFY(instance->context); // restarted with a new context
        QVERIFY(instance->context->network());
        QVERIFY(instance->context->members());
    }

    void stoppingCancelsOutstandingRequests()
    {
        Fixture fx;
        Counters c;
        QJsonObject m = baseManifest("a");
        m["permissions"] = QJsonArray { "network" };
        FakePlugin *instance = nullptr;
        fx.add(m, [&] {
            auto p = std::make_unique<FakePlugin>(&c, Hooks(), "a");
            instance = p.get();
            return p;
        });
        auto host = fx.makeHost();
        host->setGranted("a", Permission::Network);
        QVERIFY(host->setEnabled("a", true));
        bool called = false;
        instance->context->network()->get(QUrl("https://example.com/"), [&](const NetworkService::Reply &) { called = true; });
        QCOMPARE(fx.network.requests.size(), 1);
        QVERIFY(host->setEnabled("a", false));
        QCOMPARE(fx.network.cancelled, 1);
        QVERIFY(fx.network.requests.isEmpty());
        QVERIFY(!called);
    }

    void hooksMustMatchManifest()
    {
        Fixture fx;
        Counters c;
        // Manifest declares no hooks, plugin implements one: refused
        QJsonObject m = baseManifest("a");
        m["permissions"] = QJsonArray { "modify_outgoing_messages" };
        fx.add(m, [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        auto host = fx.makeHost();
        QString error;
        QVERIFY(!host->setEnabled("a", true, &error));
        QVERIFY2(error.contains("hooks"), qPrintable(error));
        QVERIFY(!host->isRunning("a"));
        QVERIFY(!host->isEnabled("a"));
        QCOMPARE(c.destroyed, c.constructed);
    }

    void brokenManifestIsReportedNotFatal()
    {
        Fixture fx;
        Counters c;
        QJsonObject bad = baseManifest("bad");
        bad.remove("hooks");
        fx.add(bad, [&] { return std::make_unique<FakePlugin>(&c, Hooks(), "bad"); });
        fx.add(sendManifest("good"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "g"); });
        auto host = fx.makeHost();
        QCOMPARE(host->pluginIds().size(), 2);
        QVERIFY(host->info("good"));
        QVERIFY(host->info("good")->loadError.isEmpty());
        const QString badId = fx.dir.filePath("bad.json");
        QVERIFY(host->info(badId));
        QVERIFY(!host->info(badId)->loadError.isEmpty());
        QVERIFY(!host->setEnabled(badId, true));
        QCOMPARE(c.constructed, 0);
    }

    void startExceptionIsContained()
    {
        Fixture fx;
        Counters c;
        fx.add(sendManifest("a"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a", true); });
        auto host = fx.makeHost();
        QString error;
        QVERIFY(!host->setEnabled("a", true, &error));
        QVERIFY(error.contains("failed to start"));
        QVERIFY(!host->isRunning("a"));
    }

    void dispatchOrderAndExceptions()
    {
        Fixture fx;
        Counters c;
        fx.add(sendManifest("b"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "B", false, true); });
        fx.add(sendManifest("a"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "A"); });
        fx.add(sendManifest("c"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "C"); });
        auto host = fx.makeHost();
        for (const char *id : { "a", "b", "c" }) {
            host->setGranted(id, Permission::ModifyOutgoingMessages);
            QVERIFY(host->setEnabled(id, true));
        }
        QCOMPARE(host->pluginsForHook(Hook::BeforeSend), (QStringList { "a", "b", "c" }));
        OutgoingMessage m;
        m.content = "x";
        host->dispatchBeforeSend(m);
        // b throws after being called; a and c still apply, in id order
        QCOMPARE(c.order, (QStringList { "A", "B", "C" }));
        QCOMPARE(m.content, QStringLiteral("xAC"));
    }

    void otherHooksDispatch()
    {
        Fixture fx;
        Counters c;
        QJsonObject m = baseManifest("a");
        m["permissions"] = QJsonArray { "modify_outgoing_messages", "reply_mentions", "modify_message_links",
                                        "modify_displayed_messages" };
        m["hooks"] = QJsonArray { "onBeforeEdit", "shouldMentionOnReply", "transformCopiedMessageLink",
                                  "transformIncomingContent" };
        FakePlugin *instance = nullptr;
        const Hooks hooks = Hook::BeforeEdit | Hook::ShouldMentionOnReply | Hook::TransformCopiedMessageLink |
                Hook::TransformIncomingContent;
        fx.add(m, [&] {
            auto p = std::make_unique<FakePlugin>(&c, hooks, "!");
            instance = p.get();
            return p;
        });
        auto host = fx.makeHost();

        ReplyContext reply;
        QCOMPARE(host->dispatchShouldMentionOnReply(reply), true); // Discord default
        reply.shiftHeld = true;
        QCOMPARE(host->dispatchShouldMentionOnReply(reply), false);

        host->setGranted("a", host->info("a")->manifest.permissions);
        QSignalSpy display(host.get(), &PluginHost::displayTransformsChanged);
        QVERIFY(host->setEnabled("a", true));
        QCOMPARE(display.count(), 1);

        OutgoingMessage e;
        e.content = "x";
        host->dispatchBeforeEdit(e);
        QCOMPARE(e.content, QStringLiteral("xe!"));

        QCOMPARE(host->dispatchShouldMentionOnReply(reply), false); // no opinion yet -> default
        instance->answer = true;
        QCOMPARE(host->dispatchShouldMentionOnReply(reply), true);

        QCOMPARE(host->dispatchTransformCopiedMessageLink("u"), QStringLiteral("u!"));

        IncomingMessage in;
        in.content = "hi";
        host->dispatchTransformIncomingContent(in);
        QCOMPARE(in.content, QStringLiteral("HI"));
    }

    void dependenciesEnableAndDisableTogether()
    {
        Fixture fx;
        Counters c;
        QJsonObject child = sendManifest("child");
        child["dependencies"] = QJsonArray { "base" };
        fx.add(child, [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "c"); });
        fx.add(sendManifest("base"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "b"); });
        auto host = fx.makeHost();
        QVERIFY(host->setEnabled("child", true));
        QVERIFY(host->isRunning("base"));
        QVERIFY(host->isEnabled("base"));
        QVERIFY(host->setEnabled("base", false));
        QVERIFY(!host->isRunning("child"));
        QVERIFY(!host->isEnabled("child"));
        QCOMPARE(c.constructed, c.destroyed);
    }

    void dependencyProblemsAreErrors()
    {
        Fixture fx;
        Counters c;
        QJsonObject a = sendManifest("a");
        a["dependencies"] = QJsonArray { "b" };
        QJsonObject b = sendManifest("b");
        b["dependencies"] = QJsonArray { "a" };
        QJsonObject d = sendManifest("d");
        d["dependencies"] = QJsonArray { "missing" };
        fx.add(a, [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        fx.add(b, [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "b"); });
        fx.add(d, [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "d"); });
        auto host = fx.makeHost();
        QString error;
        QVERIFY(!host->setEnabled("a", true, &error));
        QVERIFY(error.contains("cycle"));
        QVERIFY(!host->setEnabled("d", true, &error));
        QVERIFY(error.contains("unknown plugin"));
        QCOMPARE(c.constructed, 0);
    }

    void failedDependencyStartRollsBack()
    {
        Fixture fx;
        Counters c;
        QJsonObject child = sendManifest("child");
        child["dependencies"] = QJsonArray { "base" };
        fx.add(child, [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "c", true); });
        fx.add(sendManifest("base"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "b"); });
        auto host = fx.makeHost();
        QVERIFY(!host->setEnabled("child", true));
        QVERIFY(!host->isRunning("base"));
        QVERIFY(!host->isEnabled("base"));
    }

    void statePersistsAcrossHosts()
    {
        Fixture fx;
        Counters c;
        fx.add(sendManifest("a"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        {
            auto host = fx.makeHost();
            host->setGranted("a", Permission::ModifyOutgoingMessages);
            QVERIFY(host->setEnabled("a", true));
        }
        QCOMPARE(c.stopped, 1); // host destruction stops plugins
        auto host = fx.makeHost();
        QVERIFY(host->isRunning("a"));
        QCOMPARE(host->granted("a"), Permissions(Permission::ModifyOutgoingMessages));
        QVERIFY(host->hasHook(Hook::BeforeSend));
    }

    void enabledByDefaultGetsDeclaredGrantsOnFirstRun()
    {
        Fixture fx;
        Counters c;
        QJsonObject m = sendManifest("a");
        m["enabledByDefault"] = true;
        fx.add(m, [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        auto host = fx.makeHost();
        QVERIFY(host->isRunning("a"));
        QVERIFY(host->hasHook(Hook::BeforeSend));

        // ...but a user's choice to disable it sticks
        QVERIFY(host->setEnabled("a", false));
        auto again = fx.makeHost();
        QVERIFY(!again->isRunning("a"));
    }

    void settingsChangesReachRunningPlugin()
    {
        Fixture fx;
        Counters c;
        QJsonObject m = baseManifest("a");
        m["permissions"] = QJsonArray { "modify_displayed_messages" };
        m["hooks"] = QJsonArray { "transformIncomingContent" };
        m["settings"] = QJsonArray { QJsonObject { { "key", "on" }, { "type", "bool" }, { "default", false }, { "label", "On" } } };
        fx.add(m, [&] { return std::make_unique<FakePlugin>(&c, Hook::TransformIncomingContent, "a"); });
        auto host = fx.makeHost();
        QVERIFY(host->settings("a")); // available while disabled
        QVERIFY(host->settings("a")->setValue("on", true).isEmpty());
        host->setGranted("a", Permission::ModifyDisplayedMessages);
        QVERIFY(host->setEnabled("a", true));
        QSignalSpy display(host.get(), &PluginHost::displayTransformsChanged);
        QVERIFY(host->settings("a")->setValue("on", false).isEmpty());
        QCOMPARE(display.count(), 1);
    }

    void unknownPlugin()
    {
        Fixture fx;
        auto host = fx.makeHost();
        QString error;
        QVERIFY(!host->setEnabled("nope", true, &error));
        QVERIFY(!host->info("nope"));
        QVERIFY(!host->settings("nope"));
    }

    void globalInstance()
    {
        QVERIFY(!PluginHost::instance());
        {
            Fixture fx;
            auto host = fx.makeHost();
            PluginHost::setInstance(host.get());
            QCOMPARE(PluginHost::instance(), host.get());
        }
        QVERIFY(!PluginHost::instance()); // cleared when that host dies
    }

    // Per-message cost with nothing listening: one branch.
    void benchmarkIdleDispatch()
    {
        Fixture fx;
        Counters c;
        fx.add(sendManifest("a"), [&] { return std::make_unique<FakePlugin>(&c, Hook::BeforeSend, "a"); });
        auto host = fx.makeHost();
        IncomingMessage in;
        in.content = QStringLiteral("hello world");
        QBENCHMARK {
            if (host->hasHook(Hook::TransformIncomingContent))
                host->dispatchTransformIncomingContent(in);
        }
    }
};

QTEST_GUILESS_MAIN(TestPluginHostCore)
#include "tst_host_core.moc"
