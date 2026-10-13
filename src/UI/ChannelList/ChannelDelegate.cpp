#include "ChannelDelegate.hpp"

#include "ChannelNode.hpp"
#include "ChannelTreeModel.hpp"
#include "ChannelFilterProxyModel.hpp"

#include "Core/RoleColors.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/StatusIndicator.hpp"

namespace Acheron {
namespace UI {

namespace {
ChannelDelegate::VoiceLabelProvider &voiceLabelProvider()
{
    static ChannelDelegate::VoiceLabelProvider provider;
    return provider;
}
ChannelDelegate::LiveProvider &liveProvider()
{
    static ChannelDelegate::LiveProvider provider;
    return provider;
}
ChannelDelegate::CameraProvider &cameraProvider()
{
    static ChannelDelegate::CameraProvider provider;
    return provider;
}
ChannelDelegate::NameColorProvider &nameColorProvider()
{
    static ChannelDelegate::NameColorProvider provider;
    return provider;
}
} // namespace

void ChannelDelegate::setNameColorProvider(NameColorProvider provider)
{
    nameColorProvider() = std::move(provider);
}

void ChannelDelegate::setVoiceLabelProvider(VoiceLabelProvider provider)
{
    voiceLabelProvider() = std::move(provider);
}

void ChannelDelegate::setLiveProvider(LiveProvider provider)
{
    liveProvider() = std::move(provider);
}

void ChannelDelegate::setCameraProvider(CameraProvider provider)
{
    cameraProvider() = std::move(provider);
}

bool ChannelDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                                const QModelIndex &index)
{
    if (event && index.isValid()) {
        QModelIndex source = proxyModel ? proxyModel->mapToSource(index) : index;
        const void *node = source.internalPointer();
        for (const auto *tips : { &voiceLabelTips, &hiddenIconTips, &typingTips }) {
            auto it = tips->constFind(node);
            if (it != tips->constEnd() && it->first.contains(event->pos())) {
                QToolTip::showText(event->globalPos(), it->second, view ? view->viewport() : nullptr, it->first);
                return true;
            }
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

ChannelDelegate::ChannelDelegate(QAbstractProxyModel *proxyModel, QObject *parent)
    : QStyledItemDelegate(parent), proxyModel(proxyModel)
{
}

static ChannelIndent::Scopes indentScopeOfChildren(ChannelNode::Type parentType)
{
    switch (parentType) {
    case ChannelNode::Type::Account:
        return ChannelIndent::Scope::Accounts;
    case ChannelNode::Type::Folder:
    case ChannelNode::Type::DMHeader:
        return ChannelIndent::Scope::Folders;
    case ChannelNode::Type::Server:
        return ChannelIndent::Scope::Servers;
    case ChannelNode::Type::Category:
        return ChannelIndent::Scope::Categories;
    case ChannelNode::Type::Channel:
    case ChannelNode::Type::Forum:
    case ChannelNode::Type::VoiceChannel:
        return ChannelIndent::Scope::Channels;
    default:
        return {};
    }
}

static int indentWidth(const ChannelNode *node, ChannelIndent::Scopes scopes)
{
    int steps = 0;
    for (const ChannelNode *parent = node->parent; parent; parent = parent->parent)
        if (scopes & indentScopeOfChildren(parent->type))
            steps++;
    return steps * ChannelIndent::StepWidth;
}

static void drawBranchIndicator(QPainter *painter, const QStyleOptionViewItem &option, bool expanded)
{
    int indicatorWidth = 20;
    QRect branchRect = option.rect;
    branchRect.setWidth(indicatorWidth);

    QStyleOptionViewItem branchOpt = option;
    branchOpt.rect = branchRect;
    branchOpt.state |= QStyle::State_Children;

    if (expanded)
        branchOpt.state |= QStyle::State_Open;
    else
        branchOpt.state &= ~QStyle::State_Open;

    QApplication::style()->drawPrimitive(QStyle::PE_IndicatorBranch, &branchOpt, painter,
                                         option.widget);
}

static void drawUnreadPill(QPainter *painter, const QStyleOptionViewItem &option)
{
    int pillWidth = 4;
    int pillHeight = 8;
    int pillX = option.rect.left() - pillWidth;
    int pillY = option.rect.top() + (option.rect.height() - pillHeight) / 2;

    painter->setBrush(option.palette.brightText().color());
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(QRect(pillX, pillY, pillWidth * 2, pillHeight), pillWidth, pillWidth);
}

static void drawLucideIcon(QPainter *painter, const QRect &contentRect, const QString &name, const QColor &color)
{
    constexpr int iconSize = 16;
    int x = contentRect.left() + (24 - iconSize) / 2;
    int y = contentRect.top() + (contentRect.height() - iconSize) / 2;

    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    painter->drawPixmap(QRect(x, y, iconSize, iconSize),
                        Core::Theme::Icons::pixmap(name, iconSize, color, dpr));
}

static void drawHashIcon(QPainter *painter, const QRect &contentRect, const QColor &color)
{
    constexpr int iconSize = 16;
    int x = contentRect.left() + (24 - iconSize) / 2;
    int y = contentRect.top() + (contentRect.height() - iconSize) / 2;

    QPen pen(color, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    painter->save();
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    // two slanted vertical lines
    painter->drawLine(QLineF(x + 5.5, y + 2, x + 4, y + 14));
    painter->drawLine(QLineF(x + 10.5, y + 2, x + 9, y + 14));

    // two horizontal lines
    painter->drawLine(QLineF(x + 2, y + 5.5, x + 13, y + 5.5));
    painter->drawLine(QLineF(x + 2, y + 10.5, x + 13, y + 10.5));

    painter->restore();
}

static bool isLastThreadSibling(const ChannelNode *node)
{
    if (!node || !node->parent)
        return true;
    const ChannelNode *last = nullptr;
    for (const auto &sibling : node->parent->children)
        if (sibling->type == ChannelNode::Type::Thread)
            last = sibling.get();
    return last == node;
}

static void drawThreadBranchIcon(QPainter *painter, const QRect &contentRect, const QColor &color, bool lastSibling)
{
    const qreal vx = contentRect.left() + 9.5;
    const qreal branchY = contentRect.top() + contentRect.height() / 2.0;
    const qreal hEnd = contentRect.left() + 17.0;
    const qreal radius = 3.0;

    QColor branch = color;
    branch.setAlphaF(0.4f);
    QPen pen(branch, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::FlatCap);
    painter->save();
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    QPainterPath path;
    if (lastSibling) {
        path.moveTo(vx, contentRect.top());
        path.lineTo(vx, branchY - radius);
        path.quadTo(vx, branchY, vx + radius, branchY);
        path.lineTo(hEnd, branchY);
    } else {
        path.moveTo(vx, contentRect.top());
        path.lineTo(vx, contentRect.bottom() + 1.0);
        path.moveTo(vx, branchY);
        path.lineTo(hEnd, branchY);
    }
    painter->drawPath(path);

    painter->restore();
}

static void drawForumIcon(QPainter *painter, const QRect &contentRect, const QColor &color)
{
    constexpr int iconSize = 16;
    int x = contentRect.left() + (24 - iconSize) / 2;
    int y = contentRect.top() + (contentRect.height() - iconSize) / 2;

    QPen pen(color, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter->save();
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    // speech bubble with a small tail and two text lines
    painter->drawRoundedRect(QRectF(x + 1.5, y + 2, 13, 9), 2.5, 2.5);
    painter->drawLine(QLineF(x + 4.5, y + 11, x + 4, y + 14));
    painter->drawLine(QLineF(x + 4, y + 14, x + 8, y + 11));
    painter->drawLine(QLineF(x + 4, y + 5.5, x + 12, y + 5.5));
    painter->drawLine(QLineF(x + 4, y + 8, x + 10, y + 8));

    painter->restore();
}

static void drawPadlockOverlay(QPainter *painter, const QRect &contentRect, const QColor &color,
                               const QColor &bgColor)
{
    constexpr int iconSize = 16;
    int ix = contentRect.left() + (24 - iconSize) / 2;
    int iy = contentRect.top() + (contentRect.height() - iconSize) / 2;

    // position the lock at the top-right of the icon area
    qreal lx = ix + 10;
    qreal ly = iy - 1;

    painter->save();

    // background circle to punch out the hash behind the lock
    painter->setPen(Qt::NoPen);
    painter->setBrush(bgColor);
    painter->drawEllipse(QRectF(lx - 1.5, ly - 1.5, 10, 10));

    // shackle
    QPen shacklePen(color, 1.2);
    shacklePen.setCapStyle(Qt::RoundCap);
    painter->setPen(shacklePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(QRectF(lx + 2, ly + 1, 3, 5), 0, 180 * 16);

    // body
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawRoundedRect(QRectF(lx + 0.5, ly + 3, 6, 4.5), 0.8, 0.8);

    painter->restore();
}

static void drawSpeakerIcon(QPainter *painter, const QRect &contentRect, const QColor &color)
{
    constexpr int iconSize = 16;
    int x = contentRect.left() + (24 - iconSize) / 2;
    int y = contentRect.top() + (contentRect.height() - iconSize) / 2;

    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);

    // speaker body
    QRectF body(x + 2, y + 5, 4, 6);
    painter->drawRect(body);

    // speaker cone (triangle)
    QPainterPath cone;
    cone.moveTo(x + 6, y + 5);
    cone.lineTo(x + 10, y + 2);
    cone.lineTo(x + 10, y + 14);
    cone.lineTo(x + 6, y + 11);
    cone.closeSubpath();
    painter->drawPath(cone);

    // sound wave arc
    painter->setBrush(Qt::NoBrush);
    QPen wavePen(color, 1.5);
    wavePen.setCapStyle(Qt::RoundCap);
    painter->setPen(wavePen);
    painter->drawArc(QRectF(x + 10, y + 4, 5, 8), -60 * 16, 120 * 16);

    painter->restore();
}

static void drawMutedMicIcon(QPainter *painter, const QRectF &rect, const QColor &color,
                             const QColor &bgColor)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    qreal w = rect.width();
    qreal h = rect.height();
    qreal cx = rect.left() + w / 2.0;

    // pill
    qreal capW = w * 0.40;
    qreal capTopY = rect.top() + h * 0.05;
    qreal capBotY = rect.top() + h * 0.55;
    QRectF cap(cx - capW / 2.0, capTopY, capW, capBotY - capTopY);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawRoundedRect(cap, capW / 2.0, capW / 2.0);

    // U-shaped holder
    qreal uW = w * 0.78;
    qreal uTipsY = rect.top() + h * 0.42;
    qreal uBottomY = rect.top() + h * 0.72;
    qreal uHalfH = uBottomY - uTipsY;
    QRectF uRect(cx - uW / 2.0, uTipsY - uHalfH, uW, uHalfH * 2.0);
    qreal stroke = qMax(1.3, w * 0.11);
    QPen uPen(color, stroke);
    uPen.setCapStyle(Qt::FlatCap);
    painter->setPen(uPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(uRect, 180 * 16, 180 * 16);

    // vertical stem
    qreal stemBotY = rect.top() + h * 0.86;
    QPen stemPen(color, stroke);
    stemPen.setCapStyle(Qt::FlatCap);
    painter->setPen(stemPen);
    painter->drawLine(QPointF(cx, uBottomY - stroke / 2.0), QPointF(cx, stemBotY));

    // base
    qreal baseW = w * 0.50;
    qreal baseH = qMax(1.6, h * 0.11);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    QRectF base(cx - baseW / 2.0, stemBotY - baseH / 2.0, baseW, baseH);
    painter->drawRoundedRect(base, baseH / 2.0, baseH / 2.0);

    // slash
    qreal slashWidth = qMax(1.4, w * 0.12);
    QPointF p1(rect.left() + w * 0.08, rect.bottom() - h * 0.08);
    QPointF p2(rect.right() - w * 0.08, rect.top() + h * 0.08);

    QPen knockoutPen(bgColor, slashWidth * 2.2);
    knockoutPen.setCapStyle(Qt::RoundCap);
    painter->setPen(knockoutPen);
    painter->drawLine(p1, p2);

    QPen slashPen(color, slashWidth);
    slashPen.setCapStyle(Qt::RoundCap);
    painter->setPen(slashPen);
    painter->drawLine(p1, p2);

    painter->restore();
}

static void drawDeafenedIcon(QPainter *painter, const QRectF &rect, const QColor &color,
                             const QColor &bgColor)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    qreal w = rect.width();
    qreal h = rect.height();
    qreal cy = rect.top() + h / 2.0;

    // headphone arc
    QPen arcPen(color, qMax(1.2, w * 0.1));
    arcPen.setCapStyle(Qt::RoundCap);
    painter->setPen(arcPen);
    painter->setBrush(Qt::NoBrush);
    QRectF arcRect(rect.left() + w * 0.12, rect.top() + h * 0.18, w * 0.76, h * 0.6);
    painter->drawArc(arcRect, 0, 180 * 16);

    // earcups
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    qreal cupW = w * 0.22;
    qreal cupH = h * 0.32;
    painter->drawRoundedRect(QRectF(rect.left() + w * 0.1, cy, cupW, cupH), cupW / 3.0, cupW / 3.0);
    painter->drawRoundedRect(QRectF(rect.right() - w * 0.1 - cupW, cy, cupW, cupH),
                             cupW / 3.0, cupW / 3.0);

    // slash
    qreal slashWidth = qMax(1.4, w * 0.12);
    QPointF p1(rect.left() + w * 0.08, rect.bottom() - h * 0.08);
    QPointF p2(rect.right() - w * 0.08, rect.top() + h * 0.08);

    QPen knockoutPen(bgColor, slashWidth * 2.2);
    knockoutPen.setCapStyle(Qt::RoundCap);
    painter->setPen(knockoutPen);
    painter->drawLine(p1, p2);

    QPen slashPen(color, slashWidth);
    slashPen.setCapStyle(Qt::RoundCap);
    painter->setPen(slashPen);
    painter->drawLine(p1, p2);

    painter->restore();
}

static void drawMentionBadge(QPainter *painter, const QStyleOptionViewItem &option, int count)
{
    int badgeHeight = option.fontMetrics.height();
    QString text = QString::number(count);

    QFont font = painter->font();
    font.setWeight(QFont::Bold);
    painter->setFont(font);

    QFontMetrics fm(font);
    int padding = fm.height() / 2;
    int textWidth = fm.horizontalAdvance(text);
    int badgeWidth = qMax(badgeHeight, textWidth + padding);

    QRect badgeRect(option.rect.right() - badgeWidth - 4,
                    option.rect.top() + (option.rect.height() - badgeHeight) / 2,
                    badgeWidth, badgeHeight);

    painter->setBrush(QColor(242, 63, 67)); // Qord: Discord's mention red
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);

    painter->setPen(Qt::white);
    painter->drawText(badgeRect, Qt::AlignCenter, text);
}

void ChannelDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    painter->save();

    QModelIndex sourceIndex = proxyModel ? proxyModel->mapToSource(index) : index;
    ChannelNode *node = static_cast<ChannelNode *>(sourceIndex.internalPointer());
    if (!node) {
        painter->restore();
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    painter->fillRect(option.rect, option.palette.base());

    constexpr int iconSize = 24;
    constexpr int pillMargin = 6;

    // content rect is shifted right to leave room for the unread pill
    QStyleOptionViewItem contentOpt = option;
    contentOpt.rect = option.rect.adjusted(pillMargin + indentWidth(node, indentScopes), 0, 0, 0);

    // Qord: PinDMs' headers, in the category's color, with a chevron that
    // points down while open
    if (node->type == ChannelNode::Type::DMPinHeader) {
        const QColor color = node->pinColor ? QColor::fromRgb(node->pinColor)
                                            : Core::Theme::Manager::instance().color(Core::Theme::Token::MutedText);
        QFont f = painter->font();
        f.setPixelSize(12);
        f.setWeight(QFont::DemiBold);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
        painter->setFont(f);
        painter->setPen(color);
        QRect textRect = contentOpt.rect.adjusted(4, 0, -24, 0);
        const QString name = QFontMetrics(f).elidedText(node->name, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, name);
        if (node->pinCollapsible) {
            const int textWidth = QFontMetrics(f).horizontalAdvance(name);
            QRect chevron = contentOpt.rect;
            chevron.setLeft(textRect.left() + textWidth - 2);
            drawLucideIcon(painter, chevron,
                           node->collapsed ? Core::Theme::Icons::Name::ChevronRight
                                           : Core::Theme::Icons::Name::ChevronDown,
                           color);
        }
        painter->restore();
        return;
    }

    if (node->type == ChannelNode::Type::Account) {
        QRect textRect = contentOpt.rect.adjusted(iconSize, 0, -iconSize, 0);
        painter->fillRect(contentOpt.rect, option.palette.alternateBase().color());
        painter->setPen(option.palette.brightText().color());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, node->name);
        painter->restore();
        return;
    }

    if (node->type == ChannelNode::Type::VoiceParticipant) {
        constexpr int avatarSize = 16;
        constexpr int participantIndent = 24;
        int avatarX = contentOpt.rect.left() + participantIndent;
        int avatarY = contentOpt.rect.top() + (contentOpt.rect.height() - avatarSize) / 2;
        QRect avatarRect(avatarX, avatarY, avatarSize, avatarSize);

        constexpr qreal avatarRadius = avatarSize / 2.0; // Qord: round
        QPixmap avatar = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));
        if (!avatar.isNull()) {
            painter->save();
            QPainterPath clip;
            clip.addRoundedRect(avatarRect, avatarRadius, avatarRadius);
            painter->setClipPath(clip);
            painter->drawPixmap(avatarRect, avatar);
            painter->restore();
        } else {
            painter->save();
            painter->setPen(Qt::NoPen);
            QColor defaultColor = option.palette.text().color();
            defaultColor.setAlphaF(0.35f);
            painter->setBrush(defaultColor);
            painter->drawRoundedRect(avatarRect, avatarRadius, avatarRadius);
            painter->restore();
        }

        if (node->dmRecipientId.isValid()) {
            constexpr int statusDotSize = 6;
            StatusIndicator::paintOnAvatar(
                    *painter, avatarRect, statusDotSize,
                    index.data(ChannelTreeModel::PresenceBadgeRole).value<Core::PresenceBadge>(),
                    option.palette.color(QPalette::Base));
        }

        bool muted = index.data(ChannelTreeModel::IsVoiceMutedRole).toBool();
        bool deafened = index.data(ChannelTreeModel::IsVoiceDeafenedRole).toBool();
        constexpr int statusIconSize = 14;
        int iconCount = (muted ? 1 : 0) + (deafened ? 1 : 0);
        int rightReserve = iconCount > 0 ? (iconCount * (statusIconSize + 4) + 4) : 4;
        // Qord: Go Live and cameras
        bool live = false;
        bool camera = false;
        if (liveProvider() || cameraProvider()) {
            const ChannelNode *account = node;
            while (account && account->type != ChannelNode::Type::Account)
                account = account->parent;
            live = account && liveProvider() && liveProvider()(quint64(account->id), quint64(node->id));
            camera = account && cameraProvider() && cameraProvider()(quint64(account->id), quint64(node->id));
        }
        constexpr int cameraWidth = 20;
        if (camera) {
            // Discord's video camera glyph, in the muted text colour
            const QRectF body(contentOpt.rect.right() - rightReserve - cameraWidth + 3,
                              contentOpt.rect.center().y() - 4.5, 9, 9);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(Core::Theme::Manager::instance().color(Core::Theme::Token::MutedText), 1.4,
                                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(body, 1.5, 1.5);
            QPolygonF lens;
            lens << QPointF(body.right(), body.center().y() - 1.5) << QPointF(body.right() + 4, body.top() + 0.5)
                 << QPointF(body.right() + 4, body.bottom() - 0.5) << QPointF(body.right(), body.center().y() + 1.5);
            painter->drawPolygon(lens);
            painter->restore();
            rightReserve += cameraWidth;
        }
        constexpr int liveWidth = 30;
        if (live) {
            const QRect liveRect(contentOpt.rect.right() - rightReserve - liveWidth + 2,
                                 contentOpt.rect.center().y() - 7, liveWidth - 4, 14);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0xED, 0x42, 0x45));
            painter->drawRoundedRect(liveRect, 7, 7);
            QFont liveFont = painter->font();
            liveFont.setPixelSize(9);
            liveFont.setBold(true);
            painter->setFont(liveFont);
            painter->setPen(Qt::white);
            painter->drawText(liveRect, Qt::AlignCenter, QStringLiteral("LIVE"));
            painter->restore();
            rightReserve += liveWidth;
        }

        // Qord: plugin label (CallTimer)
        std::optional<VoiceLabel> voiceLabel;
        if (voiceLabelProvider()) {
            const ChannelNode *account = node;
            while (account && account->type != ChannelNode::Type::Account)
                account = account->parent;
            const ChannelNode *guild = ChannelTreeModel::findGuildNode(const_cast<ChannelNode *>(node));
            if (account)
                voiceLabel = voiceLabelProvider()(quint64(account->id), guild ? quint64(guild->id) : 0,
                                                  quint64(node->parentId), quint64(node->id));
        }
        voiceLabelTips.remove(node);
        QFont labelFont = painter->font();
        labelFont.setPixelSize(11);
        const int labelWidth = !voiceLabel ? 0
                               : voiceLabel->inlineText ? QFontMetrics(labelFont).horizontalAdvance(voiceLabel->text) + 6
                                                        : 16;

        QColor textColor = option.palette.text().color();
        if (Core::roleColorOptions().voiceUsers && nameColorProvider()) {
            const ChannelNode *account = node;
            while (account && account->type != ChannelNode::Type::Account)
                account = account->parent;
            const ChannelNode *guild = ChannelTreeModel::findGuildNode(const_cast<ChannelNode *>(node));
            if (account && guild) {
                const QColor roleColor =
                        nameColorProvider()(quint64(account->id), quint64(guild->id), quint64(node->id));
                if (roleColor.isValid())
                    textColor = roleColor;
            }
        }
        QRect textRect =
                contentOpt.rect.adjusted(participantIndent + avatarSize + 6, 0, -rightReserve - labelWidth, 0);
        painter->setPen(textColor);
        QString elidedName = painter->fontMetrics().elidedText(
                index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

        if (voiceLabel) {
            const int labelX = textRect.left() + painter->fontMetrics().horizontalAdvance(elidedName) + 6;
            const QColor muted = Core::Theme::Manager::instance().color(Core::Theme::Token::MutedText);
            if (voiceLabel->inlineText) {
                painter->save();
                painter->setFont(labelFont);
                painter->setPen(muted);
                painter->drawText(QRect(labelX, contentOpt.rect.top(), labelWidth, contentOpt.rect.height()),
                                  Qt::AlignLeft | Qt::AlignVCenter, voiceLabel->text);
                painter->restore();
            } else {
                // A small clock; the time is its tooltip
                const QRectF clock(labelX, contentOpt.rect.center().y() - 5.5, 11, 11);
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(QPen(muted, 1.4));
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(clock);
                painter->drawLine(clock.center(), clock.center() - QPointF(0, 3.2));
                painter->drawLine(clock.center(), clock.center() + QPointF(2.4, 0));
                painter->restore();
                voiceLabelTips.insert(node, { clock.toAlignedRect().adjusted(-2, -2, 2, 2), voiceLabel->text });
            }
        }

        if (muted || deafened) {
            QColor iconColor = option.palette.text().color();
            iconColor.setAlphaF(0.7f);
            QColor bgColor = option.palette.base().color();
            int iconY = contentOpt.rect.top() + (contentOpt.rect.height() - statusIconSize) / 2;
            int iconX = contentOpt.rect.right() - statusIconSize - 4;
            if (deafened) {
                drawDeafenedIcon(painter,
                                 QRectF(iconX, iconY, statusIconSize, statusIconSize), iconColor,
                                 bgColor);
                iconX -= (statusIconSize + 4);
            }
            if (muted) {
                drawMutedMicIcon(painter,
                                 QRectF(iconX, iconY, statusIconSize, statusIconSize), iconColor,
                                 bgColor);
            }
        }

        painter->restore();
        return;
    }

    if (node->type == ChannelNode::Type::Folder) {
        if (node->folderColor.has_value())
            painter->fillRect(contentOpt.rect, QColor::fromRgb(node->folderColor.value()));
        else
            painter->fillRect(contentOpt.rect, option.palette.alternateBase());

        painter->setFont(QFont(painter->font().family(), painter->font().pointSize(), QFont::Bold));
        QRect textRect = contentOpt.rect.adjusted(iconSize, 0, -iconSize, 0);
        painter->setPen(option.palette.text().color());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, node->name);

        drawBranchIndicator(painter, contentOpt, option.state & QStyle::State_Open);

        if (node->isUnread && !node->isMuted)
            drawUnreadPill(painter, option);

        if (node->mentionCount > 0)
            drawMentionBadge(painter, contentOpt, node->mentionCount);

        painter->restore();
        return;
    }

