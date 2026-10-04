#include "Lektra.hpp"

#include "AboutDialog.hpp"
#include "AppPaths.hpp"
#include "DispatchType.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
#include "DonateDialog.hpp"
#include "EditLastPagesWidget.hpp"
#include "GraphicsView.hpp"
#include "PageLocation.hpp"
#include "SaveSessionDialog.hpp"
#include "SearchBar.hpp"
#include "StartupWidget.hpp"
#include "TabBar.hpp"
#include "utils.hpp"

#include <QColorDialog>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QObject>
#include <QProcess>
#include <QSplitter>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWindow>
#include <variant>

// Opens a widget that allows to edit the recent files
// entries
void
Lektra::editLastPages() noexcept
{
    if (!m_config.behavior.remember_last_visited)
    {
        QMessageBox::information(
            this, tr("Edit Last Pages"),
            tr("Couldn't find the recent files data. Maybe "
               "`remember_last_visited` option is turned off in the config "
               "file"));
        return;
    }

    EditLastPagesWidget *elpw
        = new EditLastPagesWidget(&m_recent_files_store, this);
    elpw->show();
    connect(elpw, &EditLastPagesWidget::finished, this,
            &Lektra::populateRecentFiles);
}

// Helper function to open last visited file
void
Lektra::openLastVisitedFile() noexcept
{
    const auto &entries = m_recent_files_store.entries();
    if (entries.empty())
        return;

    const auto &entry = entries.front();
    if (QFile::exists(entry.file_path))
    {
        OpenFileInNewTab(entry.file_path);
        gotoPage(entry.page_number);
    }
}

bool
Lektra::sessionExists(const QString &name) const noexcept
{
    // Session names are file names: no paths.
    if (name.isEmpty() || name.contains('/') || name.contains('\\')
        || name.startsWith('.'))
        return false;
    return QFile::exists(m_session_dir.filePath(name + ".json"));
}

bool
Lektra::deleteSession(const QString &name) noexcept
{
    if (!sessionExists(name))
        return false;
    return QFile::remove(m_session_dir.filePath(name + ".json"));
}

void
Lektra::saveTabsAsSession(const QList<int> &indices, const QString &name) noexcept
{
    QString sessionName = name.trimmed();
    if (sessionName.isEmpty())
    {
        bool ok = false;
        sessionName = QInputDialog::getText(
            this, tr("Save Session"), tr("Session name:"), QLineEdit::Normal,
            QString(), &ok).trimmed();
        if (!ok || sessionName.isEmpty())
            return;
    }

    const QString fileName = m_session_dir.filePath(sessionName + ".json");
    if (QFile::exists(fileName))
    {
        if (QMessageBox::question(this, tr("Overwrite Session"),
                                  tr("Session \"%1\" already exists. Overwrite it?")
                                      .arg(sessionName))
            != QMessageBox::Yes)
            return;
    }

    QJsonArray sessionArray;
    for (int index : indices)
    {
        DocumentContainer *container = m_tab_widget->rootContainer(index);
        if (!container)
            continue;
        QJsonObject tabEntry;
        tabEntry["splits"] = container->serializeSplits();
        sessionArray.append(tabEntry);
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, tr("Save Session"),
                              tr("Could not save session: %1").arg(sessionName));
        return;
    }
    file.write(QJsonDocument(sessionArray).toJson());
}

// Insert file to store when tab is closed to track
// recent files
void
Lektra::insertFileToDB(const QString &fname, int pageno) noexcept
{
#ifndef NDEBUG
    qDebug() << "Inserting file to recent files store:" << fname
             << "Page number:" << pageno;
#endif
    const QDateTime now = QDateTime::currentDateTime();
    m_recent_files_store.upsert(fname, pageno, now);
    if (!m_recent_files_store.save())
        qWarning() << "Failed to save recent files store";
}

