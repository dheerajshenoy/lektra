#pragma once
#include "Model.hpp"
#include "Picker.hpp"

#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>

// Forward declare your spinner
class WaitingSpinnerWidget;

class HighlightSearchPicker : public Picker
{
    Q_OBJECT
public:
    explicit HighlightSearchPicker(QWidget *parent) noexcept;

    void setModel(Model *model) noexcept
    {
        m_model = model;
    }
    void refresh() noexcept;
    void launch() noexcept;

signals:
    void gotoLocationRequested(int page, float x, float y);

protected:
    // Picker interface
    QList<Picker::Item> collectItems() override;
    void onItemAccepted(const Picker::Item &item) override;

    // Override so we can apply smart-case and refresh on launch
    void onSearchChanged(const QString &text); // see note below

    // highlightsearchpicker.h
    void onFilterChanged(int visibleCount) override
    {
        m_countLabel->setText(QString("%1 results").arg(visibleCount));
    }

private:
    void setLoading(bool state) noexcept;
    QList<Item> buildItems(const QString &term) const noexcept;

    QPointer<Model> m_model;
    QFutureWatcher<std::vector<Model::HighlightText>> m_watcher;
    std::vector<Model::HighlightText> m_entries;

    // Extra controls injected into Picker's header area
    WaitingSpinnerWidget *m_spinner{nullptr};
    QPushButton *m_refreshButton{nullptr};
    QLabel *m_countLabel{nullptr};
};
