(sec:security)=

# Capabilities and Security

DragonRadio requires the `CAP_SYS_NICE` and `CAP_NET_ADMIN` capabilities to configure the USRP and network. Strictly speaking, only the `CAP_NET_ADMIN` capability is necessary; without the `CAP_SYS_NICE` capability, the radio will run, but UHD will complain about not being able to change thread priorities.

There are several ways to provide these capabilities:

1. Run the `dragonradio` binary as `root` or another privileged user.
1. Set the `setuid` bit on the `dragonradio` binary and `chown` it to a privileged user (like `root`).
1. Use `sudo` to run `dragonradio`.
1. Use `setcap` to set file capabilities on the `dragonradio` binary, e.g.,

```shell-session
$ setcap cap_sys_nice,cap_net_admin+p dragonradio
```

When `dragonradio` starts, if it was invoked `setuid` or via `sudo`, it first changes its effective uid to the invoking user's uid. It then drops all capabilities, keeping only `CAP_SYS_NICE` and `CAP_NET_ADMIN` in its permitted set. These capabilities are temporarily raised only when they are strictly required.
