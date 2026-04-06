# NeonSight

NeonSight is a crosshair/cursor overlay tool designed specifically for Linux. It allows you to place bright, customizable crosshairs securely synced with your system pointer for games that lack proper crosshair functionality or force poor hardware cursors.


**It is currently stable on X11.**
**Wayland support is experimental and may not function on all systems or compositors.**

![](https://static.billgr17.click/img/NeonSight.jpg)

## Build Requirements

Requires `CMake` and development libraries for `X11`, `DBus`, `GLFW3`, and `OpenGL`, alongside a compiler supporting C++23 (like `clang`).

```bash
sudo apt install build-essential cmake clang libx11-dev libxfixes-dev libxext-dev libxrender-dev libxcursor-dev libdbus-1-dev libglfw3-dev libgl-dev
```

## Compilation

Build NeonSight easily using the included shell script:

```bash
./build.sh -r
```

## License
This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.