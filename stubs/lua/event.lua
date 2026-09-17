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
}

--- Registers a callback to be called when the specified event is triggered.
--- `event` accepts either the string name ("OnPageChanged") or the
--- `lektra.event.EventType` enum value — the string form is recommended:
--- it's shorter and (unlike the enum form) editors reliably infer the
--- callback argument's type from it.
---@overload fun(event: "OnFileOpen", callback: fun(view: View)): integer
---@overload fun(event: "OnReady", callback: fun(view: View)): integer
---@overload fun(event: "OnFileClose", callback: fun(view: View)): integer
---@overload fun(event: "OnPageChanged", callback: fun(view: View)): integer
---@overload fun(event: "OnZoomChanged", callback: fun(view: View)): integer
---@overload fun(event: "OnLinkClicked", callback: fun(view: View)): integer
---@overload fun(event: "OnTextSelected", callback: fun(view: View)): integer
---@overload fun(event: "OnSearchStarted", callback: fun(view: View)): integer
---@overload fun(event: "OnSearchFinished", callback: fun(view: View)): integer
---@overload fun(event: "OnSearchCancelled", callback: fun(view: View)): integer
---@overload fun(event: "OnRegionSelectionContextMenuRequested", callback: fun(view: View)): integer
---@overload fun(event: "OnTextSelectionContextMenuRequested", callback: fun(view: View)): integer
---@overload fun(event: "OnViewChanged", callback: fun(view: View)): integer
---@overload fun(event: "OnScreenChanged", callback: fun(s: lektra.ScreenInfo)): integer
---@overload fun(event: "OnTabChanged", callback: fun(tab: integer)): integer
---@overload fun(event: "OnTabAdded", callback: fun(tab: integer)): integer
---@overload fun(event: "OnTabRemoved", callback: fun(tab: integer)): integer
---@overload fun(event: "OnAppShutdown", callback: fun()): integer
---@param event EventType|string
---@param callback fun(arg: any)
---@return integer handle
lektra.event.register = function (event, callback) end

--- Unregisters a callback from the specified event.
---@overload fun(arg: {event: EventType|string, callback: function})
---@param event EventType|string The name of the event to stop listening for.
---@param handle integer The unique identifier of the registered callback to unregister, as returned by `lektra.event.register`.
lektra.event.unregister = function (event, handle) end

--- Registers a callback to be called when the specified event is triggered.
--- The callback is called once and then automatically unregistered.
--- See `lektra.event.register` for the `event` argument's accepted forms.
---@overload fun(event: "OnFileOpen", callback: fun(view: View)): integer
---@overload fun(event: "OnReady", callback: fun(view: View)): integer
---@overload fun(event: "OnFileClose", callback: fun(view: View)): integer
---@overload fun(event: "OnPageChanged", callback: fun(view: View)): integer
---@overload fun(event: "OnZoomChanged", callback: fun(view: View)): integer
---@overload fun(event: "OnLinkClicked", callback: fun(view: View)): integer
---@overload fun(event: "OnTextSelected", callback: fun(view: View)): integer
---@overload fun(event: "OnSearchStarted", callback: fun(view: View)): integer
---@overload fun(event: "OnSearchFinished", callback: fun(view: View)): integer
---@overload fun(event: "OnSearchCancelled", callback: fun(view: View)): integer
---@overload fun(event: "OnRegionSelectionContextMenuRequested", callback: fun(view: View)): integer
---@overload fun(event: "OnTextSelectionContextMenuRequested", callback: fun(view: View)): integer
---@overload fun(event: "OnViewChanged", callback: fun(view: View)): integer
---@overload fun(event: "OnScreenChanged", callback: fun(s: lektra.ScreenInfo)): integer
---@overload fun(event: "OnTabChanged", callback: fun(tab: integer)): integer
---@overload fun(event: "OnTabAdded", callback: fun(tab: integer)): integer
---@overload fun(event: "OnTabRemoved", callback: fun(tab: integer)): integer
---@overload fun(event: "OnAppShutdown", callback: fun()): integer
---@param event EventType|string
---@param callback fun(arg: any)
---@return integer handle
lektra.event.once = function (event, callback) end

--- Clears all registered callbacks for the specified event.
---@param event EventType|string The name of the event to clear callbacks for.
lektra.event.clear = function (event) end

--- Returns the number of registered callbacks for the specified event.
---@param event EventType|string The name of the event to count callbacks for.
---@return integer count The number of registered callbacks for the specified event.
lektra.event.count = function (event) end
