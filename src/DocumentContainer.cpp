#include "DocumentContainer.hpp"

#include <QEvent>
#include <QJsonArray>
#include <QSplitter>
#include <qnamespace.h>
#include <qtextcursor.h>

static DocumentContainer::Id nextId{0};

static DocumentContainer::Id
g_newId() noexcept
{
    return nextId++;
}

DocumentContainer::DocumentContainer(DocumentView *initialView, QWidget *parent)
    : QWidget(parent), m_id(g_newId())
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addWidget(initialView);

    m_current_view = initialView;
    initialView->setContainer(this);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Track when view gets focus
    initialView->installEventFilter(this);
}

DocumentContainer::~DocumentContainer()
{
    // Qt's parent-child relationship handles cleanup
}

DocumentView *
DocumentContainer::split(DocumentView *view, Qt::Orientation orientation,
                         const QString &filePath) noexcept
{
    if (!view || view->filePath().isEmpty())
        return nullptr;

    // Find the widget in the layout
    QWidget *currentWidget = nullptr;
    for (int i = 0; i < m_layout->count(); ++i)
    {
        QWidget *widget = m_layout->itemAt(i)->widget();
        if (widget == view
            || (qobject_cast<QSplitter *>(widget)
                && containsView(widget, view)))
        {
            currentWidget = widget;
            break;
        }
    }

    if (!currentWidget)
        return nullptr;

    // Create new view for the split - copy settings from current view
    DocumentView *newView = createViewFromTemplate(view);
    newView->openAsync(filePath);

    // If the current widget is the view itself (not in a splitter yet)
    if (currentWidget == view)
    {
        int layoutIndex = m_layout->indexOf(view);

        // Capture the view's current geometry BEFORE it gets reparented.
        // This is the only reliable way to get real pixel dimensions for
        // both orientations without depending on layout-pass ordering.
        QRect viewGeom = view->geometry();

        // Create a new splitter
        QSplitter *splitter = new QSplitter(orientation, this);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(1);
        splitter->setStyleSheet(
            "QSplitter::handle { background-color: palette(mid); }");

        // Reparenting view into splitter removes it from the layout
        splitter->addWidget(view);
        splitter->addWidget(newView);

        // Insert splitter where the view was
        m_layout->insertWidget(layoutIndex, splitter);

        // Apply the captured geometry so equalizeStretch sees real dimensions
        splitter->setGeometry(viewGeom);

        equalizeStretch(splitter);
    }
    else
    {
        // Current widget is a splitter, need to handle nested splitting
        QSplitter *parentSplitter = qobject_cast<QSplitter *>(currentWidget);
        if (parentSplitter)
        {
            splitInSplitter(parentSplitter, view, newView, orientation);
        }
    }

    // Set up the new view
    newView->installEventFilter(this);
    m_current_view = newView;
    newView->setFocus();

    emit viewCreated(newView);
    emit currentViewChanged(newView);

    return newView;
}

DocumentView *
DocumentContainer::split(DocumentView *view,
                         Qt::Orientation orientation) noexcept
{
    return split(view, orientation, view->filePath());
}

void
DocumentContainer::splitInSplitter(QSplitter *splitter, DocumentView *view,
                                   DocumentView *newView,
                                   Qt::Orientation orientation) noexcept
{
    if (!splitter || !view || !newView)
        return;

    // Find the view in the splitter
    int viewIndex = -1;
    for (int i = 0; i < splitter->count(); ++i)
    {
        QWidget *widget = splitter->widget(i);
        if (widget == view)
        {
            viewIndex = i;
            break;
        }
        else if (QSplitter *childSplitter = qobject_cast<QSplitter *>(widget))
        {
            if (containsView(childSplitter, view))
            {
                newView->show(); // Ensure the new view is visible before
                                 // recursive split
                splitInSplitter(childSplitter, view, newView, orientation);
                return;
            }
        }
    }

    if (viewIndex == -1)
        return;

    // If the orientations match, just insert the new view
    if (splitter->orientation() == orientation)
    {
        splitter->insertWidget(viewIndex + 1, newView);
    }
    else
    {
        // Orientations differ, need to create a nested splitter
        QWidget *oldWidget = splitter->widget(viewIndex);

        // Create new splitter with the requested orientation
        QSplitter *newSplitter = new QSplitter(orientation, this);
        newSplitter->setChildrenCollapsible(false);
        newSplitter->setHandleWidth(1);
        newSplitter->setStyleSheet(
            "QSplitter::handle { background-color: palette(mid); }");

        // Add the old view and new view to the new splitter
        newSplitter->addWidget(oldWidget);
        newSplitter->addWidget(newView);

        // Insert into parent BEFORE equalizing so the splitter inherits
        // real pixel dimensions from the already-laid-out parent
        splitter->insertWidget(viewIndex, newSplitter);

        equalizeStretch(newSplitter);
    }

    // Ensure the new view is marked visible so it's included in size calcs
    newView->show();

    // Force the splitter to recalculate its child list before equalizing
    splitter->refresh();

    equalizeStretch(splitter);
}

