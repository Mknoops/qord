// Qord: the Settings pages made of preference rows, after Discord's User
// Settings. Each switch changes something in Qord right away unless it says
// it needs a restart; nothing here is for show.

#include "SettingsWindow.hpp"

#include "Core/AnimatedImageCache.hpp"
#include "Qord/Keybinds.hpp"
#include "Qord/Preferences.hpp"
#include "SettingsForm.hpp"
#include "UI/Qord/NotificationSound.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QUrl>

#ifdef ACHERON_HAVE_MINIAUDIO
#include "Core/Audio/AudioBackends.hpp"
#endif

namespace Acheron {
namespace UI {

using Qord::Keybinds;
namespace Pref = Qord::Pref;
using Qord::Preferences;

namespace {

QPushButton *restartButton(QWidget *parent)
{
    auto *button = new QPushButton(SettingsWindow::tr("Restart Qord"), parent);
    QObject::connect(button, &QPushButton::clicked, parent, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
        QCoreApplication::quit();
    });
    return button;
}

// Opening Qord when you log in: a desktop entry in the XDG autostart folder,
// or the Run key on Windows
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_WINDOWS)
constexpr bool AutostartSupported = true;
#else
constexpr bool AutostartSupported = false;
#endif

#ifdef Q_OS_WINDOWS
QSettings runKey()
{
    return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);
}
#else
QString autostartFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/autostart/io.github.mknoops.qord.desktop");
}

QString launchCommand()
{
    // An AppImage runs from a temporary mount; start the image itself
    const QByteArray appImage = qgetenv("APPIMAGE");
    const QString program = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : QString::fromLocal8Bit(appImage);
    return QLatin1Char('"') + program + QLatin1Char('"');
}
#endif

bool autostartEnabled()
{
#ifdef Q_OS_WINDOWS
    return runKey().contains(QStringLiteral("Qord"));
#else
    return QFile::exists(autostartFile());
#endif
}

bool setAutostart(bool on)
{
#ifdef Q_OS_WINDOWS
    QSettings key = runKey();
    if (on)
        key.setValue(QStringLiteral("Qord"),
                     QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"'));
    else
        key.remove(QStringLiteral("Qord"));
    key.sync();
    return key.status() == QSettings::NoError;
#else
    if (!on)
        return !QFile::exists(autostartFile()) || QFile::remove(autostartFile());
    QDir().mkpath(QFileInfo(autostartFile()).absolutePath());
    QFile file(autostartFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Qord\nExec=%1\nIcon=io.github.mknoops.qord\n"
                              "X-GNOME-Autostart-enabled=true\n")
                       .arg(launchCommand())
                       .toUtf8());
    return true;
#endif
}

} // namespace

SettingsForm *SettingsWindow::buildAccessibilityPage()
{
    auto *form = new SettingsForm(this);

    form->addSection(tr("Zoom"));
    form->addChoice(Pref::Zoom, tr("Zoom level"),
                    tr("Makes everything in Qord bigger or smaller. Takes effect when Qord restarts. Text alone "
                       "can be resized under Appearance."),
                    { { QStringLiteral("67%"), 67 },
                      { QStringLiteral("75%"), 75 },
                      { QStringLiteral("80%"), 80 },
                      { QStringLiteral("90%"), 90 },
                      { QStringLiteral("100%"), 100 },
                      { QStringLiteral("110%"), 110 },
                      { QStringLiteral("125%"), 125 },
                      { QStringLiteral("150%"), 150 },
                      { QStringLiteral("175%"), 175 },
                      { QStringLiteral("200%"), 200 } });
    form->addRow(QString(), tr("Restart to apply a new zoom level."), restartButton(form));

    form->addSection(tr("Motion"));
    form->addToggle(Pref::ReduceMotion, tr("Reduce motion"),
                    tr("Animated emoji and stickers stand still, whatever the Chat settings say."));

    form->addSection(tr("Readability"));
    form->addToggle(Pref::UnderlineLinks, tr("Always underline links"),
                    tr("Links in messages are underlined, not only coloured, so they stand out without colour."));
    form->addToggle(Pref::HighlightMentions, tr("Highlight messages that mention you"),
                    tr("A coloured background and bar on messages that mention you or a role you have."));
    form->finish();
    return form;
}

