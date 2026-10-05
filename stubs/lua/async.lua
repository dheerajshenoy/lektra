---@meta
lektra = lektra or {}

--- Runs a function as a coroutine, so that it can wait for slow things (`lektra.job.await`, `lektra.sleep`, `lektra.await`) and still read from top to bottom. It starts at once and goes on until its first wait; the rest runs later, from the event loop, while the window stays responsive. An error inside is printed and ends it.
--- Returns the coroutine. Extra arguments are passed to the function.
---@param fn function
---@param ... any
---@return thread
lektra.async = function(fn, ...) end

--- Pauses the running `lektra.async` function until the `resume` function given to `starter` is called, and returns the values it was called with. Use it to wait for any callback-based function. Only the first call of `resume` counts. Raises an error outside of `lektra.async`.
---@param starter fun(resume: fun(...))
---@return any ...
lektra.await = function(starter) end

--- Waits for some seconds (inside `lektra.async`); the window stays responsive.
---@param seconds number
lektra.sleep = function(seconds) end

--- `lektra.job.async`, waited for (inside `lektra.async`): returns the result table.
---@param command string|string[]
---@param opts? JobOptions
---@return JobResult
lektra.job.await = function(command, opts) end
