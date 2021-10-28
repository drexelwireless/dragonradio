Overview
========

DragonRadio consists of a C++ core that is exposed to Python using the excellent pybind11_ library---the ``dragonradio`` binary embeds a Python interpreter. The radio is initialized and configured in Python, a process driven by a Python script.

Capabilities and Security
-------------------------

DragonRadio requires the ``CAP_SYS_NICE`` and ``CAP_NET_ADMIN`` capabilities to configure the USRP and network. Strictly speaking, only the ``CAP_NET_ADMIN`` capability is necessary; without the ``CAP_SYS_NICE`` capability, the radio will run, but UHD will complain about not being able to change thread priorities.

There are several ways to provide these capabilities:

#. Run the ``dragonradio`` binary as ``root`` or another privileged user.
#. Set the ``setuid`` bit on the ``dragonradio`` binary and ``chown`` it to a privileged user (like ``root``).
#. Use ``setcap`` to set file capabilities on the ``dragonradio`` binary, e.g.,

   .. code-block:: bash

      setcap cap_sys_nice,cap_net_admin+p dragonradio

On startup, ``dragonradio`` drops all capabilties, keeping only ``CAP_SYS_NICE`` and ``CAP_NET_ADMIN`` in its permitted set. These capabilities are temporarily raised when needed. If ``dragonradio`` is invoked with the ``setuid`` bit set, it will set its effective uid to the uid of the user who invoked it after dropping capabilities.

Networking
----------

After starting up, the radio will create a ``tap0`` device with IP address ``10.10.10.NODEID`` and a netmask of ``255.255.255.0``, where ``NODEID`` is the node ID. Packets sent to this subnet will in turn be sent over the radio.

Logging
-------

The radio provides extensive logging. The low-level C++ radio will create a log in `HDF5 format <https://portal.hdfgroup.org/display/HDF5/HDF5>`_ named ``radio.h5``. If the file ``radio.h5`` exists, it will create ``radio-N.h5`` where ``N`` is the first number such that ``radio-N.h5`` does not exist; this allows the radio to be restarted if it crashes while guaranteeing it won't overwrite old logs.

Each HDF5 log has the following attributes:

#. ``config``: The radio configuration, dumped from the :class:`~dragonradio.radio.Config` object used to configure the radio.
#. ``node_id``: The numeric radio node identifier.
#. ``start``: Start time of logging, in seconds since the epoch.
#. ``version``: The version of the radio.

A HDF5 log contains the following tables:

#. ``arq_event``: ARQ events.
#. ``event``: Logged messages, consisting of a time (offset from ``start`` of the log) and a string.
#. ``recv``: Received packets.
#. ``selftx``: Self-transmissions.
#. ``send``: Sent packets.
#. ``slots``: IQ data received by MAC.
#. ``snapshots``: Snapshotted IQ data.

.. _pybind11: https://github.com/pybind/pybind11
