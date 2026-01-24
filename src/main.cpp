#include "argparse.hpp"
#include "lektra.hpp"

#include <QApplication>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

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

void
init_args(argparse::ArgumentParser &program)
{
    program.add_argument("-p", "--page")
        .help("Page number to go to")
        .scan<'i', int>()
        .default_value(-1)
        .metavar("PAGE_NUMBER");

    program.add_argument("-c", "--config")
        .help("Path to config.toml file")
        .nargs(1)
        .metavar("CONFIG_PATH");

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

#ifdef HAS_SYNCTEX
    program.add_argument("--synctex-forward")
        .help(
            "Format: "
            "--synctex-forward={pdf-file-path}#{src-file-path}:{line}:{column}")
        .default_value(std::string{})
        .metavar("SYNCTEX_FORMAT");
#endif

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
    const bool foreground = program.get<bool>("--foreground");
    if (!foreground)
        return spawn_detached_child(argc, argv);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/resources/png/logo.png"));
    lektra d;
    d.ReadArgsParser(program);
    app.exec();
}