// Loads the given session (if it exists)
void
Lektra::LoadSession(QString sessionName) noexcept
{
    QStringList existingSessions = getSessionFiles();
    if (existingSessions.empty())
    {
        QMessageBox::information(this, tr("Load Session"),
                                 tr("No sessions found"));
        return;
    }

    if (sessionName.isEmpty())
    {
        bool ok;
        sessionName = QInputDialog::getItem(
            this, tr("Load Session"),
            tr("Session to load (existing sessions are listed): "),
            existingSessions, 0, true, &ok);
    }

    QFile file(m_session_dir.filePath(sessionName + ".json"));

    if (file.open(QIODevice::ReadOnly))
    {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);

        if (err.error != QJsonParseError::NoError)
        {
            QMessageBox::critical(this, tr("Session File Parse Error"),
                                  err.errorString());
#ifndef NDEBUG
            qDebug() << "JSON parse error:" << err.errorString();
#endif
            return;
        }

        if (!doc.isArray())
        {
            QMessageBox::critical(this, tr("Session File Parse Error"),
                                  tr("Session file root is not an array"));
#ifndef NDEBUG
            qDebug() << "Session file root is not an array";
#endif
            return;
        }

        // Create a new Lektra window to load the session into if there's
        // document already opened in the current window
        if (m_tab_widget->count() > 0)
        {
            Lektra *newWindow = new Lektra(sessionName, doc.array());
            newWindow->setAttribute(Qt::WA_DeleteOnClose, true);
        }
        else
        {
            // Open here in this window
            openSessionFromArray(doc.array());
        }
    }
    else
    {

        QMessageBox::critical(
            this, tr("Open Session"),
            tr("Could not open session: %1").arg(sessionName));
    }
}

// Returns the session files
QStringList
Lektra::getSessionFiles() noexcept
{
    QStringList sessions;

    if (!m_session_dir.exists())
    {
        if (!m_session_dir.mkpath("."))
        {
            QMessageBox::warning(this, tr("Session Directory"),
                                 tr("Unable to create sessions directory due "
                                    "to an unknown error."));
            return sessions;
        }
    }

    static const QStringList jsonFilter = {"*.json"};
    for (const QString &file :
         m_session_dir.entryList(jsonFilter, QDir::Files | QDir::NoSymLinks))
        sessions << QFileInfo(file).completeBaseName();

    return sessions;
}

