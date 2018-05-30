#include <stdio.h>
#include <stdlib.h>

#include <pybind11/embed.h>

namespace py = pybind11;

#include "Logger.hh"
#include "RadioConfig.hh"

int main(int argc, char** argv)
{
    if (argc == 1) {
        fprintf(stderr, "Must specify Python script to run.\n");
        exit(EXIT_FAILURE);
    }

    // Create RadioConfig
    rc = std::make_shared<RadioConfig>();

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
