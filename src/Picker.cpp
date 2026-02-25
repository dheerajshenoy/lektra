#include "Picker.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QKeyEvent>
#include <QStandardItem>
#include <QVBoxLayout>

Picker::Picker(QWidget *parent) noexcept : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);

    // Outer layout fills the full picker area — no margins, just holds the
    // frame
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

    m_searchBox = new QLineEdit(m_frame); // parent is frame, not this
    m_searchBox->setPlaceholderText("Search...");
    m_searchBox->setClearButtonEnabled(true);
    innerLayout->addWidget(m_searchBox);
    m_searchBox->installEventFilter(
        this); // to prevent key events from propagating to picker

    m_listView = new QTreeView(m_frame);
    m_listView->setRootIsDecorated(false); // no expand arrows — looks flat
    m_listView->setItemsExpandable(false);
    m_listView->setUniformRowHeights(true);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->header()->setStretchLastSection(true);
    innerLayout->addWidget(m_listView);

    // Models parented to this — they outlive any layout changes
    m_model = new QStandardItemModel(this);
    m_proxy = new PickerFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterRole(Qt::UserRole + 1);
    m_listView->setModel(m_proxy);

    connect(m_searchBox, &QLineEdit::textChanged, this,
            &Picker::onSearchChanged);
    connect(m_listView, &QTreeView::activated, this, &Picker::onItemActivated);

    applyFrameStyle(); // called once, after everything is constructed

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
}

void
Picker::reposition()
{
    // Fixed size, centered in parent
    const QSize s(600, 400);
    resize(s);
    move((parentWidget()->width() - s.width()) / 2,
         (parentWidget()->height() - s.height()) / 2);
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

    if (watched == m_searchBox)
    {
        // FIX: Only treat as QKeyEvent if the type matches
        if (event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            const auto key = keyEvent->keyCombination();

            if (event->type() == QEvent::KeyRelease)
            {
                if (key == m_keys.dismiss)
                {
                    hide();
                    return true;
                }
            }
            else if (event->type() == QEvent::KeyPress)
            {
                if (key == m_keys.moveDown || key == m_keys.moveUp
                    || key == m_keys.pageDown || key == m_keys.pageUp
                    || key == m_keys.accept)
                {
                    keyPressEvent(keyEvent);
                    return true;
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void
Picker::keyPressEvent(QKeyEvent *event)
{
    const QKeyCombination key = event->keyCombination();
    const int row             = m_listView->currentIndex().row();

    if (key == m_keys.moveDown)
    {
        m_listView->setCurrentIndex(
            m_proxy->index(qMin(row + 1, m_proxy->rowCount() - 1), 0));
        event->accept();
    }
    else if (key == m_keys.pageDown)
    {
        m_listView->setCurrentIndex(
            m_proxy->index(qMin(row
                                    + m_listView->viewport()->height()
                                          / m_listView->sizeHintForRow(0),
                                m_proxy->rowCount() - 1),
                           0));
        event->accept();
    }
    else if (key == m_keys.moveUp)
    {
        m_listView->setCurrentIndex(m_proxy->index(qMax(row - 1, 0), 0));
        event->accept();
    }
    else if (key == m_keys.pageUp)
    {
        m_listView->setCurrentIndex(
            m_proxy->index(qMax(row
                                    - m_listView->viewport()->height()
                                          / m_listView->sizeHintForRow(0),
                                0),
                           0));
        event->accept();
    }
    else if (key == m_keys.accept)
    {
        if (m_listView->currentIndex().isValid())
            onItemActivated(m_listView->currentIndex());
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
}

void
Picker::onItemActivated(const QModelIndex &index)
{
    auto item = itemAtProxyIndex(index);
    emit itemSelected(item);
    onItemAccepted(item);
    hide();
}

void
Picker::populate(const QList<Picker::Item> &items)
{
    m_model->clear();

    if (m_columns.isEmpty())
    {
        // Single-column fallback — behaves like the original QListView path
        for (const auto &item : items)
        {
            auto *si = new QStandardItem(item.title);
            si->setData(item.subtitle, Qt::ToolTipRole);
            si->setData(item.title + ' ' + item.subtitle, Qt::UserRole + 1);
            si->setData(QVariant::fromValue(item), Qt::UserRole + 2);
            m_model->appendRow(si);
        }
        return;
    }

    m_model->setColumnCount(m_columns.size());
    for (int col = 0; col < m_columns.size(); ++col)
        m_model->setHorizontalHeaderItem(
            col, new QStandardItem(m_columns[col].header));

    for (const auto &item : items)
    {
        // col 0 = title, col 1 = subtitle, further columns ignored for now
        QList<QStandardItem *> row;
        row.reserve(m_columns.size());

        for (int col = 0; col < m_columns.size(); ++col)
        {
            const QString text = (col == 0)   ? item.title
                                 : (col == 1) ? item.subtitle
                                              : QString{};
            row.append(new QStandardItem(text));
        }

        // Searchable text and payload always live on col 0
        row[0]->setData(item.title + ' ' + item.subtitle, Qt::UserRole + 1);
        row[0]->setData(QVariant::fromValue(item), Qt::UserRole + 2);
        m_model->appendRow(row);
    }

    auto *header = m_listView->header();

    if (header)
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

// picker.cpp
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

    if (m_frame_style.border)
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

    m_shadow_effect->setEnabled(m_frame_style.shadow);
    if (!m_frame_style.shadow)
        return;

    const int blur  = std::max(0, m_frame_style.shadow_blur_radius);
    const int alpha = std::clamp(m_frame_style.shadow_opacity, 0, 255);
    m_shadow_effect->setBlurRadius(blur);
    m_shadow_effect->setOffset(m_frame_style.shadow_offset_x,
                               m_frame_style.shadow_offset_y);
    m_shadow_effect->setColor(QColor(0, 0, 0, alpha));
}
