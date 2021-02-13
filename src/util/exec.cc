// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>

#include "logging.hh"
#include "util/exec.hh"

int exec(const std::vector<std::string>& args)
{
    std::string command(args[0]);
    pid_t       pid;
    int         wstatus;

    for (auto arg = ++args.begin(); arg != args.end(); arg++) {
        command += " ";
        command += *arg;
    }

    logSystem(LOGDEBUG, "%s", command.c_str());

    if ((pid = fork()) < 0) {
        throw std::runtime_error(strerror(errno));
    } else if (pid == 0) {
        std::vector<const char*> cargs(args.size() + 1);

        for (std::vector<const char*>::size_type i = 0; i < args.size(); ++i)
            cargs[i] = args[i].c_str();

        if (execvp(cargs[0], const_cast<char* const*>(&cargs[0])) < 0) {
            throw std::runtime_error(strerror(errno));
        }

        return 0;
    } else {
        waitpid(pid, &wstatus, 0);

        if (wstatus != 0)
            logSystem(LOGDEBUG, "%s (%d)", command.c_str(), wstatus);

        return wstatus;
    }
}
