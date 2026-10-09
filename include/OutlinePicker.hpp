#pragma once

#include "Config.hpp"
#include "Model.hpp"
#include "Picker.hpp"

class Model;

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

    // Call this whenever a new document is loaded. `model` resolves each
    // node's chapter-aware fz_location to a global page index — required
    // for chaptered formats (EPUB) where a node's local page-within-chapter
    // number is not the document-wide index. Pass the current document's
    // Model whenever one is available.
    void setOutline(fz_outline *outline, Model *model = nullptr) noexcept;
    // Uses the entries the model already built (see Model::outlineEntries()).
    void setEntries(const std::vector<Model::OutlineEntry> &entries) noexcept;
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
    std::vector<Model::OutlineEntry> m_entries;
    const Config::Outline &m_config;
    int m_current_page{-1};
};
