Building the radio
==================

DragonRadio is developed under Ubuntu 20.04. Running the top-level ``build.sh`` script in a base Ubuntu 20.04 installation installs all necessary prerequisites and builds DragonRadio.

.. _building a container:

Building a container
--------------------

It is relatively painless to build an ``lxc`` image that can run directly on the Colosseum using the steps below.

#. Install ``lxc`` and ``ansible``.

   Install ``lxc`` and ``ansible`` on the system where you are building the image. On Ubuntu, both can be installed as follows:

   .. code-block:: bash

     sudo apt install -y ansible lxd
     lxd init

   You will also need to edit ``/etc/subuid`` and ``/etc/subgid``. The Colosseum instructions on how to `prepare a container`_ will be helpful.

#. Build base DragonRadio image

   The image built in this step serves as the base image for all DragonRadio builds. It includes generally useful tools, the `Colosseum CLI`_, and additional software, like ``gpsd``, that are necessary to run the radio in batch mode in the Colosseum. The build is automated using ansible:

   .. code-block:: bash

      cd ansible && ansible-playbook -i inventory playbooks/dragonradio.yml

   This will result in a ``dragonradio-2004-base`` image in the top-level ``images`` directory. The password for the ``root`` and ``srn-user`` users in this image is ``dragonradio``.

#. Build a DragonRadio image

   The ``build-image.sh`` takes a ``git`` reference (tag, hash, etc.) and builds an image based on the given version of the radio. For example, an image based on the ``master`` branch can be built as follows:

   .. code-block:: bash

     ./bin/build-image.sh master

   The image wil be placed in the ``images`` directory and have a name of the form ``REF-DATE-HASH``, where ``REF`` is a ``git`` reference, ``DATE`` is the build date, and ``HASH`` is the ``git`` hash of the version of the radio use to build the image.

   The ``build-image.sh`` script takes one optional flag, ``-s``, which will install the ``dragonradio`` service in the generated image. This service automatically starts the radio on boot, making the image ready-to-run in batch mode in the Colosseum.

Building the documentation
--------------------------

The documentation can be built as follows:

.. code-block:: bash

   cd docs
   sudo apt install doxygen
   pip install -Ur requirements.txt
   make html

.. _base image: https://colosseumneu.freshdesk.com/support/solutions/articles/61000253371-transferring-the-base-lxc-image-from-the-nas
.. _prepare a container: https://colosseumneu.freshdesk.com/support/solutions/articles/61000253428-prepare-a-new-container-for-upload
.. _Colosseum CLI: https://colosseumneu.freshdesk.com/support/solutions/articles/61000253397-colosseum-cli