DragonRadio in the Colosseum
============================

Interactive Mode
----------------

See :ref:`building a container` for instructions on building a Colosseum-compatible image. Images built with those instructions can run without any changes in interactive mode on the Colosseum. A container built with the ``-s`` flag will boot with DragonRadio already running. To run DragonRadio manually in interactive mode on such a container, you will first need to disable the dragonradio service using the command ``service dragonradio stop``.

Batch Mode
----------

To run in batch mode, a container must have been build with the ``-s`` flag to enable the ``dragonradio`` service. This service starts the SC2 radio on boot and daemonizes it. Commands from the `Colosseum Radio Command and Control API`_ are processed by the ``dragonradio-client`` script.

The recommended modem configuration for batch mode is located at ``config/sce-qual/radio.conf``.

A sample batch configuration file for the `SCE Qualification Scenario`_ is located at ``config/sce-qual/sce-qual.json``. It can be used as a `Jinja`_ template; you will need to fill in the ``name``, ``image``, and ``radio_conf`` parameters to specify the job name, radio image name, and modem configuration.

.. _Colosseum Radio Command and Control API: https://colosseumneu.freshdesk.com/support/solutions/articles/61000253495-radio-command-and-control-c2-api
.. _SCE Qualification Scenario: https://colosseumneu.freshdesk.com/support/solutions/articles/61000253505-sce-qualification-9988-
.. _Batch mode: https://colosseumneu.freshdesk.com/support/solutions/articles/61000253519-batch-mode-format-and-process
.. _Jinja: https://jinja.palletsprojects.com/en/2.11.x/
