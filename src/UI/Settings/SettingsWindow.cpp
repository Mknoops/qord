#include "SettingsWindow.hpp"

#include "AppearancePage.hpp"
#include "PluginsPage.hpp"
#include "SettingsForm.hpp"
#include "UI/Qord/DevicesPage.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

SettingsWindow::SettingsWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Settings"));
    setObjectName(QStringLiteral("SettingsWindow"));
    setProperty("qordForm", true); // Discord's controls (Core/Theme/Stylesheet.cpp)
    resize(1040, 720);

    setupUi();
}

void SettingsWindow::showPage(QWidget *page)
{
    for (int i = 0; i < categoryList->count(); ++i)
        if (categoryList->item(i)->data(Qt::UserRole).toInt() == pages->indexOf(page)
            && categoryList->item(i)->flags() & Qt::ItemIsEnabled) {
            categoryList->setCurrentRow(i);
            return;
        }
}

void SettingsWindow::showPlugin(const QString &pluginId)
{
    showPage(pluginsPage);
    pluginsPage->selectPlugin(pluginId);
}

void SettingsWindow::setInstance(Core::ClientInstance *instance)
{
    devicesPage->setInstance(instance);
}

void SettingsWindow::showDevices()
{
    showPage(devicesPage);
}

void SettingsWindow::filterPages(const QString &query)
{
    // Pages whose name or settings mention the words; headers stay while
    // a page under them does
    const QStringList terms = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto matches = [&terms](const QStringList &haystack) {
        for (const QString &term : terms) {
            bool found = false;
            for (const QString &text : haystack)
                if (text.contains(term, Qt::CaseInsensitive)) {
                    found = true;
                    break;
                }
            if (!found)
                return false;
        }
        return true;
    };
    int header = -1;
    bool headerUsed = false;
    int firstMatch = -1;
    for (int i = 0; i < categoryList->count(); ++i) {
        QListWidgetItem *item = categoryList->item(i);
        if (!(item->flags() & Qt::ItemIsEnabled)) {
            if (header >= 0)
                categoryList->item(header)->setHidden(!headerUsed);
            header = i;
            headerUsed = false;
            continue;
        }
        QWidget *page = pages->widget(item->data(Qt::UserRole).toInt());
        const bool shown = matches(QStringList{ item->text() } + pageWords.value(page));
        item->setHidden(!shown);
        headerUsed |= shown;
        if (shown && firstMatch < 0)
            firstMatch = i;
    }
    if (header >= 0)
        categoryList->item(header)->setHidden(!headerUsed);
    QListWidgetItem *current = categoryList->currentItem();
    if (firstMatch >= 0 && (!current || current->isHidden()))
        categoryList->setCurrentRow(firstMatch);
}

void SettingsWindow::setupUi()
{
    // Discord's settings: a sidebar of grouped pages, the page on the right
    // under its title, and an ESC button to close
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setFixedWidth(232);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 40, 8, 16);
    sidebarLayout->setSpacing(8);
    search = new QLineEdit(sidebar);
    search->setObjectName(QStringLiteral("SettingsSearch"));
    search->setPlaceholderText(tr("Search"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, &SettingsWindow::filterPages);
    sidebarLayout->addWidget(search);
    categoryList = new QListWidget(sidebar);
    categoryList->setObjectName(QStringLiteral("SettingsNav"));
    categoryList->setFrameShape(QFrame::NoFrame);
    categoryList->setFocusPolicy(Qt::NoFocus);
    categoryList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sidebarLayout->addWidget(categoryList);

    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("SettingsContent"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(40, 36, 24, 0);
    contentLayout->setSpacing(16);
    auto *titleRow = new QHBoxLayout;
    pageTitle = new QLabel(content);
    pageTitle->setObjectName(QStringLiteral("SettingsPageTitle"));
    titleRow->addWidget(pageTitle, 1);
    auto *close = new QToolButton(content);
    close->setObjectName(QStringLiteral("SettingsClose"));
    close->setText(QStringLiteral("\u2715"));
    close->setToolTip(tr("Close (Esc)"));
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(36, 36);
    connect(close, &QToolButton::clicked, this, &QWidget::close);
    auto *closeColumn = new QVBoxLayout;
    closeColumn->setSpacing(4);
    closeColumn->addWidget(close, 0, Qt::AlignHCenter);
    auto *escLabel = new QLabel(QStringLiteral("ESC"), content);
    escLabel->setObjectName(QStringLiteral("SettingsEsc"));
    closeColumn->addWidget(escLabel, 0, Qt::AlignHCenter);
    titleRow->addLayout(closeColumn);
    contentLayout->addLayout(titleRow);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    pages = new QStackedWidget(content);
    contentLayout->addWidget(pages, 1);

    auto addHeader = [this](const QString &name) {
        auto *item = new QListWidgetItem(name, categoryList);
        item->setFlags(Qt::NoItemFlags);
        QFont font = categoryList->font();
        font.setPixelSize(12);
        font.setWeight(QFont::DemiBold);
        item->setFont(font);
        item->setSizeHint(QSize(0, categoryList->count() > 1 ? 40 : 26));
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);
    };
    auto addPage = [this](const QString &name, QWidget *page) {
        auto *item = new QListWidgetItem(name, categoryList);
        item->setData(Qt::UserRole, pages->addWidget(page));
        item->setSizeHint(QSize(0, 34));
        if (auto *form = qobject_cast<SettingsForm *>(page))
            pageWords[page] += form->searchText();
    };

    // Grouped as Discord groups them; every switch here does something in Qord
    addHeader(tr("User settings"));
    devicesPage = new DevicesPage(this);
    addPage(tr("Devices"), devicesPage);

    addHeader(tr("App settings"));
    auto *appearance = new AppearancePage(this);
    addPage(tr("Appearance"), appearance);
    pageWords.insert(appearance, { tr("theme"), tr("colors"), tr("font"), tr("channel list"), tr("role colors") });
    connect(appearance, &AppearancePage::channelListModeChanged, this, &SettingsWindow::channelListModeChanged);
    connect(appearance, &AppearancePage::channelListIndentChanged, this, &SettingsWindow::channelListIndentChanged);
    connect(appearance, &AppearancePage::roleStyleChanged, this, &SettingsWindow::roleStyleChanged);
    addPage(tr("Accessibility"), buildAccessibilityPage());
    addPage(tr("Voice & video"), buildVoicePage());
    auto *chat = buildChatPage();
    addPage(tr("Chat"), chat);
    addPage(tr("Notifications"), buildNotificationsPage());
    addPage(tr("Keybinds"), buildKeybindsPage());
    addPage(tr("Startup & window"), buildWindowPage());
    addPage(tr("Streamer mode"), buildStreamerPage());
    addPage(tr("Advanced"), buildAdvancedPage());

    addHeader(tr("Qord"));
    pluginsPage = new PluginsPage(this);
    addPage(tr("Plugins"), pluginsPage);

    connect(categoryList, &QListWidget::currentRowChanged, this, [this](int row) {
        const QListWidgetItem *item = categoryList->item(row);
        if (!item || !(item->flags() & Qt::ItemIsEnabled))
            return;
        pages->setCurrentIndex(item->data(Qt::UserRole).toInt());
        pageTitle->setText(item->text());
    });
    showPage(chat);

    mainLayout->addWidget(sidebar);
    mainLayout->addWidget(content, 1);
}

} // namespace UI
} // namespace Acheron
