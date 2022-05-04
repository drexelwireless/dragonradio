(sec:building_container)=

# Building a Container

The following instructions will build an LXC image based on Ubuntu 20.04 that contains a ready-to-run copy of DragonRadio.

1. Install `lxc` and `ansible`.

   Install `lxc` and `ansible` on the system where you are building the image. On Ubuntu, both can be installed as follows:

   ```shell-session
   $ sudo apt install -y ansible lxd
   $ lxd init
   ```

   Depending on the age of your Ubuntu installation, you may need a more recent version of ansible. You can install the most recent version using ``pip``:

   .. code-block:: bash

   ```shell-session
   $ pip install --user --upgrade ansible
   ```

   You may also need to edit `/etc/subuid` and `/etc/subgid`. The Colosseum instructions on how to [prepare a container](https://colosseumneu.freshdesk.com/support/solutions/articles/61000253428-prepare-a-new-container-for-upload) may be helpful.

1. Check out the DragonRadio source code. Since DragonRadio uses submodules, it is import to use the `--recursive` flag when cloning the repository.

   ```shell-session
   $ git clone --recursive https://github.com/drexelwireless/dragonradio.git
   ```

1. Build base DragonRadio image

   The image built in this step serves as the base image for all DragonRadio builds. It includes generally useful tools, like the [Colosseum CLI](https://colosseumneu.freshdesk.com/support/solutions/articles/61000253397-colosseum-cli), and additional software, like `gpsd`, that are necessary to run the radio in batch mode in the Colosseum. The build is automated using ansible:
   
   ```shell-session
   $ (cd ansible && ansible-playbook -e container=dragonradio-2004-base playbooks/dragonradio.yml)
   ```

   This will produce a `dragonradio-2004-base` image in the top-level `images` directory. The password for both the `root` and the `srn-user` users in this image is `dragonradio`.

   The base image can be built against other source containers. For example, a base image built on Ubuntu 22.04 can be built as follows:

   ```shell-session
   $ (cd ansible && ansible-playbook -e source=ubuntu:22.04 -e container=dragonradio-2204-base playbooks/dragonradio.yml)
   ```

   If you receive an error of the form `Unsupported parameters for (lxd_container) module: ignore_volatile_options`, then you need to update `ansible`.

1. Build a DragonRadio image

   The `build-image.sh` takes a `git` reference (tag, hash, etc.) and builds an image based on the specified version of the radio. For example, an image based on the `main` branch can be built as follows:

   ```shell-session
   $ ./bin/build-image.sh main
   ```

   The image will be placed in the `images` directory and have a name of the form `REF-DATE-HASH`, where `REF` is a `git` reference, `DATE` is the build date, and `HASH` is the `git` hash of the version of the radio use to build the image.

   The `build-image.sh` script takes the following optional flags:

     * `-s`. Installs the `dragonradio` service in the generated image. This service automatically starts the radio on boot, making the image ready-to-run in batch mode in the Colosseum.
     * `-b CONTAINER`. Use the base image `CONTAINER`. Defaults to `dragonradio-2004-base`.