bool
DocumentContainer::containsView(QWidget *widget,
                                DocumentView *view) const noexcept
{
    if (widget == view)
        return true;

    QSplitter *splitter = qobject_cast<QSplitter *>(widget);
    if (!splitter)
        return false;

    for (int i = 0; i < splitter->count(); ++i)
    {
        if (containsView(splitter->widget(i), view))
            return true;
    }

    return false;
}

QList<DocumentView *>
DocumentContainer::getAllViews() const noexcept
{
    QList<DocumentView *> views;

    for (int i = 0; i < m_layout->count(); ++i)
    {
        QWidget *widget = m_layout->itemAt(i)->widget();
        collectViews(widget, views);
    }

    return views;
}

void
DocumentContainer::collectViews(QWidget *widget,
                                QList<DocumentView *> &views) const noexcept
{
    if (!widget)
        return;

    if (DocumentView *view = qobject_cast<DocumentView *>(widget))
    {
        views.append(view);
    }
    else if (QSplitter *splitter = qobject_cast<QSplitter *>(widget))
    {
        for (int i = 0; i < splitter->count(); ++i)
        {
            collectViews(splitter->widget(i), views);
        }
    }
}

void
DocumentContainer::closeView(DocumentView *view) noexcept
{
    if (!view)
        return;

    // Don't close if it's the only view
    QList<DocumentView *> allViews = getAllViews();
    if (allViews.count() <= 1)
        return;

    // Find the parent splitter
    QSplitter *parentSplitter = qobject_cast<QSplitter *>(view->parentWidget());

    if (!parentSplitter)
    {
        // View is directly in the layout - shouldn't happen in normal use
        m_layout->removeWidget(view);
        view->deleteLater();
        emit viewClosed(view);

        // Update current view
        if (m_current_view == view)
        {
            QList<DocumentView *> remaining = getAllViews();
            m_current_view = remaining.isEmpty() ? nullptr : remaining.first();
            if (m_current_view)
            {
                m_current_view->setFocus();
                emit currentViewChanged(m_current_view);
            }
        }
        return;
    }

    // Store the index for focus management
    int viewIndex = parentSplitter->indexOf(view);

    // Remove from splitter
    view->setParent(nullptr);
    view->deleteLater();
    emit viewClosed(view);

    if (parentSplitter->count() > 0)
        equalizeStretch(parentSplitter);

    // Determine which view to focus next
    DocumentView *nextFocus = nullptr;
    if (parentSplitter->count() > 0)
    {
        // Try to focus the view that was after this one, or the one before
        int nextIndex       = qMin(viewIndex, parentSplitter->count() - 1);
        QWidget *nextWidget = parentSplitter->widget(nextIndex);
        nextFocus           = qobject_cast<DocumentView *>(nextWidget);

        // If it's a splitter, get the first view from it
        if (!nextFocus)
        {
            QList<DocumentView *> views;
            collectViews(nextWidget, views);
            nextFocus = views.isEmpty() ? nullptr : views.first();
        }
    }

    // If splitter now has only one child, replace the splitter with that child
    if (parentSplitter->count() == 1)
    {
        QWidget *remainingWidget = parentSplitter->widget(0);
        QSplitter *grandParent
            = qobject_cast<QSplitter *>(parentSplitter->parentWidget());

        if (grandParent)
        {
            int splitterIndex = grandParent->indexOf(parentSplitter);
            grandParent->insertWidget(splitterIndex, remainingWidget);
            parentSplitter->deleteLater();
        }
        else
        {
            // Parent splitter is directly in the layout
            m_layout->removeWidget(parentSplitter);
            m_layout->addWidget(remainingWidget);
            parentSplitter->deleteLater();
        }
    }

    // Update current view and focus
    if (m_current_view == view)
    {
        if (!nextFocus)
        {
            QList<DocumentView *> remaining = getAllViews();
            nextFocus = remaining.isEmpty() ? nullptr : remaining.first();
        }

        m_current_view = nextFocus;
        if (m_current_view)
        {
            m_current_view->setFocus();
            emit currentViewChanged(m_current_view);
        }
    }
}

DocumentView *
DocumentContainer::createViewFromTemplate(DocumentView *templateView) noexcept
{
    if (!templateView)
        return nullptr;

    DocumentView *newView = new DocumentView(templateView->config(), this);
    newView->setContainer(this);
    newView->setDPR(templateView->dpr());
    newView->setInvertColor(templateView->invertColor());
    newView->setAutoResize(templateView->autoResize());
    newView->setLayoutMode(templateView->layoutMode());
    newView->setFitMode(templateView->fitMode());
    return newView;
}

void
DocumentContainer::syncViewSettings(DocumentView *source,
                                    DocumentView *target) noexcept
{
    if (!source || !target)
        return;

    // Don't sync if they're viewing different files
    if (source->filePath() != target->filePath())
        return;

    target->setInvertColor(source->invertColor());
    target->setFitMode(source->fitMode());
    target->setZoom(source->zoom());
    target->GotoPage(source->pageNo());
}

