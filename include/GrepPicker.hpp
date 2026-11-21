#pragma once
#include "Model.hpp"
#include "Picker.hpp"

#include <QFutureWatcher>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <memory>

class WaitingSpinnerWidget;

// Lists the lines of the document that match what is typed (a regular
// expression, smart-case), like grep -n. Accepting a line goes to it.
class GrepPicker : public Picker
{
    Q_OBJECT
public:
    explicit GrepPicker(const Config::Picker &config, QWidget *parent) noexcept;

    void setModel(Model *model) noexcept
    {
        m_model = model;
    }

    // Start with `pattern` already typed
    void setPattern(const QString &pattern) noexcept;

signals:
    void gotoLocationRequested(int page, float x, float y);

protected:
    QList<Picker::Item> collectItems() override;
    void onItemAccepted(const Picker::Item &item) override;
    void onFilterChanged(int visibleCount) override;

private:
    void run(const QString &pattern) noexcept;
    void cancel() noexcept;

    QPointer<Model> m_model;
    QFutureWatcher<std::vector<Model::GrepHit>> m_watcher;
    std::vector<Model::GrepHit> m_hits;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QTimer m_debounce;

    WaitingSpinnerWidget *m_spinner = nullptr;
    QLabel *m_countLabel            = nullptr;
};
