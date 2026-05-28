# kdeconnect-nx

## Building from source

Prerequisites:
* devkitPro
  * devkitA64
  * switch-curl
  * switch-zlib

```
cmake --preset "switch-release"
cmake --build cmake-build-release-devkita64 -j 6 --target publish
```
Copy the directories under `cmake-build-release-devkita64/stage/*` to the SD card on the Switch.

### Remote logging

This project uses a modified version of nxlink which implements custom port support, reconnection support, and support for sysmodules.
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

Connect to the Switch processes:
```
# Run server for the sysmodule (or test applet) to connect to:
cmake-build-debug-host/tools/nxtool -l -P 28771

# In another terminal window:
# Run server for the overlay to connect to:
cmake-build-debug-host/tools/nxlink -l -P 28772
```

###

## License

Licensed under GPLv3. 

Exceptions:
* `sysmodule/src/ipc/ipc_server.*`: THE BEER-WARE LICENSE (Thanks to [retronx-team](https://github.com/retronx-team/sys-clk)!)
* `tools/nxlink.c`: ISC license (Thanks to [switchbrew](https://github.com/switchbrew/switch-tools)!)
