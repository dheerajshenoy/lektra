#include "DocumentContainer.hpp"

#include <QEvent>
#include <QSplitter>

DocumentContainer::DocumentContainer(DocumentView *initialView, QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addWidget(initialView);

    m_current_view = initialView;

    // Track when view gets focus
    initialView->installEventFilter(this);
}

DocumentContainer::~DocumentContainer()
{
    // Qt's parent-child relationship handles cleanup
}

void
DocumentContainer::split(DocumentView *view,
                         Qt::Orientation orientation) noexcept
{
    if (!view)
        return;

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
        return;

    // Create new view for the split - copy settings from current view
    DocumentView *newView = createViewFromTemplate(view);

    // If the current widget is the view itself (not in a splitter yet)
    if (currentWidget == view)
    {
        // Remove the view from layout
        m_layout->removeWidget(view);

        // Create a new splitter
        QSplitter *splitter = new QSplitter(orientation, this);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(1);
        splitter->setStyleSheet(
            "QSplitter::handle { background-color: palette(mid); }");

        // Add both views to the splitter
        splitter->addWidget(view);
        splitter->addWidget(newView);

        // Set equal sizes
        QList<int> sizes;
        sizes << 1 << 1;
        splitter->setSizes(sizes);

        // Add splitter to layout
        m_layout->addWidget(splitter);
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

        // Redistribute sizes equally
        QList<int> sizes;
        int count = splitter->count();
        for (int i = 0; i < count; ++i)
            sizes << 1;
        splitter->setSizes(sizes);
    }
    else
    {
        // Orientations differ, need to create a nested splitter
        QWidget *oldWidget = splitter->widget(viewIndex);

        // Create new splitter with the requested orientation
        QSplitter *newSplitter = new QSplitter(orientation, splitter);
        newSplitter->setChildrenCollapsible(false);
        newSplitter->setHandleWidth(1);
        newSplitter->setStyleSheet(
            "QSplitter::handle { background-color: palette(mid); }");

        // Add the old view and new view to the new splitter
        newSplitter->addWidget(oldWidget);
        newSplitter->addWidget(newView);

        // Set equal sizes for the new splitter
        QList<int> sizes;
        sizes << 1 << 1;
        newSplitter->setSizes(sizes);

        // Insert the new splitter at the old position
        splitter->insertWidget(viewIndex, newSplitter);
    }
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
DocumentContainer::getCurrentView() const noexcept
{
    return m_current_view;
}

void
DocumentContainer::focusNextView() noexcept
{
    QList<DocumentView *> views = getAllViews();
    if (views.count() <= 1)
        return;

    int currentIndex = views.indexOf(m_current_view);
    int nextIndex    = (currentIndex + 1) % views.count();

    m_current_view = views[nextIndex];
    m_current_view->setFocus();
    emit currentViewChanged(m_current_view);
}

void
DocumentContainer::focusPrevView() noexcept
{
    QList<DocumentView *> views = getAllViews();
    if (views.count() <= 1)
        return;

    int currentIndex = views.indexOf(m_current_view);
    int prevIndex    = (currentIndex - 1 + views.count()) % views.count();

    m_current_view = views[prevIndex];
    m_current_view->setFocus();
    emit currentViewChanged(m_current_view);
}

DocumentView *
DocumentContainer::createViewFromTemplate(DocumentView *templateView) noexcept
{
    if (!templateView)
        return nullptr;

    // Create new view with the same config
    DocumentView *newView = new DocumentView(templateView->config(), this);

    // Copy the document to the new view (same file)
    if (!templateView->filePath().isEmpty())
    {
        newView->openAsync(templateView->filePath());

        // Copy view settings
        newView->setDPR(templateView->dpr());
        newView->setInvertColor(templateView->invertColor());
        newView->setAutoResize(templateView->autoResize());
        newView->setLayoutMode(templateView->layoutMode());
        newView->setFitMode(templateView->fitMode());

        // Navigate to same page
        newView->GotoPage(templateView->pageNo());
        newView->setZoom(templateView->zoom());
    }

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
            m_current_view = view;
            emit currentViewChanged(view);
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

    m_current_view = view;
    view->setFocus();
    emit currentViewChanged(view);
}
