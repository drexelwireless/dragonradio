About DragonRadio
=================
DragonRadio originated as Drexel's entry into DARPA's `SC2`_ competition. It is a software-defined radio built from scratch---it is not based on GNURadio---and has the following notable features:

 * **Runs on the Northeastern Colosseum**. DragonRadio provides an easy path to getting started with the Colosseum_, a large-scale wireless emulator.
 * **Uses USRP hardware**. DragonRadio has been tested on both the N210 and X310 platforms.
 * **Pure software**. All functionality is implemented in software and can run on stock USRP firmware.
 * **Low-level functionality implemented in Modern C++**. All low-level functionality is implemented in C++17 and makes extensive use of modern C++ features like `std::shared_ptr`.
 * **Fast signal processing primitives**. DragonRadio includes fast time- and frequency-domain filters, implemented with the help of `xsimd`_.
 * **OFDM PHY layer**. Included PHY layers are based on `liquid-dsp`_. However, the PHY interface is modular and not tied to liquid-dsp, so other PHY layers could easily be integrated.
 * **FDMA, FDMA/TDMA, and ALOHA MACs**. A sophisticated FDMA/TDMA MAC layer is provided that allows scheduling in both time and frequency. FDMA and ALOHA MACs are also included.
 * **Embedded Python interpreter**. DragonRadio embeds a Python interpreter, and all low-level functionality is exposed to Python via pybind11_. The radio is configured from Python.

Please submit any issues on `GitHub`_.

|nsf| This project is supported by NSF awards `1717088 <https://www.nsf.gov/awardsearch/showAward?AWD_ID=1717088>`_ and `1730140 <https://www.nsf.gov/awardsearch/showAward?AWD_ID=1730140>`_.

.. |nsf| image:: images/NSF_4-Color_bitmap_Logo.png
   :height: 100px

.. _SC2: https://archive.darpa.mil/sc2/
.. _liquid-dsp: https://github.com/jgaeddert/liquid-dsp
.. _xsimd: https://github.com/xtensor-stack/xsimd
.. _pybind11: https://github.com/pybind/pybind11
.. _Colosseum: https://www.northeastern.edu/colosseum/
.. _GitHub: https://github.com/drexelwireless/dragonradio
