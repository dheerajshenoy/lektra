#include "AboutDialog.hpp"
#include "AppPaths.hpp"
#include "DispatchType.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
#include "DonateDialog.hpp"
#include "EditLastPagesWidget.hpp"
#include "ExportPagesDialog.hpp"
#include "GraphicsView.hpp"
#include "Lektra.hpp"
#include "PageLocation.hpp"
#include "PageRange.hpp"
#include "SaveSessionDialog.hpp"
#include "SearchBar.hpp"
#include "StartupWidget.hpp"
#include "TabBar.hpp"
#include "utils.hpp"

#include <QColorDialog>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QObject>
#include <QProcess>
#include <QSplitter>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWindow>
#include <variant>

// Zoom out the file
void
Lektra::ZoomOut() noexcept
{
    if (m_doc)
        m_doc->ZoomOut();
}

// Zoom in the file
void
Lektra::ZoomIn() noexcept
{
    if (m_doc)
        m_doc->ZoomIn();
}

// Increase EPUB/FB2/MOBI text size — reflows the document, unlike ZoomIn
// which is pure raster scaling.
void
Lektra::ReflowFontSizeIncrease() noexcept
{
    if (m_doc)
        m_doc->ReflowFontSizeIncrease();
}

void
Lektra::ReflowFontSizeDecrease() noexcept
{
    if (m_doc)
        m_doc->ReflowFontSizeDecrease();
}

void
Lektra::ReflowFontSizeReset() noexcept
{
    if (m_doc)
        m_doc->ReflowFontSizeReset();
}

void
Lektra::Zoom_set(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    double zoom;
    bool ok;

    if (args.isEmpty())
        zoom = QInputDialog::getDouble(
            this, tr("Set Zoom"), tr("Enter zoom level (e.g. 1.5 for 150%):"),
            m_doc->zoom(), 0.1, 10.0, 2, &ok);
    else
        zoom = args.at(0).toDouble(&ok);
    if (!ok)
        return;
    m_doc->setZoom(zoom);
}

// Resets zoom
void
Lektra::ZoomReset() noexcept
{
    if (m_doc)
        m_doc->ZoomReset();
}

// Go to a particular page (asks user with a dialog)
void
Lektra::Goto_page(const QStringList &args) noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    int pageno;
    bool ok;
    int total = m_doc->model()->numPages();

    if (args.isEmpty())
    {
        if (total == 0)
        {
            QMessageBox::information(this, tr("Goto Page"),
                                     tr("This document has no pages"));
            return;
        }

        pageno = QInputDialog::getInt(
            this, tr("Goto Page"), tr("Enter page number (1 to %1)").arg(total),
            m_doc->pageNo() + 1, 0, m_doc->numPages(), 1, &ok);
        if (!ok)
            return;
    }
    else
    {
        pageno = args.at(0).toInt(&ok);
        if (!ok)
            return;
    }

    if (pageno <= 0 || pageno > total)
    {
        QMessageBox::critical(this, tr("Goto Page"),
                              tr("Page %1 is out of range").arg(pageno));
        return;
    }

    gotoPage(pageno);
}

// Go to a particular page (no dialog)
void
Lektra::gotoPage(int pageno) noexcept
{
    if (m_doc)
    {
        m_doc->GotoPageWithHistory(pageno - 1);
    }
}

void
Lektra::GotoLocation(int pageno, float x, float y) noexcept
{
    if (m_doc)
        m_doc->GotoLocation({pageno, x, y});
}

void
Lektra::GotoLocation(const PageLocation &loc) noexcept
{
    if (m_doc)
        m_doc->GotoLocation(loc);
}

// Goes to the next search hit
void
Lektra::NextHit() noexcept
{
    if (m_doc)
        m_doc->NextHit();
}

void
Lektra::GotoHit(int index) noexcept
{
    if (m_doc)
        m_doc->GotoHit(index);
}

// Goes to the previous search hit
void
Lektra::PrevHit() noexcept
{
    if (m_doc)
        m_doc->PrevHit();
}

// Scrolls left in the file
void
Lektra::ScrollLeft() noexcept
{
    if (m_doc)
        m_doc->ScrollLeft();
}

// Scrolls right in the file
void
Lektra::ScrollRight() noexcept
{
    if (m_doc)
        m_doc->ScrollRight();
}

// Scrolls up in the file
void
Lektra::ScrollUp() noexcept
{
    if (m_doc)
        m_doc->ScrollUp();
}

// Scrolls down in the file
void
Lektra::ScrollDown() noexcept
{
    if (m_doc)
        m_doc->ScrollDown();
}

void
Lektra::ScrollDown_HalfPage() noexcept
{
    if (m_doc)
        m_doc->ScrollDown_HalfPage();
}

void
Lektra::ScrollUp_HalfPage() noexcept
{
    if (m_doc)
        m_doc->ScrollUp_HalfPage();
}

// Rotates the file in clockwise direction
void
Lektra::RotateClock() noexcept
{
    if (m_doc)
        m_doc->RotateClock();
}

// Rotates the file in anticlockwise direction
void
Lektra::RotateAnticlock() noexcept
{
    if (m_doc)
        m_doc->RotateAnticlock();
}

void
Lektra::FlipH() noexcept
{
    if (m_doc)
        m_doc->FlipH();
}

void
Lektra::FlipV() noexcept
{
    if (m_doc)
        m_doc->FlipV();
}

