# KDE Connect NX

A lightweight [KDE Connect](https://kdeconnect.kde.org/) implementation for the Nintendo Switch written from scratch.
Runs as a background sysmodule with an Ultrahand overlay, and pairs with any KDE Connect client present in the same local network.

## Features

Not all KDE Connect plugins are implemented. Some plugins are only implemented in one direction.

From your phone, tablet, PC to your Switch:

- **Notifications: receive Android/PC notifications on-screen as popups**
- Remote keyboard: inject USB keyboard strokes on the Switch from another device
  - (Limitation: An US English USB keyboard is simulated, so only characters present on a US keyboard can be injected.)
- Remote commands: remote control the Switch with some hardcoded commands (power control, send screenshot to PC/phone, etc.)
- Volume control: remote control the master volume level of your Switch
- File transfer: send files to your Switch's SD card

From your Switch to your other devices:

- Media remote: control playback of media on your phone/PC
- Remote commands (PC only): trigger predefined commands on a connected PC (custom shell commands; requires setup on the PC)
- Volume control (PC only): remotely choose between audio devices and set the volume level for each one
- Send screenshot: take a screenshot and send it to a device
- Ring: find your lost phone

In both directions:

- Battery status: view battery levels and charge statuses

## Screenshots

<table>
  <tr>
    <td><img src="screenshots/overlay_main_crop.jpg" alt="Device list"/></td>
    <td><img src="screenshots/overlay_device_crop.jpg" alt="Device actions"/></td>
    <td><img src="screenshots/overlay_commands_crop.jpg" alt="Remote commands"/></td>
  </tr>
  <tr>
    <td align="center">Device list</td>
    <td align="center">Device actions</td>
    <td align="center">Remote commands</td>
  </tr>
  <tr>
    <td><img src="screenshots/overlay_media_remote_crop.jpg" alt="Media remote"/></td>
    <td><img src="screenshots/overlay_sinks_crop.jpg" alt="Volume control"/></td>
    <td><img src="screenshots/overlay_settings_crop.jpg" alt="Settings"/></td>
  </tr>
  <tr>
    <td align="center">Media remote</td>
    <td align="center">Volume control</td>
    <td align="center">Settings</td>
  </tr>
</table>

Notifications appear as a pop-up:

![Notification example](screenshots/example_notification.jpg)

## Building from sources

Refer to [BUILDING.md](BUILDING.md) for detailed build instructions and development environment setup recommendations.

## License

Licensed under GPLv3.

Exceptions:
* `sysmodule/src/ipc/ipc_server.*`: THE BEER-WARE LICENSE (Thanks to [retronx-team](https://github.com/retronx-team/sys-clk)!)
* `tools/nxlink.c`: ISC license (Thanks to [switchbrew](https://github.com/switchbrew/switch-tools)!)