// Saves the current session
void
Lektra::SaveSession() noexcept
{
    if (!m_doc)
    {
        QMessageBox::information(this, tr("Save Session"),
                                 tr("No files in session to save the session"));
        return;
    }

    const QStringList &existingSessions = getSessionFiles();

    while (true)
    {
        SaveSessionDialog dialog(existingSessions, this);

        if (dialog.exec() != QDialog::Accepted)
            return;

        const QString &sessionName = dialog.sessionName();

        if (sessionName.isEmpty())
        {
            QMessageBox::information(this, tr("Save Session"),
                                     tr("Session name cannot be empty"));
            return;
        }

        if (m_session_name != sessionName)
        {
            // Ask for overwrite if session with same name exists
            if (existingSessions.contains(sessionName))
            {
                auto choice = QMessageBox::warning(
                    this, tr("Overwrite Session"),
                    tr("Session named \"%1\" already exists. Do you "
                       "want to "
                       "overwrite it?")
                        .arg(sessionName),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

                if (choice == QMessageBox::No)
                    continue;
                if (choice == QMessageBox::Yes)
                {
                    setSessionName(sessionName);
                    break;
                }
            }
            else
            {
                setSessionName(sessionName);
                break;
            }
        }
    }
    // Save the session now
    writeSessionToFile();
}

void
Lektra::writeSessionToFile() noexcept
{
    QJsonArray sessionArray;

    for (int i = 0; i < m_tab_widget->count(); ++i)
    {
        DocumentContainer *container = m_tab_widget->rootContainer(i);
        if (!container)
            continue;

        QJsonObject tabEntry;
        tabEntry["splits"] = container->serializeSplits();
        sessionArray.append(tabEntry);
    }

    const QString sessionFileName
        = m_session_dir.filePath(m_session_name + ".json");
    QFile file(sessionFileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(
            this, tr("Save Session"),
            tr("Could not save session: %1").arg(m_session_name));
        return;
    }
    file.write(QJsonDocument(sessionArray).toJson());
    file.close();
}

// Saves the current session under new name
void
Lektra::SaveAsSession(const QString &sessionPath) noexcept
{
    Q_UNUSED(sessionPath);
    if (m_session_name.isEmpty())
    {
        QMessageBox::information(this, tr("Save As Session"),
                                 tr("Cannot save session as you are not "
                                    "currently in a session"));
        return;
    }

    QStringList existingSessions = getSessionFiles();

    QString selectedPath = QFileDialog::getSaveFileName(
        this, tr("Save As Session"), m_session_dir.absolutePath(),
        tr("Lektra session files") + " " + "(*.json);" + tr("All Files") + " "
            + "(*.*)");

    if (selectedPath.isEmpty())
        return;

    if (QFile::exists(selectedPath))
    {
        auto choice = QMessageBox::warning(
            this, tr("Overwrite Session"),
            tr("Session named \"%1\" already exists. Do you want to "
               "overwrite it?")
                .arg(QFileInfo(selectedPath).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (choice != QMessageBox::Yes)
            return;
    }

    // Save the session
    QString currentSessionPath
        = m_session_dir.filePath(m_session_name + ".json");
    if (!QFile::copy(currentSessionPath, selectedPath))
    {
        QMessageBox::critical(this, tr("Save As Session"),
                              tr("Failed to save session."));
    }
}

// Trims the recent files store to `num_recent_files` number of files
void
Lektra::trimRecentFilesDatabase() noexcept
{
    // If num_recent_files config entry has negative value,
    // retain all the recent files
    if (m_config.behavior.num_recent_files < 0)
        return;

    m_recent_files_store.trim(m_config.behavior.num_recent_files);
    if (!m_recent_files_store.save())
        qWarning() << tr("Failed to trim recent files store");
}

void
Lektra::cleanRecentFilesDatabase() noexcept
{
    const int removed = m_recent_files_store.removeMissingFiles();
    if (removed <= 0)
        return;

    if (!m_recent_files_store.save())
        qWarning() << tr("Failed to save recent files store after cleaning");

    populateRecentFiles();
}

void
Lektra::setSessionName(const QString &name) noexcept
{
    m_session_name = name;
    m_statusbar->setSessionName(name);
}

void
Lektra::openSessionFromArray(const QJsonArray &sessionArray) noexcept
{
    for (const QJsonValue &val : sessionArray)
    {
        const QJsonObject tabObj     = val.toObject();
        const QJsonObject splitsNode = tabObj["splits"].toObject();

        // Legacy format — flat entry with file_path at top level
        if (splitsNode.isEmpty() && tabObj.contains("file_path"))
        {
            const QString filePath = tabObj["file_path"].toString();
            if (filePath.isEmpty())
                continue;

            const int page    = tabObj["current_page"].toInt();
            const double zoom = tabObj["zoom"].toDouble();
            const int fitMode = tabObj["fit_mode"].toInt();
            const bool invert = tabObj["invert_color"].toBool();

            DocumentView *view = OpenFileInNewTab(filePath, {});
            if (!view)
                continue;

            connect(view, &DocumentView::openFileFinished, this,
                    [view, page, zoom, fitMode, invert](DocumentView *,
                                                        Model::FileType)
            {
                if (invert)
                    view->setInvertColor(true);
                view->setFitMode(static_cast<DocumentView::FitMode>(fitMode));
                view->setZoom(zoom);
                view->GotoPage(page);
            },
                    Qt::SingleShotConnection);

            continue;
        }

        if (splitsNode.isEmpty())
            continue;

        // Find the first file path in the splits tree to open the tab with
        std::function<QString(const QJsonObject &)> firstFilePath
            = [&firstFilePath](const QJsonObject &node) -> QString
        {
            if (node["type"].toString() == "view")
                return node["file_path"].toString();
            const QJsonArray children = node["children"].toArray();
            for (const QJsonValue &child : children)
            {
                const QString path = firstFilePath(child.toObject());
                if (!path.isEmpty())
                    return path;
            }
            return {};
        };

        const QString startFile = firstFilePath(splitsNode);
        if (startFile.isEmpty())
            continue;

        DocumentView *view = OpenFileInNewTab(startFile, {});
        if (!view)
            continue;

        connect(view, &DocumentView::openFileFinished, this,
                [this, view, splitsNode](DocumentView *, Model::FileType)
        {
            int idx = m_tab_widget->indexOf(view->container());
            if (idx < 0)
                return;
            DocumentContainer *container = m_tab_widget->rootContainer(idx);
            if (!container)
                return;
            restoreSplitNode(container, view, splitsNode, nullptr);
            m_tab_widget->tabBar()->set_split_count(idx,
                                                    container->getViewCount());
        }, Qt::SingleShotConnection);
    }

    // Restore focus to first tab after all tabs are queued
    if (m_tab_widget->count() > 0)
        m_tab_widget->setCurrentIndex(0);
}

void
Lektra::AddBookmark() noexcept
{
    if (!m_doc)
        return;

    m_bookmark_manager.addBookmark({m_doc->filePath(), m_doc->CurrentLocation(),
                                    QDateTime::currentDateTime()});
}

void
Lektra::RemoveBookmark() noexcept
{
    if (!m_doc)
        return;

    // TODO: Fix the implementation
}

void
Lektra::BookmarkExport(const QString &file_path) noexcept
{
    QString path = file_path;
    if (path.isEmpty())
    {
        path = QFileDialog::getSaveFileName(
            this, tr("Export Bookmarks"),
            QDir(m_app_data_dir).filePath("bookmarks-export.json"),
            tr("JSON files (*.json);;All files (*)"));
        if (path.isEmpty())
            return;
    }

    // BookmarkManager::saveBookmarks handles the serialisation format —
    // same schema as ~/.local/share/lektra/bookmarks.json, so an export can
    // be moved to another machine and dropped in as bookmarks.json.
    m_bookmark_manager.saveBookmarks(path);

    if (m_message_bar)
        m_message_bar->showMessage(
            tr("Exported %1 bookmark(s) to %2")
                .arg(m_bookmark_manager.bookmarks().size())
                .arg(QFileInfo(path).fileName()),
            3.0f);
}

void
Lektra::BookmarkImport(const QString &file_path) noexcept
{
    QString path = file_path;
    if (path.isEmpty())
    {
        path = QFileDialog::getOpenFileName(
            this, tr("Import Bookmarks"), QString(),
            tr("JSON files (*.json);;All files (*)"));
        if (path.isEmpty())
            return;
    }

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Import Bookmarks"),
                             tr("Could not open %1").arg(path));
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &err);
    in.close();

    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
        QMessageBox::warning(this, tr("Import Bookmarks"),
                             tr("Invalid bookmarks file: %1")
                                 .arg(err.error == QJsonParseError::NoError
                                          ? tr("root is not a JSON array")
                                          : err.errorString()));
        return;
    }

    // Merge, don't replace: import is idempotent on repeated runs.
    auto existing = m_bookmark_manager.bookmarks();
    QSet<Bookmark::BookmarkId> existing_ids;
    for (const auto &b : existing)
        existing_ids.insert(b.id());

    int added = 0, skipped = 0;
    for (const QJsonValue &v : doc.array())
    {
        if (!v.isObject())
        {
            ++skipped;
            continue;
        }
        const QJsonObject o = v.toObject();
        const QString id    = o["id"].toString();
        if (!id.isEmpty() && existing_ids.contains(id))
        {
            ++skipped;
            continue;
        }

        PageLocation loc;
        try
        {
            loc = PageLocation::fromJson(o["location"].toArray());
        }
        catch (const std::exception &)
        {
            ++skipped;
            continue;
        }
        const QString imported_file = o["file_path"].toString();
        const QDateTime created
            = QDateTime::fromString(o["added_on"].toString(), Qt::ISODate);

        existing.emplace_back(imported_file, loc, created, id);
        if (!id.isEmpty())
            existing_ids.insert(id);
        ++added;
    }

    m_bookmark_manager.setBookmarks(existing);
    // Write back to the live bookmarks file so the merge persists.
    m_bookmark_manager.saveBookmarks(m_bookmarks_file_path);

    if (m_message_bar)
        m_message_bar->showMessage(
            tr("Imported %1 bookmark(s), skipped %2 duplicate(s)")
                .arg(added)
                .arg(skipped),
            3.0f);
}