SettingsForm *SettingsWindow::buildVoicePage()
{
    auto *form = new SettingsForm(this);
#ifdef ACHERON_HAVE_MINIAUDIO
    form->addSection(tr("Audio"));
    auto *backend = new QComboBox;
    backend->addItem(tr("Automatic"), QString());
    for (const QString &name : Core::Audio::supportedAudioBackends())
        backend->addItem(name, name);
    const int current = backend->findData(Core::Audio::configuredAudioBackend());
    backend->setCurrentIndex(current >= 0 ? current : 0);
    connect(backend, qOverload<int>(&QComboBox::currentIndexChanged), backend,
            [backend](int index) { Core::Audio::setConfiguredAudioBackend(backend->itemData(index).toString()); });
    form->addRow(tr("Audio system"),
                 tr("How Qord talks to your speakers and microphone. Automatic picks your system's usual one. "
                    "Takes effect when Qord restarts."),
                 backend);
#endif
#ifdef ACHERON_HAVE_RNNOISE
    form->addSection(tr("Voice processing"));
    form->addToggle(Pref::NoiseSuppression, tr("Noise suppression"),
                    tr("Filters keyboard clatter, fans and other background noise out of your microphone."));
    form->addToggle(Pref::RnnoiseVad, tr("Detect speech with noise suppression"),
                    tr("Sends your voice only while you are speaking, judged by the noise filter rather than by "
                       "volume."));
#endif
    form->addNote(tr("Bitrate, codec and packet loss settings are in the voice window while you are in a call."));
    form->finish();
    return form;
}

SettingsForm *SettingsWindow::buildChatPage()
{
    auto *form = new SettingsForm(this);

    form->addSection(tr("Images, videos and link previews"));
    form->addToggle(Pref::ShowAttachmentMedia, tr("Show images and videos uploaded to Discord"),
                    tr("When off, they stay covered until you click them, like spoilers."));
    form->addToggle(Pref::ShowEmbeds, tr("Show link previews"),
                    tr("Previews under messages with links, and embeds from bots."));
    auto *linkMedia = form->addToggle(Pref::ShowLinkMedia, tr("Show images and videos from links"),
                                      tr("Pictures, GIFs and videos in link previews. When off, the link stays."));
    form->dependsOn(linkMedia, Pref::ShowEmbeds);

    form->addSection(tr("Emoji and stickers"));
    form->addToggle(Pref::AnimateEmoji, tr("Play animated emoji"));
    form->addToggle(Pref::AnimateStickers, tr("Play animated stickers"));
    form->addToggle(Pref::ShowReactions, tr("Show emoji reactions"), tr("The reactions under messages."));
    form->addToggle(Pref::ConvertEmoticons, tr("Convert emoticons to emoji"),
                    tr("Typing :) sends \U0001F642, <3 sends ❤️ and so on. Code stays as typed."));

    form->addSection(tr("Spoilers"));
    form->addToggle(Pref::RevealSpoilers, tr("Always show spoilers"),
                    tr("Show spoiler text and spoilered images without clicking them."));

    form->addSection(tr("Messages"));
    form->addChoice(Pref::TimeFormat, tr("Time format"), tr("How times next to messages are written."),
                    { { tr("Your system's"), QStringLiteral("system") },
                      { tr("12-hour (3:45 PM)"), QStringLiteral("12") },
                      { tr("24-hour (15:45)"), QStringLiteral("24") } });
    form->addChoice(Pref::GroupSpacing, tr("Space between message groups"),
                    tr("The gap above a new message group, where the name and picture are."),
                    { { tr("None"), 0 }, { tr("4 px"), 4 }, { tr("8 px"), 8 }, { tr("16 px"), 16 },
                      { tr("24 px"), 24 } });

    form->addSection(tr("Message box"));
    form->addToggle(Pref::CtrlEnterSends, tr("Send with Ctrl+Enter"),
                    tr("Enter starts a new line and Ctrl+Enter sends, for writing longer messages. When off, "
                       "Enter sends and Shift+Enter starts a new line."));
    form->finish();
    return form;
}

SettingsForm *SettingsWindow::buildNotificationsPage()
{
    auto *form = new SettingsForm(this);

    form->addSection(tr("Desktop notifications"),
                     tr("Muted servers and channels, Do Not Disturb and the conversation you are reading stay "
                        "quiet."));
    form->addToggle(Pref::NotifyDesktop, tr("Enable desktop notifications"),
                    tr("A notification when someone messages or mentions you."));
    form->addToggle(Pref::NotifyDms, tr("Direct messages"), tr("Every message in your direct messages."));
    form->addToggle(Pref::NotifyMentions, tr("Mentions"),
                    tr("Messages that mention you, @everyone or a role you have."));
    form->addToggle(Pref::NotifyShowContent, tr("Show message text"),
                    tr("When off, notifications only say who wrote, not what."));
    form->addToggle(Pref::NotifyFlash, tr("Flash the taskbar"),
                    tr("Draw your attention to Qord's window when a notification comes in."));

    form->addSection(tr("Sounds"));
    form->addToggle(Pref::NotifySound, tr("Message sound"), tr("A short chime for the same messages."));
    auto *volume = form->addSlider(Pref::NotifyVolume, tr("Volume"), QString(), 0, 100, 5, QStringLiteral("%"));
    form->dependsOn(volume, Pref::NotifySound);
    auto *player = new NotificationSound(form);
    auto *play = new QPushButton(tr("Play"));
    connect(play, &QPushButton::clicked, player, &NotificationSound::play);
    form->addRow(QString(), tr("Hear the message sound at this volume."), play);
    form->finish();
    return form;
}

