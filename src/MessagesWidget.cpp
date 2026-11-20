#include "MessagesWidget.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QVBoxLayout>

static QString
entryText(const MessageLog::Entry &entry)
{
    return QStringLiteral("%1  %2  %3")
        .arg(entry.time.toString(QStringLiteral("HH:mm:ss")),
             QStringLiteral("[%1]")
                 .arg(QLatin1String(MessageLog::levelName(entry.level)))
                 .leftJustified(9),
             entry.text);
}

MessagesWidget::MessagesWidget(QWidget *parent) : QWidget(parent)
{
    setProperty("tabRole", "messages");

    auto *layout = new QVBoxLayout(this);

    auto *bar = new QHBoxLayout;
    m_count   = new QLabel(this);
    auto *copy  = new QPushButton(tr("Copy All"), this);
    auto *clear = new QPushButton(tr("Clear"), this);
    bar->addWidget(m_count);
    bar->addStretch(1);
    bar->addWidget(copy);
    bar->addWidget(clear);
    layout->addLayout(bar);

    m_list = new QListWidget(this);
    m_list->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(false);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    connect(m_list, &QListWidget::customContextMenuRequested, this,
            &MessagesWidget::showContextMenu);
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, m_list);
    copyShortcut->setContext(Qt::WidgetShortcut);
    connect(copyShortcut, &QShortcut::activated, this,
            [this] { copySelected(false); });

    connect(copy, &QPushButton::clicked, this, &MessagesWidget::copyAll);
    connect(clear, &QPushButton::clicked, this,
            [] { MessageLog::instance().clear(); });

    MessageLog &log = MessageLog::instance();
    connect(&log, &MessageLog::added, this, &MessagesWidget::append);
    connect(&log, &MessageLog::cleared, this, &MessagesWidget::rebuild);

    rebuild();
}

void
MessagesWidget::rebuild() noexcept
{
    m_list->clear();
    for (const MessageLog::Entry &entry : MessageLog::instance().entries())
        append(entry);
    updateCount();
}

void
MessagesWidget::append(const MessageLog::Entry &entry) noexcept
{
    QScrollBar *scroll = m_list->verticalScrollBar();
    const bool atEnd   = scroll->value() == scroll->maximum();

    auto *item = new QListWidgetItem(entryText(entry), m_list);
    item->setData(Qt::UserRole, entry.text); // the message alone, for copying
    if (entry.level == MessageLog::Level::Warning)
        item->setForeground(QColor(0xD1, 0x9A, 0x00));
    else if (entry.level == MessageLog::Level::Error)
        item->setForeground(QColor(0xE5, 0x39, 0x35));

    // The log drops its oldest entries past its limit; do the same here.
    const int excess = m_list->count()
                       - static_cast<int>(MessageLog::instance().entries().size());
    for (int i = 0; i < excess; ++i)
        delete m_list->takeItem(0);

    if (atEnd)
        m_list->scrollToBottom();
    updateCount();
}

void
MessagesWidget::updateCount() noexcept
{
    m_count->setText(tr("%n message(s)", nullptr, m_list->count()));
}

void
MessagesWidget::copyAll() const noexcept
{
    QStringList lines;
    for (const MessageLog::Entry &entry : MessageLog::instance().entries())
        lines << entryText(entry);
    QApplication::clipboard()->setText(lines.join('\n'));
}

void
MessagesWidget::showContextMenu(const QPoint &pos) noexcept
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    // Right-clicking a row outside the selection copies just that row.
    if (!item->isSelected())
        m_list->setCurrentItem(item);

    QMenu menu(this);
    menu.addAction(tr("Copy Message"), QKeySequence::Copy, this,
                   [this] { copySelected(false); });
    menu.addAction(tr("Copy with Time and Level"), this,
                   [this] { copySelected(true); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

// Copies the selected rows, top to bottom: the message text alone, or the
// whole row as shown.
void
MessagesWidget::copySelected(bool withTimeAndLevel) const noexcept
{
    QStringList lines;
    for (int i = 0; i < m_list->count(); ++i)
    {
        const QListWidgetItem *item = m_list->item(i);
        if (item->isSelected())
            lines << (withTimeAndLevel ? item->text()
                                       : item->data(Qt::UserRole).toString());
    }
    if (!lines.isEmpty())
        QApplication::clipboard()->setText(lines.join('\n'));
}
