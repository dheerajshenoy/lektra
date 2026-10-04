#pragma once

#include <QLabel>

class ElidableLabel : public QLabel
{
    Q_OBJECT
public:
    explicit ElidableLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setFullText(const QString &text)
    {
        m_fullText = text.simplified();
        updateElidedText();
    }

    void updateElidedText() noexcept
    {
        QFontMetrics metrics(font());
        QString elided = metrics.elidedText(m_fullText, Qt::ElideRight, width());
        setText(elided);
    }

    // The size the whole text wants (not the shortened text that is shown),
    // and a small minimum, so a layout can give it room or squeeze it.
    QSize sizeHint() const override
    {
        const QSize base = QLabel::sizeHint();
        // A few pixels more than the text: eliding at exactly its width can
        // still cut a character because of rounding.
        return {fontMetrics().horizontalAdvance(m_fullText) + 2 * margin()
                    + 2 * frameWidth() + 4,
                base.height()};
    }

    QSize minimumSizeHint() const override
    {
        const QSize base = QLabel::minimumSizeHint();
        return {fontMetrics().horizontalAdvance(QStringLiteral("MMM")),
                base.height()};
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        updateElidedText();
        QLabel::resizeEvent(e);
    }

private:
    QString m_fullText;
};
