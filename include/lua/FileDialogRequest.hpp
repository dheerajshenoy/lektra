#pragma once

#include <QDir>
#include <QFileDialog>
#include <QString>
#include <QStringList>
#include <memory>

// What a script asks of lektra.ui.file_dialog, and the dialog made from it.
struct FileDialogRequest
{
    enum class Mode
    {
        Open,         // one existing file
        OpenMultiple, // several existing files
        Save,         // a file name, which need not exist
        Directory,    // an existing folder
    };

    Mode mode = Mode::Open;
    QString title;                // default: depends on the mode
    QString directory;            // folder to start in
    QString filename;             // suggested (save) or preselected (open) name
    QStringList filters;          // "Images (*.png *.jpg)", "All files (*)", ...
    QString selectedFilter;       // the one to start with (default: the first)
    QString defaultSuffix;        // save: added to a name typed without one
    bool confirmOverwrite = true; // save: ask before replacing a file

    // The mode named by a script ("open", "open_multiple", "save",
    // "directory"; a few synonyms), or false.
    static bool
    modeFromName(const QString &name, Mode &out)
    {
        const QString n = name.trimmed().toLower();
        if (n == QLatin1String("open") || n == QLatin1String("open_file"))
            out = Mode::Open;
        else if (n == QLatin1String("open_multiple") || n == QLatin1String("open_files")
                 || n == QLatin1String("multiple"))
            out = Mode::OpenMultiple;
        else if (n == QLatin1String("save") || n == QLatin1String("save_file"))
            out = Mode::Save;
        else if (n == QLatin1String("directory") || n == QLatin1String("dir")
                 || n == QLatin1String("folder"))
            out = Mode::Directory;
        else
            return false;
        return true;
    }

    // Filters written as one text, "Images (*.png);;All files (*)", or as a
    // list.
    static QStringList
    splitFilters(const QString &text)
    {
        return text.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
    }

    // The dialog, set up but not shown.
    std::unique_ptr<QFileDialog>
    makeDialog(QWidget *parent) const
    {
        QString caption = title;
        if (caption.isEmpty())
        {
            switch (mode)
            {
                case Mode::Open:         caption = QObject::tr("Open File"); break;
                case Mode::OpenMultiple: caption = QObject::tr("Open Files"); break;
                case Mode::Save:         caption = QObject::tr("Save File"); break;
                case Mode::Directory:    caption = QObject::tr("Choose Folder"); break;
            }
        }

        // The start: a folder, with the name in it when there is one.
        QString start = directory;
        if (!filename.isEmpty() && mode != Mode::Directory)
            start = directory.isEmpty() ? filename : QDir(directory).filePath(filename);

        auto dialog = std::make_unique<QFileDialog>(parent, caption, start);
        switch (mode)
        {
            case Mode::Open:
                dialog->setAcceptMode(QFileDialog::AcceptOpen);
                dialog->setFileMode(QFileDialog::ExistingFile);
                break;
            case Mode::OpenMultiple:
                dialog->setAcceptMode(QFileDialog::AcceptOpen);
                dialog->setFileMode(QFileDialog::ExistingFiles);
                break;
            case Mode::Save:
                dialog->setAcceptMode(QFileDialog::AcceptSave);
                dialog->setFileMode(QFileDialog::AnyFile);
                dialog->setOption(QFileDialog::DontConfirmOverwrite, !confirmOverwrite);
                if (!defaultSuffix.isEmpty())
                    dialog->setDefaultSuffix(defaultSuffix.startsWith(QLatin1Char('.'))
                                                 ? defaultSuffix.mid(1)
                                                 : defaultSuffix);
                break;
            case Mode::Directory:
                dialog->setAcceptMode(QFileDialog::AcceptOpen);
                dialog->setFileMode(QFileDialog::Directory);
                dialog->setOption(QFileDialog::ShowDirsOnly, true);
                break;
        }
        if (!filters.isEmpty() && mode != Mode::Directory)
        {
            dialog->setNameFilters(filters);
            if (!selectedFilter.isEmpty())
                dialog->selectNameFilter(selectedFilter);
        }
        return dialog;
    }
};
