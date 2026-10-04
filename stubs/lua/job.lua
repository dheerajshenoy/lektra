---@meta
lektra = lektra or {}
lektra.job = {}

---@class JobResult
---@field ok boolean True if the command ran and exited with code 0 (not cancelled, not timed out).
---@field code integer The exit code, or -1 if it could not start or was killed.
---@field stdout string Everything it wrote to standard output.
---@field stderr string Everything it wrote to standard error.
---@field timed_out boolean True if it was stopped because of `timeout`.
---@field cancelled boolean True if it was stopped with `Job:cancel()`.
---@field error? string Why it failed, when it did not simply exit with a code ("could not start: ...", "timed out", "cancelled", ...).

---@class JobOptions
---@field cwd? string Folder to run the command in.
---@field env? table<string,string> Environment variables to add or change.
---@field stdin? string Text to give to the command on its standard input (it is closed afterwards; without this it is closed at once).
---@field timeout? number Seconds after which the command is stopped.
---@field on_stdout? fun(chunk: string) Called with the output as it arrives.
---@field on_stderr? fun(chunk: string) Called with the error output as it arrives.
---@field on_done? fun(result: JobResult) The same as the `callback` argument.

---@class Job
local Job = {}
--- Stops the command (and the commands it started). Its callback is still called, with `cancelled = true`.
function Job:cancel() end
--- Whether the command is still running.
---@return boolean
function Job:running() end
--- The process id while it runs, otherwise nil.
---@return integer?
function Job:pid() end

--- Runs a command without waiting for it: the script goes on at once, and the callback is called when the command has finished (the window stays responsive meanwhile).
--- A string is run by the shell (`sh -c`, or `cmd /c` on Windows); a list is a program and its arguments, run directly, so nothing needs quoting.
---@overload fun(command: string|string[], callback: fun(result: JobResult)): Job
---@overload fun(command: string|string[], options: JobOptions, callback?: fun(result: JobResult)): Job
---@param command string|string[]
---@return Job
lektra.job.async = function(command, options, callback) end
