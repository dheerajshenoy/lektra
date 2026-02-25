#include "Statusbar.hpp"

#include "GraphicsView.hpp"

#include <mupdf/pdf/page.h>
#include <qmessagebox.h>
#include <qnamespace.h>
#include <qsizepolicy.h>

Statusbar::Statusbar(const Config &config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    initGui();
    initConnections();
}

void
Statusbar::initConnections() noexcept
{
    connect(m_mode_color_label, &CircleLabel::clicked,
            [&]() { emit modeColorChangeRequested(m_current_mode); });
}

void
Statusbar::initGui() noexcept
{
    const auto padding = m_config.statusbar.padding;
    setContentsMargins(padding[0], padding[1], padding[2], padding[3]);
    m_layout->setContentsMargins(0, 0, 0, 0);

    setLayout(m_layout);

    // Left
    auto *leftLayout = new QHBoxLayout;

    leftLayout->addWidget(m_session_label);
    leftLayout->addWidget(m_filename_label);
    leftLayout->addWidget(m_portal_label);
    m_portal_label->setHidden(true);

    // Center
    auto *centerLayout = new QHBoxLayout;
    m_pageno_label->setFocusPolicy(Qt::ClickFocus);

    centerLayout->addWidget(m_pageno_label);
    centerLayout->addWidget(m_pageno_separator);
    centerLayout->addWidget(m_totalpage_label);

    // Right
    auto *rightLayout = new QHBoxLayout;

    rightLayout->addWidget(m_progress_label);
    rightLayout->addWidget(m_mode_color_label);
    rightLayout->addWidget(m_mode_label);

    m_layout->addLayout(leftLayout, 0, 0, Qt::AlignLeft);
    m_layout->addLayout(centerLayout, 0, 1, Qt::AlignCenter);
    m_layout->addLayout(rightLayout, 0, 2, Qt::AlignRight);

    // Stretch the columns correctly
    m_layout->setColumnStretch(0, 1); // left can expand
    m_layout->setColumnStretch(1, 0); // center fixed
    m_layout->setColumnStretch(2, 1); // right can expand

    connect(m_mode_label, &QPushButton::clicked,
            [&]() { emit modeChangeRequested(); });

    m_filename_label->setVisible(m_config.statusbar.show_file_info);

    m_pageno_label->setVisible(m_config.statusbar.show_page_number);
    m_pageno_separator->setVisible(m_config.statusbar.show_page_number);
    m_totalpage_label->setVisible(m_config.statusbar.show_page_number);

    m_mode_color_label->setVisible(m_config.statusbar.show_mode);
    m_mode_label->setVisible(m_config.statusbar.show_mode);
    m_progress_label->setVisible(m_config.statusbar.show_progress);
}

void
Statusbar::labelBG(QLabel *label, const QColor &color) noexcept
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::Window, color);
    label->setAutoFillBackground(true); // REQUIRED for background to show
    label->setPalette(palette);
}

void
Statusbar::setTotalPageCount(int total) noexcept
{
    m_totalpage_label->setText(QString::number(total));
}

void
Statusbar::setFileName(const QString &name) noexcept
{
    m_filename_label->setFullText(name);
}

void
Statusbar::setPageNo(int pageno) noexcept
{
    m_pageno_label->setText(QString::number(pageno));
    m_pageno_label->setMaximumWidth(
        m_pageno_label->fontMetrics().horizontalAdvance(QString::number(9999))
        + 10);
    m_progress_label->setText(QString("%1%").arg(QString::number(
        (pageno * 100) / std::max(1, m_totalpage_label->text().toInt()))));
}

void
Statusbar::setMode(GraphicsView::Mode mode) noexcept
{
    bool show_color = false;

    switch (mode)
    {
        case GraphicsView::Mode::RegionSelection:
        {
            m_mode_label->setText("Region Selection");
            show_color = false;
        }
        break;

        case GraphicsView::Mode::TextSelection:
        {
            m_mode_label->setText("Text Selection");
            show_color = false;
        }
        break;

        case GraphicsView::Mode::TextHighlight:
        {
            m_mode_label->setText("Text Highlight");
            show_color = true;
        }
        break;

        case GraphicsView::Mode::AnnotSelect:
        {
            m_mode_label->setText("Annot Select");
            show_color = false;
        }
        break;

        case GraphicsView::Mode::AnnotRect:
        {
            m_mode_label->setText("Annot Rect");
            show_color = true;
        }
        break;

        case GraphicsView::Mode::AnnotPopup:
        {
            m_mode_label->setText("Annot Popup");
            show_color = true;
        }
        break;

        case GraphicsView::Mode::VisualLine:
        {
            m_mode_label->setText("Visual Line");
            show_color = false;
        }
        break;

        default:
            break;
    }

    // Respect config setting for mode visibility
    m_mode_color_label->setVisible(m_config.statusbar.show_mode && show_color);
    m_current_mode = mode;
}

void
Statusbar::setHighlightColor(const QColor &color) noexcept
{
    m_mode_color_label->setColor(color);
}

void
Statusbar::hidePageInfo(bool state) noexcept
{
    bool show_page = !state && m_config.statusbar.show_page_number;
    bool show_mode = !state && m_config.statusbar.show_mode;

    m_pageno_label->setVisible(show_page);
    m_pageno_separator->setVisible(show_page);
    m_totalpage_label->setVisible(show_page);
    m_mode_label->setVisible(show_mode);
}

void
Statusbar::setSessionName(const QString &name) noexcept
{
    if (name.isEmpty())
        m_session_label->hide();
    else
    {
        if (m_config.statusbar.show_session_name)
        {
            m_session_label->setText(name);
            m_session_label->show();
        }
    }
}

void
Statusbar::setPortalMode(bool state) noexcept
{
    if (state)
    {
        m_portal_label->setStyleSheet(
            "QLabel { background-color: red; color: white; padding: 2px; }");
        m_portal_label->show();
    }
    else
    {
        m_portal_label->hide();
    }
}
