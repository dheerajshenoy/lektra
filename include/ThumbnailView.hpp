#pragma once

#include "Config.hpp"
#include "DocumentView.hpp"
#include "PageLocation.hpp"

#include <QWidget>

class ThumbnailView : public QWidget
{
    Q_OBJECT

public:
    explicit ThumbnailView(const Config &config, float dpr,
                           QWidget *parent = nullptr) noexcept;

    // Open a file and jump to initialLocation once loaded.
    void open(const QString &path, const PageLocation &initialLocation) noexcept;

    // Scroll the thumbnail strip to centre on pageno (0-based).
    void syncToPage(int pageno) noexcept;

    // Draw a highlight border on pageno (0-based). Pass -1 to clear.
    // No-op when config.thumbnail.highlight_current_page is false.
    void highlightPage(int pageno) noexcept;

    [[nodiscard]] DocumentView *documentView() const noexcept { return m_view; }

signals:
    void pageClicked(int pageno); // 0-based

private:
    DocumentView *m_view      = nullptr;
    int m_highlighted_page    = -1;
};
