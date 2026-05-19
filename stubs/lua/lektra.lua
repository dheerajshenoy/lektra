---@meta
lektra = lektra or {}

---@class lektra.ScreenInfo
---@field name string Platform-specific screen name (e.g. "HDMI-1")
---@field dpr number Device pixel ratio (e.g. 2.0 on HiDPI screens)
---@field logical_dpi number Logical dots per inch
---@field physical_dpi number Physical dots per inch
---@field refresh_rate number Refresh rate in Hz
---@field geometry {x: integer, y: integer, w: integer, h: integer} Screen geometry in pixels

