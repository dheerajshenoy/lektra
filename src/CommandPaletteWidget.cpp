#include "CommandPaletteWidget.hpp"

#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QKeySequence>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

class CommandItemDelegate final : public QStyledItemDelegate
{
public:
    CommandItemDelegate(QObject *parent    = nullptr,
                        bool showShortcuts = true) noexcept
        : QStyledItemDelegate(parent), m_showShortcuts(showShortcuts)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        QStyle *style
            = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter,
                             opt.widget);

        const QString name = index.data(Qt::DisplayRole).toString();

        painter->save();
        painter->setFont(opt.font);
        painter->setPen(opt.palette.color(QPalette::Text));

        const QRect textRect = opt.rect.adjusted(8, 0, -8, 0);
        const QFontMetrics fm(opt.font);

        if (!m_showShortcuts)
        {
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, name);
            painter->restore();
            return;
        }

        const QString shortcut = index.data(Qt::UserRole).toString();
        const QString shortcutText
            = shortcut.isEmpty() ? QString() : QString("(%1)").arg(shortcut);
        if (shortcutText.isEmpty())
        {
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, name);
            painter->restore();
            return;
        }
        const int shortcutWidth = fm.horizontalAdvance(shortcutText);

        const int spacing = fm.horizontalAdvance(QStringLiteral("  "));
        const int nameWidth
            = qMax(0, textRect.width() - shortcutWidth - spacing);
        const QRect nameRect(textRect.left(), textRect.top(), nameWidth,
                             textRect.height());
        const QRect shortcutRect(textRect.left(), textRect.top(),
                                 textRect.width(), textRect.height());

        const QString elidedName
            = fm.elidedText(name, Qt::ElideRight, nameWidth);
        painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                          elidedName);

        painter->setPen(opt.palette.color(QPalette::PlaceholderText));
        painter->drawText(shortcutRect, Qt::AlignVCenter | Qt::AlignRight,
                          shortcutText);
        painter->restore();
    }

private:
    bool m_showShortcuts{true};
};

namespace
{
class CommandFilterProxy final : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFilterInput(const QString &input)
    {
        const QString trimmed = input.trimmed();
        if (trimmed.isEmpty())
        {
            m_tokens.clear();
        }
        else
        {
            m_tokens
                = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (QString &token : m_tokens)
                token = token.toLower();
        }

        invalidate();
    }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override
    {
        if (m_tokens.isEmpty())
            return true;

        const QModelIndex index
            = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString name = index.data(Qt::DisplayRole).toString().toLower();
        const QString normalized_name
            = QString(name).replace(QChar('_'), QChar(' '));

        for (const QString &token : m_tokens)
        {
            const QString normalized_token
                = QString(token).replace(QChar('_'), QChar(' '));
            if (!name.contains(token)
                && !normalized_name.contains(normalized_token))
                return false;
        }

        return true;
    }

private:
    QStringList m_tokens;
};
} // namespace

// ---- CommandPaletteWidget Implementation ----

CommandPaletteWidget::CommandPaletteWidget(
    const Config &config,
    const std::vector<std::pair<QString, QString>> &commands,
    QWidget *parent) noexcept
    : QWidget(parent), m_config(config)
{
    m_command_model = new CommandModel(commands, this);
    initGui();
    initConnections();
    selectFirstItem();
}

void
CommandPaletteWidget::initGui() noexcept
{
    m_command_table = new QTableView(this);
    m_command_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_command_table->horizontalHeader()->setStretchLastSection(true);
    if (!m_command_model)
        m_command_model = new CommandModel({}, this);
    m_proxy_model = new CommandFilterProxy(this);
    m_proxy_model->setSourceModel(m_command_model);
    m_command_table->setModel(m_proxy_model);
    m_command_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_command_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_command_table->horizontalHeader()->setVisible(false);
    m_command_table->verticalHeader()->setVisible(false);

    if (m_config.command_palette.show_grid)
        m_command_table->setShowGrid(true);
    else
        m_command_table->setGridStyle(Qt::NoPen);
    m_command_table->setContentsMargins(0, 0, 0, 0);
    m_command_table->setFrameStyle(QFrame::NoFrame);
    m_command_table->setItemDelegate(new CommandItemDelegate(
        m_command_table, m_config.command_palette.show_shortcuts));

    this->setMinimumSize(m_config.command_palette.width,
                         m_config.command_palette.height);

    m_input_line = new QLineEdit(this);
    m_input_line->setPlaceholderText(
        m_config.command_palette.placeholder_text);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(m_input_line);
    layout->addWidget(m_command_table);

    this->setLayout(layout);

    setStyleSheet("QLineEdit {"
                  "  padding: 8px 10px;"
                  "  border-radius: 8px;"
                  "  border: 1px solid palette(midlight);"
                  "  background: palette(base);"
                  "}"
                  "QTableView {"
                  "  background: palette(base);"
                  "  border: 1px solid palette(midlight);"
                  "  border-radius: 10px;"
                  "}"
                  "QTableView::item {"
                  "  padding: 6px;"
                  "}"
                  "QHeaderView::section {"
                  "  background: palette(window);"
                  "  padding: 6px 8px;"
                  "  border: none;"
                  "  font-weight: 600;"
                  "}");
}

