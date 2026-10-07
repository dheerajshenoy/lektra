#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

// What to export: which pages, in which format and, for the formats where it
// is a choice, whether to make one file with all the pages or one file per
// page. Where to save comes after, in the file dialog.
class ExportPagesDialog : public QDialog
{
public:
    struct Result
    {
        QString pages;  // as typed, e.g. "1-5,8"
        QString format; // the extension: png, jpg, pdf, ...
        bool split = false;
    };

    // `moreFormats`: also offer svg, text and html (documents with pages, not
    // images or DjVu).
    ExportPagesDialog(int pageCount, int currentPage, bool moreFormats,
                      const QString &format, bool split,
                      QWidget *parent = nullptr);

    Result result() const;

    // Formats that make one file with all the pages, so that making one file
    // per page is a choice (pictures and SVG always make one per page).
    static bool canSplit(const QString &format);

private:
    void refresh();

    int m_page_count;
    int m_current;
    // What the user chose for the box. The box is also shown ticked, and
    // cannot be changed, for the formats that always make one file per page;
    // that must not become the user's choice.
    bool m_split_choice         = false;
    QLineEdit *m_pages          = nullptr;
    QLabel *m_problem           = nullptr;
    QComboBox *m_format         = nullptr;
    QCheckBox *m_split          = nullptr;
    QLabel *m_hint              = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};
