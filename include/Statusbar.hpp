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

    // Called when narrowed to a page range: shows "from–to (of total)".
    // Pass from1 == -1 to revert to the plain total count.
    inline void setPageRangeInfo(int from1, int to1, int total) noexcept
    {
        if (from1 < 0)
            m_totalpage_label->setText(QString::number(total));
        else
            m_totalpage_label->setText(
                QString("%1–%2 (%3 %4)")
                    .arg(from1)
                    .arg(to1)
                    .arg(total)
                    .arg(tr("total")));
    }

    void setPageInfoVisible(bool state) noexcept;
    void setPageNo(int pageno) noexcept;
    void setFitMode(DocumentView::FitMode mode) noexcept;
    void setMode(GraphicsView::Mode) noexcept;
    void setModeVisible(bool visible) noexcept;
    void setProgressVisible(bool visible) noexcept;
    void setSessionName(const QString &name) noexcept;
    void setPortalMode(bool state) noexcept;
    void setNarrowMode(bool state) noexcept;
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
    QLabel *m_narrow_label          = new QLabel(tr("N"));
    QPushButton *m_session_label    = new QPushButton();
    QGridLayout *m_layout           = new QGridLayout(this);
    GraphicsView::Mode m_current_mode;
    bool m_mode_forced_hidden     = false;
    bool m_progress_forced_hidden = false;
    bool m_pageinfo_forced_hidden = false;
};
