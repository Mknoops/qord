#include "MemberListDelegate.hpp"
#include "Core/RoleColors.hpp"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include "UI/Qord/NameDecorations.hpp"
#include "Core/RoleStyle.hpp"

#include "Core/MemberListManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/StatusIndicator.hpp"

// Qord: Discord's member list metrics (32px round avatars, 42px rows)
constexpr static int GroupHeight = 36;
constexpr static int MemberHeight = 42;
constexpr static int MemberHeightWithActivity = 46;
constexpr static int AvatarSize = 32;
constexpr static int AvatarRadius = AvatarSize / 2;
constexpr static int HorizontalPadding = 8;
constexpr static int AvatarTextSpacing = 12;
constexpr static int StatusDotSize = 10;
constexpr static int ActivityIconSize = 12;
constexpr static int ActivityIconSpacing = 4;

namespace {

// the dot punches a hole in whatever the row is painted on, hover tint included
QColor blendOver(const QColor &base, const QColor &tint)
{
    const qreal alpha = tint.alphaF();
    return QColor::fromRgbF(base.redF() * (1 - alpha) + tint.redF() * alpha,
                            base.greenF() * (1 - alpha) + tint.greenF() * alpha,
                            base.blueF() * (1 - alpha) + tint.blueF() * alpha);
}

} // namespace

namespace Acheron {
namespace UI {

MemberListDelegate::MemberListDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void MemberListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    int itemType = index.data(MemberListModel::ItemTypeRole).toInt();

    if (itemType == static_cast<int>(Core::MemberListItem::Type::Group))
        paintGroup(painter, option, index);
    else if (itemType == static_cast<int>(Core::MemberListItem::Type::Member))
        paintMember(painter, option, index);
    else
        paintPlaceholder(painter, option);

    painter->restore();
}

QSize MemberListDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    int itemType = index.data(MemberListModel::ItemTypeRole).toInt();

    if (itemType == static_cast<int>(Core::MemberListItem::Type::Group))
        return QSize(option.rect.width(), GroupHeight);

    if (itemType == static_cast<int>(Core::MemberListItem::Type::Member) &&
        index.data(MemberListModel::HasActivityRole).toBool())
        return QSize(option.rect.width(), MemberHeightWithActivity);

    return QSize(option.rect.width(), MemberHeight);
}

void MemberListDelegate::paintGroup(QPainter *painter, const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    QString groupName = index.data(MemberListModel::GroupNameRole).toString();
    int groupCount = index.data(MemberListModel::GroupCountRole).toInt();

    // separator except for the first
    if (index.row() > 0) {
        QColor sepColor = option.palette.mid().color();
        sepColor.setAlpha(60);
        painter->setPen(QPen(sepColor, 1));
        painter->drawLine(option.rect.left() + HorizontalPadding,
                          option.rect.top(),
                          option.rect.right() - HorizontalPadding,
                          option.rect.top());
    }

    QString text = groupName.toUpper() + QString::fromUtf8(" \u2014 ") + QString::number(groupCount);

    QFont font = option.font;
    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
    painter->setFont(font);

    // Qord: RoleColorEverywhere shows a role header in the role's color
    const QColor roleColor = index.data(MemberListModel::GroupColorRole).value<QColor>();
    painter->setPen(Core::roleColorOptions().memberListHeaders && roleColor.isValid() && roleColor != QColor(Qt::black)
                            ? roleColor
                            : Core::Theme::Manager::instance().color(Core::Theme::Token::MutedText));

    QRect textRect = option.rect.adjusted(HorizontalPadding, 0, -HorizontalPadding, 0);
    textRect.setTop(textRect.top() + 6);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
}

