(sec:interactive)=

# Interactive Mode

Interactive mode is enabled with the `--interactive` flag. This mode starts the radio and drops the user into an IPython shell. Once in the IPython shell, the `radio` variable holds a reference to the current {py:class}`~dragonradio.radio.Radio` instance.

## Starting an IPython Kernel

Instead of starting an IPython shell, the radio will start an IPython kernel when given the `--kernel` flag (the `--interactive` and `--kernel` flags are mutually exclusive). When the kernel starts, it prints the name of the JSON file containing the configuration parameters necessary to connect to the kernel. The file will have a name of the form `kernel-<PID>.json`, and it is located in the directory given by `jupyter --runtime-dir`, typically `~/.local/share/jupyter/runtime/`. A jupyter console can connect to the kernel be running the following command at the terminal

```shell-session
$ jupyter console --existing kernel-<PID>.json
```

Note that it is not necessary to specify the full path to the JSON file. You may need to `pip install jupyter` to run this command.

It is also possible to connect to a [remote kernel via ssh](https://github.com/ipython/ipython/wiki/Cookbook%3A-Connecting-to-a-remote-kernel-via-ssh).