    if (node->type == ChannelNode::Type::Server || node->type == ChannelNode::Type::DMChannel) {
        QRect iconRect = QRect(contentOpt.rect.left(),
                               contentOpt.rect.top() + (contentOpt.rect.height() - iconSize) / 2,
                               iconSize, iconSize);

        QPixmap icon = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            // Qord: round icons and avatars, like Discord
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            QPainterPath clip;
            clip.addEllipse(QRectF(iconRect));
            painter->setClipPath(clip);
            painter->drawPixmap(iconRect, icon);
            painter->restore();
        }

        if (node->type == ChannelNode::Type::DMChannel && node->dmRecipientId.isValid()) {
            constexpr int statusDotSize = 8;
            StatusIndicator::paintOnAvatar(
                    *painter, iconRect, statusDotSize,
                    index.data(ChannelTreeModel::PresenceBadgeRole).value<Core::PresenceBadge>(),
                    option.palette.color(QPalette::Base));
        }
    }

    // determine text color
    QColor textColor = option.palette.text().color();
    bool isSelected = index.data(ChannelFilterProxyModel::SelectedRole).toBool();
    const auto &theme = Core::Theme::Manager::instance();
    const bool rowIsChannel = node->opensChat() || node->type == ChannelNode::Type::VoiceChannel;
    if (rowIsChannel && (isSelected || (option.state & QStyle::State_MouseOver))) {
        // Qord: Discord's rounded hover/selected highlight
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.color(isSelected ? Core::Theme::Token::SelectedBg : Core::Theme::Token::HoverBg));
        painter->drawRoundedRect(QRectF(contentOpt.rect.adjusted(-4, 1, -6, -1)), 4, 4);
        painter->restore();
    }
    if (node->opensChat()) {
        textColor = theme.color(Core::Theme::Token::MutedText);
        if (node->isMuted)
            textColor = option.palette.text().color().darker(150);
        else if (isSelected || node->isUnread)
            textColor = theme.color(Core::Theme::Token::HeaderText);
        else if (option.state & QStyle::State_MouseOver)
            textColor = option.palette.text().color();
        if (node->isUnread && !node->isMuted && !isSelected) {
            QFont f = painter->font();
            f.setWeight(QFont::DemiBold);
            painter->setFont(f);
        }
    } else if (node->type == ChannelNode::Type::Server) {
        if (node->isUnread && !node->isMuted)
            textColor = option.palette.brightText().color();
    } else if (node->type == ChannelNode::Type::Category) {
        textColor = theme.color(Core::Theme::Token::MutedText);
        if (node->isMuted)
            textColor = textColor.darker(130);
        // Qord: Discord's small uppercase category headers
        QFont f = painter->font();
        f.setPixelSize(12);
        f.setWeight(QFont::DemiBold);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
        painter->setFont(f);
    } else if (node->type == ChannelNode::Type::VoiceChannel) {
        textColor = theme.color(Core::Theme::Token::MutedText);
        if (option.state & QStyle::State_MouseOver)
            textColor = option.palette.text().color();
    }

    // Qord: ShowHiddenChannels' channels you can't see
    const auto &hiddenOptions = ChannelTreeModel::hiddenChannelOptions();
    const bool hidden = hiddenOptions.show && !index.data(ChannelTreeModel::CanViewRole).toBool();
    if (hidden && hiddenOptions.muted)
        textColor = option.palette.text().color().darker(150);
    const bool lockReplacesIcon = hidden && hiddenOptions.icon == ChannelTreeModel::HiddenChannelOptions::Icon::Lock;

    if (lockReplacesIcon) {
        drawLucideIcon(painter, contentOpt.rect, Core::Theme::Icons::Name::Lock, textColor);
    } else if (node->type == ChannelNode::Type::Channel) {
        const ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
        if (guildNode && guildNode->rulesChannelId == node->id)
            drawLucideIcon(painter, contentOpt.rect, Core::Theme::Icons::Name::BookCheck, textColor);
        else if (node->isAnnouncement)
            drawLucideIcon(painter, contentOpt.rect, Core::Theme::Icons::Name::Megaphone, textColor);
        else
            drawHashIcon(painter, contentOpt.rect, textColor);
        if (node->isPrivate)
            drawPadlockOverlay(painter, contentOpt.rect, textColor, option.palette.base().color());
    } else if (node->type == ChannelNode::Type::Forum) {
        drawForumIcon(painter, contentOpt.rect, textColor);
        if (node->isPrivate)
            drawPadlockOverlay(painter, contentOpt.rect, textColor, option.palette.base().color());
    } else if (node->type == ChannelNode::Type::Thread) {
        drawThreadBranchIcon(painter, contentOpt.rect, textColor, isLastThreadSibling(node));
    } else if (node->type == ChannelNode::Type::VoiceChannel) {
        drawSpeakerIcon(painter, contentOpt.rect, textColor);
        if (node->isPrivate)
            drawPadlockOverlay(painter, contentOpt.rect, textColor, option.palette.base().color());
    }

    const bool showForumBadge =
            node->type == ChannelNode::Type::Forum && node->forumBadgeCount > 0 && !node->isMuted;
    const QString forumBadgeText =
            !showForumBadge ? QString()
            : node->forumBadgeIsNew
                    ? ChannelDelegate::tr("%1 New").arg(node->forumBadgeCount)
                    : QString::number(node->forumBadgeCount);

    // the mention badge takes the same spot, so the user limit yields to it
    const bool showUserLimit = node->type == ChannelNode::Type::VoiceChannel && node->userLimit > 0 && node->mentionCount == 0;

    int rightReserve = iconSize;
    if (showUserLimit) {
        QString countText = QStringLiteral("%1/%2").arg(node->voiceParticipantCount).arg(node->userLimit);
        QFontMetrics fm(painter->font());
        rightReserve = fm.horizontalAdvance(countText) + fm.height() / 2 + 8;
    } else if (showForumBadge) {
        rightReserve = painter->fontMetrics().horizontalAdvance(forumBadgeText) + 12;
    }

    // Qord: TypingIndicator's avatars and dots, on the right
    typingTips.remove(node);
    const auto typing = index.data(ChannelTreeModel::TypingIndicatorRole).value<ChannelTreeModel::ChannelTyping>();
    if (!typing.avatars.isEmpty() || typing.dots) {
        constexpr int avatarSize = 16;
        constexpr int avatarStep = 12;
        const QFontMetrics fm(painter->font());
        const QString more = typing.moreUsers > 0 ? QStringLiteral("+%1").arg(typing.moreUsers) : QString();
        int width = 0;
        if (!typing.avatars.isEmpty())
            width += avatarSize + (int(typing.avatars.size()) - 1) * avatarStep;
        if (!more.isEmpty())
            width += 3 + fm.horizontalAdvance(more);
        if (typing.dots)
            width += (width ? 4 : 0) + 20;
        const int top = contentOpt.rect.top() + (contentOpt.rect.height() - avatarSize) / 2;
        int x = contentOpt.rect.right() - rightReserve - width + 4;
        const QRect area(x, top, width, avatarSize);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        for (const QPixmap &avatar : typing.avatars) {
            const QRectF circle(x, top, avatarSize, avatarSize);
            painter->setPen(QPen(option.palette.base().color(), 2));
            painter->setBrush(Qt::NoBrush);
            QPainterPath clip;
            clip.addEllipse(circle);
            painter->save();
            painter->setClipPath(clip);
            painter->drawPixmap(circle.toRect(), avatar);
            painter->restore();
            painter->drawEllipse(circle);
            x += avatarStep;
        }
        if (!typing.avatars.isEmpty())
            x += avatarSize - avatarStep;
        if (!more.isEmpty()) {
            painter->setPen(textColor);
            painter->drawText(QRect(x + 3, contentOpt.rect.top(), fm.horizontalAdvance(more), contentOpt.rect.height()),
                              Qt::AlignVCenter, more);
            x += 3 + fm.horizontalAdvance(more);
        }
        if (typing.dots) {
            if (!typing.avatars.isEmpty() || !more.isEmpty())
                x += 4;
            const int phase = typingPhase() % 8;
            painter->setPen(Qt::NoPen);
            for (int i = 0; i < 3; ++i) {
                QColor dot = textColor;
                dot.setAlphaF(phase / 2 == i ? 1.0 : 0.35);
                painter->setBrush(dot);
                painter->drawEllipse(QPointF(x + 3 + i * 7, contentOpt.rect.center().y() + 0.5), 3, 3);
            }
        }
        painter->restore();
        if (!typing.tooltip.isEmpty())
            typingTips.insert(node, { area.adjusted(-2, -2, 2, 2), typing.tooltip });
        rightReserve += width + 4;
    }

    hiddenIconTips.remove(node);
    if (hidden && !lockReplacesIcon) {
        // The lock or crossed-out eye on the right, before any badge
        constexpr int hiddenIconSize = 16;
        const QRect hiddenIcon(contentOpt.rect.right() - rightReserve - hiddenIconSize + 4,
                               contentOpt.rect.top() + (contentOpt.rect.height() - hiddenIconSize) / 2, hiddenIconSize,
                               hiddenIconSize);
        const bool eye = hiddenOptions.icon == ChannelTreeModel::HiddenChannelOptions::Icon::EyeRight;
        painter->drawPixmap(hiddenIcon, Core::Theme::Icons::pixmap(eye ? Core::Theme::Icons::Name::EyeOff
                                                                       : Core::Theme::Icons::Name::Lock,
                                                                   hiddenIconSize, textColor, painter->device()->devicePixelRatioF()));
        hiddenIconTips.insert(node, { hiddenIcon.adjusted(-2, -2, 2, 2), ChannelDelegate::tr("Hidden Channel") });
        rightReserve += hiddenIconSize + 4;
    }

    QRect textRect = contentOpt.rect.adjusted(iconSize, 0, -rightReserve, 0);
    painter->setPen(textColor);
    QString elidedName = painter->fontMetrics().elidedText(
            index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    // branch indicator for categories
    if (node->type == ChannelNode::Type::Category)
        drawBranchIndicator(painter, contentOpt, !node->collapsed);

    // unread pill for channels and servers (draws in the left margin)
    if ((node->opensChat() || node->type == ChannelNode::Type::Server) && node->isUnread &&
        !node->isMuted && !(hidden && hiddenOptions.muted))
        drawUnreadPill(painter, option);

    if (showUserLimit) {
        QString countText = QStringLiteral("%1/%2").arg(node->voiceParticipantCount).arg(node->userLimit);
        QFontMetrics fm(painter->font());
        int textWidth = fm.horizontalAdvance(countText);
        int badgeH = fm.height();
        int padding = badgeH / 2;
        int badgeW = textWidth + padding;
        QRect badgeRect(contentOpt.rect.right() - badgeW - 4,
                        contentOpt.rect.top() + (contentOpt.rect.height() - badgeH) / 2,
                        badgeW, badgeH);

        QColor bgColor = option.palette.text().color();
        bgColor.setAlphaF(0.15f);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bgColor);
        painter->drawRoundedRect(badgeRect, badgeH / 2.0, badgeH / 2.0);

        painter->setPen(textColor);
        painter->drawText(badgeRect, Qt::AlignCenter, countText);
    }

    if (showForumBadge && node->mentionCount == 0) {
        QFontMetrics fm(painter->font());
        QRect badgeRect(contentOpt.rect.right() - fm.horizontalAdvance(forumBadgeText) - 4,
                        contentOpt.rect.top(),
                        fm.horizontalAdvance(forumBadgeText), contentOpt.rect.height());
        painter->setPen(node->forumBadgeIsNew ? option.palette.highlight().color()
                                              : option.palette.text().color().darker(150));
        painter->drawText(badgeRect, Qt::AlignRight | Qt::AlignVCenter, forumBadgeText);
    }

    // mention badge for channels, servers
    if (node->type != ChannelNode::Type::Category && node->type != ChannelNode::Type::DMHeader && node->mentionCount > 0)
        drawMentionBadge(painter, contentOpt, node->mentionCount);

    painter->restore();
}

void ChannelDelegate::setIndentScopes(ChannelIndent::Scopes scopes)
{
    indentScopes = scopes;
}

QSize ChannelDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize sz = QStyledItemDelegate::sizeHint(option, index);
    return QSize(sz.width(), 24);
}
} // namespace UI
} // namespace Acheron
