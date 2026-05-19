---@meta
lektra = lektra or {}
lektra.event = {}

---@enum EventType
EventType = {
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
}

lektra.event.EventType = EventType

--- Registers a callback to be called when the specified event is triggered.
---@overload fun(event: EventType.OnScreenChanged, callback: fun(s: lektra.ScreenInfo)): integer
---@overload fun(event: EventType.OnTabChanged, callback: fun(tab: integer)): integer
---@overload fun(event: EventType.OnTabRemoved, callback: fun(tab: integer)): integer
---@param event EventType
---@param callback fun(arg: any)
---@return integer handle
lektra.event.register = function (event, callback) end

--- Unregisters a callback from the specified event.
---@overload fun(arg: {event: EventType, callback: function})
---@param event EventType The name of the event to stop listening for.
---@param handle integer The unique identifier of the registered callback to unregister, as returned by `lektra.event.register`.
lektra.event.unregister = function (event, handle) end

--- Registers a callback to be called when the specified event is triggered.
--- The callback is called once and then automatically unregistered.
---@overload fun(event: EventType.OnScreenChanged, callback: fun(s: lektra.ScreenInfo)): integer
---@overload fun(event: EventType.OnTabChanged, callback: fun(tab: integer)): integer
---@overload fun(event: EventType.OnTabRemoved, callback: fun(tab: integer)): integer
---@param event EventType
---@param callback fun(arg: any)
---@return integer handle
lektra.event.once = function (event, callback) end

--- Clears all registered callbacks for the specified event.
---@param event EventType The name of the event to clear callbacks for.
lektra.event.clear = function (event) end

--- Returns the number of registered callbacks for the specified event.
---@param event EventType The name of the event to count callbacks for.
---@return integer count The number of registered callbacks for the specified event.
lektra.event.count = function (event) end
