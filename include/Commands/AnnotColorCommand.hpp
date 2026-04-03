#pragma once

#include "Model.hpp"

#include <QUndoCommand>

class AnnotColorCommand : public QUndoCommand
{
public:
    AnnotColorCommand(Model *model, int pageno, int objNum,
                      const QColor &oldColor, const QColor &newColor,
                      QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_oldColor(oldColor),
          m_newColor(newColor), m_pageno(pageno), m_objNum(objNum)
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
    Model *m_model = nullptr;
    QColor m_oldColor;
    QColor m_newColor;
    int m_pageno = -1;
    int m_objNum = -1;
};
