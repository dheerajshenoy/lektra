#include "PrintJob.hpp"

#include "DocumentView.hpp"
#include "Model.hpp"

#include <QFutureWatcher>
#include <QPageLayout>
#include <QProgressDialog>

PrintJob::PrintJob(DocumentView *view, std::unique_ptr<QPrinter> printer,
                   Options options, QObject *parent)
    : QObject(parent), m_view(view), m_printer(std::move(printer)),
      m_options(std::move(options)), m_cancel(std::make_shared<RenderCancel>())
{
}

PrintJob::~PrintJob()
{
    m_cancel->cancel();
    if (m_painter.isActive())
    {
        m_printer->abort();
        m_painter.end();
    }
    delete m_progress;
}

void
PrintJob::start()
{
    if (!m_view || !m_view->model())
    {
        finish(false, tr("The document is closed."));
        return;
    }
    if (m_options.pages.empty())
    {
        finish(false, tr("There are no pages to print."));
        return;
    }

    if (m_options.showProgress)
    {
        m_progress = new QProgressDialog(
            tr("Printing…"), tr("Cancel"), 0,
            static_cast<int>(m_options.pages.size()), m_view->window());
        m_progress->setWindowModality(Qt::WindowModal);
        m_progress->setMinimumDuration(0);
        m_progress->setAutoClose(false);
        m_progress->setAutoReset(false);
        connect(m_progress, &QProgressDialog::canceled, this,
                &PrintJob::cancel);
        m_progress->setValue(0);
    }

    requestRender(0);
}

void
PrintJob::cancel()
{
    if (m_finished)
        return;
    m_cancelled = true;
    m_cancel->cancel(); // the render in flight comes back and ends the job
}

void
PrintJob::requestRender(int index)
{
    Model *model = m_view ? m_view->model() : nullptr;
    if (!model)
    {
        finish(false, tr("The document was closed."));
        return;
    }

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, index]
    {
        QImage image = watcher->result();
        watcher->deleteLater();
        onRendered(index, std::move(image));
    });
    watcher->setFuture(model->renderPageForPrintAsync(
        m_options.pages[index], m_options.dpi, m_options.grayscale, m_cancel));
}

void
PrintJob::onRendered(int index, QImage image)
{
    if (m_finished)
        return;
    if (m_cancelled)
    {
        finish(false, {});
        return;
    }
    if (!m_view || !m_view->model())
    {
        finish(false, tr("The document was closed."));
        return;
    }
    if (image.isNull())
    {
        finish(false, tr("Page %1 could not be rendered at %2 dpi.")
                          .arg(m_options.pages[index] + 1)
                          .arg(m_options.dpi));
        return;
    }

    // The next page is rendered while this one is painted.
    const int next = index + 1;
    const bool more = next < static_cast<int>(m_options.pages.size());
    if (more)
        requestRender(next);

    paintPage(index, image);
    if (m_finished)
        return;

    if (m_progress)
        m_progress->setValue(next);
    if (!more)
        finish(true, {});
}

void
PrintJob::paintPage(int index, const QImage &image)
{
    const int pageno  = m_options.pages[index];
    const QSizeF pageSize = m_view->model()->pageSizePts(pageno);
    const double w        = pageSize.width();
    const double h        = pageSize.height();

    // The paper turns to match the page, so a landscape page is not shrunk
    // onto portrait paper. (The dimensions are known: the page was rendered.)
    m_printer->setPageOrientation(w > h ? QPageLayout::Landscape
                                        : QPageLayout::Portrait);

    if (index == 0)
    {
        if (!m_painter.begin(m_printer.get()))
        {
            finish(false, tr("Could not start printing."));
            return;
        }
    }
    else if (!m_printer->newPage())
    {
        finish(false, tr("Could not print page %1.").arg(pageno + 1));
        return;
    }

    // Fit the page to the printable area, keeping its proportions, centred.
    const QSizeF area(m_printer->width(), m_printer->height());
    const double aspect = (w > 0 && h > 0)
                              ? w / h
                              : static_cast<double>(image.width())
                                    / image.height();
    QSizeF size(area.width(), area.width() / aspect);
    if (size.height() > area.height())
        size = QSizeF(area.height() * aspect, area.height());
    const QRectF target(QPointF((area.width() - size.width()) / 2.0,
                                (area.height() - size.height()) / 2.0),
                        size);

    m_painter.setRenderHint(QPainter::SmoothPixmapTransform);
    m_painter.drawImage(target, image);
}

void
PrintJob::finish(bool ok, const QString &error)
{
    if (m_finished)
        return;
    m_finished = true;

    if (m_painter.isActive())
    {
        if (!ok)
            m_printer->abort();
        m_painter.end();
    }
    delete m_progress;
    m_progress = nullptr;

    emit finished(ok, error);
    deleteLater();
}
