Running the Radio
=================

DragonRadio is built against UHD 3.9.5, which is the default version on the Colosseum. We provide a patched version of UHD 3.9.5 that compiles under Ubuntu 20.04. If you are running DragonRadio on your own USRP instead of on the Colosseum, make sure it has been flashed with the 3.9.5 firmware.

The ``build.sh`` script will create a virtualenv environment in the directory ``venv`` containing all required Python modules. You may either activate this virtualenv before invoking the radio, or you may set the ``VIRTUAL_ENV`` environment variable to point to it before you invoke ``dragonradio``. You must make sure the ``dragonradio`` binary can obtain the ``CAP_SYS_NICE`` and ``CAP_NET_ADMIN`` capabilities when it runs (see :ref:`Capabilities and Security`).

Invoking the radio
------------------

The Stand-alone Radio
^^^^^^^^^^^^^^^^^^^^^

Let's take a look at an example invocation of the stand-alone radio, which uses the ``scripts/standalone-radio.py`` to configure the radio:

.. code-block:: bash

   VIRTUAL_ENV=venv ./dragonradio scripts/standalone-radio.py \
      --auto-soft-tx-gain 100 -G 25 -R 25 \
      --slot-size=0.05 --guard-size=0.001 --superslots --tdma-fdma \
      --fifo --packet-compression \
      -l logs -d \
      -b 10e6

The command-line parameters have the following effects:

#. The digital soft gain is automatically adjusted to 0dB using the first 100 transmitted packets. This is necessary for liquid-dsp-based PHYs.
#. The transmit and received hard gain is set to 25 dB.
#. The TDMA/FDMA MAC is used with a TDMA slot size of 50ms and a guard interval of 1ms. Superslots are enabled.
#. A FIFO network queue is used.
#. Packet header compression is enabled.
#. Logs will be written to the ``logs`` directory, and debugging is enabled.
#. The radio bandwidth is set to 10 Mhz.

Note the following defaults:

#. The TDMA/FDMA MAC uses 1 MHz channels. This can be overridden using the ``--channel-bandwidth`` option.
#. The radio node ID is derived from the hostname. This can be overridden using the ``-i`` option.
#. The radio assumes there are two radio nodes, numbered 1 and 2, in the network. This number is used to configure a MAC schedule. It can be overridden using the ``-n`` option.

The next invocation of the stand-alone radio adds the options ``--config config/features/amc.conf --amc --arq``.

.. code-block:: bash

   VIRTUAL_ENV=venv ./dragonradio scripts/standalone-radio.py \
      --config config/features/amc.conf --amc --arq \
      --auto-soft-tx-gain 100 -G 25 -R 25 \
      --slot-size=0.05 --guard-size=0.001 --superslots --tdma-fdma \
      --fifo --packet-compression \
      -l logs -d \
      -b 10e6

These options have additional effects:

#. Options are loaded from the configuration file ``config/features/amc.conf``.
#. Automatic Modulation and Coding (AMC) is enabled.
#. Automatic Repeat Request (ARQ) is enabled.

The ``--config`` option may be specified multiple times. This features loads configuration settings from a `libconfig <http://www.hyperrealm.com/libconfig/libconfig_manual.html#Configuration-Files>`_ format configuration file, making it easy to specify a large set of options without typing out a long command line. Radio options are managed by the :class:`dragonradio.radio.Config` class; see its documentation for a description of all available radio options. See the :ref:`CLI Reference` for a description of all available command-line options. Most, but not all, radio options can be configured on the command line.

The SC2 Radio
^^^^^^^^^^^^^

Here is an example invocation of the SC2 competition radio:

.. code-block:: bash

   VIRTUAL_ENV=venv ./dragonradio scripts/sc2-radio.py \
      --config config/srn/radio.conf \
      --colosseum-ini config/srn/colosseum_config.ini
      --collab-server-ip 10.32.143.101 \
      --bootstrap --foreground \
      -b 10e6 \
      -l logs \
      start

These parameters have the following effects:

#. Load the configuration file ``config/srn/radio.conf``.
#. Load the Colosseum INI file ``config/srn/colosseum_config.ini``.
#. Set the IP address of the collaboration server to ``10.32.143.101``.
#. Boostrap the radio. Without the ``--bootstrap`` option, the radio will wait to be told to start transmitting.
#. Run in the foreground. Without this option, the radio will daemonize and run in the background.
#. ``start`` the radio. This initializes the radio but does not start transmitting until told to begin unless the ``--bootstrap`` option is given.

By default, when run in the background, that radio will write its PID to the file ``/var/run/dragonradio.pid``. It can be invoked with the ``stop`` command instead of the ``start`` command to terminate the radio. The ``dragonradio-client`` script can be used to control the daemonized radio.

.. _CLI Reference:

Command-line Argument Reference
-------------------------------

.. argparse::
   :module: dragonradio.radio.config
   :func: parser
   :prog: dragonradio SCRIPT
