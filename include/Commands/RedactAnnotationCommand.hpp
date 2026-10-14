#pragma once

#include "Model.hpp"

#include <QUndoCommand>
extern "C"
{
#include <mupdf/pdf.h>
}

// Marks an area for redaction. Nothing is removed until the redactions are
// applied (Model::applyRedactions).
class RedactAnnotationCommand : public QUndoCommand
{
public:
    RedactAnnotationCommand(Model *model, int pageno, const fz_rect &rect,
                            QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno), m_rect(rect)
    {
        setText(QObject::tr("Mark for redaction"));
    }

    void undo() override
    {
        m_model->removeAnnotations(m_pageno, {m_objNum});
    }

    void redo() override
    {
        m_objNum = m_model->addRedactAnnotation(m_pageno, m_rect);
    }

private:
    Model *m_model;
    int m_pageno;
    fz_rect m_rect;
    int m_objNum{-1};
};
