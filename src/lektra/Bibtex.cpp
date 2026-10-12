#include "DocumentView.hpp"
#include "Lektra.hpp"
#include "MessageBar.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

namespace
{
// A DOI ends where the surrounding prose does: drop trailing punctuation.
QString
cleanDoi(QString doi) noexcept
{
    while (!doi.isEmpty() && QStringLiteral(".,;:)]}>\"'").contains(doi.back()))
        doi.chop(1);
    return doi;
}

// The DOI in `text`. A labelled one (doi:, DOI:, doi.org/) is preferred over
// a bare one, which may just be a reference to another paper.
QString
findDoi(const QString &text, bool labelledOnly = false) noexcept
{
    static const QRegularExpression labelled(
        R"((?:doi\s*:|doi\.org/)\s*(10\.\d{4,9}/[^\s"<>]+))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bare(R"((10\.\d{4,9}/[^\s"<>]+))");

    auto m = labelled.match(text);
    if (m.hasMatch())
        return cleanDoi(m.captured(1));
    if (labelledOnly)
        return {};
    m = bare.match(text);
    return m.hasMatch() ? cleanDoi(m.captured(1)) : QString();
}

QString
findArxivId(const QString &text) noexcept
{
    static const QRegularExpression re(
        R"(arXiv\s*:\s*(\d{4}\.\d{4,5}|[a-z\-]+(?:\.[A-Z]{2})?/\d{7}))",
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString
citationKey(const QString &entry) noexcept
{
    static const QRegularExpression re(R"(^\s*@\w+\s*\{\s*([^,\s]+)\s*,)");
    const auto m = re.match(entry);
    return m.hasMatch() ? m.captured(1) : QString();
}
} // namespace

// Copies the BibTeX entry of the open paper to the clipboard. The DOI (or,
// without one, the arXiv number) is looked up in the PDF's metadata and then
// in the text of the first two pages; the entry is fetched with curl in the
// background.
void
Lektra::Bibtex() noexcept
{
    if (!m_doc)
        return;

    Model *model = m_doc->model();
    if (!model || model->numPages() <= 0)
        return;

    QString doi;
    QString arxiv;

    for (const auto &[key, value] : model->properties())
    {
        doi = findDoi(value);
        if (!doi.isEmpty())
            break;
    }

    if (doi.isEmpty())
    {
        QString text;
        for (int p = 0; p < std::min(2, model->numPages()); ++p)
            text += model->pageText(p) + QLatin1Char('\n');

        doi = findDoi(text);
        if (!doi.isEmpty())
        {
            // A bare DOI is only a mention; an arXiv number that is printed
            // as the paper's own is the better bet then.
            const QString labelled = findDoi(text, true);
            const QString id       = findArxivId(text);
            if (labelled.isEmpty() && !id.isEmpty())
            {
                doi.clear();
                arxiv = id;
            }
        }
        else
        {
            arxiv = findArxivId(text);
        }
    }

    if (doi.isEmpty() && arxiv.isEmpty())
    {
        m_message_bar->showMessage(
            tr("BibTeX: no DOI or arXiv number found on the first pages"));
        return;
    }

    const QString url = !doi.isEmpty()
                            ? QStringLiteral("https://doi.org/") + doi
                            : QStringLiteral("https://arxiv.org/bibtex/")
                                  + arxiv;

    QStringList args{"-sSL", "--max-time", "15", "-w", "\n%{http_code}"};
    if (!doi.isEmpty())
        args << "-H" << "Accept: application/x-bibtex";
    args << url;

    auto *process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError err)
    {
        if (err != QProcess::FailedToStart)
            return;
        m_message_bar->showMessage(tr("BibTeX: curl not found"));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status)
    {
        process->deleteLater();

        if (status != QProcess::NormalExit || exitCode != 0)
        {
            m_message_bar->showMessage(
                exitCode == 28   ? tr("BibTeX: timed out")
                : exitCode == 6 || exitCode == 7 ? tr("BibTeX: no connection")
                                                 : tr("BibTeX: request failed"));
            return;
        }

        // The body, then the HTTP status on a line of its own (see -w).
        QString out = QString::fromUtf8(process->readAllStandardOutput());
        const int nl = out.lastIndexOf(QLatin1Char('\n'));
        const int httpCode = nl >= 0 ? out.mid(nl + 1).trimmed().toInt() : 0;
        const QString entry
            = (nl >= 0 ? out.left(nl) : out).trimmed();

        if (httpCode != 200 || !entry.startsWith(QLatin1Char('@')))
        {
            m_message_bar->showMessage(tr("BibTeX: not found"));
            return;
        }

        QGuiApplication::clipboard()->setText(entry);
        const QString key = citationKey(entry);
        m_message_bar->showMessage(key.isEmpty()
                                       ? tr("BibTeX copied")
                                       : tr("BibTeX copied: %1").arg(key));
    });

    m_message_bar->showMessage(tr("BibTeX: fetching..."));
    process->start("curl", args);
}
