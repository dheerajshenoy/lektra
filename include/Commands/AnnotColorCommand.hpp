#pragma once

#include "Model.hpp"

#include <QUndoCommand>

extern "C"
{
#include <mupdf/pdf.h>
}

class AnnotColorCommand : public QUndoCommand
{
public:
    AnnotColorCommand(Model *model, int pageno, int objNum,
                      const QColor &oldColor, const QColor &newColor,
                      QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno),
          m_objNum(objNum), m_oldColor(oldColor), m_newColor(newColor)
    {
    }

    void undo() override
    {
        m_model->annotChangeColor(m_pageno, m_objNum, m_oldColor);
    }

    void redo() override
    {
        m_model->annotChangeColor(m_pageno, m_objNum, m_newColor);
    }

private:
    QColor m_oldColor;
    QColor m_newColor;
    Model *m_model{nullptr};
    int m_pageno{-1};
    int m_objNum{-1};
};
