#include "Lektra.hpp"
#include "lua/MainState.hpp"

#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <memory>
#include <new>

#ifdef Q_OS_UNIX
    #include <csignal>
    #include <unistd.h>
#endif

// lektra.job.async(command, [options], [callback]) -> Job
//
// Runs a command without waiting for it: the script goes on at once and the
// callback is called, from the event loop, when the command has finished.
//
//   command   a string (run by the shell) or a list: program and arguments
//             (run directly, no shell, so nothing needs quoting)
//   options   { cwd=, env={NAME="value"}, stdin="text", timeout=seconds,
//               on_stdout=function(chunk), on_stderr=function(chunk),
//               on_done=function(result) }
//   callback  function(result), the same as options.on_done
//   result    { ok=, code=, stdout=, stderr=, error=, timed_out=, cancelled= }
//   Job       :cancel(), :running(), :pid()

namespace
{
constexpr const char *kJobMeta = "LektraJobMetaTable";

// Everything about one running job. Held by the process's signal handlers, so
// it lives as long as the process runs.
struct JobData
{
    lua_State *L      = nullptr;
    QProcess *process = nullptr;
    QTimer *timeout   = nullptr;
    int onDone        = LUA_NOREF;
    int onStdout      = LUA_NOREF;
    int onStderr      = LUA_NOREF;
    QByteArray out, err;
    QString error;
    bool done      = false;
    bool cancelled = false;
    bool timedOut  = false;
};

// What a script holds: it does not keep the job alive.
struct JobHandle
{
    std::weak_ptr<JobData> data;
};

JobHandle *
checkJob(lua_State *L)
{
    return static_cast<JobHandle *>(luaL_checkudata(L, 1, kJobMeta));
}

void
unref(lua_State *L, int &ref)
{
    if (ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    ref = LUA_NOREF;
}

// Calls the function in `ref` with one string (or the result table that is
// already on top of the stack when `args` is 0).
void
callCallback(lua_State *L, int ref, int args)
{
    // stack: ..., arg(s)
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_insert(L, -(args + 1));
    if (lua_pcall(L, args, 0, 0) != LUA_OK)
    {
        fprintf(stderr, "Lua error in job callback: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// Takes what the process has written so far and gives it to the callbacks.
void
drain(const std::shared_ptr<JobData> &job)
{
    if (!job->process)
        return;
    const QByteArray out = job->process->readAllStandardOutput();
    const QByteArray err = job->process->readAllStandardError();
    job->out += out;
    job->err += err;
    if (job->onStdout != LUA_NOREF && !out.isEmpty())
    {
        lua_pushlstring(job->L, out.constData(),
                        static_cast<size_t>(out.size()));
        callCallback(job->L, job->onStdout, 1);
    }
    if (job->onStderr != LUA_NOREF && !err.isEmpty())
    {
        lua_pushlstring(job->L, err.constData(),
                        static_cast<size_t>(err.size()));
        callCallback(job->L, job->onStderr, 1);
    }
}

void
stopProcess(const std::shared_ptr<JobData> &job, bool hard)
{
    if (!job->process || job->process->state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_UNIX
    // The process leads a group of its own: signal the commands it started too.
    const pid_t pid = static_cast<pid_t>(job->process->processId());
    if (pid > 0)
    {
        ::kill(-pid, hard ? SIGKILL : SIGTERM);
        return;
    }
#endif
    if (hard)
        job->process->kill();
    else
        job->process->terminate();
}

// The job is over: call the callback with what happened, then clean up.
void
finish(const std::shared_ptr<JobData> &job, int code,
       QProcess::ExitStatus status)
{
    if (job->done)
        return;
    job->done          = true;
    lua_State *L       = job->L;
    const bool crashed = status == QProcess::CrashExit;

    if (job->timeout)
    {
        job->timeout->stop();
        job->timeout->deleteLater();
        job->timeout = nullptr;
    }

    if (job->onDone != LUA_NOREF)
    {
        lua_newtable(L);
        lua_pushboolean(L, !crashed && code == 0 && job->error.isEmpty()
                               && !job->timedOut && !job->cancelled);
        lua_setfield(L, -2, "ok");
        lua_pushinteger(L, crashed || !job->error.isEmpty() ? -1 : code);
        lua_setfield(L, -2, "code");
        lua_pushlstring(L, job->out.constData(),
                        static_cast<size_t>(job->out.size()));
        lua_setfield(L, -2, "stdout");
        lua_pushlstring(L, job->err.constData(),
                        static_cast<size_t>(job->err.size()));
        lua_setfield(L, -2, "stderr");
        lua_pushboolean(L, job->timedOut);
        lua_setfield(L, -2, "timed_out");
        lua_pushboolean(L, job->cancelled);
        lua_setfield(L, -2, "cancelled");
        QString why = job->error;
        if (why.isEmpty() && job->timedOut)
            why = QStringLiteral("timed out");
        else if (why.isEmpty() && job->cancelled)
            why = QStringLiteral("cancelled");
        else if (why.isEmpty() && crashed)
            why = QStringLiteral("the command was killed or crashed");
        if (!why.isEmpty())
        {
            lua_pushstring(L, why.toUtf8().constData());
            lua_setfield(L, -2, "error");
        }
        callCallback(L, job->onDone, 1);
    }

    unref(L, job->onDone);
    unref(L, job->onStdout);
    unref(L, job->onStderr);
    if (job->process)
    {
        job->process->deleteLater();
        job->process = nullptr;
    }
}

bool
isFunction(lua_State *L, int index)
{
    return lua_type(L, index) == LUA_TFUNCTION;
}

// Reference to the function in field `key` of the table at `table`, or NOREF.
int
functionField(lua_State *L, int table, const char *key)
{
    lua_getfield(L, table, key);
    if (isFunction(L, -1))
        return luaL_ref(L, LUA_REGISTRYINDEX); // pops it
    lua_pop(L, 1);
    return LUA_NOREF;
}
} // namespace

void
Lektra::initLuaJob() noexcept
{
    // the Job object
    luaL_newmetatable(m_L, kJobMeta);
    lua_pushvalue(m_L, -1);
    lua_setfield(m_L, -2, "__index");
    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        static_cast<JobHandle *>(lua_touserdata(L, 1))->~JobHandle();
        return 0;
    });
    lua_setfield(m_L, -2, "__gc");

    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        if (auto job = checkJob(L)->data.lock(); job && !job->done)
        {
            job->cancelled = true;
            stopProcess(job, false);
            // a command that ignores the polite request is stopped after a
            // while
            std::weak_ptr<JobData> weak = job;
            QTimer::singleShot(2000, [weak]
            {
                if (auto j = weak.lock(); j && !j->done)
                    stopProcess(j, true);
            });
        }
        return 0;
    });
    lua_setfield(m_L, -2, "cancel");

    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        const auto job = checkJob(L)->data.lock();
        lua_pushboolean(L,
                        job && !job->done && job->process
                            && job->process->state() != QProcess::NotRunning);
        return 1;
    });
    lua_setfield(m_L, -2, "running");

    lua_pushcfunction(m_L, [](lua_State *L) -> int
    {
        const auto job = checkJob(L)->data.lock();
        if (job && !job->done && job->process && job->process->processId() > 0)
            lua_pushinteger(
                L, static_cast<lua_Integer>(job->process->processId()));
        else
            lua_pushnil(L);
        return 1;
    });
    lua_setfield(m_L, -2, "pid");
    lua_pop(m_L, 1);

    // lektra.job
    lua_newtable(m_L);

    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, [](lua_State *L) -> int
    {
        auto *lektra
            = static_cast<Lektra *>(lua_touserdata(L, lua_upvalueindex(1)));

        // the command: a string (for the shell) or a list (program, arguments)
        QString program;
        QStringList arguments;
        if (lua_type(L, 1) == LUA_TSTRING)
        {
            const QString command = QString::fromUtf8(lua_tostring(L, 1));
            if (command.trimmed().isEmpty())
                return luaL_error(L, "job.async: the command is empty");
#ifdef Q_OS_WIN
            program   = QStringLiteral("cmd.exe");
            arguments = {QStringLiteral("/c"), command};
#else
            program   = QStringLiteral("/bin/sh");
            arguments = {QStringLiteral("-c"), command};
#endif
        }
        else if (lua_istable(L, 1))
        {
            const int n = static_cast<int>(lua_rawlen(L, 1));
            for (int i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, 1, i);
                if (!lua_isstring(L, -1))
                    return luaL_error(
                        L, "job.async: the command list must hold strings");
                const QString item = QString::fromUtf8(lua_tostring(L, -1));
                lua_pop(L, 1);
                if (i == 1)
                    program = item;
                else
                    arguments << item;
            }
            if (program.isEmpty())
                return luaL_error(L, "job.async: the command is empty");
        }
        else
            return luaL_error(
                L, "job.async: the command is a string or a list of strings");

        // (command, callback) or (command, options, callback)
        int options = 0, callback = 0;
        if (isFunction(L, 2))
            callback = 2;
        else if (lua_istable(L, 2))
        {
            options = 2;
            if (isFunction(L, 3))
                callback = 3;
        }
        else if (!lua_isnoneornil(L, 2))
            return luaL_error(L, "job.async: the second argument is the "
                                 "options or the callback");

        auto job = std::make_shared<JobData>();
        job->L   = luaMainState();

        QString workDir, stdinText;
        bool hasStdin   = false;
        double timeoutS = 0;
        QProcessEnvironment environment;
        bool hasEnv = false;
        if (options)
        {
            lua_getfield(L, options, "cwd");
            if (lua_isstring(L, -1))
                workDir = QString::fromUtf8(lua_tostring(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, options, "stdin");
            if (lua_isstring(L, -1))
            {
                hasStdin  = true;
                stdinText = QString::fromUtf8(lua_tostring(L, -1));
            }
            lua_pop(L, 1);

            lua_getfield(L, options, "timeout");
            if (lua_isnumber(L, -1))
                timeoutS = lua_tonumber(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, options, "env");
            if (lua_istable(L, -1))
            {
                hasEnv      = true;
                environment = QProcessEnvironment::systemEnvironment();
                for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1))
                    if (lua_type(L, -2) == LUA_TSTRING && lua_isstring(L, -1))
                        environment.insert(
                            QString::fromUtf8(lua_tostring(L, -2)),
                            QString::fromUtf8(lua_tostring(L, -1)));
            }
            lua_pop(L, 1);

            job->onStdout = functionField(L, options, "on_stdout");
            job->onStderr = functionField(L, options, "on_stderr");
            job->onDone   = functionField(L, options, "on_done");
        }
        if (callback)
        {
            unref(L, job->onDone);
            lua_pushvalue(L, callback);
            job->onDone = luaL_ref(L, LUA_REGISTRYINDEX);
        }

        auto *process = new QProcess(lektra);
        process->setObjectName(QStringLiteral("lektraJob"));
        job->process = process;
        process->setProgram(program);
        process->setArguments(arguments);
        if (!workDir.isEmpty())
            process->setWorkingDirectory(workDir);
        if (hasEnv)
            process->setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
        // its own process group, so that cancelling stops what it started too
        process->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif

        QObject::connect(process, &QProcess::readyReadStandardOutput, process,
                         [job] { drain(job); });
        QObject::connect(process, &QProcess::readyReadStandardError, process,
                         [job] { drain(job); });
        QObject::connect(process, &QProcess::started, process,
                         [job, hasStdin, stdinText]
        {
            if (!job->process)
                return;
            if (hasStdin)
                job->process->write(stdinText.toUtf8());
            // a command that reads its input must not wait for ever for it
            job->process->closeWriteChannel();
        });
        QObject::connect(process, &QProcess::errorOccurred, process,
                         [job](QProcess::ProcessError error)
        {
            if (error == QProcess::FailedToStart && job->process)
            {
                job->error = QStringLiteral("could not start: ")
                             + job->process->errorString();
                finish(job, -1, QProcess::NormalExit);
            }
        });
        QObject::connect(process, &QProcess::finished, process,
                         [job](int code, QProcess::ExitStatus status)
        {
            drain(job); // what is left in the pipes
            finish(job, code, status);
        });

        if (timeoutS > 0)
        {
            job->timeout = new QTimer(process);
            job->timeout->setSingleShot(true);
            QObject::connect(job->timeout, &QTimer::timeout, process, [job]
            {
                if (job->done)
                    return;
                job->timedOut = true;
                stopProcess(job, false);
                std::weak_ptr<JobData> weak = job;
                QTimer::singleShot(2000, [weak]
                {
                    if (auto j = weak.lock(); j && !j->done)
                        stopProcess(j, true);
                });
            });
            job->timeout->start(static_cast<int>(timeoutS * 1000));
        }

        process->start();

        // the Job the script gets
        auto *handle = new (lua_newuserdata(L, sizeof(JobHandle))) JobHandle;
        handle->data = job;
        luaL_getmetatable(L, kJobMeta);
        lua_setmetatable(L, -2);
        return 1;
    }, 1);
    lua_setfield(m_L, -2, "async");

    lua_setfield(m_L, -2, "job");
}

// Stops the jobs that are still running (the application is closing), so that
// none of them calls into Lua once it is gone.
void
Lektra::killLuaJobs() noexcept
{
    const auto processes
        = findChildren<QProcess *>(QStringLiteral("lektraJob"));
    for (QProcess *process : processes)
    {
        process->disconnect();
        if (process->state() != QProcess::NotRunning)
        {
#ifdef Q_OS_UNIX
            if (process->processId() > 0)
                ::kill(-static_cast<pid_t>(process->processId()), SIGKILL);
#endif
            process->kill();
            process->waitForFinished(500);
        }
        delete process;
    }
}
