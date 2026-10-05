#pragma once

#include "Model.hpp"

#include <QRectF>
#include <QUndoCommand>
extern "C"
{
#include <mupdf/pdf.h>
}

class RectAnnotationCommand : public QUndoCommand
{
public:
    RectAnnotationCommand(Model *model, int pageno, const fz_rect &rect,
                          const QString &comment = {},
                          QUndoCommand *parent   = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno), m_rect(rect),
          m_comment(comment)
    {
    }

    // The annotation that was made (-1 if it could not be)
    int objNum() const noexcept
    {
        return m_objNum;
    }

    void undo() override
    {
        m_comment = m_model->getAnnotComment(m_pageno, m_objNum);
        m_model->removeAnnotations(m_pageno, {m_objNum});
    }

    void redo() override
    {
        m_objNum = m_model->addRectAnnotation(m_pageno, m_rect);
        if (!m_comment.isEmpty())
            m_model->addAnnotComment(m_pageno, m_objNum, m_comment);
    }

private:
    Model *m_model;
    int m_pageno;
    fz_rect m_rect;
    int m_objNum{-1};
    QString m_comment;
};
