#include "CrashReporterDialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <vector>

static constexpr char GITHUB_ISSUES_URL[]
    = "https://github.com/dheerajshenoy/lektra/issues/new"
      "?labels=crash&template=crash_report.md"
      "&title=Crash+Report+(v" APP_VERSION ")";

namespace
{

// CrashHandler (POSIX) writes a "=== Frame Offsets (for symbolization) ==="
// section: one "<module_path> <hex_offset>" line per stack frame, the
// offset already made file-relative (module load base subtracted out) so
// it's directly usable with `addr2line -e <module_path>`. Raw
// backtrace_symbols_fd() output alone is close to useless on a Release
// build — no line info, and no name at all for any non-exported frame — so
// resolve these properly here where a normal process (QProcess, no
// signal-handler restrictions) can shell out to addr2line.
QString
symbolizeFrameOffsets(const QString &content)
{
    static const QString marker
        = QStringLiteral("=== Frame Offsets (for symbolization) ===");
    const int idx = content.indexOf(marker);
    if (idx < 0)
        return {};

    const QString addr2line = QStandardPaths::findExecutable("addr2line");
    if (addr2line.isEmpty())
        return {};

    struct Frame
    {
        QString module;
        QString offset;
    };
    std::vector<Frame> frames;

    const QStringList lines
        = content.mid(idx + marker.size()).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines)
    {
        const int sp = line.lastIndexOf(' ');
        if (sp <= 0)
            break; // end of the offsets section (next "===" header or blank)
        frames.push_back({line.left(sp), line.mid(sp + 1)});
    }
    if (frames.empty())
        return {};

    QMap<QString, QStringList> offsetsByModule;
    for (const Frame &f : frames)
        if (f.module != QLatin1String("?"))
            offsetsByModule[f.module].append(f.offset);

    QMap<QString, QStringList> resolvedByModule;
    for (auto it = offsetsByModule.constBegin(); it != offsetsByModule.constEnd();
        ++it)
    {
        QProcess proc;
        proc.start(addr2line,
                   QStringList{"-e", it.key(), "-f", "-C", "-p"} + it.value());
        if (!proc.waitForFinished(3000))
            continue;
        resolvedByModule[it.key()] = QString::fromLocal8Bit(
                                         proc.readAllStandardOutput())
                                         .split('\n', Qt::SkipEmptyParts);
    }
    if (resolvedByModule.isEmpty())
        return {};

    QString out = QStringLiteral("\n=== Symbolized Stack Trace ===\n");
    QMap<QString, int> cursor;
    for (const Frame &f : frames)
    {
        const QStringList *lines2
            = f.module == QLatin1String("?")
                  ? nullptr
                  : (resolvedByModule.contains(f.module)
                         ? &resolvedByModule[f.module]
                         : nullptr);
        int &i = cursor[f.module];
        if (lines2 && i < lines2->size())
            out += QStringLiteral("  ") + lines2->at(i) + '\n';
        else
            out += QStringLiteral("  ?? ??:0\n");
        ++i;
    }
    return out;
}

} // namespace

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

    content += symbolizeFrameOffsets(content);

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
