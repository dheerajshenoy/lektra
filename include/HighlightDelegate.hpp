// HighlightDelegate.h
#pragma once
#include <QPair>
#include <QStyledItemDelegate>
#include <QVector>

class MatchHighlighter
{
public:
    virtual ~MatchHighlighter() = default;
    // Return [start, length] pairs for `text`. Empty = nothing to highlight.
    virtual QVector<QPair<int, int>> ranges(const QString &text) const = 0;
    virtual bool isActive() const                                      = 0;
};

class HighlightDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit HighlightDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void setHighlighter(const MatchHighlighter *h)
    {
        m_highlighter = h;
    }
    void setHighlightColor(const QColor &c)
    {
        m_color = c;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    const MatchHighlighter *m_highlighter = nullptr;
    QColor m_color                        = QColor(255, 235, 59); // yellow
};