// Shows link hints for each visible link to visit link
// using the keyboard
void
Lektra::VisitLinkKB() noexcept
{
    if (m_doc)
    {
        m_lockedInputBuffer.clear();
        m_link_hint_map = m_doc->LinkKB();
        if (!m_link_hint_map.isEmpty())
        {
            m_link_hint_current_mode = LinkHintMode::Visit;
            m_link_hint_mode         = true;
            m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);
        }
    }
}

// Shows link hints for each visible link to copy link
// using the keyboard
void
Lektra::CopyLinkKB() noexcept
{
    if (m_doc)
    {
        m_lockedInputBuffer.clear();
        m_link_hint_map = m_doc->LinkKB();
        if (!m_link_hint_map.isEmpty())
        {
            m_link_hint_current_mode = LinkHintMode::Copy;
            m_link_hint_mode         = true;
            m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);
        }
    }
}

// Clears the currently selected text in the file
void
Lektra::Selection_cancel() noexcept
{
    if (m_doc)
        m_doc->ClearTextSelection();
}

// Copies the text selection (if any) to the clipboard
void
Lektra::Selection_copy() noexcept
{
    if (m_doc)
        m_doc->YankSelection();
}

// Fit the document to the width of the window
void
Lektra::Fit_width() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Width);
}

// Fit the document to the height of the window
void
Lektra::Fit_height() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Height);
}

// Fit the document to the window
void
Lektra::Fit_page() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::Window);
}

// Fit to width of the tight content bounding box (blank margins pushed
// off-screen instead of consuming zoom).
void
Lektra::Fit_width_smart() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::WidthSmart);
}

// Fit to height of the tight content bounding box.
void
Lektra::Fit_height_smart() noexcept
{
    if (m_doc)
        m_doc->setFitMode(DocumentView::FitMode::HeightSmart);
}

// Toggle auto-resize mode
void
Lektra::ToggleAutoResize() noexcept
{
    if (m_doc)
        m_doc->ToggleAutoResize();
}

// Show or hide the outline panel
void
Lektra::ShowOutline() noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    fz_outline *outline = m_doc->model()->getOutline();
    if (!outline)
        outline = m_doc->model()->getGeneratedOutline();

    if (!outline)
    {
        QMessageBox::information(
            this, tr("Outline"),
            tr("This document has no outline.\n\n"
               "You can generate one from the document text via "
               "the \"Generate Outline\" menu item or the "
               "generate_outline command."));
        return;
    }

    if (!m_outline_picker)
    {
        m_outline_picker = new OutlinePicker(m_config.outline, this);
        m_outline_picker->setKeybindings(m_picker_keybinds);
        connect(m_outline_picker, &OutlinePicker::jumpToLocationRequested, this,
                [this](int page, const QPointF &pos) // page returned is 1-based
        {
            m_doc->GotoLocationWithHistory(
                {page, (float)pos.x(), (float)pos.y()});
        });
    }

    // Prepared when the document was opened; built here only if that has not
    // happened yet.
    m_outline_picker->setEntries(m_doc->model()->outlineEntries());

    if (m_outline_picker->hasOutline())
    {
        m_outline_picker->setCurrentPage(m_doc->pageNo() + 1);
        m_outline_picker->launch();
        m_outline_picker->selectCurrentPage();
    }
}

void
Lektra::GenerateOutline() noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    if (!m_doc->model()->supports_outline())
    {
        QMessageBox::information(
            this, tr("Generate Outline"),
            tr("Outline generation is not supported for this file type."));
        return;
    }

    const float ratio = m_config.outline.generate_heading_ratio;
    const int levels  = m_config.outline.generate_max_levels;

    fz_outline *outline = m_doc->model()->generateOutline(ratio, levels);
    if (!outline)
    {
        QMessageBox::information(
            this, tr("Generate Outline"),
            tr("Could not detect any headings in this document.\n\n"
               "Try lowering outline.generate_heading_ratio in your config "
               "(current value: %1).")
                .arg(ratio));
        return;
    }

    if (!m_outline_picker)
    {
        m_outline_picker = new OutlinePicker(m_config.outline, this);
        m_outline_picker->setKeybindings(m_picker_keybinds);
        connect(m_outline_picker, &OutlinePicker::jumpToLocationRequested,
                this, [this](int page, const QPointF &pos) {
            m_doc->GotoLocationWithHistory(
                {page, (float)pos.x(), (float)pos.y()});
        });
    }

    m_outline_picker->setOutline(outline, m_doc->model());

    if (m_outline_picker->hasOutline())
    {
        m_outline_picker->setCurrentPage(m_doc->pageNo() + 1);
        m_outline_picker->launch();
        m_outline_picker->selectCurrentPage();
    }
}

