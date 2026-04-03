#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QListWidget;
class QPushButton;

class SaveSessionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SaveSessionDialog(const QStringList &existingSessions,
                               QWidget *parent = nullptr);

    QString sessionName() const;

private slots:
    void onAccept();
    void onDoubleClickExistingItem();

private:
    QLineEdit *m_lineEdit;
    QListWidget *m_existingList;
    QPushButton *m_saveButton;
    QStringList m_existingSessions;
    QString m_sessionName;
};
