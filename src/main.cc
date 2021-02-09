// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <signal.h>

#include <pybind11/embed.h>

namespace py = pybind11;

using namespace pybind11::literals;

#include "Logger.hh"
#include "Util.hh"

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

/** @brief Set Python sys.argv */
void setPythonArgv(int argc, char** argv)
{
    py::list args(argc);

    for (int i = 0; i < argc; ++i)
        args[i] = argv[i];

    py::module::import("sys").attr("argv") = args;
}

int main(int argc, char** argv)
{
    if (argc == 1) {
        fprintf(stderr, "Must specify Python script to run.\n");
        exit(EXIT_FAILURE);
    }

    // Drop capabilities
    Caps caps(cap_get_proc());

    caps.clear();
    caps.set_flag(CAP_PERMITTED, {CAP_SYS_NICE, CAP_NET_ADMIN});
    caps.set_proc();

    // Drop euid
    if (geteuid() != getuid()) {
        if (seteuid(getuid()) < 0)
            throw std::runtime_error(strerror(errno));
    }

    // Install backtrace signal handler
    struct sigaction s;

    s.sa_flags = SA_SIGINFO|SA_RESETHAND;
    s.sa_sigaction = backtraceHandler;
    sigemptyset(&s.sa_mask);
    sigaction(SIGSEGV, &s, 0);

    // Start the interpreter and keep it alive
    py::scoped_interpreter guard{};

    // Activate any Python virtual environment
    activateVirtualenv();

    // Stuff our arguments into Python's sys.argv, but skip the first argument,
    // which is the name of this binary. Instead, Python will see the name of
    // the script we run as the first argument.
    setPythonArgv(argc-1, argv+1);

    // Evaluate the Python script
    int ret;

    try {
        py::eval_file(argv[1]);
        ret = EXIT_SUCCESS;
    } catch (const py::error_already_set& e) {
        if (e.matches(py::module::import("builtins").attr("SystemExit"))) {
            auto args = py::tuple(e.value().attr("args"));

            ret = py::cast<int>(args[0]);
        } else
            throw;
    } catch (const std::exception &e) {
        fprintf(stderr, "Python exception: %s\n", e.what());
        ret = EXIT_FAILURE;
    }

    if (logger)
        logger.reset();

    return ret;
}