void
Lektra::ExportOutline() noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    fz_outline *outline = m_doc->model()->getOutline();
    if (!outline)
        outline = m_doc->model()->getGeneratedOutline();

    if (!outline)
    {
        QMessageBox::information(
            this, tr("Export Outline"),
            tr("No outline available to export.\n\n"
               "Open the document outline or generate one first."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Outline"), QString(),
        tr("Outline JSON (*.json);;All Files (*)"));
    if (path.isEmpty())
        return;

    if (!m_doc->model()->exportOutlineToFile(path, outline))
        QMessageBox::warning(this, tr("Export Outline"),
                             tr("Failed to write outline to:\n%1").arg(path));
}

void
Lektra::LoadOutline() noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Outline"), QString(),
        tr("Outline JSON (*.json);;All Files (*)"));
    if (path.isEmpty())
        return;

    fz_outline *outline = m_doc->model()->loadOutlineFromFile(path);
    if (!outline)
    {
        QMessageBox::warning(this, tr("Load Outline"),
                             tr("Failed to load outline from:\n%1\n\n"
                                "Make sure the file is a valid outline JSON.")
                                 .arg(path));
        return;
    }

    if (!m_outline_picker)
    {
        m_outline_picker = new OutlinePicker(m_config.outline, this);
        m_outline_picker->setKeybindings(m_picker_keybinds);
        connect(m_outline_picker, &OutlinePicker::jumpToLocationRequested,
                this, [this](int page, const QPointF &pos) {
            m_doc->GotoLocationWithHistory(
                {page, (float)pos.x(), (float)pos.y()});
        });
    }

    m_outline_picker->setOutline(outline, m_doc->model());

    if (m_outline_picker->hasOutline())
    {
        m_outline_picker->setCurrentPage(m_doc->pageNo() + 1);
        m_outline_picker->launch();
        m_outline_picker->selectCurrentPage();
    }
}

// Show the highlight search panel
void
Lektra::Show_highlight_search() noexcept
{
    if (!m_doc || !m_doc->model()->supports_annotations())
        return;

    if (!m_highlight_search_picker)
    {
        m_highlight_search_picker
            = new HighlightSearchPicker(m_config.highlight_search, this);
        m_highlight_search_picker->setKeybindings(m_picker_keybinds);

        connect(m_highlight_search_picker,
                &HighlightSearchPicker::gotoLocationRequested, this,
                [this](int page, float x, float y)
        { GotoLocation(page, x, y); });
    }

    m_highlight_search_picker->setModel(m_doc->model());
    m_highlight_search_picker->launch();
}

void
Lektra::Show_annot_comment_search() noexcept
{
    if (!m_doc || !m_doc->model()->supports_annotations())
        return;

    if (!m_comment_search_picker)
    {
        m_comment_search_picker
            = new CommentSearchPicker(m_config.picker, this);
        m_comment_search_picker->setKeybindings(m_picker_keybinds);

        connect(m_comment_search_picker,
                &CommentSearchPicker::gotoLocationRequested, this,
                [this](int page, float x, float y)
        { GotoLocation(page, x, y); });
    }

    m_comment_search_picker->setModel(m_doc->model());
    m_comment_search_picker->launch();
}

// Invert colors of the document
void
Lektra::InvertColor() noexcept
{
    if (m_doc)
    {
        const bool invert = !m_doc->invertColor();
        m_doc->setInvertColor(invert);
        m_actionInvertColor->setChecked(invert);
    }
}

// Toggle the high-contrast tone stretch. Like any option change, this sets
// the global default (for views created later) and the current view.
void
Lektra::ToggleHighContrast() noexcept
{
    const bool on = m_doc ? !m_doc->config().behavior.high_contrast
                          : !m_config.behavior.high_contrast;
    m_config.behavior.high_contrast = on;

    if (m_doc)
    {
        m_doc->localConfig().behavior.high_contrast = on;
        m_doc->localConfigChanged("behavior");
    }

    if (m_actionHighContrast)
        m_actionHighContrast->setChecked(on);
}

// Toggle text highlight mode
void
Lektra::ToggleTextHighlight() noexcept
{
    if (m_doc)
    {
        if (m_doc->fileType() == Model::FileType::PDF)
            m_doc->ToggleTextHighlight();
        else
            QMessageBox::information(this, tr("Toggle Text Highlight"),
                                     tr("Not a PDF file to annotate"));
    }
}

// Toggle text selection mode
void
Lektra::ToggleTextSelection() noexcept
{
    if (m_doc)
        m_doc->ToggleTextSelection();
}

// Toggle rectangle annotation mode
void
Lektra::ToggleAnnotRect() noexcept
{
    if (m_doc)
    {
        if (m_doc->fileType() == Model::FileType::PDF)
            m_doc->ToggleAnnotRect();
        else
            QMessageBox::information(this, tr("Toggle Annot Rect"),
                                     tr("Not a PDF file to annotate"));
    }
}

// Toggle annotation select mode
void
Lektra::ToggleAnnotSelect() noexcept
{
    if (m_doc)
    {
        if (m_doc->fileType() == Model::FileType::PDF)
            m_doc->ToggleAnnotSelect();
        else
            QMessageBox::information(this, tr("Toggle Annot Select"),
                                     tr("Not a PDF file to annotate"));
    }
}

// Toggle popup annotation mode
void
Lektra::ToggleAnnotPopup() noexcept
{
    if (!m_doc)
        return;

    if (m_doc->fileType() == Model::FileType::PDF)
        m_doc->ToggleAnnotPopup();
    else
        QMessageBox::information(this, tr("Toggle Annot Popup"),
                                 tr("Not a PDF file to annotate"));
}

// Toggle region select mode
void
Lektra::ToggleRegionSelect() noexcept
{
    if (!m_doc)
        return;
    m_doc->ToggleRegionSelect();
}

void
Lektra::NarrowToRegion() noexcept
{
    if (!m_doc)
        return;
    m_doc->NarrowToRegion();
}

