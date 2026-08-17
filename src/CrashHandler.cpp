#include "CrashHandler.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

// ─────────────────────────────────────────────────────────────────────────────
// POSIX  (Linux + macOS)
// ─────────────────────────────────────────────────────────────────────────────
#if !defined(_WIN32)

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static char s_exePath[4096] = {};
static char s_logPath[4096] = {};
static char s_header[1024]  = {};

// Async-signal-safe write — no malloc, no printf.
static void sig_write(int fd, const char *s)
{
    if (!s || fd < 0) return;
    size_t len = 0;
    while (s[len]) ++len;
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0) break;
        s   += n;
        len -= (size_t)n;
    }
}

static const char *sig_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (Segmentation fault)";
    case SIGABRT: return "SIGABRT (Abort)";
    case SIGFPE:  return "SIGFPE  (Floating-point exception)";
    case SIGILL:  return "SIGILL  (Illegal instruction)";
    case SIGBUS:  return "SIGBUS  (Bus error)";
    default:      return "Unknown signal";
    }
}

static void posixCrashHandler(int sig, siginfo_t *, void *) noexcept
{
    // Restore default so a double-fault doesn't re-enter us.
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, nullptr);

    int fd = -1;
    if (s_logPath[0])
        fd = open(s_logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fd = STDERR_FILENO;

    sig_write(fd, s_header);
    sig_write(fd, "Signal: ");
    sig_write(fd, sig_name(sig));
    sig_write(fd, "\n\n=== Stack Trace ===\n");

    void *frames[128];
    int   n = backtrace(frames, 128);
    backtrace_symbols_fd(frames, n, fd);
    sig_write(fd, "\n");

    if (fd != STDERR_FILENO) close(fd);

    // Fork a clean child to show the crash reporter dialog.
    if (s_exePath[0] && s_logPath[0]) {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGSEGV, SIG_DFL);
            signal(SIGABRT, SIG_DFL);
            signal(SIGFPE,  SIG_DFL);
            signal(SIGILL,  SIG_DFL);
            signal(SIGBUS,  SIG_DFL);
            execl(s_exePath, s_exePath, "--crash-reporter", s_logPath, (char *)nullptr);
            _exit(1);
        }
    }

    _exit(1);
}

void CrashHandler::install() noexcept
{
    const std::string exe = QCoreApplication::applicationFilePath().toStdString();
    snprintf(s_exePath, sizeof(s_exePath), "%s", exe.c_str());

    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppLocalDataLocation) + "/crashes";
    QDir().mkpath(dir);
    const std::string lp = (dir + "/crash_latest.log").toStdString();
    snprintf(s_logPath, sizeof(s_logPath), "%s", lp.c_str());

    snprintf(s_header, sizeof(s_header),
             "=== Lektra Crash Report ===\n"
             "Version: %s\n"
             "Build:   %s\n"
             "Qt:      %s\n\n",
             APP_VERSION, APP_BUILD_TYPE, qVersion());

    struct sigaction sa{};
    sa.sa_sigaction = posixCrashHandler;
    sa.sa_flags     = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows
// ─────────────────────────────────────────────────────────────────────────────
#else // _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>

static void win_write(HANDLE h, const char *s)
{
    DWORD w;
    WriteFile(h, s, (DWORD)strlen(s), &w, nullptr);
}

static void win_writeln(HANDLE h, const char *s)
{
    win_write(h, s);
    win_write(h, "\r\n");
}

static const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "Access violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array bounds exceeded";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "Datatype misalignment";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:   return "Float divide-by-zero";
    case EXCEPTION_FLT_OVERFLOW:          return "Float overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "Illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "Integer divide-by-zero";
    case EXCEPTION_INT_OVERFLOW:          return "Integer overflow";
    case EXCEPTION_STACK_OVERFLOW:        return "Stack overflow";
    default:                              return "Unknown exception";
    }
}

static LONG WINAPI windowsCrashHandler(EXCEPTION_POINTERS *ep) noexcept
{
    char tmpDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmpDir);
    char logPath[MAX_PATH] = {};
    snprintf(logPath, MAX_PATH, "%slektra_crash.log", tmpDir);

    HANDLE hFile = CreateFileA(logPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buf[512];

        win_writeln(hFile, "=== Lektra Crash Report ===");
        snprintf(buf, sizeof(buf), "Version: %s", APP_VERSION);  win_writeln(hFile, buf);
        snprintf(buf, sizeof(buf), "Build:   %s", APP_BUILD_TYPE); win_writeln(hFile, buf);
        snprintf(buf, sizeof(buf), "Qt:      %s", qVersion());   win_writeln(hFile, buf);
        win_writeln(hFile, "");

        if (ep) {
            DWORD code = ep->ExceptionRecord->ExceptionCode;
            snprintf(buf, sizeof(buf), "Exception: 0x%08lX (%s)", code, exception_name(code));
            win_writeln(hFile, buf);
        }
        win_writeln(hFile, "");
        win_writeln(hFile, "=== Stack Trace ===");

        HANDLE proc = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(proc, nullptr, TRUE);

        CONTEXT ctx = {};
        if (ep)
            ctx = *ep->ContextRecord;
        else
            RtlCaptureContext(&ctx);

        STACKFRAME64 sf = {};
        sf.AddrPC.Mode    = AddrModeFlat;
        sf.AddrStack.Mode = AddrModeFlat;
        sf.AddrFrame.Mode = AddrModeFlat;

#if defined(_M_X64)
        sf.AddrPC.Offset    = ctx.Rip;
        sf.AddrStack.Offset = ctx.Rsp;
        sf.AddrFrame.Offset = ctx.Rbp;
        const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
        sf.AddrPC.Offset    = ctx.Pc;
        sf.AddrStack.Offset = ctx.Sp;
        sf.AddrFrame.Offset = ctx.Fp;
        const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
        sf.AddrPC.Offset    = ctx.Eip;
        sf.AddrStack.Offset = ctx.Esp;
        sf.AddrFrame.Offset = ctx.Ebp;
        const DWORD machine = IMAGE_FILE_MACHINE_I386;
#endif

        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME + 1] = {};
        SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = MAX_SYM_NAME;

        HANDLE thread = GetCurrentThread();
        for (int frame = 0; frame < 64; ++frame) {
            if (!StackWalk64(machine, proc, thread, &sf, &ctx,
                             nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            if (sf.AddrPC.Offset == 0) break;

            DWORD64 disp = 0;
            if (SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym))
                snprintf(buf, sizeof(buf), "#%-2d %s + 0x%llx", frame, sym->Name,
                         (unsigned long long)disp);
            else
                snprintf(buf, sizeof(buf), "#%-2d 0x%016llx", frame,
                         (unsigned long long)sf.AddrPC.Offset);
            win_write(hFile, buf);

            IMAGEHLP_LINE64 lineInfo = {sizeof(IMAGEHLP_LINE64)};
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &lineDisp, &lineInfo)) {
                snprintf(buf, sizeof(buf), "  (%s:%lu)", lineInfo.FileName, lineInfo.LineNumber);
                win_write(hFile, buf);
            }
            win_write(hFile, "\r\n");
        }

        SymCleanup(proc);
        CloseHandle(hFile);
    }

    // Launch the crash reporter in a new process.
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    char cmdLine[MAX_PATH * 2 + 64] = {};
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" --crash-reporter \"%s\"", exePath, logPath);

    STARTUPINFOA si        = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE, 0,
                   nullptr, nullptr, &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread)  CloseHandle(pi.hThread);

    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::install() noexcept
{
    SetUnhandledExceptionFilter(windowsCrashHandler);
}

#endif // _WIN32
