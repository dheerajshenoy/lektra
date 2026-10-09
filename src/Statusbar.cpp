#include "Statusbar.hpp"

#include "GraphicsView.hpp"
#include "StatusbarLayoutSpec.hpp"

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

    m_pageno_label->setFocusPolicy(Qt::ClickFocus);
    m_portal_label->setHidden(true);
    m_narrow_label->setHidden(true);
    m_zoom_label->setHidden(true);

    // The modules. Where each goes is decided by rebuildLayout().
    auto addModule = [this](const QString &name, const QList<QWidget *> &parts)
    {
        auto *box = new QWidget(this);
        auto *row = new QHBoxLayout(box);
        row->setContentsMargins(0, 0, 0, 0);
        for (QWidget *part : parts)
            row->addWidget(part);
        box->hide();
        m_modules.insert(name, {box, parts});
    };
    addModule("session", {m_session_label});
    addModule("filename", {m_filename_label});
    addModule("portal", {m_portal_label});
    addModule("narrow", {m_narrow_label});
    addModule("page", {m_pageno_label, m_pageno_separator, m_totalpage_label});
    addModule("zoom", {m_zoom_label});
    addModule("progress", {m_progress_label});
    addModule("mode", {m_mode_color_label, m_mode_label});

    connect(m_mode_label, &QPushButton::clicked,
            [&]() { emit modeChangeRequested(); });

    m_filename_label->setVisible(m_config.component.filename.show);

    m_pageno_label->setVisible(m_config.component.pagenumber.show);
    m_pageno_separator->setVisible(m_config.component.pagenumber.show);
    m_totalpage_label->setVisible(m_config.component.pagenumber.show);

    m_mode_color_label->setVisible(m_config.component.mode.show);
    m_mode_label->setVisible(m_config.component.mode.show);
    m_progress_label->setVisible(m_config.component.progress.show);

    rebuildLayout();
}

void
Statusbar::rebuildLayout() noexcept
{
    const auto &padding = m_config.padding;
    setContentsMargins(padding[0], padding[1], padding[2], padding[3]);

    m_layout->clear();
    qDeleteAll(m_texts);
    m_texts.clear();
    m_placed.clear();
    for (const Module &module : std::as_const(m_modules))
        module.box->hide();

    QStringList warnings;
    const statusbar_layout::Rows rows
        = statusbar_layout::parse(m_config.layout, &warnings);
    for (const QString &warning : std::as_const(warnings))
        qWarning().noquote() << "[statusbar] layout:" << warning;

    int rowNumber = 0;
    for (const auto &row : rows)
    {
        for (const statusbar_layout::Item &item : row)
        {
            switch (item.kind)
            {
                case statusbar_layout::Item::Kind::Module:
                {
                    const auto it = m_modules.constFind(item.name);
                    if (it == m_modules.constEnd())
                        break;
                    m_layout->addWidgetTo(rowNumber, it->box, item.spec);
                    m_placed << item.name;
                    break;
                }
                case statusbar_layout::Item::Kind::Text:
                {
                    auto *label = new QLabel(item.name, this);
                    m_texts << label;
                    m_layout->addWidgetTo(rowNumber, label, item.spec);
                    label->show();
                    break;
                }
                case statusbar_layout::Item::Kind::Gap:
                    m_layout->addGapTo(rowNumber, item.spec);
                    break;
            }
        }
        ++rowNumber;
    }

    // custom modules the layout does not name go to the right end
    for (auto it = m_custom.constBegin(); it != m_custom.constEnd(); ++it)
    {
        if (m_placed.contains(it.key()))
            continue;
        StatusbarLayout::Spec spec;
        spec.marginLeft = 8;
        m_layout->addWidgetTo(std::max(0, rowNumber - 1),
                              m_modules.value(it.key()).box, spec);
        m_placed << it.key();
    }

    refreshModules();
    m_layout->invalidate();
    updateGeometry();
}

bool
Statusbar::setCustomModule(const QString &name, const QString &text,
                           const QString &tooltip, bool clickable) noexcept
{
    const QString key = statusbar_layout::canonicalModule(name);
    if (!statusbar_layout::isModuleName(key)
        || statusbar_layout::modules().contains(key))
        return false;

    const bool created = !m_custom.contains(key);
    if (created)
    {
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        label->setProperty("module", key);
        label->installEventFilter(this);

        auto *box = new QWidget(this);
        auto *row = new QHBoxLayout(box);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(label);
        box->hide();
        m_modules.insert(key, {box, {label}});
        m_custom.insert(key, label);
    }

    QLabel *label = m_custom.value(key);
    label->setText(text);
    label->setToolTip(tooltip);
    label->setProperty("clickable", clickable);
    label->setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    label->setVisible(!text.isEmpty());

    // setting a segment must not ask the scripts for the segments again
    m_in_custom = true;
    if (created)
        rebuildLayout(); // places it
    else
        refreshModules();
    m_in_custom = false;
    return true;
}

void
Statusbar::removeCustomModule(const QString &name) noexcept
{
    const QString key = statusbar_layout::canonicalModule(name);
    if (!m_custom.contains(key))
        return;

    m_custom.remove(key);
    m_placed.removeAll(key);
    const Module module = m_modules.take(key);
    m_layout->clear(); // forget the box before it is deleted
    delete module.box;

    m_in_custom = true;
    rebuildLayout();
    m_in_custom = false;
}

bool
Statusbar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *label = qobject_cast<QLabel *>(watched);
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (label && mouse->button() == Qt::LeftButton
            && label->property("clickable").toBool()
            && label->rect().contains(mouse->position().toPoint()))
        {
            emit customModuleClicked(label->property("module").toString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void
Statusbar::refreshModules() noexcept
{
    for (auto it = m_modules.constBegin(); it != m_modules.constEnd(); ++it)
    {
        bool anyPart = false;
        for (const QWidget *part : it->parts)
            anyPart = anyPart || !part->isHidden();
        it->box->setVisible(m_placed.contains(it.key()) && anyPart);
    }
    if (!m_in_custom)
        emit changed();
}

void
Statusbar::setZoom(double factor) noexcept
{
    if (factor <= 0.0 || !m_config.component.zoom.show)
        m_zoom_label->hide();
    else
    {
        m_zoom_label->setText(QString("%1%").arg(qRound(factor * 100.0)));
        m_zoom_label->show();
    }
    refreshModules();
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
    emit changed();
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
    refreshModules();
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
    refreshModules();
}

void
Statusbar::setProgressVisible(bool visible) noexcept
{
    m_progress_forced_hidden = !visible;
    m_progress_label->setVisible(visible && m_config.component.progress.show);
    refreshModules();
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
    refreshModules();
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
    refreshModules();
}

void
Statusbar::setNarrowMode(bool state) noexcept
{
    if (state)
    {
        m_narrow_label->setStyleSheet("QLabel { background-color: #e67e00; "
                                      "color: white; padding: 2px; }");
        m_narrow_label->setToolTip(
            tr("Narrow region active — use Wide Region to exit"));
        m_narrow_label->show();
    }
    else
    {
        m_narrow_label->hide();
    }
    refreshModules();
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
        {GraphicsView::Mode::TextUnderline,
         {"format-text-underline", QT_TR_NOOP("Text Underline"), true}},
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
    refreshModules();
}

void
Statusbar::setFilePath(const QString &name) noexcept
{
    if (m_config.component.filename.full_path)
        m_filename_label->setFullText(name);
    else
        m_filename_label->setFullText(QFileInfo(name).fileName());
    emit changed();
}
