#include "Statusbar.hpp"

#include "GraphicsView.hpp"

#include <mupdf/pdf/page.h>
#include <qmessagebox.h>
#include <qnamespace.h>
#include <qsizepolicy.h>

Statusbar::Statusbar(const Config::Statusbar &config, QWidget *parent)
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
    const auto &padding = m_config.padding;
    setContentsMargins(padding[0], padding[1], padding[2], padding[3]);
    // setContentsMargins(padding[0], padding[1], padding[2], padding[3]);
    m_layout->setContentsMargins(0, 0, 0, 0);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

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

    m_layout->addLayout(leftLayout, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_layout->addLayout(centerLayout, 0, 1, Qt::AlignCenter | Qt::AlignVCenter);
    m_layout->addLayout(rightLayout, 0, 2, Qt::AlignRight | Qt::AlignVCenter);

    // Stretch the columns correctly
    m_layout->setColumnStretch(0, 1); // left can expand
    m_layout->setColumnStretch(1, 0); // center fixed
    m_layout->setColumnStretch(2, 1); // right can expand

    connect(m_mode_label, &QPushButton::clicked,
            [&]() { emit modeChangeRequested(); });

    m_filename_label->setVisible(m_config.component.filename.show);

    m_pageno_label->setVisible(m_config.component.pagenumber.show);
    m_pageno_separator->setVisible(m_config.component.pagenumber.show);
    m_totalpage_label->setVisible(m_config.component.pagenumber.show);

    m_mode_color_label->setVisible(m_config.component.mode.show);
    m_mode_label->setVisible(m_config.component.mode.show);
    m_progress_label->setVisible(m_config.component.progress.show);
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
Statusbar::setPageInfoVisible(bool state) noexcept
{
    bool show_page_info = !state && m_config.component.pagenumber.show
                          && !m_pageinfo_forced_hidden;
    bool show_mode
        = !state && m_config.component.mode.show && !m_mode_forced_hidden;
    bool show_progress = !state && m_config.component.progress.show
                         && !m_progress_forced_hidden;
    m_pageno_label->setVisible(show_page_info);
    m_pageno_separator->setVisible(show_page_info);
    m_totalpage_label->setVisible(show_page_info);
    m_mode_label->setVisible(show_mode);
    m_progress_label->setVisible(show_progress);
}

void
Statusbar::setModeVisible(bool visible) noexcept
{
    m_mode_forced_hidden = !visible;
    if (!visible)
    {
        m_mode_label->setVisible(false);
        m_mode_color_label->setVisible(false);
    }
    else
    {
        setMode(m_current_mode);
    }
}

void
Statusbar::setProgressVisible(bool visible) noexcept
{
    m_progress_forced_hidden = !visible;
    m_progress_label->setVisible(visible && m_config.component.progress.show);
}

void
Statusbar::setSessionName(const QString &name) noexcept
{
    if (name.isEmpty())
        m_session_label->hide();
    else
    {
        if (m_config.component.session.show)
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

void
Statusbar::setMode(GraphicsView::Mode mode) noexcept
{
    struct ModeInfo
    {
        const char *theme_icon;
        const char *text;
        bool show_color;
    };

    static const std::unordered_map<GraphicsView::Mode, ModeInfo> mode_map = {
        {GraphicsView::Mode::None, {"input-mouse", QT_TR_NOOP("None"), false}},
        {GraphicsView::Mode::RegionSelection,
         {"edit-select", QT_TR_NOOP("Region Selection"), false}},
        {GraphicsView::Mode::TextSelection,
         {"edit-select-text", QT_TR_NOOP("Text Selection"), false}},
        {GraphicsView::Mode::TextHighlight,
         {"format-text-color", QT_TR_NOOP("Text Highlight"), true}},
        {GraphicsView::Mode::AnnotSelect,
         {"edit-select", QT_TR_NOOP("Annot Select"), false}},
        {GraphicsView::Mode::AnnotRect,
         {"draw-rectangle", QT_TR_NOOP("Annot Rect"), true}},
        {GraphicsView::Mode::AnnotPopup,
         {"document-preview", QT_TR_NOOP("Annot Popup"), true}},
        {GraphicsView::Mode::VisualLine,
         {"draw-line", QT_TR_NOOP("Visual Line"), false}},
    };

    const auto it = mode_map.find(mode);
    if (it == mode_map.end())
        return;

    const auto &[theme_icon, text, show_color] = it->second;
    const auto &cfg                            = m_config.component.mode;

    m_mode_label->setIcon(cfg.icon ? QIcon::fromTheme(theme_icon) : QIcon());
    m_mode_label->setText(cfg.text ? tr(text) : "");
    m_mode_label->setToolTip(
        QString(tr("Current mode: <b>%1</b>.<br>Click to change."))
            .arg(tr(text)));

    m_mode_color_label->setVisible(cfg.show && show_color
                                   && !m_mode_forced_hidden);
    m_mode_label->setVisible(cfg.show && !m_mode_forced_hidden);
    m_current_mode = mode;
}

void
Statusbar::setFilePath(const QString &name) noexcept
{
    if (m_config.component.filename.full_path)
        m_filename_label->setFullText(name);
    else
        m_filename_label->setFullText(QFileInfo(name).fileName());
}
