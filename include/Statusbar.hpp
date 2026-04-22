#pragma once

#include "CircleLabel.hpp"
#include "Config.hpp"
#include "DocumentView.hpp"
#include "ElidableLabel.hpp"
#include "GraphicsView.hpp"

#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

enum class FitMode;

class Statusbar : public QWidget
{
    Q_OBJECT
public:
    Statusbar(const Config::Statusbar &config, QWidget *parent = nullptr);

    inline void setHighlightColor(const QColor &color) noexcept
    {
        m_mode_color_label->setColor(color);
    }

    inline void setTotalPageCount(int total) noexcept
    {
        m_totalpage_label->setText(QString::number(total));
    }

    void hidePageInfo(bool state) noexcept;
    void setPageNo(int pageno) noexcept;
    void setFitMode(DocumentView::FitMode mode) noexcept;
    void setMode(GraphicsView::Mode) noexcept;
    void setModeVisible(bool visible) noexcept;
    void setProgressVisible(bool visible) noexcept;
    void setSessionName(const QString &name) noexcept;
    void setPortalMode(bool state) noexcept;
    void setFilePath(const QString &name) noexcept;

signals:
    void modeChangeRequested();
    void fitModeChangeRequested();
    void modeColorChangeRequested(GraphicsView::Mode);
    void pageChangeRequested(int pageno);

private:
    const Config::Statusbar &m_config;
    void initGui() noexcept;
    void initConnections() noexcept;
    void labelBG(QLabel *label, const QColor &color) noexcept;
    ElidableLabel *m_filename_label = new ElidableLabel();
    QPushButton *m_mode_label       = new QPushButton();
    CircleLabel *m_mode_color_label = new CircleLabel();
    QLabel *m_pageno_label          = new QLabel();
    QLabel *m_totalpage_label       = new QLabel();
    QLabel *m_pageno_separator      = new QLabel(" " + tr("of") + " ");
    QLabel *m_progress_label        = new QLabel();
    QLabel *m_portal_label          = new QLabel("P");
    QPushButton *m_session_label    = new QPushButton();
    QGridLayout *m_layout           = new QGridLayout(this);
    GraphicsView::Mode m_current_mode;
    bool m_mode_forced_hidden{false};
    bool m_progress_forced_hidden{false};
};
