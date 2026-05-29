# kdeconnect-nx

## Building from source

Prerequisites:
* devkitPro
  * devkitA64
  * switch-curl
  * switch-zlib

Compile in release mode:
```
cmake --preset "switch-release"
cmake --build cmake-build-release-devkita64 -j 6 --target publish
```
Copy the contents of `cmake-build-release-devkita64/stage/` to the root of the SD card.

## Development environment

Setting up a proper dev environment can greatly speed up development using remote logging and remote deployment.
If you plan on doing non-trivial changes, I highly recommend following the steps below.

Prerequisites:
* Everything from above
* [sys-ftpd](https://github.com/ELY3M/sys-ftpd) sysmodule installed on the Switch

Open `CMakeUserPresets.json` and edit the `cacheVariables` blocks for both the debug and release targets.

* If you want to use remote logging, set `NXLINK_HOST` to the IP address of your computer. See [below](#remote-logging) for details.
* Set `SWITCH_FTP_BASE_URL` to the FTP URL (using format: `ftp://user:pass@address:port`) so it can connect to sys-ftpd.
  You may want to check your sys-ftpd config file at `/config/sys-ftpd/config.ini` and provide the correct FTP port and credentials.

Example:
```json
  "cacheVariables": {
    "NXLINK_HOST": "192.168.178.72",
    "SWITCH_FTP_BASE_URL": "ftp://anonymous:@192.168.178.177:5000"
  }
```

Then configure the build:
```
cmake --preset "switch-dev"
```

### FTP upload build targets

To build and automatically upload the sysmodule to the Switch, run:
```
cmake --build cmake-build-debug-devkita64-dev --target MiniKDEConnect_sysmodule_upload
```
Then use the [sysmodules overlay](https://github.com/ppkantorski/ovl-sysmodules) to reload the sysmodule on the Switch.

> [!TIP]
> You can create keycombo shortcuts for overlays in Ultrahand by pressing Y while hovering over an overlay in the list. That way, you'll avoid navigating Ultrahand's main menu over and over.


To build and automatically upload the overlay to the Switch, run:
```
cmake --build cmake-build-debug-devkita64-dev --target MiniKDEConnect_overlay_upload
```

### Crash reports

To symbolize Atmosphère crash reports (`/atmosphere/crash_reports/*.log`), use the [`tools/symbolize_crash.py`](tools/symbolize_crash.py) Python script.
You must provide the ELF file that was produced during build, as it contains all the debug symbols. You may also need to provide the path to GDB from the devkitA64 toolkit.

Example:
```
python parse_crash.py 01779993533_4de000000c011ec7.log cmake-build-debug-devkita64-dev/sysmodule/MiniKDEConnect_sysmodule.elf

# If gdb is not found, specify it:
python parse_crash.py crash_report.log sysmodule.elf /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb
```

### Remote logging

This project uses a modified version of nxlink that implements custom port support, reconnection support, and support for sysmodules.
With it, you can use multiple nxlink sessions simultaneously for the overlay and sysmodule.

Build the nxlink tool:
```
cmake --preset "default"
cmake --build cmake-build-debug-host --target nxlink -j 6
```

> [!IMPORTANT]
> nxlink support is only enabled in debug builds.
> You must set the `NXLINK_HOST` CMake build option to your computer's IP address which hosts the log servers!

Edit `CMakeUserPresets.json` and set `NXLINK_HOST` to the IP address of the log server (your host computer).
Then use that user preset to build the project:
```
cmake --preset "switch-dev"
cmake --build cmake-build-debug-devkita64-dev -j 6
```

Start the logging servers before running the sysmodule & overlay:
```
# Run server for the sysmodule (or test applet) to connect to:
cmake-build-debug-host/tools/nxtool -l -P 28771

# In another terminal window:
# Run server for the overlay to connect to:
cmake-build-debug-host/tools/nxlink -l -P 28772
```

## License

Licensed under GPLv3.

Exceptions:
* `sysmodule/src/ipc/ipc_server.*`: THE BEER-WARE LICENSE (Thanks to [retronx-team](https://github.com/retronx-team/sys-clk)!)
* `tools/nxlink.c`: ISC license (Thanks to [switchbrew](https://github.com/switchbrew/switch-tools)!)
