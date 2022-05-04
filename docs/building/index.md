# Building the Radio

DragonRadio is developed under Ubuntu 20.04. Running the top-level `build.sh` script on a machine running Ubuntu 20.04 will install all necessary prerequisites and build DragonRadio. However, all DragonRadio development is done in an LXC container, which ensures isolation and avoids package dependency issues. The following instructions will build an LXC image containing DragonRadio on a Ubuntu 20.04 host. This container can be used either on the Colosseum or on another machine running LXC. See {ref}`sec:container` for instructions for using a container outside the Colosseum.

```{toctree}
---
maxdepth: 1
caption: in this section
---

container
developing
doc
```
