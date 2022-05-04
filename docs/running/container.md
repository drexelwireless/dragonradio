(sec:container)=

# Using a DragonRadio Container

Although it is possible to install DragonRadio directly on a machine running Ubuntu 20.04, using a container ensures a reproducible environment and prevents components that DragonRadio installs from conflicting with other software. Containers are also necessary to run two copies of DragonRadio on the same host machine.

There are several steps that can be taken to make developing with a container as seamless as possible.

* **Expose a USRP to the container**.

  One (or more) USRPs can be exposed to a container by adding the appropriate ethernet device. Assuming the USRP is attached to the `eno2` interface on the host machine, it can be exposed to the container named `dragonradio` as follows:

  ```shell-session
  $ lxc config device add dragonradio usrp nic nictype=physical parent=eno2 name=usrp
  ```

  After exposing a USRP, you will need to set up its network interface. The easiest way to do this is by editing the `/etc/rc.local` file, which is run on boot. For an X310 radio in the default configuration, the following `/etc/rc.local` will assign the host side the IP address 192.168.40.1 and set the MTU to 9000.

  ```shell
  #!/bin/sh -e
  ip addr add 192.168.40.1/30 dev usrp
  ip link set usrp mtu 9000
  ```

  For an N210 radio in the default configuration, `rc.local` should contain:

  ```shell
  #!/bin/sh -e
  ip addr add 192.168.10.1/30 dev usrp
  ```

  For further details, read the full documentation for [X3x0 network configuration](https://files.ettus.com/manual/page_usrp_x3x0.html#x3x0_setup_network) or [N2x0 network configuration](https://files.ettus.com/manual/page_usrp2.html#usrp2_network>). Radios whose configurations have been modified will need to be reset back to the default configuration to work with the `rc.local` parameters above.

* **Expose a JTAG port**. *This is only necessary if you plan to flash the USRP firmware over JTAG*.

  Use `lsusb` to find JTAG devices---they will be labeled "Future Technology Devices International." A JTAG device found at bus 001 device 002 can be exposed to the container named `dragonradio` by executing the following command on the host:

  ```shell-session
  $ lxc config device add dragonradio bus001dev002 disk source=/dev/bus/usb/001/002 path=/dev/bus/usb/001/002
  ```

* **Mirror your local user and home directory in the container**.

  The script `lxd-mirror-user.sh`, located in the `bin` directory of the repository, will mirror the current user's account and home directory in the container whose name is passed as the first argument to the script. **The UID of the host user being mirrored in the container must not already exist in the container**.

You can now log in to the container by executing the following command, where `USERNAME` is your username on the host computer (now mirrored in the container).

```shell-session
$ lxc exec dragonradio -- su --login USERNAME
```
