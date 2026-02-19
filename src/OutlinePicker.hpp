#pragma once
#include "Config.hpp"
#include "Picker.hpp"

#include <mupdf/fitz.h>
#include <vector>

class OutlinePicker : public Picker
{
    Q_OBJECT
public:
    explicit OutlinePicker(QWidget *parent) noexcept;

    // Call this whenever a new document is loaded
    void setOutline(fz_outline *outline) noexcept;
    void clearOutline() noexcept;

    bool hasOutline() const noexcept
    {
        return !m_entries.empty();
    }

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
};
