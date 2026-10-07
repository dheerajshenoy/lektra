#pragma once

#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

class InputDialog : public QDialog
{
    Q_OBJECT
public:
    InputDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setMinimumWidth(400);

        QVBoxLayout *layout = new QVBoxLayout(this);
        m_infoLabel         = new QLabel(this);
        m_infoLabel->setWordWrap(true);
        m_commentBox = new QTextEdit(this);

        m_commentBox->setMinimumHeight(100);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        const QString btnStyle
            = "QPushButton { border: 1px solid palette(mid); border-radius: "
              "4px;"
              " padding: 4px 16px; background: palette(button); color: "
              "palette(button-text); }"
              "QPushButton:hover { background: palette(light); }"
              "QPushButton:pressed { background: palette(dark); }";
        for (QAbstractButton *btn : buttons->buttons())
            btn->setStyleSheet(btnStyle);

        layout->addWidget(m_infoLabel);
        layout->addWidget(m_commentBox);
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    static QString getText(const QString &title, const QString &infoText,
                           const QString &placeholderText, const QString &text,
                           bool &ok, QWidget *parent = nullptr) noexcept
    {
        InputDialog dlg(parent); // instantiate locally
        dlg.setWindowTitle(title);
        dlg.m_infoLabel->setText(infoText);
        dlg.m_commentBox->setPlaceholderText(placeholderText);
        dlg.m_commentBox->setText(text);
        dlg.m_commentBox->selectAll();

        if (dlg.exec() == QDialog::Accepted)
        {
            const QString comment = dlg.m_commentBox->toPlainText().trimmed();
            ok                    = !comment.isEmpty();
            return comment;
        }

        ok = false;
        return {};
    }

private:
    QLabel *m_infoLabel     = nullptr;
    QTextEdit *m_commentBox = nullptr;
};
