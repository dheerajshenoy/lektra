#pragma once

#include <QFont>
#include <QStandardItemModel>
#include <mupdf/fitz.h>

class OutlineModel : public QStandardItemModel
{
public:
    OutlineModel(QObject *parent = nullptr) : QStandardItemModel(parent)
    {
        setHorizontalHeaderLabels({"Title", "Page"});
    }

    // Define a custom role for the hidden data
    enum OutlineRoles
    {
        TargetLocationRole = Qt::UserRole + 1
    };

    void loadFromOutline(fz_outline *root)
    {
        clear();
        setHorizontalHeaderLabels({"Title", "Page"});

        struct StackItem
        {
            fz_outline *node;
            int depth;
        };

        StackItem stack[256];
        int sp = 0;

        fz_outline *current = root;
        int depth           = 0;

        while (current || sp > 0)
        {
            while (current)
            {
                const bool isHeading = current->down != nullptr;
                // QString titleText    = QString::fromUtf8(
                //     current->title ? current->title : "<no title>");
                QByteArray rawData(current->title ? current->title
                                                  : "Untitled");
                QString titleText = QString::fromUtf8(rawData).trimmed();
                if (depth > 0)
                    titleText.prepend(QString(depth * 2, ' '));

                titleText.remove(QChar::ParagraphSeparator); // Remove newlines
                titleText.remove(QChar::LineSeparator);      // Remove newlines

                QStandardItem *titleItem = new QStandardItem(titleText);
                QStandardItem *pageItem  = new QStandardItem(
                    QString::number(current->page.page + 1));

                titleItem->setData(QPointF{current->x, current->y},
                                   TargetLocationRole);

                if (isHeading)
                {
                    QFont font = titleItem->font();
                    font.setBold(true);
                    titleItem->setFont(font);
                }

                invisibleRootItem()->appendRow({titleItem, pageItem});

                if (current->down)
                {
                    if (current->next)
                    {
                        stack[sp++] = {current->next, depth};
                    }
                    depth += 1;
                    current = current->down;
                }
                else
                {
                    current = current->next;
                }
            }

            if (sp > 0)
            {
                StackItem s = stack[--sp];
                current     = s.node;
                depth       = s.depth;
            }
        }
    }
};
