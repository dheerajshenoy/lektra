
#include "SaveSessionDialog.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

SaveSessionDialog::SaveSessionDialog(const QStringList &existingSessions,
                                     QWidget *parent)
    : QDialog(parent), m_existingSessions(existingSessions)
{
    setWindowTitle(tr("Save Session"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *label = new QLabel(tr("Enter session name:"), this);
    mainLayout->addWidget(label);

    m_lineEdit = new QLineEdit(this);
    mainLayout->addWidget(m_lineEdit);

    m_existingList = new QListWidget(this);
    m_existingList->addItems(existingSessions);
    mainLayout->addWidget(m_existingList);

    auto *buttonLayout = new QHBoxLayout();
    m_saveButton       = new QPushButton(tr("Save"), this);
    auto *cancelButton = new QPushButton(tr("Cancel"), this);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_saveButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_saveButton, &QPushButton::clicked, this,
            &SaveSessionDialog::onAccept);
    connect(cancelButton, &QPushButton::clicked, this,
            &SaveSessionDialog::reject);
    connect(m_existingList, &QListWidget::itemDoubleClicked, this,
            &SaveSessionDialog::onDoubleClickExistingItem);
}

QString
SaveSessionDialog::sessionName() const
{
    return m_sessionName;
}

void
SaveSessionDialog::onAccept()
{
    QString name = m_lineEdit->text().trimmed();

    if (name.isEmpty())
        return;

    m_sessionName = m_lineEdit->text();
    accept();
}

void
SaveSessionDialog::onDoubleClickExistingItem()
{
    QListWidgetItem *item = m_existingList->currentItem();
    if (item)
    {
        m_lineEdit->setText(item->text());
    }
}
