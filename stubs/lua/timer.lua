---@meta

lektra = lektra or {}
lektra.timer = {}

---@class Timer
local Timer = {}

--- Starts (or restarts) the timer.
function Timer:start() end

--- Stops the timer.
function Timer:stop() end

--- Sets the timer's interval in milliseconds.
---@param ms integer
function Timer:set_interval(ms) end

--- Sets whether the timer fires once (true) or repeatedly (false).
---@param single_shot boolean
function Timer:set_single_shot(single_shot) end

--- Returns true if the timer is currently running.
---@return boolean is_active
function Timer:is_active() end

--- Returns true if the timer is set to fire only once.
---@return boolean is_single_shot
function Timer:is_single_shot() end

--- Returns the timer's interval in milliseconds.
---@return integer interval
function Timer:interval() end

--- Stops the timer and releases its callback. The timer must not be used after this.
function Timer:destroy() end

--- Creates a new timer that calls `callback` every `interval_ms` milliseconds.
---@param interval_ms integer Interval in milliseconds between callback invocations.
---@param callback function Function to call when the timer fires.
---@param single_shot? boolean If true, the timer fires once and stops; default is false (repeats).
---@return Timer timer
lektra.timer.new = function(interval_ms, callback, single_shot) end
