#pragma once

#include <QLayout>
#include <QList>

namespace Acheron {
namespace UI {

// Lays widgets out left to right, wrapping to a new line when the width
// runs out (CSS's flex-wrap: wrap), with the same gap both ways.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int gap = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    [[nodiscard]] int count() const override { return int(items.size()); }
    [[nodiscard]] QLayoutItem *itemAt(int index) const override { return items.value(index); }
    QLayoutItem *takeAt(int index) override;
    void insertWidget(int index, QWidget *widget);
    [[nodiscard]] int indexOfWidget(const QWidget *widget) const;

    [[nodiscard]] Qt::Orientations expandingDirections() const override { return {}; }
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), true); }
    [[nodiscard]] QSize sizeHint() const override { return minimumSize(); }
    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;

private:
    int arrange(const QRect &rect, bool measureOnly) const;

    QList<QLayoutItem *> items;
    int gap;
};

} // namespace UI
} // namespace Acheron