void
Lektra::ZoomToSelection() noexcept
{
    if (!m_doc)
        return;
    m_doc->ZoomToSelection();
}

void
Lektra::WidenRegion() noexcept
{
    if (!m_doc)
        return;
    m_doc->WidenRegion();
}

void
Lektra::NarrowToPages(int startPage1, int endPage1) noexcept
{
    if (!m_doc)
        return;
    m_doc->NarrowToPages(startPage1, endPage1);
}

void
Lektra::NarrowToSection(const QStringList &args) noexcept
{
    if (!m_doc || !m_doc->model())
        return;

    fz_outline *outline = m_doc->model()->getOutline();
    if (!outline)
        outline = m_doc->model()->getGeneratedOutline();

    if (!outline)
    {
        QMessageBox::information(this, tr("Narrow to Section"),
                                 tr("This document has no outline."));
        return;
    }

    struct Section
    {
        QString title;
        int depth;
        int startPage0; // 0-based
        int endPage0;   // 0-based, filled in second pass
    };

    // Flatten the outline tree, recording depth.
    QList<Section> sections;
    std::function<void(fz_outline *, int)> harvest
        = [&](fz_outline *node, int depth)
    {
        for (fz_outline *n = node; n; n = n->next)
        {
            const int pageno = m_doc->model()->resolveOutlineNode(n);
            if (pageno >= 0)
            {
                Section s;
                s.title      = QString(n->title ? n->title : "").simplified();
                s.depth      = depth;
                s.startPage0 = pageno;
                s.endPage0   = -1;
                sections.append(s);
            }
            if (n->down)
                harvest(n->down, depth + 1);
        }
    };
    harvest(outline, 0);

    if (sections.isEmpty())
    {
        QMessageBox::information(this, tr("Narrow to Section"),
                                 tr("No navigable sections found in outline."));
        return;
    }

    const int totalPages = m_doc->model()->numPages();

    // Second pass: compute end page for each section.
    //
    // A section ends just before the next entry that is NOT one of its
    // descendants. Two complementary tests determine "descendant":
    //
    //  1. Depth-based (properly nested outlines): j.depth > i.depth.
    //  2. Title-prefix-based (flat outlines where all entries share the same
    //     depth): j.title starts with i.title followed by "." or " ".
    //     Handles PDFs where "1.2.4.1", "1.2.4.2" are all at depth 2 even
    //     though they are logically children of "1.2.4".
    //
    // We also require j to start on a strictly later page so out-of-order
    // outline entries (same-page duplicates) don't truncate the range.
    for (int i = 0; i < sections.size(); ++i)
    {
        const QString childPrefix1 = sections[i].title + ".";
        const QString childPrefix2 = sections[i].title + " ";

        auto isDescendant = [&](const Section &j) -> bool
        {
            if (j.depth > sections[i].depth)
                return true;
            return j.title.startsWith(childPrefix1)
                   || j.title.startsWith(childPrefix2);
        };

        int end = totalPages - 1;
        for (int j = i + 1; j < sections.size(); ++j)
        {
            if (sections[j].startPage0 > sections[i].startPage0
                && !isDescendant(sections[j]))
            {
                // Include the page where the next section starts — section
                // content frequently overflows onto the same page as the
                // following heading, so ending at startPage0 - 1 would cut
                // off the last paragraph.
                end = sections[j].startPage0;
                break;
            }
        }
        sections[i].endPage0 = std::max(end, sections[i].startPage0);
    }

    // Determine which section to narrow to.
    int chosen = -1;

    if (!args.isEmpty())
    {
        const QString query = args.join(" ").trimmed();
        // Exact match first, then case-insensitive substring.
        for (int i = 0; i < sections.size(); ++i)
        {
            if (sections[i].title.compare(query, Qt::CaseInsensitive) == 0)
            {
                chosen = i;
                break;
            }
        }
        if (chosen < 0)
        {
            for (int i = 0; i < sections.size(); ++i)
            {
                if (sections[i].title.contains(query, Qt::CaseInsensitive))
                {
                    chosen = i;
                    break;
                }
            }
        }
        if (chosen < 0)
        {
            QMessageBox::information(
                this, tr("Narrow to Section"),
                tr("No section matching \"%1\" found.").arg(query));
            return;
        }
    }
    else
    {
        // Build display list with indentation to show hierarchy.
        QStringList items;
        items.reserve(sections.size());
        for (const auto &s : sections)
            items << QString(s.depth * 2, ' ') + s.title;

        bool ok              = false;
        const QString picked = QInputDialog::getItem(
            this, tr("Narrow to Section"), tr("Select a section:"), items, 0,
            false, &ok);

        if (!ok)
            return;

        chosen = items.indexOf(picked);
    }

    if (chosen < 0 || chosen >= sections.size())
        return;

    NarrowToPages(sections[chosen].startPage0 + 1,
                  sections[chosen].endPage0 + 1);
}

// Go to the first page
void
Lektra::FirstPage() noexcept
{
    if (!m_doc)
        return;

    m_doc->GotoFirstPage();
    updatePageNavigationActions();
}

// Go to the previous page
void
Lektra::PrevPage() noexcept
{
    if (!m_doc)
        return;

    // In caret mode, Shift+K follows vim visual-mode convention (K = up)
    // and extends the selection instead of paging — matches Shift+H/L
    // already doing caret_select_left/right, and Shift+Up already doing
    // caret_select_up.
    if (m_doc->caretMode())
    {
        m_doc->caretSelectUp();
        return;
    }

    m_doc->GotoPrevPage();
    updatePageNavigationActions();
}

