---@meta
lektra = lektra or {}
lektra.event = {}

---@enum EventType
lektra.event.EventType = {
    OnAppReady = 0,
    OnReady = 1,
    OnFileOpen = 2,
    OnFileClose = 3,
    OnPageChanged = 4,
    OnZoomChanged = 5,
    OnLinkClicked = 6,
    OnTextSelected = 7,
    OnTabChanged = 8,
    OnSearchStarted = 9,
    OnSearchFinished = 10,
    OnSearchCancelled = 11,
    OnAnnotationAdded = 12,
    OnAnnotationRemoved = 13,
    OnRegionSelectionContextMenuRequested = 14,
    OnTextSelectionContextMenuRequested = 15,
    OnTabAdded = 16,
    OnTabRemoved = 17,
    OnViewChanged = 18,
    OnScreenChanged = 19,
    OnAppShutdown = 20,
    OnModeChanged = 21,
    OnSessionLoaded = 22,
    OnBookmarkAdded = 23,
    OnFileSaved = 24,
    OnSynctexJumpRequested = 25,
    OnPresentationStarted = 26,
    OnPresentationEnded = 27,
}

-- Event payloads ----------------------------------------------------

--- The table every event callback receives.
---@class Event
---@field type string The event name, e.g. "OnLinkClicked".

--- An event that came from a view.
---@class ViewEvent : Event
---@field view View The view the event came from.

---@class LinkClickedEvent : ViewEvent
---@field link PageLink The link that was followed (without `index` and `rect`).

---@class SynctexJumpEvent : ViewEvent
---@field source_file string The source file, as SyncTeX recorded it.
---@field line integer
---@field col integer -1 if SyncTeX does not know the column.

---@class TabEvent : Event
---@field index integer

---@class SessionLoadedEvent : Event
---@field name string

---@class BookmarkAddedEvent : Event
---@field bookmark Bookmark

---@class ScreenChangedEvent : Event
---@field screen lektra.ScreenInfo

--- Event name -> the table its callbacks receive.
---@class EventMap
---@field OnAppReady Event
---@field OnAppShutdown Event
---@field OnPresentationStarted Event
---@field OnPresentationEnded Event
---@field OnReady ViewEvent
---@field OnFileOpen ViewEvent
---@field OnFileClose ViewEvent
---@field OnFileSaved ViewEvent
---@field OnPageChanged ViewEvent
---@field OnZoomChanged ViewEvent
---@field OnModeChanged ViewEvent
---@field OnViewChanged ViewEvent
---@field OnTextSelected ViewEvent
---@field OnSearchStarted ViewEvent
---@field OnSearchFinished ViewEvent
---@field OnSearchCancelled ViewEvent
---@field OnRegionSelectionContextMenuRequested ViewEvent
---@field OnTextSelectionContextMenuRequested ViewEvent
---@field OnLinkClicked LinkClickedEvent
---@field OnSynctexJumpRequested SynctexJumpEvent
---@field OnTabChanged TabEvent
---@field OnTabAdded TabEvent
---@field OnTabRemoved TabEvent
---@field OnSessionLoaded SessionLoadedEvent
---@field OnBookmarkAdded BookmarkAddedEvent
---@field OnScreenChanged ScreenChangedEvent

--- Registers a callback to be called when the specified event is triggered.
--- `event` accepts either the string name ("OnPageChanged") or the
--- `lektra.event.EventType` enum value — the string form is recommended:
--- it's shorter and (unlike the enum form) editors reliably infer the
--- callback argument's type from it.
---@generic E : keyof EventMap
---@param event E|EventType
---@param callback fun(event: EventMap[E])
---@return integer handle
lektra.event.register = function(event, callback) end

--- Unregisters a callback from the specified event.
---@param event EventType|string The name of the event to stop listening for.
---@param handle integer The unique identifier of the registered callback to unregister, as returned by `lektra.event.register`.
lektra.event.unregister = function(event, handle) end

--- Registers a callback to be called when the specified event is triggered.
--- The callback is called once and then automatically unregistered.
--- See `lektra.event.register` for the `event` argument's accepted forms.
---@generic E : keyof EventMap
---@param event E|EventType
---@param callback fun(event: EventMap[E])
---@return integer handle
lektra.event.once = function(event, callback) end

--- Clears all registered callbacks for the specified event.
---@param event EventType|string The name of the event to clear callbacks for.
lektra.event.clear = function(event) end

--- Returns the number of registered callbacks for the specified event.
---@param event EventType|string The name of the event to count callbacks for.
---@return integer count The number of registered callbacks for the specified event.
lektra.event.count = function(event) end
