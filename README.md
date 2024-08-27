# Remote VIRTIO GPU Wayland Proxy (rvgpu-wlproxy)

> Remote VIRTIO GPU Wayland Proxy (rvgpu-wlproxy) is a component of the Remote VIRTIO GPU (RVGPU) project, serving as a Wayland server.
> It enables Wayland clients to operate in a virtualized environment by interfacing with the remote-virtio-gpu system.
> This allows for 3D rendering on one device (client) and displaying the output via network on another device (server).

## Contents

- [Remote VIRTIO GPU Wayland Proxy (rvgpu-wlproxy)](#remote-virtio-gpu-wayland-proxy-rvgpu-wlproxy)
  - [Contents](#contents)
  - [Repository structure](#repository-structure)
  - [How to install rvgpu-wlproxy](#how-to-install-rvgpu-wlproxy)
    - [Building from Source](#building-from-source)
    - [How to Install RVGPU](#how-to-install-rvgpu)
  - [How to Remotely Display Wayland Client Applications Using RVGPU](#how-to-remotely-display-wayland-client-applications-using-rvgpu)
    - [Run RVGPU](#run-rvgpu)
      - [Run rvgpu-renderer on Wayland](#run-rvgpu-renderer-on-wayland)
      - [Run rvgpu-proxy](#run-rvgpu-proxy)
    - [Run rvgpu-wlproxy](#run-rvgpu-wlproxy)
    - [Run Wayland Client Application Remotely](#run-wayland-client-application-remotely)

## Repository structure

```
.
├── CMakeLists.txt
├── CONTRIBUTING.md
├── LICENSE.md
├── README.md
├── common
│   ├── assertegl.c
│   ├── assertegl.h
│   ├── assertgl.c
│   ├── assertgl.h
│   ├── util_egl.c
│   ├── util_egl.h
│   ├── util_env.c
│   ├── util_env.h
│   ├── util_extension.h
│   ├── util_gles_header.h
│   ├── util_log.h
│   ├── util_render2d.c
│   ├── util_render2d.h
│   ├── util_shader.c
│   ├── util_shader.h
│   └── winsys
│       ├── winsys.h
│       ├── winsys_drm.c
│       ├── winsys_drm.h
│       ├── winsys_wayland.c
│       ├── winsys_wayland.h
│       ├── winsys_x11.c
│       └── winsys_xcb.c
├── compositor
│   ├── CMakeLists.txt
│   ├── compositor.h
│   ├── main.c
│   └── wayland_seat.c
└── third_party
    └── wayland
        └── protocols
            ├── wayland-protocol.c
            ├── wayland-server-protocol.h
            ├── xdg-shell-protocol.c
            └── xdg-shell-server-protocol.h
```
# How to install rvgpu-wlproxy

The installation instructions described here are tested on Ubuntu 20.04 LTS AMD64.
However, you can try it with different Linux distributions that support Wayland.
Assuming you have a clean Ubuntu 20.04 installed, perform the following steps.

## Building from Source

- Install the build prerequisites

  ```
  sudo apt install cmake pkg-config libegl-dev libgles-dev libwayland-dev libgbm-dev libdrm-dev libinput-dev libxkbcommon-dev libudev-dev libsystemd-dev
  ```

- Build and install rvgpu-wlproxy
  ```
  git clone https://github.com/unified-hmi/rvgpu-wlproxy.git
  cd ./rvgpu-wlproxy
  mkdir build
  cd build
  cmake -DTARGET_ENV=drm ..
  make
  sudo make install
  ```

## How to Install RVGPU
When using rvgpu-wlproxy, RVGPU is also necessary.
For instructions on how to install remove-virtio-gpu, please refer to the [README](https://github.com/unified-hmi/remote-virtio-gpu).


# How to Remotely Display Wayland Client Applications Using RVGPU
rvgpu-wlproxy enables to remotely display wayland client applications using RVGPU.

## Run RVGPU
RVGPU software consists of client (`rvgpu-proxy`) and server (`rvgpu-renderer`).
Let's describe how to run them on the same machine via the localhost interface.
We will start with `rvgpu-renderer`.
To use RVGPU, you should be able to load the kernel, so turn **Secure Boot** off.

### Run rvgpu-renderer on Wayland

`rvgpu-renderer` with Wayland backend creates a window in the Wayland environment
and renders into it.  So you should have a window system supporting Wayland protocol
(such as Gnome with Wayland protocol or Weston) running.
To make this work, you can choose from [Using Ubuntu on Wayland](#using-ubuntu-on-wayland) or [Using Ubuntu (default)](#using-ubuntu-default).


#### Using Ubuntu on Wayland
Select [Ubuntu on Wayland](https://linuxconfig.org/how-to-enable-disable-wayland-on-ubuntu-20-04-desktop) at the login screen.
Open a terminal and run this command:

```
rvgpu-renderer -b 1280x720@0,0 -p 55667
```
After this command, launching rvgpu-proxy will make the rvgpu-renderer create a window.

#### Using Ubuntu (default)
You can launch a dedicated instance of Weston and run `rvgpu-renderer`
inside it.

```
weston --width 2200 --height 1200 &
rvgpu-renderer -b 1280x720@0,0 -p 55667
```

This command will create a weston window. Launching rvgpu-proxy will make the rvgpu-renderer create nested subwindow.
The script does not require the window system uses Wayland protocol,
so it could be run under X Window system.

### Run rvgpu-proxy

`rvgpu-proxy` should be able to access the kernel modules `virtio-lo` and `virtio-gpu`
so it should be run with superuser privileges.

```
sudo -i
modprobe virtio-gpu
modprobe virtio-lo
rvgpu-proxy -s 1280x720@0,0 -n 127.0.0.1:55667
```
**Note:** Those who have performed "Building from source" please follow the instructions below.
```
rvgpu-proxy -s 1280x720@0,0 -n 127.0.0.1:55667 -c /usr/local/etc/virgl.capset
```

After you run this, another GPU node `/dev/dri/cardX` appear.
Also, if you are running `rvgpu-renderer` in Wayland mode, it create
a new window.


## Run rvgpu-wlproxy
To run rvgpu-wlproxy and display Wayland client applications remotely using RVGPU, follow the steps below using the provided options and environment variables.

- Options
  - -s size: Specify compositor window size (default: 1024x768).
  - -S socket name: Specify Wayland socket name. If NULL, it is automatically determined.
  - -f fullscreen: Send fullscreen configuration to the client application.
  - -h help: Show help message.

**Note**
`f` option is fully supported by Wayland client applications that use the xdg-shell protocol, enabling fullscreen display as intended.
However, applications using the wl-shell protocol will not enter fullscreen mode and will instead be displayed from the top-left corner of the screen.

- Environment Variables
  - EGLWINSYS_DRM_DEV_NAME: Specify the DRM device to open (default: "/dev/dri/card0").
  - EGLWINSYS_DRM_CONNECTOR_IDX: Specify which connector of the DRM device to use (default: 0).
  - EGLWINSYS_DRM_MOUSE_DEV: Specify the relative mouse event device path.
  - EGLWINSYS_DRM_MOUSEABS_DEV: Specify the absolute mouse event device path.
  - EGLWINSYS_DRM_KEYBOARD_DEV: Specify the keyboard event device path.
  - EGLWINSYS_DRM_TOUCH_DEV: Specify the touch event device path.
  - EGLWINSYS_DRM_SEAT: Specify the seat for input devices (default: "seat_virtual").
  - DEBUG_LOG: Enable debug log output.

Set environment variables as necessary:
```
export EGLWINSYS_DRM_DEV_NAME="/dev/dri/rvgpu_virtio"
export EGLWINSYS_DRM_MOUSE_DEV="/dev/input/rvgpu_mouse"
export EGLWINSYS_DRM_MOUSEABS_DEV="/dev/input/rvgpu_mouse_abs"
export EGLWINSYS_DRM_KEYBOARD_DEV="/dev/input/rvgpu_keyboard"
export EGLWINSYS_DRM_TOUCH_DEV="/dev/input/rvgpu_touch"
export XDG_RUNTIME_DIR="/tmp"
rvgpu-wlproxy -s 1280x720 -S wayland-rvgpu-0  &
```

**Note**
The window size specified for `rvgpu-wlproxy` must match the size used for `rvgpu-renderer`. This ensures that the remote display has the correct scaling and rendering.


## Run Wayland Client Application Remotely

With `rvgpu-wlproxy` running, you can start a Wayland client application:
```
export XDG_RUNTIME_DIR=/tmp
export WAYLAND_DISPLAY=wayland-rvgpu-0
glmark2-es2-wayland -s  1280x720
```

This will launch the Wayland client application with remote display capabilities enabled by `rvgpu-wlproxy`.

**Note**
It is essential to ensure the window size provided to the Wayland client application (`glmark2-es2-wayland` in this example) matches the size specified when running both `rvgpu-renderer` and `rvgpu-wlproxy`.