// Go to the next page
void
Lektra::NextPage() noexcept
{
    if (!m_doc)
        return;

    // See PrevPage(): Shift+J (vim visual-mode "down") extends the
    // selection in caret mode instead of paging.
    if (m_doc->caretMode())
    {
        m_doc->caretSelectDown();
        return;
    }

    m_doc->GotoNextPage();
    updatePageNavigationActions();
}

// Go to the last page
void
Lektra::LastPage() noexcept
{
    if (m_doc)
        m_doc->GotoLastPage();

    updatePageNavigationActions();
}

// Go back in the page history
void
Lektra::GoBackHistory() noexcept
{
    if (m_doc)
        m_doc->GoBackHistory();
}

// Go forward in the page history
void
Lektra::GoForwardHistory() noexcept
{
    if (m_doc)
        m_doc->GoForwardHistory();
}

// Highlight text annotation for the current selection
void
Lektra::TextHighlightCurrentSelection() noexcept
{
    if (m_doc)
        m_doc->handleTextHighlightRequested();
}

// Underline annotation for the current selection
void
Lektra::TextUnderlineCurrentSelection() noexcept
{
    if (m_doc)
        m_doc->handleTextUnderlineRequested();
}

bool
Lektra::handleLinkHintEvent(QEvent *event) noexcept
{
    const QEvent::Type type = event->type();
    switch (type)
    {
        case QEvent::KeyPress:
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            switch (keyEvent->key())
            {
                case Qt::Key_Escape:
                    handleEscapeKeyPressed();
                    return true;

                case Qt::Key_Backspace:
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
                    m_lockedInputBuffer.removeLast();
#else
                    if (!m_lockedInputBuffer.isEmpty())
                        m_lockedInputBuffer.chop(1);
#endif
                    if (m_doc)
                        m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);
                    return true;
                default:
                    break;
            }

            QString text = keyEvent->text();
            if (text.isEmpty())
            {
                const int key = keyEvent->key();
                if (key >= Qt::Key_0 && key <= Qt::Key_9)
                    text = QString(QChar('0' + (key - Qt::Key_0)));
            }

            bool appended = false;
            if (text.size() == 1 && text.at(0).isDigit())
            {
                m_lockedInputBuffer += text;
                appended = true;
            }

            if (!appended)
                return true;

            if (m_doc)
                m_doc->UpdateKBHintsOverlay(m_lockedInputBuffer);

            int num = m_lockedInputBuffer.toInt();
            auto it = m_link_hint_map.find(num);
            if (it != m_link_hint_map.end())
            {
                const Model::LinkInfo &info = it.value();

                switch (m_link_hint_current_mode)
                {
                    case LinkHintMode::None:
                        break;

                    case LinkHintMode::Visit:
                        m_doc->FollowLink(info);
                        break;

                    case LinkHintMode::Copy:
                        m_clipboard->setText(info.uri);
                        break;
                }

                m_lockedInputBuffer.clear();
                m_link_hint_map.clear();
                m_doc->ClearKBHintsOverlay();
                m_link_hint_mode = false;
                return true;
            }
            keyEvent->accept();
            return true;
        }
        case QEvent::ShortcutOverride:
            event->accept();
            return true;
        default:
            break;
    }

    return false;
}

void
Lektra::search(const QString &term, bool use_regex) noexcept
{
    if (m_doc)
        m_doc->Search(term, use_regex);
}

void
Lektra::searchCancel() noexcept
{
    if (m_doc)
    {
        m_doc->SearchCancel();
    }
}

void
Lektra::searchInPage(const int pageno, const QString &term) noexcept
{
    if (m_doc)
        m_doc->SearchInPage(pageno, term);
}

void
Lektra::Search(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    if (args.isEmpty())
    {
        if (m_doc->model()->supports_text_search())
        {
            m_search_bar->setVisible(true);
            m_search_bar->focusSearchInput();
        }
        else
        {
            QMessageBox::information(
                this, tr("Search Not Supported"),
                tr("The current document does not support text search."));
        }
    }
    else
    {
        m_doc->Search(args.at(0), false);
    }
}

void
Lektra::SearchRegex(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    if (args.isEmpty())
    {
        m_search_bar->setVisible(true);
        m_search_bar->setRegexMode(true);
        m_search_bar->focusSearchInput();
    }
    else
    {
        m_doc->Search(args.at(0), true);
    }
}

void
Lektra::SearchDirectional(const QStringList &args,
                          DocumentView::SearchScope scope) noexcept
{
    if (!m_doc)
        return;

    if (!m_doc->model()->supports_text_search())
    {
        QMessageBox::information(
            this, tr("Search Not Supported"),
            tr("The current document does not support text search."));
        return;
    }

    // Scope is one-shot: DocumentView::Search consumes it and reverts to All.
    m_doc->setSearchScope(scope);

    if (args.isEmpty())
    {
        m_search_bar->setVisible(true);
        m_search_bar->focusSearchInput();
    }
    else
    {
        m_doc->Search(args.at(0), false);
    }
}

void
Lektra::ReselectLastTextSelection() noexcept
{
    if (m_doc)
        m_doc->ReselectLastTextSelection();
}

