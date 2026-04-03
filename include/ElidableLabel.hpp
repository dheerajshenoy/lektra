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

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        updateElidedText();
        QLabel::resizeEvent(e);
    }

private:
    QString m_fullText;
};