void
CommandPaletteWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_input_line->clear();
    m_input_line->setFocus();
}

void
CommandPaletteWidget::selectFirstItem() noexcept
{
    if (!m_proxy_model || m_proxy_model->rowCount() == 0)
        return;

    const QModelIndex first = m_proxy_model->index(0, 0);
    if (!first.isValid())
        return;

    m_command_table->setCurrentIndex(first);
    m_command_table->selectionModel()->select(
        first, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_command_table->scrollTo(first);
}

void
CommandPaletteWidget::initConnections() noexcept
{
    connect(m_input_line, &QLineEdit::textChanged, this,
            [this](const QString &text)
    {
        auto *proxy = static_cast<CommandFilterProxy *>(m_proxy_model);
        proxy->setFilterInput(text);
        selectFirstItem();
    });

    connect(m_input_line, &QLineEdit::returnPressed, this, [this]()
    {
        const QModelIndex current = m_command_table->currentIndex();
        if (!current.isValid())
            return;
        const QString commandName
            = current.siblingAtColumn(0).data().toString();

        // Get args by splitting command name by spaces, and assume args start
        // from 2nd word
        QStringList args = commandName.split(' ');
        if (args.size() > 1)
            args = args.mid(1);
        else
            args.clear();
        hide();
        emit commandSelected(commandName, args);
    });

    auto *downShortcut = new QShortcut(QKeySequence(Qt::Key_Down), this);
    auto *upShortcut   = new QShortcut(QKeySequence(Qt::Key_Up), this);

    connect(downShortcut, &QShortcut::activated, this, [this]()
    {
        QModelIndex current = m_command_table->currentIndex();
        if (!current.isValid())
            return;
        int nextRow = current.row() + 1;
        if (nextRow >= m_proxy_model->rowCount())
            nextRow = 0; // Wrap around
        const QModelIndex nextIndex = m_proxy_model->index(nextRow, 0);
        m_command_table->setCurrentIndex(nextIndex);
        m_command_table->selectionModel()->select(
            nextIndex,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_command_table->scrollTo(nextIndex);
    });

    connect(upShortcut, &QShortcut::activated, this, [this]()
    {
        QModelIndex current = m_command_table->currentIndex();
        if (!current.isValid())
            return;
        int prevRow = current.row() - 1;
        if (prevRow < 0)
            prevRow = m_proxy_model->rowCount() - 1; // Wrap around
        const QModelIndex prevIndex = m_proxy_model->index(prevRow, 0);
        m_command_table->setCurrentIndex(prevIndex);
        m_command_table->selectionModel()->select(
            prevIndex,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_command_table->scrollTo(prevIndex);
    });
}

// ---- CommandModel Implementation ----

CommandModel::CommandModel(
    const std::vector<std::pair<QString, QString>> &commands,
    QObject *parent) noexcept
    : QAbstractTableModel(parent)
{
    m_commands.reserve(commands.size());
    for (const auto &entry : commands)
        m_commands.push_back({entry.first, entry.second});
}

int
CommandModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return static_cast<int>(m_commands.size());
}

int
CommandModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1; // Name, Shortcut
}

QVariant
CommandModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    if (index.row() < 0 || index.row() >= static_cast<int>(m_commands.size()))
        return QVariant();
    const CommandEntry &entry = m_commands[index.row()];

    switch (role)
    {
        case Qt::DisplayRole:
            return entry.name;
        case Qt::UserRole:
            return entry.shortcut;
    }

    return QVariant();
}