SettingsForm *SettingsWindow::buildKeybindsPage()
{
    auto *form = new SettingsForm(this);
    form->addSection(tr("Keyboard shortcuts"),
                     tr("Click a shortcut and press the keys you want. Clear removes it; the arrow puts the default "
                        "back."));
    auto *warning = new QLabel;
    warning->setObjectName(QStringLiteral("SettingsHelp"));
    warning->setWordWrap(true);
    warning->hide();
    form->addWidget(warning);

    const auto nameOf = [](const QString &id) {
        for (const Keybinds::Action &action : Keybinds::actions())
            if (action.id == id)
                return action.name;
        return id;
    };
    for (const Keybinds::Action &action : Keybinds::actions()) {
        auto *box = new QWidget;
        auto *h = new QHBoxLayout(box);
        h->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QKeySequenceEdit(Keybinds::key(action.id), box);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        edit->setMaximumSequenceLength(1);
#endif
        edit->setFixedWidth(170);
        edit->setAccessibleName(action.name);
        auto *clear = new QToolButton(box);
        clear->setText(tr("Clear"));
        clear->setAutoRaise(false);
        auto *reset = new QToolButton(box);
        reset->setText(QStringLiteral("↺"));
        reset->setToolTip(tr("Default: %1")
                                  .arg(action.defaultKey.isEmpty() ? tr("none")
                                                                   : action.defaultKey.toString(QKeySequence::NativeText)));
        reset->setAutoRaise(false);
        h->addWidget(edit);
        h->addWidget(clear);
        h->addWidget(reset);
        form->addRow(action.name, action.description, box);

        const QString id = action.id;
        connect(edit, &QKeySequenceEdit::editingFinished, edit, [edit, id, warning, nameOf]() {
            QKeySequence key = edit->keySequence();
            if (key.count() > 1)
                key = QKeySequence(key[0]);
            const QString taken = Keybinds::conflict(key, id);
            if (!taken.isEmpty()) {
                warning->setText(SettingsWindow::tr("%1 is already %2. Pick another key or change that one first.")
                                         .arg(key.toString(QKeySequence::NativeText), nameOf(taken)));
                warning->show();
                edit->setKeySequence(Keybinds::key(id));
                return;
            }
            warning->hide();
            edit->setKeySequence(key);
            Keybinds::setKey(id, key);
        });
        connect(clear, &QToolButton::clicked, edit, [edit, id, warning]() {
            warning->hide();
            edit->clear();
            Keybinds::setKey(id, QKeySequence());
        });
        connect(reset, &QToolButton::clicked, edit, [edit, id, warning, nameOf]() {
            const QString taken = Keybinds::conflict(Keybinds::defaultKey(id), id);
            if (!taken.isEmpty()) {
                warning->setText(SettingsWindow::tr("The default key belongs to %1. Change that one first.")
                                         .arg(nameOf(taken)));
                warning->show();
                return;
            }
            warning->hide();
            Keybinds::reset(id);
            edit->setKeySequence(Keybinds::key(id));
        });
    }
    form->finish();
    return form;
}

SettingsForm *SettingsWindow::buildWindowPage()
{
    auto *form = new SettingsForm(this);

    form->addSection(tr("Startup"));
    if (AutostartSupported) {
        form->addSwitch(tr("Open Qord when you log in"), tr("Start Qord with your computer."), autostartEnabled(),
                        [form](bool on) {
                            if (!setAutostart(on))
                                QMessageBox::warning(form, SettingsWindow::tr("Open Qord when you log in"),
                                                     SettingsWindow::tr("Qord could not change your login items."));
                        });
    }
    const bool tray = QSystemTrayIcon::isSystemTrayAvailable();
    form->addToggle(Pref::StartMinimized, tr("Start minimized"),
                    tray ? tr("Qord starts in the system tray instead of opening its window.")
                         : tr("Qord starts minimized instead of opening its window."));

    form->addSection(tr("System tray"),
                     tray ? QString() : tr("Your desktop has no system tray, so these do nothing here."));
    auto *minimize = form->addToggle(Pref::MinimizeToTray, tr("Minimize to tray"),
                                     tr("Minimizing hides the window; click the tray icon to bring it back."));
    auto *close = form->addToggle(Pref::CloseToTray, tr("Close to tray"),
                                  tr("The close button keeps Qord running in the tray. Quit from the tray icon's "
                                     "menu."));
    minimize->setEnabled(tray);
    close->setEnabled(tray);

    form->addSection(tr("Window"));
    form->addToggle(Pref::ShowMenuBar, tr("Show the menu bar"),
                    tr("Qord's File and View menus above the window. Discord has none."));
    form->finish();
    return form;
}

