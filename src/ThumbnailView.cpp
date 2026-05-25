#include "ThumbnailView.hpp"

#include "GraphicsImageItem.hpp"
#include "GraphicsView.hpp"

#include <QVBoxLayout>

ThumbnailView::ThumbnailView(const Config &config, float dpr,
                             QWidget *parent) noexcept
    : QWidget(parent)
{
    m_view = new DocumentView(config, dpr, this, /*thumbnailMode=*/true);
    m_view->setAutoResize(true);
    m_view->setLayoutMode(DocumentView::LayoutMode::VERTICAL);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view);
    setLayout(layout);

    connect(m_view->graphicsView(), &GraphicsView::clickRequested, this,
            [this](int /*count*/, QPointF scenePos)
    {
        GraphicsImageItem *item = nullptr;
        int pageno              = -1;
        m_view->pageAtScenePos(scenePos, pageno, item);
        if (pageno >= 0)
            emit pageClicked(pageno);
    });
}

void
ThumbnailView::open(const QString &path,
                    const PageLocation &initialLocation) noexcept
{
    connect(m_view, &DocumentView::openFileFinished, this,
            [this, initialLocation](DocumentView *)
    { m_view->GotoLocation(initialLocation); }, Qt::SingleShotConnection);

    m_view->openAsync(path);
}

void
ThumbnailView::syncToPage(int pageno) noexcept
{
    m_view->GotoPage(pageno);
}

void
ThumbnailView::highlightPage(int pageno) noexcept
{
    if (!m_view->config().thumbnail.highlight_current_page)
        return;

    if (m_highlighted_page == pageno)
        return;

    if (auto *prev = m_view->pageItemAt(m_highlighted_page))
        prev->setHighlighted(false);

    m_highlighted_page = pageno;

    if (auto *cur = m_view->pageItemAt(pageno))
        cur->setHighlighted(true);
}
