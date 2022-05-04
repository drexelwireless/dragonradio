(sec:hardware)=

# Hardware Support

DragonRadio supports USRP X310 and N210 radios running the UHD 3.9.5 firmware, which is the default version on the Colosseum. Radios **must use the UHD 3.9.5 firmware**. See the USRP manual for instructions for [flashing the N210](https://files.ettus.com/manual/page_usrp2.html#usrp2_loadflash) and instructions for [flashing the X310](https://files.ettus.com/manual/page_usrp_x3x0.html#x3x0_flash>). We provide a patched version of UHD 3.9.5 that compiles under Ubuntu 20.04. If you are running DragonRadio on your own USRP instead of on the Colosseum, make sure it has been flashed with the 3.9.5 firmware.

Despite its age, UHD 3.9.5 is the last version that does not suffer from a [FIFO DRAM bug](https://github.com/EttusResearch/uhd/issues/203). This bugs causes timed transmissions, which DragonRadio uses extensively, to cause the radio to lock up within minutes. We have not yet come up with a way to work around this bug that does not degrade radio performance.
