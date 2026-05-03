#pragma once

#include <QString>

enum class DispatchType
{
    OnAppReady = 0,
    OnReady,
    OnFileOpen,
    OnFileClose,
    OnPageChanged,
    OnZoomChanged,
    OnLinkClicked,
    OnTextSelected,
    OnTabChanged,
};
