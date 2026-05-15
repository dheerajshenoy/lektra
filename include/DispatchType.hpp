#pragma once

#include <QHash>
#include <QString>
#include <stdexcept>

enum class DispatchType
{
    OnAppReady                            = 0,
    OnReady                               = 1,
    OnFileOpen                            = 2,
    OnFileClose                           = 3,
    OnPageChanged                         = 4,
    OnZoomChanged                         = 5,
    OnLinkClicked                         = 6,
    OnTextSelected                        = 7,
    OnTabChanged                          = 8,
    OnSearchStarted                       = 9,
    OnSearchFinished                      = 10,
    OnSearchCancelled                     = 11,
    OnAnnotationAdded                     = 12,
    OnAnnotationRemoved                   = 13,
    OnRegionSelectionContextMenuRequested = 14,
    OnTextSelectionContextMenuRequested   = 15,
    OnTabAdded                            = 16,
    OnTabRemoved                          = 17,
    OnViewChanged                         = 18,
    OnScreenChanged                       = 19,
    OnAppShutdown                         = 20,
    COUNT
};

static const QHash<QString, DispatchType> s_dispatchEventMap = {
    {"OnAppReady", DispatchType::OnAppReady},
    {"OnReady", DispatchType::OnReady},
    {"OnFileOpen", DispatchType::OnFileOpen},
    {"OnFileClose", DispatchType::OnFileClose},
    {"OnPageChanged", DispatchType::OnPageChanged},
    {"OnZoomChanged", DispatchType::OnZoomChanged},
    {"OnLinkClicked", DispatchType::OnLinkClicked},
    {"OnTextSelected", DispatchType::OnTextSelected},
    {"OnTabChanged", DispatchType::OnTabChanged},
    {"OnSearchStarted", DispatchType::OnSearchStarted},
    {"OnSearchFinished", DispatchType::OnSearchFinished},
    {"OnSearchCancelled", DispatchType::OnSearchCancelled},
    {"OnAnnotationAdded", DispatchType::OnAnnotationAdded},
    {"OnAnnotationRemoved", DispatchType::OnAnnotationRemoved},
    {"OnRegionSelectionContextMenuRequested",
     DispatchType::OnRegionSelectionContextMenuRequested},
    {"OnTextSelectionContextMenuRequested",
     DispatchType::OnTextSelectionContextMenuRequested},
    {"OnTabAdded", DispatchType::OnTabAdded},
    {"OnTabRemoved", DispatchType::OnTabRemoved},
    {"OnViewChanged", DispatchType::OnViewChanged},
    {"OnScreenChanged", DispatchType::OnScreenChanged},
    {"OnAppShutdown", DispatchType::OnAppShutdown},
};

inline static DispatchType
stringToDispatchType(const QString &name)
{
    if (!s_dispatchEventMap.contains(name))
        throw std::invalid_argument(
            QString("Unknown event name: %1").arg(name).toStdString());
    return s_dispatchEventMap.value(name);
}

inline static QString
dispatchTypeToString(DispatchType type)
{
    for (auto it = s_dispatchEventMap.constBegin();
         it != s_dispatchEventMap.constEnd(); ++it)
    {
        if (it.value() == type)
            return it.key();
    }
    throw std::invalid_argument(QString("Unknown event type: %1")
                                    .arg(static_cast<int>(type))
                                    .toStdString());
}