bool
DocumentContainer::eventFilter(QObject *watched, QEvent *event) noexcept
{
    if (event->type() == QEvent::FocusIn)
    {
        DocumentView *view = qobject_cast<DocumentView *>(watched);

        if (view && view != m_current_view)
        {
            focusView(view);
        }
    }

    return QWidget::eventFilter(watched, event);
}

int
DocumentContainer::getViewCount() const noexcept
{
    return getAllViews().count();
}

void
DocumentContainer::focusView(DocumentView *view) noexcept
{
    if (!view)
        return;

    QList<DocumentView *> views = getAllViews();
    if (!views.contains(view))
        return;

    if (m_current_view && m_current_view != view)
        m_current_view->graphicsView()->setActive(false);

    m_current_view = view;
    m_current_view->graphicsView()->setActive(true);
    m_current_view->setFocus();

    emit currentViewChanged(m_current_view);
}

void
DocumentContainer::equalizeStretch(QSplitter *splitter) noexcept
{
    if (!splitter || splitter->count() == 0)
        return;

    // 1. Get the current total size of the splitter
    int totalSize = 0;
    for (int size : splitter->sizes())
    {
        totalSize += size;
    }

    // 2. If total size is 0 (not yet rendered), use a fallback
    // to ensure they aren't initialized to 0px
    if (totalSize <= 0)
    {
        totalSize = 1000;
    }

    // 3. Create a list where every widget gets an equal share
    int share = totalSize / splitter->count();
    QList<int> newSizes;
    for (int i = 0; i < splitter->count(); ++i)
    {
        newSizes << share;
    }

    // 4. Force the splitter to apply these sizes immediately
    splitter->setSizes(newSizes);

    // 5. Keep the stretch factors so they stay equal when the window is
    // resized
    for (int i = 0; i < splitter->count(); ++i)
    {
        splitter->setStretchFactor(i, 1);
    }
}

void
DocumentContainer::focusSplit(Direction direction) noexcept
{
    Q_UNUSED(direction);
    assert(0 && "Not implemented yet");
    if (!m_current_view)
        return;
}

// Recursively serialize a widget (either a view or a splitter)
static QJsonObject
serializeWidget(QWidget *widget)
{
    if (DocumentView *view = qobject_cast<DocumentView *>(widget))
    {
        QJsonObject obj;
        obj["type"]         = "view";
        obj["file_path"]    = view->filePath();
        obj["current_page"] = view->pageNo() + 1;
        obj["zoom"]         = view->zoom();
        obj["fit_mode"]     = static_cast<int>(view->fitMode());
        obj["invert_color"] = view->invertColor();
        obj["rotation"]     = view->model() ? view->model()->rotation() : 0;
        return obj;
    }

    if (QSplitter *splitter = qobject_cast<QSplitter *>(widget))
    {
        QJsonObject obj;
        obj["type"]        = "splitter";
        obj["orientation"] = static_cast<int>(splitter->orientation());

        QJsonArray sizes;
        for (int s : splitter->sizes())
            sizes.append(s);
        obj["sizes"] = sizes;

        QJsonArray children;
        for (int i = 0; i < splitter->count(); ++i)
            children.append(serializeWidget(splitter->widget(i)));
        obj["children"] = children;

        return obj;
    }

    return {};
}

QJsonObject
DocumentContainer::serializeSplits() const noexcept
{
    // The layout has exactly one top-level widget (either a view or
    // splitter)
    if (m_layout->count() == 0)
        return {};

    QWidget *root = m_layout->itemAt(0)->widget();
    return serializeWidget(root);
}

// Splits the given view into a new empty view using the same template, and
// returns the new view. The new view is created and focused, but the file is
// not loaded until the user explicitly opens one. Returns nullptr if the split
// failed (e.g. view not found in layout).
DocumentView *
DocumentContainer::splitEmpty(DocumentView *view,
                              Qt::Orientation orientation) noexcept
{
    // Find the widget in the layout
    QWidget *currentWidget = nullptr;
    for (int i = 0; i < m_layout->count(); ++i)
    {
        QWidget *widget = m_layout->itemAt(i)->widget();
        if (widget == view
            || (qobject_cast<QSplitter *>(widget)
                && containsView(widget, view)))
        {
            currentWidget = widget;
            break;
        }
    }

    if (!currentWidget)
        return nullptr;

    // Create new empty view from template
    DocumentView *newView = createViewFromTemplate(view);

    if (currentWidget == view)
    {
        int layoutIndex = m_layout->indexOf(view);
        QRect viewGeom  = view->geometry();

        QSplitter *splitter = new QSplitter(orientation, this);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(1);
        splitter->setStyleSheet(
            "QSplitter::handle { background-color: palette(mid); }");

        splitter->addWidget(view);
        splitter->addWidget(newView);
        m_layout->insertWidget(layoutIndex, splitter);
        splitter->setGeometry(viewGeom);
        equalizeStretch(splitter);
    }
    else
    {
        QSplitter *parentSplitter = qobject_cast<QSplitter *>(currentWidget);
        if (parentSplitter)
            splitInSplitter(parentSplitter, view, newView, orientation);
    }

    newView->installEventFilter(this);
    m_current_view = newView;

    emit viewCreated(newView);
    emit currentViewChanged(newView);

    return newView;
}
