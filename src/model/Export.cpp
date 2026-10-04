#include "Model.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// Writes pages with MuPDF's document writers: pdf, text, html, xhtml, cbz, docx,
// odt, and svg (which MuPDF writes as one file per page).
bool
Model::exportWithWriter(const std::vector<int> &pages, const QStringList &paths,
                        const QString &format, QString *error) noexcept
{
    auto fail = [error](const QString &message)
    {
        if (error)
            *error = message;
        return false;
    };

    if (!supportsWriterExport())
        return fail(tr("This kind of document cannot be written in that format"));

    QString fmt = format.toLower();
    if (fmt == QLatin1String("txt"))
        fmt = QStringLiteral("text");
    static const QStringList known = {"pdf",  "text", "html", "xhtml",
                                      "cbz",  "docx", "odt",  "svg"};
    if (!known.contains(fmt))
        return fail(tr("Cannot write \"%1\" files").arg(format));

    // A PDF from a PDF: copy the original pages, which keeps their fonts (so
    // the text can still be selected and searched), links and annotations.
    // Drawing them again with a document writer would lose the text mapping.
    if (fmt == QLatin1String("pdf") && m_pdf_doc && paths.size() == 1
        && !pages.empty())
    {
        const QByteArray target = paths.first().toUtf8();
        QString problem;
        bool ok = false;
        std::lock_guard<std::mutex> lock(m_doc_mutex);
        pdf_document *copy = nullptr;
        pdf_graft_map *map = nullptr;
        fz_var(copy);
        fz_var(map);
        fz_try(m_ctx)
        {
            copy = pdf_create_document(m_ctx);
            map  = pdf_new_graft_map(m_ctx, copy);
            for (const int pageno : pages)
                pdf_graft_mapped_page(m_ctx, map, -1, m_pdf_doc, pageno);
            pdf_write_options options = pdf_default_write_options;
            options.do_garbage        = 1;
            pdf_save_document(m_ctx, copy, target.constData(), &options);
            ok = true;
        }
        fz_always(m_ctx)
        {
            pdf_drop_graft_map(m_ctx, map);
            pdf_drop_document(m_ctx, copy);
        }
        fz_catch(m_ctx)
        {
            problem = QString::fromUtf8(fz_caught_message(m_ctx));
        }
        if (!ok)
        {
            QFile::remove(paths.first());
            return fail(problem.isEmpty() ? tr("Writing failed") : problem);
        }
        return true;
    }

    const bool perPage = fmt == QLatin1String("svg");
    if (perPage ? paths.size() != static_cast<qsizetype>(pages.size())
                : paths.size() != 1)
        return fail(perPage ? tr("One file name is needed for each page")
                            : tr("This format is one file: give one file name"));
    if (pages.empty())
        return fail(tr("No pages were given"));

    // SVG files are written by MuPDF as <pattern with %d>, numbered from 1, in a
    // folder of their own, and then moved to the names that were asked for.
    QTemporaryDir scratch;
    if (perPage && !scratch.isValid())
        return fail(tr("Could not make a temporary folder"));
    const QString writerPath
        = perPage ? scratch.filePath(QStringLiteral("page-%d.svg")) : paths.first();

    const QByteArray pathBytes = writerPath.toUtf8();
    const QByteArray fmtBytes  = fmt.toLatin1();

    QString problem;
    bool ok = false;
    // Taken outside fz_try: a MuPDF error jumps out of that block and would
    // skip the guard's destructor.
    std::lock_guard<std::mutex> lock(m_doc_mutex);

    fz_document_writer *writer = nullptr;
    fz_page *page              = nullptr;
    fz_var(writer);
    fz_var(page);
    fz_try(m_ctx)
    {
        writer = fz_new_document_writer(m_ctx, pathBytes.constData(),
                                        fmtBytes.constData(), "");
        for (const int pageno : pages)
        {
            page                 = fz_load_page(m_ctx, m_doc, pageno);
            const fz_rect box    = fz_bound_page(m_ctx, page);
            fz_device *device    = fz_begin_page(m_ctx, writer, box);
            fz_try(m_ctx)
            {
                fz_run_page(m_ctx, page, device, fz_identity, nullptr);
            }
            fz_always(m_ctx)
            {
                fz_end_page(m_ctx, writer); // also closes the device
            }
            fz_catch(m_ctx)
            {
                fz_rethrow(m_ctx);
            }
            fz_drop_page(m_ctx, page);
            page = nullptr;
        }
        fz_close_document_writer(m_ctx, writer);
        ok = true;
    }
    fz_always(m_ctx)
    {
        fz_drop_page(m_ctx, page);
        fz_drop_document_writer(m_ctx, writer);
    }
    fz_catch(m_ctx)
    {
        problem = QString::fromUtf8(fz_caught_message(m_ctx));
    }

    if (!ok)
    {
        if (!perPage)
            QFile::remove(paths.first()); // not a half-written file
        return fail(problem.isEmpty() ? tr("Writing failed") : problem);
    }

    if (perPage)
    {
        for (qsizetype i = 0; i < paths.size(); ++i)
        {
            const QString made = scratch.filePath(QStringLiteral("page-%1.svg").arg(i + 1));
            QFile::remove(paths.at(i)); // the caller has already allowed replacing it
            if (!QFile::copy(made, paths.at(i)))
                return fail(tr("Could not write %1").arg(paths.at(i)));
        }
    }
    return true;
}
