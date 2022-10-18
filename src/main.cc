// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>

#include <pwd.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <pybind11/embed.h>

namespace py = pybind11;

using namespace pybind11::literals;

#include "Logger.hh"
#include "WorkQueue.hh"
#include "util/capabilities.hh"
#include "phy/PHY.hh"

#define MAXFRAMES 25

/** @brief A signal handler that prints a backtrace */
/** See:
 * https://stackoverflow.com/questions/77005/how-to-automatically-generate-a-stacktrace-when-my-program-crashes
 */
extern "C" void backtraceHandler(int signum, siginfo_t *si, void *ptr)
{
    void   *frames[MAXFRAMES];
    size_t nframes;

    // Get backtrace
    nframes = backtrace(frames, MAXFRAMES);

    // Print the backtrace to stderr
    fprintf(stderr, "CRASH: signal %d:\n", si->si_signo);
    backtrace_symbols_fd(frames, nframes, STDERR_FILENO);

    // Re-raise the signal to get a core dump
    signal(signum, SIG_DFL);
    raise(signum);
}

/** @brief Activate Python virtual environment */
/** If the environmement variable VIRTUAL_ENV is set, use the associated
 * virtualenv. This allows us to use the virtualenv even through the dragonradio
 * binary is not located in the virtual environment's bin directory.
 */
void activateVirtualenv(void)
{
    char *venv = getenv("VIRTUAL_ENV");

    if (venv) {
        py::object join = py::module::import("os").attr("path").attr("join");
        py::object path = join(venv, "bin", "activate_this.py");

        py::eval_file(path, py::globals(), py::dict("__file__"_a=path));
    }
}

bool endswith(const char *s1, const char *s2)
{
    return strlen(s1) >= strlen(s2) && strcmp(s1 + strlen(s1) - strlen(s2), s2) == 0;
}

int main(int argc, char** argv)
{
    // If this binary's name ends in "python,"" just run the python interpreter.
    // This provides a standard Python interpreter with the complete dragonradio
    // module available, which is useful for, e.g., mypy.
    if (argc > 0 && endswith(argv[0], "python")) {
        return Py_BytesMain(argc, argv);
    }

    if (argc == 1) {
        fprintf(stderr, "Must specify Python script to run.\n");
        exit(EXIT_FAILURE);
    }

    // If we were run under sudo, change euid and egid to match the user who
    // invoked us.
    const char *SUDO_UID = "SUDO_UID";
    const char *SUDO_GID = "SUDO_GID";
    const char *HOME = "HOME";

    if (geteuid() == 0 && getuid() == 0 && getenv(SUDO_UID) != NULL && getenv(SUDO_GID) != NULL) {
        int sudo_uid      = atoi(getenv(SUDO_UID));
        int sudo_gid      = atoi(getenv(SUDO_GID));
        struct passwd *pw = getpwuid(sudo_uid);

        if (setegid(sudo_gid) < 0)
            throw std::runtime_error(strerror(errno));

        if (seteuid(sudo_uid) < 0)
            throw std::runtime_error(strerror(errno));

        if (setenv(HOME, pw->pw_dir, 1) < 0)
            throw std::runtime_error(strerror(errno));
    } else {
        // If we were invoked setuid, change euid and egid to match the user who
        // invoked us.
        if (getegid() != getgid()) {
            if (setegid(getgid()) < 0)
                throw std::runtime_error(strerror(errno));
        }

        if (geteuid() != getuid()) {
            if (seteuid(getuid()) < 0)
                throw std::runtime_error(strerror(errno));
        }
    }

    // Drop capabilities
    try {
        Caps caps(cap_get_proc());

        caps.clear();
        caps.set_flag(CAP_PERMITTED, {CAP_SYS_NICE, CAP_NET_ADMIN});
        caps.set_proc();
    } catch(const std::exception& e) {
        fprintf(stderr, "WARNING: Cannot obtain CAP_SYS_NICE and CAP_NET_ADMIN capabilities.\n");
    }

    // Install backtrace signal handler
    struct sigaction s;

    s.sa_flags = SA_SIGINFO|SA_RESETHAND;
    s.sa_sigaction = backtraceHandler;
    sigemptyset(&s.sa_mask);
    sigaction(SIGSEGV, &s, 0);

    // Result returned by Python
    int ret;

    // Start the Python interpreter
    {
        // Pass our command-line arguments to Python, but skip the first
        // argument, which is the name of this binary. Instead, Python will see
        // the name of the script we run as the first argument.
        py::scoped_interpreter guard{ true, argc-1, argv+1, true };

        // Activate any Python virtual environment
        activateVirtualenv();

        // Evaluate the Python script
        try {
            py::eval_file(argv[1]);
            ret = EXIT_SUCCESS;
        } catch (const py::error_already_set& e) {
            if (e.matches(py::module::import("builtins").attr("SystemExit"))) {
                auto args = py::tuple(e.value().attr("args"));

                ret = py::cast<int>(args[0]);
            } else {
                fprintf(stderr, "Python exception: %s\n", e.what());
                ret = EXIT_FAILURE;
            }
        } catch (const std::exception &e) {
            fprintf(stderr, "Python exception: %s\n", e.what());
            ret = EXIT_FAILURE;
        }

        // Stop the work queue
        work_queue.stop();
    }

    // Ensure snapshot collector is gracefully closed
    PHY::resetSnapshotCollector();

    // Ensure logger is gracefully closed
    if (logger)
        logger->close();

    // Release USRP from Clock.
    MonoClock::reset_time_keeper();

    return ret;
}
