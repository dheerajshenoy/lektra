#include "AppPaths.hpp"
#include "Lektra.hpp"
#include "argparse.hpp"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <fcntl.h>
#include <qcoreapplication.h>
#include <signal.h>

#ifdef linux
    #include <sys/resource.h>
    #include <unistd.h>
#elif _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <string>
    #include <vector>
#endif

#ifdef linux
static std::string
get_self_executable_path()
{
    char buf[PATH_MAX + 1];
    const ssize_t n = ::readlink("/proc/self/exe", buf, PATH_MAX);
    if (n <= 0)
        return std::string{};
    buf[n] = '\0';
    return std::string(buf);
}

static void
detach_stdio_to_devnull()
{
    int fd = ::open("/dev/null", O_RDWR);
    if (fd < 0)
        return;
    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        ::close(fd);
}

static int
spawn_detached_child(int argc, char *argv[])
{
    // Double-fork so the child cannot accidentally regain a controlling TTY.
    pid_t pid = ::fork();
    if (pid < 0)
        return 1;
    if (pid > 0)
        return 0; // parent exits immediately

    if (::setsid() < 0)
        _exit(1);

    pid = ::fork();
    if (pid < 0)
        _exit(1);
    if (pid > 0)
        _exit(0); // first child exits

    ::signal(SIGHUP, SIG_IGN);
    detach_stdio_to_devnull();

    std::string exe = get_self_executable_path();
    if (exe.empty())
        exe = argv[0];

    std::vector<char *> new_argv;
    new_argv.reserve(static_cast<size_t>(argc + 2));

    new_argv.push_back(const_cast<char *>(exe.c_str()));
    new_argv.push_back(const_cast<char *>("--foreground"));

    for (int i = 1; i < argc; ++i)
    {
        // Avoid duplicate flag if user passed it manually.
        if (std::string_view(argv[i]) == "--foreground")
            continue;
        new_argv.push_back(argv[i]);
    }
    new_argv.push_back(nullptr);

    ::execv(new_argv[0], new_argv.data());
    _exit(1);
}

#elif _WIN32
static std::string
get_self_executable_path()
{
    char buffer[MAX_PATH];
    DWORD size = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (size == 0)
        return "";
    return std::string(buffer, size);
}

static int
spawn_detached_child(int argc, char *argv[])
{
    std::string exe = get_self_executable_path();

    // Construct the command line string
    // Windows requires a single string for arguments, not an array
    std::string commandLine = "\"" + exe + "\" --foreground";

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--foreground")
            continue;

        commandLine += " ";
        // Wrap arguments in quotes to handle spaces in paths
        commandLine += "\"" + arg + "\"";
    }

    STARTUPINFOA si        = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};

    // DETACHED_PROCESS: Disconnects from the parent console (Windows equivalent
    // of detaching stdio) CREATE_BREAKAWAY_FROM_JOB: Ensures the child isn't
    // killed if the parent is in a job
    DWORD flags
        = DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB | CREATE_NO_WINDOW;

    BOOL success = CreateProcessA(
        NULL,            // Application Name
        &commandLine[0], // Command Line (must be modifiable buffer)
        NULL,            // Process Attributes
        NULL,            // Thread Attributes
        FALSE,           // Inherit Handles
        flags,           // Creation Flags
        NULL,            // Environment
        NULL,            // Current Directory
        &si,             // Startup Info
        &pi              // Process Information
    );

    if (success)
    {
        // We don't need to communicate with the child, so close these handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0; // Parent exits/continues
    }

    return 1; // Failed to spawn
}
#endif

void
init_args(argparse::ArgumentParser &program)
{
    program.add_argument("-p", "--page")
        .help("Page number to go to")
        .scan<'i', int>()
        .default_value(-1)
        .metavar("PAGE_NUMBER");

    program.add_argument("--list-commands")
        .help("List available commands and exit")
        .flag();

    program.add_argument("-c", "--config")
        .help("Path to config.toml file")
        .nargs(1)
        .metavar("CONFIG_PATH");

    program.add_argument("--vsplit")
        .help("Open file(s) in vertical split")
        .flag();

    program.add_argument("--hsplit")
        .help("Open file(s) in horizontal split")
        .flag();

    program.add_argument("--about")
        .help("Show about dialog")
        .default_value(false)
        .implicit_value(true);

    program.add_argument("-s", "--session")
        .help("Load a session")
        .nargs(1)
        .metavar("SESSION_NAME");

    program.add_argument("--foreground")
        .help("Run in the foreground (do not detach from the terminal)")
        .default_value(false)
        .implicit_value(true);

#ifdef WITH_SYNCTEX
    program.add_argument("--synctex-forward")
        .help(
            "Format: "
            "--synctex-forward={pdf-file-path}#{src-file-path}:{line}:{column}")
        .default_value(std::string{})
        .metavar("SYNCTEX_FORMAT");
#endif

    program.add_argument("--layout")
        .help("Set initial layout (single, vertical, horizontal, book)")
        .default_value(std::string{"vertical"})
        .metavar("LAYOUT");

    program.add_argument("--tutorial")
        .help("Start with the tutorial file open")
        .flag();

    program.add_argument("--command")
        .help(
            "Execute one or more valid lektra command (see `--list-commands`) "
            "on startup (separate multiple commands with ';')")
        .default_value(std::string{})
        .metavar("COMMAND(s)");

    // This can take optional argument.
    program.add_argument("--check-config")
        .help("Check the validity of the config file and exit with non-zero "
              "code if invalid. Optionally specify a config file path to check "
              "(defaults to searching for config.toml in standard locations)")
        .nargs(0, 1)
        .metavar("CONFIG_PATH");

    program.add_argument("files").remaining().metavar("FILE_PATH(s)");
}

int
main(int argc, char *argv[])
{
    argparse::ArgumentParser program("lektra", APP_VERSION,
                                     argparse::default_arguments::all);
    init_args(program);
    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::exception &e)
    {
        qDebug() << e.what();
        return 1;
    }

    // Zed-style behavior: by default, detach from the terminal so the shell
    // returns immediately. Use --foreground to disable this (useful for
    // debugging/logging).
    bool foreground   = program.is_used("--foreground");
    bool listCommands = program.is_used("--list-commands");
    bool check_config = program.is_used("--check-config");
    bool showVersion  = program.is_used("version");

    if (!(foreground || listCommands || check_config || showVersion))
        return spawn_detached_child(argc, argv);

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);

#ifdef _WIN32
    app.setWindowIcon(QIcon(":/resources/lektra.ico"));
#else
    app.setWindowIcon(QIcon(":/resources/png/lektra.png"));
#endif

    // Load correct localization file
    QTranslator *translator = new QTranslator(&app);

    QLocale locale = QLocale::system();

    const QStringList searchPaths
        = {QCoreApplication::applicationDirPath() + "/translations",
           QString("%1/share/lektra/translations").arg(APP_INSTALL_PREFIX)};

    const QString fileName = QString("lektra.%1").arg(locale.name());

    bool loaded = false;
    for (const QString &path : searchPaths)
    {
        if (translator->load(fileName, path))
        {
            loaded = true;
            break;
        }
    }

    if (loaded)
    {
        app.installTranslator(translator);
        qDebug() << "Loaded language: " << translator->language();
    }
    else
    {
        delete translator;
    }

    Lektra d;
    d.Read_args_parser(program);

    if (listCommands || check_config || showVersion)
        return 0;

    return app.exec();
}
