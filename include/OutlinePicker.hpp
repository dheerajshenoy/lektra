#pragma once

#include "Config.hpp"
#include "Picker.hpp"

extern "C"
{
#include <mupdf/fitz.h>
}

#include <vector>

class OutlinePicker : public Picker
{
    Q_OBJECT
public:
    explicit OutlinePicker(const Config::Outline &config,
                           QWidget *parent) noexcept;

    // Call this whenever a new document is loaded
    void setOutline(fz_outline *outline) noexcept;
    void clearOutline() noexcept;

    bool hasOutline() const noexcept
    {
        return !m_entries.empty();
    }

    inline void setCurrentPage(int page) noexcept
    {
        m_current_page = page;
    }

    void selectCurrentPage() noexcept;

signals:
    void jumpToLocationRequested(int page, const QPointF &pos);

protected:
    QList<Item> collectItems() override;
    void onItemAccepted(const Item &item) override;

private:
    struct OutlineEntry
    {
        QString title;
        int depth;
        int page;
        QPointF location;
        bool isHeading; // has children
    };

    void harvest(fz_outline *node, int depth) noexcept;

    std::vector<OutlineEntry> m_entries;
    const Config::Outline &m_config;
    int m_current_page{-1};
};