SettingsForm *SettingsWindow::buildStreamerPage()
{
    auto *form = new SettingsForm(this);
    form->addSection(tr("Streamer mode"),
                     tr("For streaming or sharing your screen: hides what viewers should not see. A shortcut "
                        "for it can be set under Keybinds."));
    form->addToggle(Pref::Streamer, tr("Enable streamer mode"));
    auto *personal = form->addToggle(Pref::StreamerHidePersonal, tr("Hide personal information"),
                                     tr("Your custom status under your name, and where your devices logged in from."));
    auto *invites = form->addToggle(Pref::StreamerHideInvites, tr("Hide invite links"),
                                    tr("Invite codes in messages are blanked out, so viewers cannot join."));
    auto *notifications = form->addToggle(Pref::StreamerNoNotifications, tr("Disable notifications"),
                                          tr("No desktop notifications pop up on screen."));
    auto *sounds = form->addToggle(Pref::StreamerNoSounds, tr("Disable sounds"), tr("No message sounds."));
    for (QWidget *control : { static_cast<QWidget *>(personal), static_cast<QWidget *>(invites),
                              static_cast<QWidget *>(notifications), static_cast<QWidget *>(sounds) })
        form->dependsOn(control, Pref::Streamer);
    form->finish();
    return form;
}

SettingsForm *SettingsWindow::buildAdvancedPage()
{
    auto *form = new SettingsForm(this);

    form->addSection(tr("Developer"));
    form->addToggle(Pref::DeveloperMode, tr("Developer mode"),
                    tr("Adds Copy ID to the menus of messages, people, channels and servers, for bots and bug "
                       "reports."));

    form->addSection(tr("Memory"));
    using Core::AnimatedImageCache;
    const auto memoryLimit = [form](const QString &key, const AnimatedImageCache::MiBRange &range) {
        auto *spin = new QSpinBox;
        spin->setRange(range.min, range.max);
        spin->setSingleStep(8);
        spin->setSuffix(tr(" MB"));
        spin->setValue(QSettings().value(key, range.fallback).toInt());
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), form,
                [key](int megabytes) { QSettings().setValue(key, megabytes); });
        return spin;
    };
    QSpinBox *cacheLimit = memoryLimit(QStringLiteral("chat/animation_cache_mb"), AnimatedImageCache::CacheLimit);
    form->addRow(tr("Memory for animations"),
                 tr("The most memory animated emoji and stickers may use. When it is full, the animations you have "
                    "not seen for longest take a moment to move again. Raise it for busy channels, lower it to "
                    "save memory."),
                 cacheLimit);
    connect(cacheLimit, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::animationCacheLimitChanged);
    QSpinBox *sizeLimit = memoryLimit(QStringLiteral("chat/animation_max_mb"), AnimatedImageCache::AnimationLimit);
    form->addRow(tr("Largest single animation"),
                 tr("An animation that needs more plays at a lower resolution, or as a still picture."), sizeLimit);
    connect(sizeLimit, qOverload<int>(&QSpinBox::valueChanged), this, &SettingsWindow::animationSizeLimitChanged);
    form->addSwitch(tr("Keep the message cache in memory"),
                    tr("Faster, but messages are fetched again after every start. Takes effect when Qord restarts."),
                    QSettings().value(QStringLiteral("general/in_memory_cache"), false).toBool(),
                    [](bool on) { QSettings().setValue(QStringLiteral("general/in_memory_cache"), on); });

#ifdef Q_OS_WINDOWS
    auto *engine = new QComboBox;
    engine->addItem(tr("DirectWrite (default)"), QStringLiteral("directwrite"));
    engine->addItem(tr("FreeType"), QStringLiteral("freetype"));
    engine->setCurrentIndex(qMax(0, engine->findData(QSettings().value("general/font_engine", "directwrite").toString())));
    connect(engine, qOverload<int>(&QComboBox::currentIndexChanged), engine,
            [engine](int index) { QSettings().setValue("general/font_engine", engine->itemData(index).toString()); });
    form->addRow(tr("Font engine"), tr("How text is drawn. Takes effect when Qord restarts."), engine);
#endif

    form->addSection(tr("Files"));
    auto *open = new QPushButton(tr("Open folder"));
    connect(open, &QPushButton::clicked, open, []() {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    form->addRow(tr("Qord's data folder"), tr("Logs, the message cache and plugin data."), open);
    form->finish();
    return form;
}

} // namespace UI
} // namespace Acheron
