## Tools


### nxlink

Modified nxlink server that supports custom ports and reconnects after a disconnection (e.g. console entered sleep state). 

Needs a custom nxlink client. See [nxlink_sink.cpp](../common/src/utils/nxlink_sink.cpp).

### symbolize_crash.py

Tool to symbolize Atmosphère crash reports using ELF files with GDB.

### memtrace

Tools to parse and visualize memory allocation traces created by the built-in memory tracker (see `src/utils/mem_tracker.cpp`)
