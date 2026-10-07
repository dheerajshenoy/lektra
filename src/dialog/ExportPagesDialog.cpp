#include "ExportPagesDialog.hpp"

#include "PageRange.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

bool
ExportPagesDialog::canSplit(const QString &format)
{
    static const QStringList one
        = {"pdf", "txt", "text", "html", "xhtml", "docx", "odt", "cbz"};
    return one.contains(format.toLower());
}

ExportPagesDialog::ExportPagesDialog(int pageCount, int currentPage,
                                     bool moreFormats, const QString &format,
                                     bool split, QWidget *parent)
    : QDialog(parent), m_page_count(pageCount), m_current(currentPage)
{
    setWindowTitle(tr("Export Pages"));
    setMinimumWidth(420);

    m_pages = new QLineEdit(QString::number(currentPage + 1), this);
    m_pages->setPlaceholderText(tr("e.g. 1-5,8,10-"));
    m_pages->setToolTip(tr("Page numbers and ranges separated by commas: 1-5,8,10-\n"
                           "Also: all, odd, even, current, first, last"));
    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_problem->setStyleSheet(QStringLiteral("color: #d33;"));

    m_format = new QComboBox(this);
    const QList<QPair<QString, QString>> formats = {
        {"png", tr("PNG image")},   {"jpg", tr("JPEG image")},
        {"webp", tr("WebP image")}, {"bmp", tr("BMP image")},
        {"tif", tr("TIFF image")},  {"pdf", tr("PDF document")},
    };
    for (const auto &[extension, label] : formats)
        m_format->addItem(QStringLiteral("%1  (.%2)").arg(label, extension), extension);
    if (moreFormats)
    {
        m_format->addItem(tr("SVG vector picture  (.svg)"), QStringLiteral("svg"));
        m_format->addItem(tr("Plain text  (.txt)"), QStringLiteral("txt"));
        m_format->addItem(tr("HTML  (.html)"), QStringLiteral("html"));
    }
    const int at = m_format->findData(format.toLower());
    m_format->setCurrentIndex(at >= 0 ? at : 0);

    m_split        = new QCheckBox(this);
    m_split_choice = split;
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: gray;"));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Choose Location…"));

    auto *form = new QFormLayout;
    form->addRow(tr("Pages (of %1):").arg(pageCount), m_pages);
    form->addRow(QString(), m_problem);
    form->addRow(tr("Format:"), m_format);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_split);
    layout->addWidget(m_hint);
    layout->addWidget(m_buttons);

    connect(m_split, &QCheckBox::clicked, this, [this](bool on)
    {
        m_split_choice = on; // only the user's own clicks come here
        refresh();
    });
    connect(m_pages, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_format, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refresh();
    m_pages->selectAll();
    m_pages->setFocus();
}

ExportPagesDialog::Result
ExportPagesDialog::result() const
{
    Result r;
    r.pages  = m_pages->text().trimmed();
    r.format = m_format->currentData().toString();
    r.split  = canSplit(r.format) && m_split_choice;
    return r;
}

void
ExportPagesDialog::refresh()
{
    QString problem;
    const std::vector<int> pages
        = page_range::parse(m_pages->text(), m_page_count, m_current, &problem);
    const int count = static_cast<int>(pages.size());

    if (problem.isEmpty())
        m_problem->clear();
    else
    {
        problem[0] = problem[0].toUpper();
        m_problem->setText(problem + QLatin1Char('.'));
    }
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(count > 0);
    m_problem->setVisible(!problem.isEmpty());

    const QString format = m_format->currentData().toString();
    if (canSplit(format))
    {
        // A choice: all the pages in one file, or a file for each. Nothing to
        // choose when only one page is exported.
        m_split->setEnabled(count > 1);
        m_split->setChecked(m_split_choice);
        m_split->setText(tr("Split into one file per page"));
        m_hint->setText(count > 1 && !m_split_choice
                            ? tr("Off: one .%1 file with all %2 pages.").arg(format).arg(count)
                        : count > 1
                            ? tr("On: %1 files, with the page number added to each name.").arg(count)
                            : QString());
    }
    else
    {
        // Pictures and SVG are always one file per page.
        m_split->setEnabled(false);
        m_split->setChecked(true);
        m_split->setText(tr("Split into one file per page"));
        m_hint->setText(count > 1
                            ? tr("This format always makes one file per page: %1 files, with the "
                                 "page number added to each name.")
                                  .arg(count)
                            : QString());
    }
    m_hint->setVisible(!m_hint->text().isEmpty());
}
