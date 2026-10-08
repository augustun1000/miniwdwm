# minidwc

A deliberately small Linux-first Wayland compositor using neuswc.

It is tiling-only for now and implements dwm-style tags (1-9).
There is intentionally no floating, mouse move/resize, IPC, bar, or autostart yet.

## Keys

- `Super+t`: launch `foot`
- `Super+j`: focus next tiled client
- `Super+k`: focus previous tiled client
- `Super+Shift+q`: close selected client
- `Super+Shift+e`: quit compositor
- `Super+1..9`: view tag
- `Super+Ctrl+1..9`: toggle tag in current view
- `Super+Shift+1..9`: move selected client to tag
- `Super+Ctrl+Shift+1..9`: toggle selected client's membership in tag
- `Super+0`: view all tags
- `Super+Shift+0`: put selected client on all tags

## Layout

With one visible client it fills the usable output area, respecting outer gaps.
With two or more clients, the first visible client is the 60% master and the
remaining clients are vertically stacked in the remaining 40%.

## Build on Linux

Dependencies are intentionally the same pkg-config set currently used by DWC.

```sh
bmake clean
bmake
sudo bmake install
```

Start it similarly to DWC, for example:

```sh
XDG_SESSION_TYPE=wayland \
GDK_BACKEND=wayland \
QT_QPA_PLATFORM=wayland \
XKB_DEFAULT_LAYOUT=es \
swc-launch minidwc
```

If you need your display override:

```sh
WLR_WL_OUTPUTS=1366x768 swc-launch minidwc
```

## Design

Applications -> minidwc -> neuswc -> neuwld/DRM/KMS -> Linux GPU

The compositor itself only talks to the `swc` API. It does not need to call
neuwld directly for normal window management and tiling.
