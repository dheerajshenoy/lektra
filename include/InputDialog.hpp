#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

class InputDialog : public QDialog
{
public:
    InputDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        QVBoxLayout *layout = new QVBoxLayout(this);
        m_infoLabel         = new QLabel(this);
        m_infoLabel->setWordWrap(true);
        m_commentBox = new QTextEdit(this);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        QPushButton *okBtn     = new QPushButton("OK", this);
        QPushButton *cancelBtn = new QPushButton("Cancel", this);
        btnLayout->addWidget(okBtn);
        btnLayout->addWidget(cancelBtn);

        layout->addWidget(m_infoLabel);
        layout->addWidget(m_commentBox);
        layout->addLayout(btnLayout);

        connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
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
    QLabel *m_infoLabel;
    QTextEdit *m_commentBox;
};
