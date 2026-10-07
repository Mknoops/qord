#include "AddServerDialog.hpp"

#include "Core/ClientInstance.hpp"
#include "Discord/Client.hpp"

#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

QLabel *heading(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.5);
    f.setBold(true);
    l->setFont(f);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

QLabel *muted(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("UserPanelSubtitle"));
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

QPushButton *button(const QString &text, QWidget *parent, bool primary = false)
{
    auto *b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    if (primary)
        b->setObjectName(QStringLiteral("PrimaryButton"));
    b->setMinimumHeight(36);
    return b;
}

} // namespace

QString AddServerDialog::inviteCode(const QString &input)
{
    static const QRegularExpression re(
            QStringLiteral(R"((?:https?://)?(?:www\.)?(?:discord(?:app)?\.com/invite|discord\.gg)/([A-Za-z0-9-]+))"));
    const QString trimmed = input.trimmed();
    const auto m = re.match(trimmed);
    if (m.hasMatch())
        return m.captured(1);
    static const QRegularExpression bare(QStringLiteral(R"(^[A-Za-z0-9-]{2,64}$)"));
    return bare.match(trimmed).hasMatch() ? trimmed : QString();
}

AddServerDialog::AddServerDialog(Core::ClientInstance *instance, QWidget *parent)
    : QDialog(parent), instance(instance)
{
    setWindowTitle(tr("Add a Server"));
    setMinimumWidth(440);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    pages = new QStackedWidget(this);
    layout->addWidget(pages);

    // Start
    auto *start = new QWidget(pages);
    auto *s = new QVBoxLayout(start);
    s->setSpacing(10);
    s->addWidget(heading(tr("Create Your Server"), start));
    s->addWidget(muted(tr("Your server is where you and your friends hang out. Make yours and start talking."), start));
    QPushButton *create = button(tr("Create My Own"), start, true);
    s->addWidget(create);
    s->addSpacing(12);
    s->addWidget(muted(tr("Have an invite already?"), start));
    QPushButton *join = button(tr("Join a Server"), start);
    s->addWidget(join);
    pages->addWidget(start);

    // Create
    auto *createPage = new QWidget(pages);
    auto *c = new QVBoxLayout(createPage);
    c->setSpacing(8);
    c->addWidget(heading(tr("Customize Your Server"), createPage));
    c->addWidget(muted(tr("Give your new server a personality with a name. You can always change it later."), createPage));
    c->addWidget(new QLabel(tr("SERVER NAME"), createPage));
    nameField = new QLineEdit(createPage);
    nameField->setMaxLength(100);
    if (instance)
        nameField->setText(tr("%1's server").arg(instance->accountInfo().displayName.isEmpty()
                                                         ? instance->accountInfo().username
                                                         : instance->accountInfo().displayName));
    c->addWidget(nameField);
    createStatus = new QLabel(createPage);
    createStatus->setWordWrap(true);
    c->addWidget(createStatus);
    auto *createButtons = new QHBoxLayout;
    QPushButton *createBack = button(tr("Back"), createPage);
    QPushButton *createGo = button(tr("Create"), createPage, true);
    createButtons->addWidget(createBack);
    createButtons->addStretch(1);
    createButtons->addWidget(createGo);
    c->addLayout(createButtons);
    pages->addWidget(createPage);

    // Join
    auto *joinPage = new QWidget(pages);
    auto *j = new QVBoxLayout(joinPage);
    j->setSpacing(8);
    j->addWidget(heading(tr("Join a Server"), joinPage));
    j->addWidget(muted(tr("Enter an invite below to join an existing server"), joinPage));
    j->addWidget(new QLabel(tr("INVITE LINK"), joinPage));
    inviteField = new QLineEdit(joinPage);
    inviteField->setPlaceholderText(QStringLiteral("https://discord.gg/hTKzmak"));
    j->addWidget(inviteField);
    j->addWidget(muted(tr("Invites should look like hTKzmak, https://discord.gg/hTKzmak or https://discord.gg/cool-people"),
                       joinPage));
    joinStatus = new QLabel(joinPage);
    joinStatus->setWordWrap(true);
    j->addWidget(joinStatus);
    auto *joinButtons = new QHBoxLayout;
    QPushButton *joinBack = button(tr("Back"), joinPage);
    QPushButton *joinGo = button(tr("Join Server"), joinPage, true);
    joinButtons->addWidget(joinBack);
    joinButtons->addStretch(1);
    joinButtons->addWidget(joinGo);
    j->addLayout(joinButtons);
    pages->addWidget(joinPage);

    connect(create, &QPushButton::clicked, this, [this]() {
        pages->setCurrentIndex(1);
        nameField->setFocus();
        nameField->selectAll();
    });
    connect(join, &QPushButton::clicked, this, [this]() {
        pages->setCurrentIndex(2);
        inviteField->setFocus();
    });
    connect(createBack, &QPushButton::clicked, this, [this]() { pages->setCurrentIndex(0); });
    connect(joinBack, &QPushButton::clicked, this, [this]() { pages->setCurrentIndex(0); });
    connect(createGo, &QPushButton::clicked, this, &AddServerDialog::createServer);
    connect(nameField, &QLineEdit::returnPressed, this, &AddServerDialog::createServer);
    connect(joinGo, &QPushButton::clicked, this, &AddServerDialog::joinServer);
    connect(inviteField, &QLineEdit::returnPressed, this, &AddServerDialog::joinServer);
}

void AddServerDialog::createServer()
{
    const QString name = nameField->text().trimmed();
    if (name.size() < 2 || !instance) {
        createStatus->setText(tr("Server name must be between 2 and 100 characters."));
        return;
    }
    createStatus->setText(tr("Creating…"));
    const QPointer<AddServerDialog> guard(this);
    instance->discord()->createGuild(name, [guard](const Core::Result<QJsonDocument> &result) {
        if (!guard)
            return;
        if (result.success())
            guard->accept();
        else
            guard->createStatus->setText(tr("Couldn't create the server: %1").arg(result.error));
    });
}

void AddServerDialog::joinServer()
{
    const QString code = inviteCode(inviteField->text());
    if (code.isEmpty() || !instance) {
        joinStatus->setText(tr("Please enter a valid invite link or invite code."));
        return;
    }
    joinStatus->setText(tr("Joining…"));
    const QPointer<AddServerDialog> guard(this);
    instance->discord()->joinGuildByInvite(code, [guard](const Core::Result<QJsonDocument> &result) {
        if (!guard)
            return;
        if (result.success())
            guard->accept();
        else
            guard->joinStatus->setText(tr("The invite is invalid or has expired. (%1)").arg(result.error));
    });
}

} // namespace UI
} // namespace Acheron
