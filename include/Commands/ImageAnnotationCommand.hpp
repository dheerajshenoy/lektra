#pragma once

#include "Model.hpp"

#include <QUndoCommand>

// Places an image on the page.
class ImageAnnotationCommand : public QUndoCommand
{
public:
    ImageAnnotationCommand(Model *model, int pageno, QByteArray data,
                           Model::AnnotGeometry frame,
                           QUndoCommand *parent = nullptr)
        : QUndoCommand(QObject::tr("Add Image"), parent), m_model(model),
          m_pageno(pageno), m_data(std::move(data)), m_frame(std::move(frame))
    {
    }

    // The annotation that was made (-1 if it could not be)
    int objNum() const noexcept
    {
        return m_objNum;
    }

    void undo() override
    {
        // The comment may have been edited since.
        m_comment = m_model->getAnnotComment(m_pageno, m_objNum);
        m_model->removeAnnotations(m_pageno, {m_objNum});
    }

    void redo() override
    {
        const int before = m_objNum;
        m_objNum
            = m_model->addImageAnnotation(m_pageno, m_data, m_frame, m_comment);
        if (before >= 0 && m_objNum >= 0)
            m_model->aliasAnnot(before, m_objNum);
    }

private:
    Model *m_model;
    int m_pageno;
    QByteArray m_data;
    Model::AnnotGeometry m_frame;
    QString m_comment;
    int m_objNum{-1};
};
