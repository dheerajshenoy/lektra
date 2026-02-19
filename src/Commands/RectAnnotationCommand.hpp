#pragma once

#include "../Model.hpp"

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
                          QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno), m_rect(rect)
    {
    }

    void undo() override
    {
        m_model->removeAnnotations(m_pageno, {m_objNum});
        emit m_model->reloadRequested(m_pageno);
    }

    void redo() override
    {
        m_objNum = m_model->addRectAnnotation(m_pageno, m_rect);
        emit m_model->reloadRequested(m_pageno);
    }

private:
    Model *m_model;
    int m_pageno;
    fz_rect m_rect;
    int m_objNum{-1};
};
