#pragma once

#include "WaitingSpinnerWidget.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

class SearchBar : public QWidget
{
    Q_OBJECT

public:
    explicit SearchBar(QWidget *parent = nullptr);
    void setSearchCount(int count) noexcept;
    void setSearchIndex(int index) noexcept;
    void search(const QString &term) noexcept;

    inline void focusSearchInput() noexcept
    {
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    }

    inline void showSpinner(bool state) noexcept
    {
        if (state)
        {
            m_spinner->show();
            m_spinner->start();
        }
        else
        {
            m_spinner->hide();
            m_spinner->stop();
        }
    }

private:
    QLabel *m_label;
    QLabel *m_searchSeparator{new QLabel("of")};
    QLineEdit *m_searchInput;
    QLineEdit *m_searchIndexLabel;
    QLabel *m_searchCountLabel;
    QPushButton *m_prevButton;
    QPushButton *m_nextButton;
    QPushButton *m_closeButton;
    WaitingSpinnerWidget *m_spinner;

protected:
    void showEvent(QShowEvent *event) override;

signals:
    void searchRequested(const QString &term);
    void searchIndexChangeRequested(int index);
    void nextHitRequested();
    void prevHitRequested();
};
