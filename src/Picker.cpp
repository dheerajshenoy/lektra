#include "Picker.hpp"

#include "utils.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QKeyEvent>
#include <QShortcutEvent>
#include <QStandardItem>
#include <QVBoxLayout>

namespace
{
// The key of an event without the modifiers that only say how it was typed
// (a keypad key, or an arrow key some platforms flag as one), so that "Up"
// matches whichever way it arrives.
QKeyCombination
normalizedKey(const QKeyEvent *e) noexcept
{
    return QKeyCombination(e->modifiers() & ~Qt::KeypadModifier
                               & ~Qt::GroupSwitchModifier,
                           static_cast<Qt::Key>(e->key()));
}
} // namespace

Picker::Picker(const Config::Picker &config, QWidget *parent) noexcept
    : QWidget(parent), m_config(config)
{
    float w = m_config.width, h = m_config.height;

    if (w > 0.0f && w <= 1.0f)
        w *= parent->width();
    if (h > 0.0f && h <= 1.0f)
        h *= parent->height();

    resize(static_cast<int>(w), static_cast<int>(h));
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(12, 12, 12, 12); // room for shadow bleed
    outerLayout->setSpacing(0);

    // Frame is the visible card — shadow is applied to this
    m_frame = new QFrame(this);
    m_frame->setFrameShape(QFrame::NoFrame);
    m_frame->setAttribute(Qt::WA_StyledBackground, true);
    m_frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_shadow_effect = new QGraphicsDropShadowEffect(m_frame);
    m_frame->setGraphicsEffect(m_shadow_effect);

    outerLayout->addWidget(m_frame);

    // Inner layout lives on the frame, widgets parented to frame
    auto *innerLayout = new QVBoxLayout(m_frame);
    innerLayout->setContentsMargins(8, 8, 8, 8);
    innerLayout->setSpacing(4);

    auto *hboxLayout = new QHBoxLayout();
    hboxLayout->setContentsMargins(0, 0, 0, 0);
    hboxLayout->setSpacing(4);

    m_searchBox = new QLineEdit(m_frame);
    m_searchBox->setClearButtonEnabled(true);

    m_promptLabel = new QLabel(m_frame);
    m_promptLabel->setText("Prompt:");
    m_promptLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    hboxLayout->addWidget(m_promptLabel);
    hboxLayout->addWidget(m_searchBox);

    innerLayout->addLayout(hboxLayout);
    m_searchBox->installEventFilter(this);

    m_listView = new QTreeView(m_frame);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->header()->setStretchLastSection(true);
    m_listView->setAlternatingRowColors(m_config.alternating_row_color);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);

    innerLayout->addWidget(m_listView);

    // Models parented to this — they outlive any layout changes
    m_model = new QStandardItemModel(this);
    m_proxy = new PickerFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterRole(Qt::UserRole + 1);
    m_listView->setModel(m_proxy);

    if (m_config.highlight_matches)
    {
        m_highlight_delegate = new HighlightDelegate(this);
        m_highlight_delegate->setHighlighter(m_proxy);
        m_listView->setItemDelegate(m_highlight_delegate);

        m_highlight_delegate->setHighlightColor(
            rgbaToQColor(m_config.highlight_matches_color));
    }

    connect(m_searchBox, &QLineEdit::textChanged, this,
            &Picker::onSearchChanged);
    connect(m_listView, &QTreeView::clicked, this, &Picker::onItemClicked);
    connect(m_listView, &QTreeView::activated, this, &Picker::onItemActivated);

    applyFrameStyle();

    parent->installEventFilter(this);
    hide();
}

void
Picker::launch() noexcept
{
    m_searchBox->clear();
    populate(collectItems());
    reposition();
    show();
    raise();
    m_listView->setCurrentIndex(m_proxy->index(0, 0));
    m_searchBox->setFocus();
    QApplication::instance()->installEventFilter(this);
}

void
Picker::releaseInputGrab() noexcept
{
    QApplication::instance()->removeEventFilter(this);
}

void
Picker::reposition()
{
    QWidget *p = parentWidget();

    float w = m_config.width, h = m_config.height;

    if (w > 0.0f && w <= 1.0f)
        w *= p->width();
    if (h > 0.0f && h <= 1.0f)
        h *= p->height();

    resize(static_cast<int>(w), static_cast<int>(h));
    move((p->width() - width()) / 2, (p->height() - height()) / 2);
}

bool
Picker::isPickerKey(const QKeyCombination &key) const noexcept
{
    return m_keys.moveDown.contains(key) || m_keys.moveUp.contains(key)
           || m_keys.pageDown.contains(key) || m_keys.pageUp.contains(key)
           || m_keys.sectionNext.contains(key)
           || m_keys.sectionPrev.contains(key) || m_keys.accept.contains(key)
           || m_keys.expand.contains(key) || m_keys.collapse.contains(key)
           || m_keys.toggleStructureMode.contains(key)
           || m_keys.historyNext.contains(key)
           || m_keys.historyPrev.contains(key);
}