void
Lektra::SetLayoutMode(DocumentView::LayoutMode mode) noexcept
{
    if (m_doc)
        m_doc->setLayoutMode(mode);
}

void
Lektra::SetGridColumns(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    const int current = m_doc->gridColumns();
    int columns       = current;
    bool ok           = true;

    if (args.isEmpty())
    {
        columns = QInputDialog::getInt(this, tr("Grid Columns"),
                                       tr("Pages per row:"), current, 1, 32, 1,
                                       &ok);
    }
    else
    {
        const QString arg = args.first().trimmed();
        const int value   = arg.toInt(&ok);
        if (ok)
            columns = (arg.startsWith('+') || arg.startsWith('-'))
                          ? current + value
                          : value;
    }

    if (!ok)
    {
        m_message_bar->showMessage(tr("Grid columns: a number such as 4, or +1 "
                                      "/ -1"));
        return;
    }

    // The columns first, so showing the grid lays it out with the new number.
    m_doc->setGridColumns(columns);
    m_doc->setLayoutMode(DocumentView::LayoutMode::GRID);
}

void
Lektra::Show_command_picker() noexcept
{
    if (!m_command_picker)
    {
        m_command_picker = new CommandPicker(
            m_config.command_palette, m_command_manager->commands(),
            m_config.keybinds, m_command_manager.get(), this);
        m_command_picker->setKeybindings(m_picker_keybinds);
    }

    m_command_picker->launch();
}

void
Lektra::Show_bookmark_picker() noexcept
{
    if (!m_bookmark_picker)
    {
        m_bookmark_picker
            = new BookmarkPicker(m_config.picker, &m_bookmark_manager, this);
        m_bookmark_picker->setKeybindings(m_picker_keybinds);
        connect(m_bookmark_picker, &BookmarkPicker::fileOpenRequested, this,
                [this](const QString &file_path) { OpenFileDWIM(file_path); });
    }

    m_bookmark_picker->launch();
}

// If a jump marker was shown for the current document view, re-show it
// (e.g. after a reload)
void
Lektra::Reshow_jump_marker() noexcept
{
    if (m_doc)
        m_doc->Reshow_jump_marker();
}

// export_pages [path] [pages] [dpi]
// Without arguments: asks which pages, then where to save. With a path, the
// pages (default: the current one) are written without asking anything.
void
Lektra::ExportPages(const QStringList &args) noexcept
{
    if (!m_doc || !m_doc->model() || m_doc->model()->numPages() <= 0)
        return;

    const int count     = m_doc->model()->numPages();
    const int current   = m_doc->pageNo();
    const QString title = tr("Export Pages");
    auto sentence       = [](QString s)
    {
        if (!s.isEmpty())
            s[0] = s[0].toUpper();
        return s;
    };

    std::vector<int> pages;
    QString path;
    int dpi = 150;

    bool split = false;
    if (args.isEmpty())
    {
        // What to export: the pages, the format and, where it applies, one file
        // or a file per page.
        ExportPagesDialog options(count, current,
                                  m_doc->model()->supportsWriterExport(),
                                  m_export_format, m_export_split, this);
        if (options.exec() != QDialog::Accepted)
            return;
        const ExportPagesDialog::Result chosen = options.result();
        m_export_format                        = chosen.format;
        if (ExportPagesDialog::canSplit(chosen.format))
            m_export_split = chosen.split; // the other formats have no choice
        split = chosen.split;

        QString problem;
        pages = page_range::parse(chosen.pages, count, current, &problem);
        if (pages.empty())
            return; // the dialog does not accept what is not valid

        // Where to save it: the dialog offers just the chosen format.
        static const QHash<QString, QString> filters = {
            {"png", tr("PNG Image (*.png)")},
            {"jpg", tr("JPEG Image (*.jpg *.jpeg)")},
            {"webp", tr("WebP Image (*.webp)")},
            {"bmp", tr("BMP Image (*.bmp)")},
            {"tif", tr("TIFF Image (*.tif *.tiff)")},
            {"pdf", tr("PDF Document (*.pdf)")},
            {"svg", tr("SVG Picture (*.svg)")},
            {"txt", tr("Text (*.txt)")},
            {"html", tr("HTML (*.html)")},
        };
        const bool several = pages.size() > 1;
        const QFileInfo doc(m_doc->filePath());
        const QString base = doc.completeBaseName().isEmpty()
                                 ? tr("pages")
                                 : doc.completeBaseName();
        const QString name
            = (pages.size() == 1)
                  ? QStringLiteral("%1-page-%2.%3")
                        .arg(base)
                        .arg(pages.front() + 1)
                        .arg(chosen.format)
                  : QStringLiteral("%1.%2").arg(base, chosen.format);
        const bool perPage
            = several && (split || !ExportPagesDialog::canSplit(chosen.format));
        path = QFileDialog::getSaveFileName(
            this,
            perPage
                ? tr("Export %1 Pages (the page number is added to the name)")
                      .arg(pages.size())
                : tr("Export Pages"),
            QDir(doc.absolutePath()).filePath(name),
            filters.value(chosen.format, tr("All Files (*)")));
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += QLatin1Char('.') + chosen.format;
    }
    else
    {
        path = args.at(0);
        QString problem;
        pages = page_range::parse(args.value(1, QStringLiteral("current")),
                                  count, current, &problem);
        if (pages.empty())
        {
            QMessageBox::warning(this, title,
                                 sentence(problem) + QLatin1Char('.'));
            return;
        }
        split
            = args.size() > 3 && args.at(3).toLower() == QLatin1String("split");
        if (args.size() > 2)
        {
            bool ok     = false;
            const int v = args.at(2).toInt(&ok);
            if (!ok)
            {
                QMessageBox::warning(
                    this, title,
                    tr("\"%1\" is not a resolution (dpi)").arg(args.at(2)));
                return;
            }
            dpi = v;
        }
    }

    // The dialog has already asked about replacing the file that was chosen,
    // which is the only file when the result is one file; otherwise (pages
    // numbered into several files) we ask here.
    const QString kind = QFileInfo(path).suffix().toLower();
    static const QStringList oneFile
        = {"pdf", "txt", "text", "html", "xhtml", "cbz", "docx", "odt"};
    const bool single = pages.size() == 1 || (oneFile.contains(kind) && !split)
                        || args.size() > 0;

    QStringList written;
    QString error;
    bool existing = false;
    bool ok = m_doc->exportPages({path}, pages, dpi, single, &written, &error,
                                 &existing, split);
    if (!ok && existing
        && QMessageBox::question(
               this, title,
               tr("Some of the files already exist. Replace them?"))
               == QMessageBox::Yes)
        ok = m_doc->exportPages({path}, pages, dpi, true, &written, &error,
                                &existing, split);

    if (ok)
        m_message_bar->showMessage(
            written.size() == 1
                ? (pages.size() == 1 ? tr("Saved page %1 as %2")
                                           .arg(pages.front() + 1)
                                           .arg(written.first())
                                     : tr("Saved %1 pages as %2")
                                           .arg(pages.size())
                                           .arg(written.first()))
                : tr("Saved %1 files, from %2 to %3")
                      .arg(written.size())
                      .arg(QFileInfo(written.first()).fileName(),
                           QFileInfo(written.last()).fileName()),
            4.0f);
    else if (!existing)
        QMessageBox::warning(this, title,
                             tr("Could not export the pages:\n%1").arg(error));
}

