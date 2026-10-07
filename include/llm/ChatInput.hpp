#pragma once

#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QTextEdit>
#include <QUrl>
#include <functional>

// The chat's text field. Pasting or dropping an image (or an image file)
// attaches it to the message instead of inserting text.
class ChatInput : public QTextEdit
{
public:
    using ImageHandler = std::function<void(const QImage &image)>;
    using FileHandler  = std::function<void(const QString &path)>;

    explicit ChatInput(QWidget *parent = nullptr) : QTextEdit(parent) {}

    void setHandlers(ImageHandler onImage, FileHandler onFile)
    {
        m_on_image = std::move(onImage);
        m_on_file  = std::move(onFile);
    }

    // The image files in a mime payload (paste / drop).
    static QStringList imageFiles(const QMimeData *source)
    {
        QStringList files;
        if (!source || !source->hasUrls())
            return files;
        for (const QUrl &url : source->urls())
        {
            if (!url.isLocalFile())
                continue;
            if (QImageReader(url.toLocalFile()).canRead())
                files << url.toLocalFile();
        }
        return files;
    }

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override
    {
        return source->hasImage() || !imageFiles(source).isEmpty()
               || QTextEdit::canInsertFromMimeData(source);
    }

    void insertFromMimeData(const QMimeData *source) override
    {
        if (source->hasImage() && m_on_image)
        {
            const QImage image = qvariant_cast<QImage>(source->imageData());
            if (!image.isNull())
            {
                m_on_image(image);
                return;
            }
        }
        const QStringList files = imageFiles(source);
        if (!files.isEmpty() && m_on_file)
        {
            for (const QString &file : files)
                m_on_file(file);
            return;
        }
        // plain text only: no rich text pasted from web pages
        if (source->hasText())
            insertPlainText(source->text());
        else
            QTextEdit::insertFromMimeData(source);
    }

private:
    ImageHandler m_on_image;
    FileHandler m_on_file;
};
