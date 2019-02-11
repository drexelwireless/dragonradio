#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <signal.h>

#include <pybind11/embed.h>

namespace py = pybind11;

#include "Logger.hh"
#include "RadioConfig.hh"

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

int main(int argc, char** argv)
{
    if (argc == 1) {
        fprintf(stderr, "Must specify Python script to run.\n");
        exit(EXIT_FAILURE);
    }

    // Install backtrace signal handler
    struct sigaction s;

    s.sa_flags = SA_SIGINFO|SA_RESETHAND;
    s.sa_sigaction = backtraceHandler;
    sigemptyset(&s.sa_mask);
    sigaction(SIGSEGV, &s, 0);

    // Start the interpreter and keep it alive
    py::scoped_interpreter guard{};

    // Evaluate in scope of main module
    py::object scope = py::module::import("__main__").attr("__dict__");

    // Stuff our arguments into Python's sys.argv, but skip the first argument,
    // which is the name of this binary. Instead, Python will see the name of
    // the script we run as the first argument.
    py::list args(argc-1);

    for (int i = 1; i < argc; ++i)
        args[i-1] = argv[i];

    py::module::import("sys").attr("argv") = args;

    // Add the directory where the script lives to sys.path
    py::exec(R"(
        import os
        import sys
        sys.path.append(os.path.dirname(os.path.abspath(sys.argv[0])))
    )");

    // Evaluate the Python script
    int ret;

    try {
        py::eval_file(argv[1], scope);

        printf("Done!\n");
        ret = EXIT_SUCCESS;
    } catch (const std::exception &e) {
        fprintf(stderr, "Python exception: %s\n", e.what());
        ret = EXIT_FAILURE;
    }

    if (logger)
        logger.reset();

    return ret;
}
