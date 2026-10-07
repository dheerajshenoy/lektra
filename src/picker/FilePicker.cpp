#include "FilePicker.hpp"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>

FilePicker::FilePicker(const Config::Picker &config,
                       QWidget *parent) noexcept
    : Picker(config, parent), m_currentDir(QDir::homePath())
{
    setColumns({{.header  = "Name",
                 .stretch = 2},
                {.header  = "Path",
                 .stretch = 3}});
    setStructureMode(StructureMode::Flat);
    setSearchModes(PickerFilterProxy::SearchMode::Fixed);
}

static QString shortenPath(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QLatin1Char('~') + path.mid(home.length());
    return path;
}

void
FilePicker::launch() noexcept
{
    navigateTo(QDir::homePath(), QString());
    Picker::launch();
}

QList<Picker::Item>
FilePicker::collectItems()
{
    QList<Item> items;
    const QDir dir(m_currentDir);

    QDir parent(m_currentDir);
    if (parent.cdUp())
    {
        items.push_back({.columns  = {QStringLiteral(".."),
                                      parent.absolutePath()},
                         .data     = parent.absolutePath(),
                         .children = {}});
    }

    const auto entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    items.reserve(items.size() + entries.size());
    for (const QFileInfo &fi : entries)
    {
        const QString name
            = fi.isDir() ? fi.fileName() + QLatin1Char('/') : fi.fileName();
        items.push_back({.columns  = {name, fi.absolutePath()},
                         .data     = fi.absoluteFilePath(),
                         .children = {}});
    }
    return items;
}

void
FilePicker::onItemAccepted(const Item &item)
{
    const QString path = item.data.toString();
    if (QFileInfo(path).isDir())
        navigateTo(path, QString());
    else
        emit fileRequested(path);
}

void
FilePicker::onFilterChanged(int)
{
    const QString text = m_searchBox->text();
    if (text.isEmpty())
        return;

    QString expanded = text;
    if (expanded.startsWith(QLatin1Char('~')))
        expanded = QDir::homePath() + expanded.mid(1);

    if (!QDir::isAbsolutePath(expanded))
        expanded = QDir(m_currentDir).filePath(expanded);

    expanded = QDir::cleanPath(expanded);

    if (text.endsWith(QLatin1Char('/')) || text.endsWith(QLatin1Char('\\')))
    {
        if (QFileInfo(expanded).isDir())
            navigateTo(expanded, QString());
        return;
    }

    const QFileInfo fi(expanded);
    const QString dirPart  = fi.absolutePath();
    const QString namePart = fi.fileName();

    if (dirPart != m_currentDir && QFileInfo(dirPart).isDir())
        navigateTo(dirPart, namePart);
}

bool
FilePicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchBox && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete)
            && m_searchBox->text().isEmpty())
        {
            QDir parent(m_currentDir);
            if (parent.cdUp())
                navigateTo(parent.absolutePath(), QString());
            return true;
        }
    }
    return Picker::eventFilter(watched, event);
}

void
FilePicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab)
    {
        completeFromBestMatch();
        event->accept();
        return;
    }

    Picker::keyPressEvent(event);
}

void
FilePicker::completeFromBestMatch() noexcept
{
    int startRow = 0;
    if (m_proxy->rowCount() > 0
        && m_proxy->index(0, 0).data().toString() == QStringLiteral(".."))
        startRow = 1;

    if (startRow >= m_proxy->rowCount())
        return;

    const QString name = m_proxy->index(startRow, 0).data().toString();
    if (name.isEmpty())
        return;

    if (name.endsWith(QLatin1Char('/')))
    {
        navigateTo(QDir(m_currentDir).filePath(name), QString());
    }
    else
    {
        m_searchBox->blockSignals(true);
        m_searchBox->setText(name);
        m_searchBox->blockSignals(false);
        m_proxy->setFilterText(name, caseSensitivity(name));
    }
}

void
FilePicker::navigateTo(const QString &dir, const QString &filter) noexcept
{
    m_currentDir = QDir::cleanPath(dir);
    setPrompt(shortenPath(m_currentDir));
    repopulate();
    m_searchBox->blockSignals(true);
    m_searchBox->setText(filter);
    m_searchBox->blockSignals(false);
    m_proxy->setFilterText(filter, caseSensitivity(filter));
}
