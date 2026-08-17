#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;
class QPushButton;

class CrashReporterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CrashReporterDialog(const QString &logPath, QWidget *parent = nullptr);

private:
    void copyToClipboard();
    void openGitHubIssues();

    QPlainTextEdit *m_logView = nullptr;
    QPushButton    *m_copyBtn = nullptr;
};
