#pragma once

#include <QHash>

enum class MouseAction
{
    Button = 0,
    WheelUp,
    WheelDown
};

struct MouseBindKey
{
    Qt::KeyboardModifiers modifiers{Qt::NoModifier};
    Qt::MouseButton button{Qt::NoButton};
    MouseAction action{MouseAction::Button};

    bool operator==(const MouseBindKey &o) const
    {
        return modifiers == o.modifiers && action == o.action
               && (action == MouseAction::Button || button == o.button);
    }
};

inline size_t
qHash(const MouseBindKey &k, size_t seed = 0)
{
    return qHashMulti(seed, k.modifiers, k.button, k.action);
}
