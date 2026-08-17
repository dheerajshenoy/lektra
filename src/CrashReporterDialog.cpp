#include "CrashReporterDialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

static constexpr char GITHUB_ISSUES_URL[]
    = "https://github.com/dheerajshenoy/lektra/issues/new"
      "?labels=crash&template=crash_report.md"
      "&title=Crash+Report+(v" APP_VERSION ")";

CrashReporterDialog::CrashReporterDialog(const QString &logPath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Lektra crashed"));
    setMinimumSize(680, 460);

    // ── header ──────────────────────────────────────────────────────────────
    auto *iconLabel = new QLabel(this);
    iconLabel->setPixmap(
        style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(48, 48));

    auto *msgLabel = new QLabel(
        tr("<b>Lektra has crashed.</b><br>"
           "The crash report below may help the developers diagnose the issue.<br>"
           "Please consider reporting it on GitHub so it can be fixed."),
        this);
    msgLabel->setWordWrap(true);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    headerLayout->addSpacing(12);
    headerLayout->addWidget(msgLabel, 1);

    // ── log view ────────────────────────────────────────────────────────────
    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(9);
    m_logView->setFont(mono);

    QString content;
    QFile f(logPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        content = QString::fromLocal8Bit(f.readAll());
    else
        content = tr("(crash log not found at: %1)").arg(logPath);

    m_logView->setPlainText(content);

    // ── buttons ─────────────────────────────────────────────────────────────
    m_copyBtn = new QPushButton(tr("Copy to Clipboard"), this);
    auto *reportBtn = new QPushButton(tr("Report on GitHub"), this);
    auto *closeBtn  = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);

    connect(m_copyBtn,  &QPushButton::clicked, this, &CrashReporterDialog::copyToClipboard);
    connect(reportBtn,  &QPushButton::clicked, this, &CrashReporterDialog::openGitHubIssues);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::accept);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_copyBtn);
    btnLayout->addWidget(reportBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);

    // ── main layout ─────────────────────────────────────────────────────────
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(headerLayout);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(m_logView, 1);
    mainLayout->addSpacing(4);
    mainLayout->addLayout(btnLayout);
}

void CrashReporterDialog::copyToClipboard()
{
    QApplication::clipboard()->setText(m_logView->toPlainText());
    m_copyBtn->setText(tr("Copied!"));
}

void CrashReporterDialog::openGitHubIssues()
{
    QDesktopServices::openUrl(QUrl(QLatin1String(GITHUB_ISSUES_URL)));
}
