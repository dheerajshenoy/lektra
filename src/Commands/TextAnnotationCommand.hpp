#pragma once

#include "../Model.hpp"

#include <QRectF>
#include <QString>
#include <QUndoCommand>
extern "C"
{
#include <mupdf/pdf.h>
}

class TextAnnotationCommand : public QUndoCommand
{
public:
    TextAnnotationCommand(Model *model, int pageno, const fz_rect &rect,
                          const QString &text, QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno), m_rect(rect),
          m_text(text)
    {
    }

    void undo() override
    {
        m_model->removeAnnotations(m_pageno, {m_objNum});
    }

    void redo() override
    {
        m_objNum = m_model->addTextAnnotation(m_pageno, m_rect, m_text);
    }

private:
    Model *m_model;
    int m_pageno;
    fz_rect m_rect;
    QString m_text;
    int m_objNum{-1};
};
