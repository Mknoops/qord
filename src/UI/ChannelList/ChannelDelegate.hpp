#pragma once

#include <QtWidgets>

#include "ChannelIndent.hpp"

#include <functional>
#include <optional>

class QAbstractProxyModel;

namespace Acheron {
namespace UI {
class ChannelDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChannelDelegate(QAbstractProxyModel *proxyModel = nullptr, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    void setIndentScopes(ChannelIndent::Scopes scopes);

    // Qord: text plugins add to voice participants (CallTimer): shown after the
    // name, or as a clock icon with the text as tooltip
    struct VoiceLabel
    {
        QString text;
        bool inlineText = false;
    };
    using VoiceLabelProvider = std::function<std::optional<VoiceLabel>(
            quint64 accountId, quint64 guildId, quint64 channelId, quint64 userId)>;
    static void setVoiceLabelProvider(VoiceLabelProvider provider);
    // Qord: whether a voice participant is streaming (Go Live): a red LIVE badge
    using LiveProvider = std::function<bool(quint64 accountId, quint64 userId)>;
    static void setLiveProvider(LiveProvider provider);
    // Qord: whether a voice participant has their camera on: a camera icon
    using CameraProvider = std::function<bool(quint64 accountId, quint64 userId)>;
    static void setCameraProvider(CameraProvider provider);
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

private:
    mutable QHash<const void *, QPair<QRect, QString>> voiceLabelTips;
    QAbstractProxyModel *proxyModel;
    ChannelIndent::Scopes indentScopes;
};
} // namespace UI
} // namespace Acheron
