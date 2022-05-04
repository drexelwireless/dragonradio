# Running the Radio

DragonRadio consists of an embedded Python interpreter and a C++ core that is exposed to Python using the excellent [pybind11](https://github.com/pybind/pybind11) library. Radio initialization and configuration is driven by a Python script. Configuration parameters are taken from an instance of {py:class}`~dragonradio.config.Config`. These parameters can be changed by explicitly modifying the configuration instance. The DragonRadio command-line parser exposes most of the parameters so that they can be specified at radio boot. Parameters can also be stored in a `libconf`-style configuration file and loaded into the {py:class}`~dragonradio.config.Config` instance using {py:func}`~dragonradio.config.Config.loadConfig`.

```{toctree}
---
maxdepth: 1
caption: in this section
---
hardware
container
invoking
interactive
security
networking
logging
cli
```