void
Lektra::Copy_page_image() noexcept
{
    if (!m_doc)
        return;

    m_doc->Copy_page_image();
}

void
Lektra::SetMark(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    QString key;

    if (args.isEmpty())
    {

        key = QInputDialog::getText(
            this, tr("Set Mark"),
            tr("Enter mark key (a-z for local, A-Z for global):"));

        if (key.isEmpty())
        {
            QMessageBox::critical(this, tr("Set Mark"),
                                  tr("Mark key cannot be empty"));
            return;
        }
    }
    else
    {
        key = args.at(0);
    }

    setMarkFor(m_doc, key);
}

bool
Lektra::setMarkFor(DocumentView *view, const QString &key) noexcept
{
    if (!view || key.isEmpty())
        return false;

    if (m_marks_manager->isGlobalKey(key))
        m_marks_manager->addGlobalMark(key, view->id(),
                                       view->CurrentLocation());
    else
        m_marks_manager->addLocalMark(key, view->id(), view->CurrentLocation());
    return true;
}

void
Lektra::DeleteMark(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    QString key;
    if (args.isEmpty())
    {
        const QStringList existingMarks = m_marks_manager->allKeys(m_doc->id());
        key = QInputDialog::getItem(this, tr("Delete Mark"),
                                    tr("Mark to delete:"), existingMarks, 0);

        if (key.isEmpty())
        {
            QMessageBox::critical(this, tr("Delete Mark"),
                                  tr("Mark key cannot be empty"));
            return;
        }
    }
    else
    {
        key = args.at(0);
    }

    if (m_marks_manager->isGlobalKey(key))
        m_marks_manager->removeGlobalMark(key);
    else
        m_marks_manager->removeLocalMark(key, m_doc->id());
}

void
Lektra::GotoMark(const QStringList &args) noexcept
{
    if (!m_doc)
        return;

    QString key;

    if (args.isEmpty())
    {
        const QStringList existingMarks = m_marks_manager->allKeys(m_doc->id());
        key = QInputDialog::getItem(this, tr("Goto Mark"), tr("Mark to go to:"),
                                    existingMarks, 0);

        if (key.isEmpty())
        {
            QMessageBox::critical(this, tr("Goto Mark"),
                                  tr("Mark key cannot be empty"));
            return;
        }
    }
    else
    {
        key = args.at(0);
    }

    gotoMarkIn(m_doc, key);
}

bool
Lektra::gotoMarkIn(DocumentView *view, const QString &key) noexcept
{
    if (!view || key.isEmpty())
        return false;

    if (m_marks_manager->isGlobalKey(key))
    {
        const auto *mark = m_marks_manager->getGlobalMark(key);
        if (!mark)
            return false;
        // Switch to the right document first, then jump
        DocumentView *target = get_view_by_id(mark->docId);
        if (!target)
            return false;
        setCurrentDocumentView(target);
        target->GotoLocationWithHistory(mark->plocation);
        return true;
    }

    const auto *mark = m_marks_manager->getLocalMark(key, view->id());
    if (!mark)
        return false;
    view->GotoLocationWithHistory(mark->plocation);
    return true;
}

void
Lektra::ToggleVisualLineMode() noexcept
{
    if (!m_doc)
        return;

    bool newState = !m_doc->visual_line_mode();
    m_doc->set_visual_line_mode(newState);

    if (m_doc->visual_line_mode())
        m_statusbar->setMode(GraphicsView::Mode::VisualLine);
    else
        m_statusbar->setMode(m_doc->graphicsView()->getDefaultMode());
}

