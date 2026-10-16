#include "PluginsPage.hpp"

#include "Plugins/Client/ClientPlugins.hpp"
#include "Plugins/PluginHost.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>

#include <memory>
#include <QScrollArea>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

using namespace Qord::Plugins;

namespace Acheron {
namespace UI {

namespace {
constexpr int IdRole = Qt::UserRole + 1;
constexpr int SearchRole = Qt::UserRole + 2;
constexpr int CategoriesRole = Qt::UserRole + 3;

QLabel *helpLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

// A text editor that commits when focus leaves it, like upstream's onBlur.
class CommitOnBlurEdit : public QPlainTextEdit
{
public:
    std::function<void(const QString &)> onCommit;

protected:
    void focusOutEvent(QFocusEvent *e) override
    {
        QPlainTextEdit::focusOutEvent(e);
        if (onCommit)
            onCommit(toPlainText());
    }
};
} // namespace

PluginsPage::PluginsPage(QWidget *parent)
    : QWidget(parent), host(PluginHost::instance())
{
    auto *layout = new QVBoxLayout(this);
    if (!host) {
        layout->addWidget(new QLabel(tr("Plugins are not available."), this));
        return;
    }

    auto *filters = new QHBoxLayout;
    search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search plugins..."));
    search->setClearButtonEnabled(true);
    category = new QComboBox(this);
    category->addItem(tr("All categories"), QString());
    filters->addWidget(search, 1);
    filters->addWidget(category);
    layout->addLayout(filters);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    list = new QListWidget(splitter);
    list->setMinimumWidth(170);
    detailsScroll = new QScrollArea(splitter);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    splitter->addWidget(list);
    splitter->addWidget(detailsScroll);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    populate();

    connect(search, &QLineEdit::textChanged, this, &PluginsPage::applyFilter);
    connect(category, qOverload<int>(&QComboBox::currentIndexChanged), this, &PluginsPage::applyFilter);
    connect(list, &QListWidget::itemChanged, this, &PluginsPage::onItemChanged);
    connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        showDetails(item ? item->data(IdRole).toString() : QString());
    });
    connect(host, &PluginHost::pluginStateChanged, this, [this](const QString &id) {
        updating = true;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(IdRole).toString() == id)
                list->item(i)->setCheckState(host->isEnabled(id) ? Qt::Checked : Qt::Unchecked);
        updating = false;
        // Queued: the change may come from a widget inside the details page
        if (id == shownId)
            QMetaObject::invokeMethod(this, [this, id] { if (id == shownId) showDetails(id); }, Qt::QueuedConnection);
    });

    if (list->count() > 0)
        list->setCurrentRow(0);
}

void PluginsPage::populate()
{
    QStringList categories;
    updating = true;
    for (const QString &id : host->pluginIds()) {
        const PluginHost::PluginInfo *info = host->info(id);
        const PluginManifest &m = info->manifest;
        auto *item = new QListWidgetItem(m.name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(info->enabled ? Qt::Checked : Qt::Unchecked);
        item->setData(IdRole, id);
        item->setData(SearchRole, QStringList { m.name, m.description, m.equicordName, m.tags.join(' ') }.join(' ').toLower());
        item->setData(CategoriesRole, m.categories);
        item->setToolTip(m.description);
        for (const QString &c : m.categories)
            if (!categories.contains(c))
                categories.append(c);
    }
    updating = false;
    list->sortItems();
    categories.sort();
    for (const QString &c : categories)
        category->addItem(c.left(1).toUpper() + c.mid(1), c);
}

void PluginsPage::applyFilter()
{
    const QString q = search->text().trimmed().toLower();
    const QString cat = category->currentData().toString();
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        const bool textOk = q.isEmpty() || item->data(SearchRole).toString().contains(q);
        const bool catOk = cat.isEmpty() || item->data(CategoriesRole).toStringList().contains(cat);
        item->setHidden(!(textOk && catOk));
    }
}

void PluginsPage::onItemChanged(QListWidgetItem *item)
{
    if (updating)
        return;
    const QString id = item->data(IdRole).toString();
    const bool enable = item->checkState() == Qt::Checked;
    const PluginHost::PluginInfo *info = host->info(id);
    if (!info)
        return;

    if (enable) {
        // Ask once for whatever the plugin declares but hasn't been granted
        const Permissions missing = info->manifest.permissions & ~info->granted;
        if (missing) {
            QStringList lines;
            for (Permission p : allPermissions())
                if (missing.testFlag(p))
                    lines.append(QStringLiteral("• ") + permissionDescription(p));
            const auto answer = QMessageBox::question(
                    this, tr("Enable %1").arg(info->manifest.name),
                    tr("%1 needs permission to:\n\n%2\n\nYou can revoke these later in the plugin's settings.")
                            .arg(info->manifest.name, lines.join('\n')),
                    QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
            if (answer != QMessageBox::Ok) {
                updating = true;
                item->setCheckState(Qt::Unchecked);
                updating = false;
                return;
            }
            host->setGranted(id, info->granted | missing);
        }
    }

    QString error;
    if (!host->setEnabled(id, enable, &error)) {
        QMessageBox::warning(this, tr("Plugin error"), tr("%1 could not be started: %2").arg(info->manifest.name, error));
        updating = true;
        item->setCheckState(host->isEnabled(id) ? Qt::Checked : Qt::Unchecked);
        updating = false;
    }
}

void PluginsPage::selectPlugin(const QString &id)
{
    search->clear();
    category->setCurrentIndex(0);
    applyFilter();
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(IdRole).toString() == id) {
            list->setCurrentRow(i);
            list->scrollToItem(list->item(i));
            return;
        }
    }
}