void MemberListDelegate::paintMember(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    QColor rowColor = option.palette.color(QPalette::Window);
    if (option.state & QStyle::State_MouseOver) {
        const QColor hoverColor = Core::Theme::Manager::instance().color(Core::Theme::Token::HoverBg);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(hoverColor);
        painter->drawRoundedRect(option.rect.adjusted(HorizontalPadding / 2, 1, -HorizontalPadding / 2, -1), 4, 4);
        painter->restore();
        rowColor = blendOver(rowColor, hoverColor);
    }

    const auto activity = index.data(MemberListModel::ActivityRole).value<MemberActivity>();

    int x = option.rect.left() + HorizontalPadding;
    int centerY = option.rect.top() + (option.rect.height() - AvatarSize) / 2;

    QPixmap avatar = index.data(MemberListModel::AvatarRole).value<QPixmap>();
    QRect avatarRect(x, centerY, AvatarSize, AvatarSize);

    if (!avatar.isNull()) {
        QPainterPath clipPath;
        clipPath.addRoundedRect(avatarRect, AvatarRadius, AvatarRadius);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->setClipPath(clipPath);
        painter->drawPixmap(avatarRect, avatar.scaled(AvatarSize, AvatarSize,
                                                      Qt::KeepAspectRatioByExpanding,
                                                      Qt::SmoothTransformation));
        painter->restore();
    } else {
        QColor defaultBg = option.palette.mid().color();
        defaultBg.setAlpha(100);
        painter->setBrush(defaultBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(avatarRect, AvatarRadius, AvatarRadius);
    }

    const auto badge = index.data(MemberListModel::PresenceBadgeRole).value<Core::PresenceBadge>();
    if (index.data(MemberListModel::IsTypingRole).toBool()) {
        // Qord: Discord's typing dots, cycling once every 1.2 s (the view
        // repaints while someone types)
        const qreal phase = (QDateTime::currentMSecsSinceEpoch() % 1200) / 1200.0;
        StatusIndicator::paintTypingOnAvatar(*painter, avatarRect, StatusDotSize, badge, rowColor, phase);
    } else {
        StatusIndicator::paintOnAvatar(*painter, avatarRect, StatusDotSize, badge, rowColor);
    }

    x += AvatarSize + AvatarTextSpacing;
    int textWidth = option.rect.right() - x - HorizontalPadding;

    QString displayName = index.data(MemberListModel::UsernameRole).toString();
    QColor roleColor = index.data(MemberListModel::RoleColorRole).value<QColor>();

    QFont font = option.font;
    font.setPixelSize(14);
    font.setWeight(QFont::Medium);
    painter->setFont(font);

    // Qord: the role color is on the name, in a dot after it, or both
    // (Discord's Role Colors setting, BetterRoleDot)
    QColor nameColor;
    if (roleColor.isValid() && Core::roleColorOnName())
        nameColor = roleColor;
    else
        nameColor = option.palette.color(QPalette::Text);

    painter->setPen(nameColor);

    QFontMetrics fm(font);

    // Qord: plugin icons after the name (PlatformIndicators)
    QList<NameDecorations::Decoration> decorations;
    if (auto *provider = NameDecorations::instance()) {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        decorations = provider->forUser(Core::Snowflake(index.data(MemberListModel::AccountIdRole).toULongLong()),
                                        Core::Snowflake(index.data(MemberListModel::GuildIdRole).toULongLong()),
                                        Core::Snowflake(index.data(MemberListModel::UserIdRole).toULongLong()),
                                        NameDecorations::Place::MemberList, dpr);
    }
    // Qord: the role dot, then Discord's crown for the server owner, before
    // plugin icons
    if (index.data(MemberListModel::OwnerCrownRole).toBool()) {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        constexpr int CrownSize = 14;
        decorations.prepend({ Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Crown, CrownSize,
                                                          QColor(0xf0, 0xb2, 0x32), dpr),
                              QSize(CrownSize, CrownSize), tr("Server Owner") });
    }
    if (roleColor.isValid() && Core::roleDotAfterName()) {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        NameDecorations::Decoration dot;
        dot.pixmap = Core::roleDotPixmap(roleColor, dpr);
        dot.size = QSize(Core::RoleDotSize, Core::RoleDotSize);
        if (Core::roleDotOptions().enabled)
            dot.copyText = roleColor.name();
        decorations.prepend(dot);
    }
    const int decorationWidth = qMin(NameDecorations::width(decorations), textWidth / 2);
    QString elidedName = fm.elidedText(displayName, Qt::ElideRight, textWidth - decorationWidth);
    auto paintDecorations = [&](int lineTop, int lineHeight) {
        if (decorations.isEmpty()) {
            decorationRects.remove(index.row());
            return;
        }
        const QList<QRect> rects = NameDecorations::paint(*painter, decorations, x + fm.horizontalAdvance(elidedName),
                                                          lineTop + lineHeight / 2);
        QList<QPair<QRect, NameDecorations::Decoration>> tips;
        for (int i = 0; i < rects.size(); ++i)
            tips.append({ rects[i], decorations[i] });
        if (decorationRects.size() > 512)
            decorationRects.clear();
        decorationRects.insert(index.row(), tips);
    };

    if (activity.text.isEmpty()) {
        painter->drawText(QRect(x, option.rect.top(), textWidth, option.rect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, elidedName);
        paintDecorations(option.rect.top(), option.rect.height());
        return;
    }

    const int lineHeight = fm.height();
    const int blockHeight = lineHeight * 2;
    const int blockTop = option.rect.top() + (option.rect.height() - blockHeight) / 2;

    painter->drawText(QRect(x, blockTop, textWidth, lineHeight),
                      Qt::AlignLeft | Qt::AlignVCenter, elidedName);
    paintDecorations(blockTop, lineHeight);

    paintActivityLine(painter, option, activity,
                      QRect(x, blockTop + lineHeight, textWidth, lineHeight));
}

void MemberListDelegate::paintActivityLine(QPainter *painter, const QStyleOptionViewItem &option,
                                           const MemberActivity &activity,
                                           const QRect &lineRect) const
{
    QFont font = option.font;
    font.setPixelSize(12);
    painter->setFont(font);

    QColor color = Core::Theme::Manager::instance().color(Core::Theme::Token::MutedText);
    painter->setPen(color);

    int x = lineRect.left();
    int available = lineRect.width();

    if (!activity.emoji.isNull()) {
        QRect emojiRect(x, lineRect.top() + (lineRect.height() - ActivityIconSize) / 2,
                        ActivityIconSize, ActivityIconSize);
        painter->drawPixmap(emojiRect, activity.emoji);
        x += ActivityIconSize + ActivityIconSpacing;
        available -= ActivityIconSize + ActivityIconSpacing;
    } else if (!activity.emojiText.isEmpty()) {
        QFontMetrics emojiMetrics(font);
        const int width = emojiMetrics.horizontalAdvance(activity.emojiText);
        painter->drawText(QRect(x, lineRect.top(), width, lineRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, activity.emojiText);
        x += width + ActivityIconSpacing;
        available -= width + ActivityIconSpacing;
    } else {
        const QString iconName = activityIconName(activity.kind);
        if (!iconName.isEmpty()) {
            const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
            QPixmap icon = Core::Theme::Icons::pixmap(iconName, ActivityIconSize, color, dpr);
            QRect iconRect(x, lineRect.top() + (lineRect.height() - ActivityIconSize) / 2,
                           ActivityIconSize, ActivityIconSize);
            painter->drawPixmap(iconRect, icon);
            x += ActivityIconSize + ActivityIconSpacing;
            available -= ActivityIconSize + ActivityIconSpacing;
        }
    }

    if (available <= 0)
        return;

    QFontMetrics fm(font);
    const QString text = fm.elidedText(activity.text, Qt::ElideRight, available);
    painter->drawText(QRect(x, lineRect.top(), available, lineRect.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, text);
}

bool MemberListDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                                   const QModelIndex &index)
{
    if (event && index.isValid()) {
        for (const auto &[rect, decoration] : decorationRects.value(index.row())) {
            if (rect.contains(event->pos()) && !decoration.tooltip.isEmpty()) {
                QToolTip::showText(event->globalPos(), decoration.tooltip, view ? view->viewport() : nullptr, rect);
                return true;
            }
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

std::optional<NameDecorations::Decoration> MemberListDelegate::clickableDecorationAt(int row, const QPoint &pos) const
{
    for (const auto &[rect, decoration] : decorationRects.value(row))
        if (rect.contains(pos) && decoration.clickable())
            return decoration;
    return std::nullopt;
}

QString MemberListDelegate::activityIconName(Discord::ActivityType kind)
{
    switch (kind) {
    case Discord::ActivityType::PLAYING:
    case Discord::ActivityType::COMPETING:
        return Core::Theme::Icons::Name::Gamepad;
    case Discord::ActivityType::LISTENING:
        return Core::Theme::Icons::Name::Music;
    case Discord::ActivityType::WATCHING:
    case Discord::ActivityType::STREAMING:
        return Core::Theme::Icons::Name::Monitor;
    default:
        return {};
    }
}

void MemberListDelegate::paintPlaceholder(QPainter *painter,
                                          const QStyleOptionViewItem &option) const
{
    QColor placeholderColor = option.palette.mid().color();
    placeholderColor.setAlpha(40);
    painter->setPen(Qt::NoPen);
    painter->setBrush(placeholderColor);

    int x = option.rect.left() + HorizontalPadding;
    int centerY = option.rect.top() + (option.rect.height() - AvatarSize) / 2;

    painter->drawRoundedRect(QRect(x, centerY, AvatarSize, AvatarSize),
                             AvatarRadius, AvatarRadius);

    x += AvatarSize + AvatarTextSpacing;
    int nameWidth = qMin(80, option.rect.right() - x - HorizontalPadding);
    int nameHeight = 10;
    int nameY = option.rect.top() + (option.rect.height() - nameHeight) / 2;
    painter->drawRoundedRect(QRect(x, nameY, nameWidth, nameHeight), 3, 3);
}

} // namespace UI
} // namespace Acheron