void
Lektra::ToggleNoneMode() noexcept
{
    if (!m_doc)
        return;

    bool oldState = m_doc->graphicsView()->mode() == GraphicsView::Mode::None;
    m_doc->set_visual_line_mode(!oldState);

    if (!oldState)
        m_statusbar->setMode(GraphicsView::Mode::None);
    else
        m_statusbar->setMode(m_doc->graphicsView()->getDefaultMode());
}

void
Lektra::ToggleCommentMarkers() noexcept
{
    if (!m_doc)
        return;

    // Global default (for views created later) and the current view.
    Config::Annotations &local     = m_doc->localConfig().annotations;
    local.highlight.comment_marker = !local.highlight.comment_marker;
    local.rect.comment_marker      = !local.rect.comment_marker;
    m_config.annotations.highlight.comment_marker
        = local.highlight.comment_marker;
    m_config.annotations.rect.comment_marker = local.rect.comment_marker;

    m_doc->ToggleCommentMarkers();
}

void
Lektra::ToggleThumbnailPanel() noexcept
{
    if (!m_doc)
        return;

    m_doc->ToggleThumbnailPanel();
}

void
Lektra::ToggleTrimMargins() noexcept
{
    if (!m_doc)
        return;

    m_doc->ToggleTrimMargins();
}

void
Lektra::ToggleCaretMode() noexcept
{
    if (m_doc)
        m_doc->ToggleCaretMode();
}

void
Lektra::CaretLeft() noexcept
{
    if (m_doc)
        m_doc->caretMoveLeft();
}

void
Lektra::CaretRight() noexcept
{
    if (m_doc)
        m_doc->caretMoveRight();
}

void
Lektra::CaretUp() noexcept
{
    if (m_doc)
        m_doc->caretMoveUp();
}

void
Lektra::CaretDown() noexcept
{
    if (m_doc)
        m_doc->caretMoveDown();
}

void
Lektra::CaretLineStart() noexcept
{
    if (m_doc)
        m_doc->caretMoveLineStart();
}

void
Lektra::CaretLineEnd() noexcept
{
    if (m_doc)
        m_doc->caretMoveLineEnd();
}

void
Lektra::CaretSelectLeft() noexcept
{
    if (m_doc)
        m_doc->caretSelectLeft();
}

void
Lektra::CaretSelectRight() noexcept
{
    if (m_doc)
        m_doc->caretSelectRight();
}

void
Lektra::CaretSelectUp() noexcept
{
    if (m_doc)
        m_doc->caretSelectUp();
}

void
Lektra::CaretSelectDown() noexcept
{
    if (m_doc)
        m_doc->caretSelectDown();
}

// Sync zoom and scroll of all the views of the current tab.
void
Lektra::SyncViews() noexcept
{
    if (!m_doc || !m_doc->container())
        return;

    if (m_doc->container()->getViewCount() < 2)
    {
        m_message_bar->showMessage(tr("Nothing to sync: only one view"));
        return;
    }
    m_doc->container()->sync_views();
}

// Sync zoom and scroll of the views with the given ids (current tab).
bool
Lektra::sync_views(const std::vector<DocumentView::Id> &ids) noexcept
{
    if (!m_doc || !m_doc->container())
        return false;

    DocumentContainer *container = m_doc->container();
    QList<DocumentView *> views;
    for (const DocumentView::Id id : ids)
        if (DocumentView *view = container->get_child_view_by_id(id))
            views << view;

    if (views.size() < 2)
        return false;
    container->sync_views(views);
    return container->isSynced();
}

void
Lektra::StopSyncViews() noexcept
{
    if (m_doc && m_doc->container())
        m_doc->container()->stop_sync();
}

// Select views of current tab interactively.
void
Lektra::SelectViews() noexcept
{
    if (!m_doc || !m_doc->container())
        return;

    if (m_doc->container()->getViewCount() < 2)
    {
        m_message_bar->showMessage(tr("Nothing to sync: only one view"));
        return;
    }

    m_doc->container()->select_views();
}

// For the selected tabs (the current one if none is selected): keeps the first
// split of each tab in place and moves the other splits into windows of their
// own, or into tabs of their own.
void
Lektra::SplitsToWindows() noexcept
{
    if (!m_tab_widget)
        return;

    if (splitTabsToWindows(targetTabs()) == 0)
        m_message_bar->showMessage(
            tr("Nothing to separate: the tab has only one split"));
}

void
Lektra::SplitsToTabs() noexcept
{
    if (!m_tab_widget)
        return;

    if (splitTabsIntoTabs(targetTabs()) == 0)
        m_message_bar->showMessage(
            tr("Nothing to separate: the tab has only one split"));
}

void
Lektra::ToggleLaserPointerCursor() noexcept
{
    setLaserPointerCursor(!m_presentation.laser_pointer_enabled);
}

void
Lektra::setLaserPointerCursor(bool state) noexcept
{
    m_presentation.laser_pointer_enabled = state;
    if (state)
    {
        const auto &lp = m_config.presentation.laser_pointer;
        this->setCursor(
            createLaserPointerCursor(lp.size, rgbaToQColor(lp.color)));
    }
    else
        this->unsetCursor();
}