void PluginsPage::showDetails(const QString &id)
{
    shownId = id;
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    const PluginHost::PluginInfo *info = host->info(id);
    if (!info) {
        detailsScroll->setWidget(page);
        return;
    }
    const PluginManifest &m = info->manifest;

    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(m.name.toHtmlEscaped()), page);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto *desc = new QLabel(m.description, page);
    desc->setWordWrap(true);
    layout->addWidget(desc);
    layout->addWidget(helpLabel(tr("Port of Equicord's %1 by %2").arg(m.equicordName, m.authors.join(QStringLiteral(", "))), page));
    if (!m.notes.isEmpty())
        layout->addWidget(helpLabel(m.notes, page));
    for (const QString &gap : m.gaps)
        layout->addWidget(helpLabel(tr("Not yet supported: %1").arg(gap), page));
    if (!info->loadError.isEmpty()) {
        auto *err = new QLabel(info->loadError, page);
        err->setWordWrap(true);
        err->setStyleSheet(QStringLiteral("color: #f23f43;"));
        layout->addWidget(err);
    }

    if (m.permissions) {
        auto *box = new QGroupBox(tr("Permissions"), page);
        auto *boxLayout = new QVBoxLayout(box);
        // An update can ask for more than was granted when it was enabled;
        // those parts stay off until allowed here
        if (info->enabled && (m.permissions & ~info->granted)) {
            auto *note = new QLabel(tr("Some of this plugin's features need a permission that isn't allowed yet. "
                                       "Tick it below to use them."),
                                    box);
            note->setWordWrap(true);
            note->setStyleSheet(QStringLiteral("color: #f0b232;"));
            boxLayout->addWidget(note);
        }
        for (Permission p : allPermissions()) {
            if (!m.permissions.testFlag(p))
                continue;
            auto *check = new QCheckBox(permissionDescription(p), box);
            check->setChecked(info->granted.testFlag(p));
            connect(check, &QCheckBox::toggled, this, [this, id, p](bool on) {
                Permissions grants = host->granted(id);
                grants.setFlag(p, on);
                host->setGranted(id, grants);
            });
            boxLayout->addWidget(check);
        }
        layout->addWidget(box);
    }

    const bool hasVisibleSettings =
            std::any_of(m.settings.begin(), m.settings.end(), [](const SettingSpec &s) { return !s.hidden; });
    if (hasVisibleSettings) {
        auto *box = new QGroupBox(tr("Settings"), page);
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->addWidget(buildSettingsEditor(id, box));
        layout->addWidget(box);
    }
    layout->addStretch();
    detailsScroll->setWidget(page);
}