bool
Picker::eventFilter(QObject *watched, QEvent *event)
{
    // Only reposition on parent resize — no move tracking needed
    if (watched == parentWidget() && event->type() == QEvent::Resize)
    {
        reposition();
        return false;
    }

    // Block all shortcuts from reaching the rest of the app while visible.
    if (isVisible() && event->type() == QEvent::Shortcut)
        return true;

    // A key the picker uses (the arrows, say) may also be a shortcut of the
    // application. Claiming the ShortcutOverride makes Qt deliver it as a
    // plain key press to the picker instead of running the shortcut.
    if (isVisible() && event->type() == QEvent::ShortcutOverride)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (isPickerKey(normalizedKey(keyEvent)))
        {
            keyEvent->accept();
            return true;
        }
    }

    if (watched == m_searchBox)
    {
        if (event->type() == QEvent::KeyRelease)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (m_keys.dismiss.contains(normalizedKey(keyEvent)))
            {
                releaseInputGrab();
                hide();
                if (parentWidget())
                    parentWidget()->setFocus();
                return true;
            }
        }
        else if (event->type() == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (isPickerKey(normalizedKey(keyEvent)))
            {
                keyPressEvent(keyEvent);
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void
Picker::keyPressEvent(QKeyEvent *event)
{
    const QKeyCombination key = normalizedKey(event);
    const QModelIndex current = m_listView->currentIndex();

    if (!current.isValid())
    {
        QWidget::keyPressEvent(event);
        return;
    }

    if (m_keys.moveDown.contains(key))
    {
        QModelIndex next = m_listView->indexBelow(current);
        if (next.isValid())
            m_listView->setCurrentIndex(next);
        event->accept();
    }

    else if (m_keys.pageDown.contains(key))
    {
        QModelIndex idx = current;
        const int steps = qMax(1, m_listView->viewport()->height()
                                      / qMax(1, m_listView->sizeHintForRow(0)));
        for (int i = 0; i < steps; ++i)
        {
            QModelIndex next = m_listView->indexBelow(idx);
            if (!next.isValid())
                break;
            idx = next;
        }
        if (idx.isValid())
            m_listView->setCurrentIndex(idx);
        event->accept();
    }

    else if (m_keys.moveUp.contains(key))
    {
        QModelIndex prev = m_listView->indexAbove(current);
        if (prev.isValid())
            m_listView->setCurrentIndex(prev);
        event->accept();
    }

    else if (m_keys.pageUp.contains(key))
    {
        QModelIndex idx = current;
        const int steps = qMax(1, m_listView->viewport()->height()
                                      / qMax(1, m_listView->sizeHintForRow(0)));
        for (int i = 0; i < steps; ++i)
        {
            QModelIndex prev = m_listView->indexAbove(idx);
            if (!prev.isValid())
                break;
            idx = prev;
        }
        if (idx.isValid())
            m_listView->setCurrentIndex(idx);
        event->accept();
    }

    else if (m_keys.sectionNext.contains(key))
    {
        QModelIndex idx = m_listView->indexBelow(current);
        while (idx.isValid() && m_proxy->rowCount(idx) <= 0)
            idx = m_listView->indexBelow(idx);
        if (idx.isValid())
            m_listView->setCurrentIndex(idx);
        event->accept();
    }

    else if (m_keys.sectionPrev.contains(key))
    {
        QModelIndex idx = m_listView->indexAbove(current);
        while (idx.isValid() && m_proxy->rowCount(idx) <= 0)
            idx = m_listView->indexAbove(idx);
        if (idx.isValid())
            m_listView->setCurrentIndex(idx);
        event->accept();
    }

    else if (m_keys.accept.contains(key))
    {
        if (m_listView->currentIndex().isValid())
            onItemActivated(m_listView->currentIndex());
        event->accept();
    }

    else if (m_keys.expand.contains(key) || m_keys.collapse.contains(key))
    {
        const QModelIndex index = m_listView->currentIndex();
        if (!index.isValid() || m_structureMode != StructureMode::Hierarchical
            || m_proxy->rowCount(index) <= 0)
        {
            event->accept();
            return;
        }

        if (m_keys.expand.contains(key) && !m_keys.collapse.contains(key))
            m_listView->setExpanded(index, true);
        else if (m_keys.collapse.contains(key) && !m_keys.expand.contains(key))
            m_listView->setExpanded(index, false);
        else
            m_listView->setExpanded(index, !m_listView->isExpanded(index));

        event->accept();
    }

    else if (m_keys.toggleStructureMode.contains(key))
    {
        setStructureMode(m_structureMode == StructureMode::Flat
                             ? StructureMode::Hierarchical
                             : StructureMode::Flat);
        event->accept();
    }

    else if (m_keys.historyPrev.contains(key))
    {
        historyPrev();
        event->accept();
    }

    else if (m_keys.historyNext.contains(key))
    {
        historyNext();
        event->accept();
    }

    else
    {
        QWidget::keyPressEvent(event);
    }
}

void
Picker::onSearchChanged(const QString &text)
{
    m_proxy->setFilterText(text, caseSensitivity(text));
    m_listView->setCurrentIndex(m_proxy->index(0, 0));
    onFilterChanged(m_proxy->rowCount());

    // Force re-highlight of every visible cell, not just re-filtered ones.
    m_listView->viewport()->update();
}

void
Picker::onItemClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    if (m_structureMode == StructureMode::Hierarchical
        && m_proxy->rowCount(index) > 0)
        m_listView->setExpanded(index, !m_listView->isExpanded(index));
}

void
Picker::onItemActivated(const QModelIndex &index)
{
    auto item = itemAtProxyIndex(index);

    releaseInputGrab();
    emit itemSelected(item);
    onItemAccepted(item);
    hide();
}

void
Picker::populate(const QList<Picker::Item> &items)
{
    m_model->clear();
    m_model->setColumnCount(m_columns.size());

    for (int col = 0; col < m_columns.size(); ++col)
        m_model->setHorizontalHeaderItem(
            col, new QStandardItem(m_columns[col].header));

    std::function<void(const QList<Item> &, QStandardItem *)> addItems
        = [&](const QList<Item> &items, QStandardItem *parent)
    {
        for (const auto &it : items)
        {
            QList<QStandardItem *> row;
            row.reserve(m_columns.size());

            for (int col = 0; col < m_columns.size(); ++col)
            {
                const QString text
                    = (col < it.columns.size()) ? it.columns[col] : QString{};
                auto *cell = new QStandardItem(text);
                cell->setTextAlignment(m_columns[col].alignment);
                row.append(cell);
            }

            QString searchText;
            for (const auto &c : it.columns)
                if (!c.isEmpty())
                    searchText += c + ' ';

            row[0]->setData(searchText.trimmed(), Qt::UserRole + 1);
            row[0]->setData(QVariant::fromValue(it), Qt::UserRole + 2);

            if (parent)
                parent->appendRow(row);
            else
                m_model->appendRow(row);

            if (!it.children.isEmpty())
                addItems(it.children, row[0]);
        }
    };

    addItems(items, nullptr);

    // header setup (unchanged)
    if (auto *header = m_listView->header())
    {
        header->setStretchLastSection(false);
        for (int i = 0; i < m_columns.size(); ++i)
        {
            if (m_columns[i].stretch > 0)
                header->setSectionResizeMode(i, QHeaderView::Stretch);
            else
                header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        }
    }
}

Picker::Item
Picker::itemAtProxyIndex(const QModelIndex &proxyIndex) const
{
    return m_model->itemFromIndex(m_proxy->mapToSource(proxyIndex))
        ->data(Qt::UserRole + 2)
        .value<Item>();
}

void
Picker::repopulate() noexcept
{
    populate(collectItems());
    m_listView->setCurrentIndex(m_proxy->index(0, 0));
    onFilterChanged(m_proxy->rowCount());
}

void
Picker::applyFrameStyle() noexcept
{
    if (!m_frame)
        return;

    if (m_config.border)
    {
        m_frame->setObjectName("overlayFrameBorder");
        m_frame->setStyleSheet("QFrame#overlayFrameBorder {"
                               " background-color: palette(base);"
                               " border: 1px solid palette(highlight);"
                               " border-radius: 8px;"
                               " }");
    }
    else
    {
        m_frame->setObjectName(QString());
        m_frame->setStyleSheet("background-color: palette(base);");
    }

    if (!m_shadow_effect)
        return;

    if (auto &enabled = m_config.shadow.enabled)
    {
        m_shadow_effect->setEnabled(enabled);
        const int blur  = std::max(0, m_config.shadow.blur_radius);
        const int alpha = std::clamp(m_config.shadow.opacity, 0, 255);
        m_shadow_effect->setBlurRadius(blur);
        m_shadow_effect->setOffset(m_config.shadow.offset_x,
                                   m_config.shadow.offset_y);
        m_shadow_effect->setColor(QColor(0, 0, 0, alpha));
    }
}

void
Picker::setStructureMode(StructureMode mode) noexcept
{
    m_structureMode = mode;

    m_listView->setRootIsDecorated(mode == StructureMode::Hierarchical);
    m_listView->setItemsExpandable(mode == StructureMode::Hierarchical);
}

void
Picker::setPrompt(const QString &prompt) noexcept
{
    if (prompt.isEmpty())
    {
        m_promptLabel->hide();
        return;
    }
    else
    {
        m_promptLabel->setText(prompt);
        m_promptLabel->show();
    }
}

void
Picker::historyPrev() noexcept
{
    if (m_history.isEmpty())
        return;

    const QString current = m_searchBox->text();
    if (!current.isEmpty()
        && (m_history.isEmpty() || m_history.last() != current))
        m_history.append(current);

    if (m_history.size() > 1)
    {
        m_history.removeLast();
        m_searchBox->setText(m_history.last());
    }
}

void
Picker::historyNext() noexcept
{
    if (m_history.isEmpty())
        return;

    const QString current = m_searchBox->text();
    if (!current.isEmpty()
        && (m_history.isEmpty() || m_history.last() != current))
        m_history.append(current);

    if (m_history.size() > 1)
    {
        m_history.removeLast();
        m_searchBox->setText(m_history.last());
    }
}
