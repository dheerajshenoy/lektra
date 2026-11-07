#pragma once

#include <QObject>
#include <QPointer>
#include <QPainter>
#include <QPrinter>
#include <QString>
#include <memory>
#include <vector>

class DocumentView;
class QProgressDialog;
struct RenderCancel;

// What to print and how, as a command or a Lua script asks for it. Anything
// left out is a default.
struct PrintRequest
{
    QString printer;         // a printer by name (the default one if empty)
    QString file;            // write a PDF here instead of printing
    QString pages;           // see page_range::parse(); all pages if empty
    int copies     = 1;
    bool grayscale = false;
    double dpi     = 0.0;    // render resolution; PrintJob::DEFAULT_DPI if <= 0
    bool dialog    = false;  // let the user choose the printer and the pages
};

// Prints pages of a document to a QPrinter, one page at a time: the next page
// is rendered on the render pool while this one is painted (a QPrinter has to
// be used from the GUI thread). Each page is fitted to the paper and the paper
// is turned to match the page. Deletes itself when it is done.
class PrintJob : public QObject
{
    Q_OBJECT
public:
    static constexpr float DEFAULT_DPI = 300.0f;

    struct Options
    {
        std::vector<int> pages; // 0-based, in the order they are printed
        bool grayscale     = false;
        float dpi          = DEFAULT_DPI;
        bool showProgress  = false; // a dialog with a Cancel button
    };

    PrintJob(DocumentView *view, std::unique_ptr<QPrinter> printer,
             Options options, QObject *parent = nullptr);
    ~PrintJob() override;

    void start();
    void cancel();

signals:
    // Once, when the job is over. `error` says why it failed, and is empty if
    // it was cancelled.
    void finished(bool ok, const QString &error);

private:
    void requestRender(int index);
    void onRendered(int index, QImage image);
    void paintPage(int index, const QImage &image);
    void finish(bool ok, const QString &error);

    QPointer<DocumentView> m_view;
    std::unique_ptr<QPrinter> m_printer;
    Options m_options;
    std::shared_ptr<RenderCancel> m_cancel;
    QPainter m_painter;
    QPointer<QProgressDialog> m_progress;
    bool m_finished  = false;
    bool m_cancelled = false;
};