QWidget *PluginsPage::buildSettingsEditor(const QString &id, QWidget *parent)
{
    auto *editor = new QWidget(parent);
    auto *form = new QVBoxLayout(editor);
    form->setContentsMargins(0, 0, 0, 0);
    PluginSettings *settings = host->settings(id);
    if (!settings)
        return editor;

    bool customDone = false;
    // Settings shown only for some values of another (showWhen), re-checked
    // after every change
    QVBoxLayout *outer = form;
    auto conditional = std::make_shared<QList<QPair<QPointer<QWidget>, SettingSpec>>>();
    const auto refreshVisibility = [settings, conditional]() {
        for (const auto &[block, spec] : std::as_const(*conditional))
            if (block)
                block->setVisible(spec.showWhenValues.contains(settings->value(spec.showWhenKey).toVariant().toString()));
    };
    for (const SettingSpec &spec : settings->schema()) {
        if (spec.hidden)
            continue;
        const QString key = spec.key;
        auto *block = new QWidget(editor);
        form = new QVBoxLayout(block);
        form->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(block);
        if (!spec.showWhenKey.isEmpty())
            conditional->append({ block, spec });
        auto addHelp = [&] {
            if (!spec.description.isEmpty())
                form->addWidget(helpLabel(spec.description, editor));
        };
        auto *error = new QLabel(editor);
        error->setStyleSheet(QStringLiteral("color: #f23f43;"));
        error->setVisible(false);
        auto commit = [settings, key, error, refreshVisibility](const QJsonValue &v) {
            const QString e = settings->setValue(key, v);
            error->setText(e);
            error->setVisible(!e.isEmpty());
            refreshVisibility();
        };

        switch (spec.type) {
        case SettingType::Action: {
            // "&" in a label is text, not a shortcut marker
            auto *button = new QPushButton(QString(spec.label).replace(u'&', QStringLiteral("&&")), editor);
            button->setCursor(Qt::PointingHandCursor);
            connect(button, &QPushButton::clicked, editor, [id, key]() {
                if (auto *host = Qord::Plugins::PluginHost::instance())
                    host->dispatchSettingAction(id, key);
            });
            auto *row = new QHBoxLayout;
            row->addWidget(button);
            row->addStretch();
            form->addLayout(row);
            addHelp();
            break;
        }
        case SettingType::Bool: {
            auto *check = new QCheckBox(spec.label, editor);
            check->setChecked(settings->boolValue(key));
            connect(check, &QCheckBox::toggled, editor, [commit](bool on) { commit(on); });
            form->addWidget(check);
            addHelp();
            break;
        }
        case SettingType::Int: {
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(spec.label, editor));
            auto *spin = new QSpinBox(editor);
            spin->setRange(int(spec.minimum.value_or(INT_MIN)), int(spec.maximum.value_or(INT_MAX)));
            spin->setValue(int(settings->intValue(key)));
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), editor, [commit](int v) { commit(v); });
            row->addWidget(spin);
            row->addStretch();
            form->addLayout(row);
            addHelp();
            break;
        }
        case SettingType::Number: {
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(spec.label, editor));
            auto *spin = new QDoubleSpinBox(editor);
            spin->setDecimals(2);
            spin->setSingleStep(0.1);
            spin->setRange(spec.minimum.value_or(-1e9), spec.maximum.value_or(1e9));
            spin->setValue(settings->numberValue(key));
            connect(spin, &QDoubleSpinBox::editingFinished, editor, [spin, commit] { commit(spin->value()); });
            row->addWidget(spin);
            row->addStretch();
            form->addLayout(row);
            addHelp();
            break;
        }
        case SettingType::Choice: {
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(spec.label, editor));
            auto *combo = new QComboBox(editor);
            for (const auto &c : spec.choices)
                combo->addItem(c.label, c.value);
            combo->setCurrentIndex(combo->findData(settings->stringValue(key)));
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), editor,
                    [combo, commit] { commit(combo->currentData().toString()); });
            row->addWidget(combo);
            row->addStretch();
            form->addLayout(row);
            addHelp();
            break;
        }
        case SettingType::String:
        case SettingType::StringList: {
            form->addWidget(new QLabel(spec.label, editor));
            addHelp();
            const bool isList = spec.type == SettingType::StringList;
            if (isList || spec.multiline) {
                auto *edit = new CommitOnBlurEdit;
                edit->setParent(editor);
                edit->setPlainText(isList ? settings->stringListValue(key).join('\n') : settings->stringValue(key));
                edit->setFixedHeight(edit->fontMetrics().lineSpacing() * 4 + 12);
                edit->onCommit = [commit, isList](const QString &text) {
                    if (isList)
                        commit(QJsonArray::fromStringList(text.split('\n', Qt::SkipEmptyParts)));
                    else
                        commit(text);
                };
                form->addWidget(edit);
            } else {
                auto *edit = new QLineEdit(settings->stringValue(key), editor);
                if (spec.secret) {
                    // Kept in the system keychain; shown hidden
                    edit->setEchoMode(QLineEdit::Password);
                    edit->setPlaceholderText(QObject::tr("Stored in the system keychain"));
                }
                connect(edit, &QLineEdit::editingFinished, editor, [edit, commit] { commit(edit->text()); });
                form->addWidget(edit);
            }
            break;
        }
        case SettingType::Json: {
            if (const SettingsWidgetFactory *factory = ClientPlugins::settingsWidgets().find(id)) {
                if (!customDone)
                    form->addWidget((*factory)(*settings, editor));
                customDone = true;
                break;
            }
            // No custom editor: raw JSON, validated on commit
            form->addWidget(new QLabel(spec.label, editor));
            addHelp();
            auto *edit = new CommitOnBlurEdit;
            edit->setParent(editor);
            // Shown wrapped in [ ] so scalars round-trip too
            edit->setPlainText(QString::fromUtf8(QJsonDocument(QJsonArray { settings->value(key) }).toJson()));
            edit->onCommit = [commit, error](const QString &text) {
                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isArray() || doc.array().size() != 1) {
                    error->setText(parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                                : QObject::tr("expected one value in [ ]"));
                    error->setVisible(true);
                    return;
                }
                commit(doc.array().at(0));
            };
            form->addWidget(edit);
            break;
        }
        }
        form->addWidget(error);
    }
    refreshVisibility();
    return editor;
}

} // namespace UI
} // namespace Acheron
