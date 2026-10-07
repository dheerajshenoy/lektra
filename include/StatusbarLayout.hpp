#pragma once

#include <QLayout>
#include <QList>

// The layout engine of the statusbar. Items sit in one or more rows. In a row,
// items are placed left to right in the order they were added, like a flexbox:
//
//  * an item has its natural width (clamped by minWidth / maxWidth);
//  * space left over is shared between the items with a `stretch` > 0, in
//    proportion to their stretch (gaps are items too, so a gap with a stretch
//    pushes the items after it away, which is how "left / centre / right" is
//    made, and any other arrangement);
//  * if there is not enough room, items shrink down to their minimum width;
//  * an item can instead be placed `absolute`: at a fraction of the row's
//    width, not taking any room in the flow.
//
// Items that are hidden, or that have no size, take no room (and no margin).
class StatusbarLayout : public QLayout
{
public:
    struct Spec
    {
        double stretch       = 0; // share of the free space, 0: natural width
        int minWidth         = 0; // px, 0: the widget's own minimum
        int maxWidth         = 0; // px, 0: no limit
        int marginLeft       = 0; // px around the item
        int marginRight      = 0;
        // Where the widget sits inside its slot when the slot is wider.
        Qt::Alignment align  = Qt::AlignLeft;
        // A gap has no widget; its width is `gapWidth` (plus its stretch).
        bool gap             = false;
        int gapWidth         = 0;
        // Placed at `at` (0 to 1) of the row's width, with its `anchor` edge
        // there, outside the flow.
        bool absolute        = false;
        double at            = 0;
        Qt::Alignment anchor = Qt::AlignLeft;
    };

    explicit StatusbarLayout(QWidget *parent = nullptr);
    ~StatusbarLayout() override;

    void addWidgetTo(int row, QWidget *widget, const Spec &spec);
    void addGapTo(int row, const Spec &spec);
    // Removes every item. The widgets are kept (and hidden by the caller).
    void clear();

    // QLayout
    void addItem(QLayoutItem *item) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    int count() const override;
    Qt::Orientations expandingDirections() const override
    {
        return Qt::Horizontal;
    }

private:
    struct Entry
    {
        QLayoutItem *item = nullptr;
        int row           = 0;
        Spec spec;
    };

    static bool isEmpty(const Entry &e);
    static int naturalWidth(const Entry &e);
    static int minimumWidth(const Entry &e);
    QList<int> rowNumbers() const;
    int rowHeight(int row) const;
    void layoutRow(int row, const QRect &rect);

    QList<Entry> m_entries;
};
