# Running annieData on a GL.iNet Travel Router

## A Self-Contained Theater Network Hub

This guide covers turning a GL.iNet travel router into a portable,
self-contained show control hub.  The router simultaneously provides a
dedicated WiFi network for TheaterGWD sensors **and** runs the annieData
Control Center — the browser-based GUI.

The result is a single device that provides full sensor management over WiFi from any laptop or tablet — no production network required.

### Tested models

| Model | Chipset | Notes |
|-------|---------|-------|
| **Beryl (GL-MT1300)** | MIPS MT7621 | The original reference platform for this guide |
| **Beryl AX (GL-MT3000)** | ARM MediaTek Filogic 830 | Faster CPU, WiFi 6 — recommended for new setups |
| **Slate AX (GL-AXT1800)** | ARM IPQ6000 | Dual-band WiFi 6, more RAM |
| **Mango (GL-MT300N-V2)** | MIPS MT7628 | Budget option — works but slower |

Any GL.iNet router running firmware 4.x with a USB port for extroot storage will work. The steps below use the Beryl as the primary example; adjust model-specific details (flash size, default IP) for your hardware.

> **Note:** The CLI command and package name remain `gooey` / `gooey-theatergwd`.

---

## Table of Contents

1. [Tested Models](#tested-models)
2. [What You Need](#1-what-you-need)
3. [Network Architecture](#2-network-architecture)
4. [Step 1 — Check Firmware Version](#3-step-1--check-firmware-version)
5. [Step 2 — Expand Storage with Extroot](#4-step-2--expand-storage-with-extroot)
6. [Step 3 — Install Python 3](#5-step-3--install-python-3)
7. [Step 4 — Install annieData](#6-step-4--install-anniedata)
8. [Step 5 — Auto-Start Service](#7-step-5--auto-start-service)
9. [Step 6 — Open annieData](#8-step-6--open-anniedata)
10. [Step 7 — Provision TheaterGWD Sensors](#9-step-7--provision-theatergwd-sensors)
11. [Connecting to an Existing Show Network](#10-connecting-to-an-existing-show-network)
12. [Firewall Notes](#11-firewall-notes)
13. [Troubleshooting](#12-troubleshooting)

---

## 1. What You Need

| Item | Notes |
|------|-------|
| GL.iNet travel router | See [tested models](#tested-models) above |
| USB 3.0 flash drive or SSD | ≥ 8 GB, formatted as ext4 (done during setup) |
| USB-C power supply or power bank | 5 V / 2 A minimum |
| Laptop or tablet | Any modern browser; used during initial setup via Ethernet or WiFi |
| TheaterGWD sensor(s) | Already assembled and ready to provision |

**Why the USB drive?**  Most GL.iNet routers ship with limited onboard flash
(32–128 MB).  Python 3 plus Flask and its dependencies require roughly 60–80 MB.
The USB drive becomes the router's `/overlay` filesystem through a procedure
called *extroot* — effectively giving the router a full-size writable root
filesystem.

---

## 2. Network Architecture

```
┌─────────────────────────────────────────────────────┐
│                GL.iNet Router                        │
│                                                      │
│  WiFi AP  ←──  TheaterGWD sensors (192.168.8.x)     │
│  WiFi AP  ←──  Laptop / tablet                      │
│                                                      │
│  annieData running on http://192.168.8.1:5000        │
│  OSC listen port  8000 (configurable)               │
└─────────────────────────────────────────────────────┘
```

- The default LAN address on GL.iNet routers is **192.168.8.1**.
- All TheaterGWD sensors provision against the router's WiFi SSID and send OSC
  to the router's address.
- Any laptop on the same WiFi opens `http://192.168.8.1:5000` to reach annieData.
- The router's WAN port can still be plugged into a production Ethernet network
  for internet access or show-network forwarding — this does not affect the
  local 192.168.8.0/24 subnet where sensors and annieData live.

---

## 3. Step 1 — Check Firmware Version

1. Power on the router and connect to its WiFi (default SSID is on the label).
2. Open `http://192.168.8.1` in a browser.
3. Log in (default password is on the label).
4. Navigate to **System → Upgrade** and check your firmware version.
5. Install the latest stable GL.iNet firmware if you are not already on 4.x.
   Firmware 4.x ships with Python 3 in the opkg package tree.

---

## 4. Step 2 — Expand Storage with Extroot

SSH into the router.  The default address is `root@192.168.8.1` — the
password matches the admin password set in the web UI.

```bash
ssh root@192.168.8.1
```

Insert the USB drive.  It appears as `/dev/sda` (or `/dev/sda1` if it
already has a partition table).  The commands below create a fresh ext4
partition and configure extroot.

> **Warning:** The following erases all data on the USB drive.

```bash
# Install partitioning tools
opkg update
opkg install block-mount kmod-fs-ext4 kmod-usb-storage e2fsprogs fdisk

# Identify the USB device
block info

# Create a single ext4 partition (replace /dev/sda with your device if different)
fdisk /dev/sda <<EOF
o
n
p
1


w
EOF

mkfs.ext4 /dev/sda1
```

Set up the overlay:

```bash
DEVICE="/dev/sda1"
eval $(block info "${DEVICE}" | grep -o -e "UUID=\S*")
uci -q delete fstab.extroot
uci set fstab.extroot="mount"
uci set fstab.extroot.uuid="${UUID}"
uci set fstab.extroot.target="/overlay"
uci commit fstab
```

Copy the current overlay to the USB drive so nothing is lost:

```bash
mount /dev/sda1 /mnt
tar -C /overlay -cvf - . | tar -C /mnt -xf -
umount /mnt
```

Reboot:

```bash
reboot
```

After rebooting, verify extroot is active:

```bash
df -h /overlay
# Should show the USB drive size, not 32 MB
```

---

## 5. Step 3 — Install Python 3

With extroot active, there is now enough space for Python:

```bash
opkg update
opkg install python3 python3-pip
```

Verify:

```bash
python3 --version
pip3 --version
```

---

## 6. Step 4 — Install annieData

Install directly from PyPI:

```bash
pip3 install gooey-theatergwd
```

This pulls in Flask, Flask-SocketIO, python-osc, and Markdown — all pure
Python, no compilation required.

Verify:

```bash
gooey --help
```

---

## 7. Step 5 — Auto-Start Service

Create a procd init script so annieData starts automatically every time the
router boots:

```bash
cat > /etc/init.d/gooey << 'EOF'
#!/bin/sh /etc/rc.common

USE_PROCD=1
START=99
STOP=10

GOOEY_PORT=5000
GOOEY_HOST=0.0.0.0

start_service() {
    procd_open_instance
    procd_set_param command gooey \
        --host "$GOOEY_HOST" \
        --port "$GOOEY_PORT" \
        --no-browser
    procd_set_param respawn "${respawn_threshold:-3600}" "${respawn_timeout:-5}" "${respawn_retry:-5}"
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_close_instance
}

stop_service() {
    return 0
}
EOF

chmod +x /etc/init.d/gooey
/etc/init.d/gooey enable
/etc/init.d/gooey start
```

Check that annieData is running:

```bash
/etc/init.d/gooey status
# or
ps | grep gooey
```

To view live logs:

```bash
logread -f | grep gooey
```

---

## 8. Step 6 — Open annieData

1. On any device connected to the Beryl's WiFi, open a browser and go to:

   ```
   http://192.168.8.1:5000
   ```

2. The annieData Control Center appears.  No further configuration is needed on
   the router.

---

## 9. Step 7 — Provision TheaterGWD Sensors

Each sensor needs to know the WiFi network and the address of the OSC host
(i.e., the router).

1. Power on a sensor.  It creates an **"annieData Setup"** WiFi network.
2. Connect to that WiFi.  The captive portal opens automatically.
3. Fill in:

   | Field | Value |
   |-------|-------|
   | WiFi network name (SSID) | The router's WiFi name |
   | WiFi password | The router's WiFi password |
   | Static IP | A unique address on the subnet, e.g. `192.168.8.101` |
   | Port | `8000` (the port the sensor listens on for OSC commands) |
   | Device name | A short label, e.g. `bart` |

4. Press **Submit**.  The sensor reboots and joins the router's WiFi.
5. In annieData, open the **Devices** panel, enter `192.168.8.101:8000` and click
   **Query** — the device appears.
6. Repeat for additional sensors, incrementing the static IP each time.

---

## 10. Connecting to an Existing Show Network

If the production environment includes an existing network (wired LAN, lighting
console, audio system), connect the router's **WAN** port to that network.

- Sensors and annieData stay on the router's LAN (192.168.8.0/24).
- The router forwards traffic between the LAN and the production network.
- OSC messages from annieData to external consoles go out the WAN port.
- External consoles access annieData at the router's WAN IP on port 5000
  (check the router's status page for its WAN IP, or assign a fixed IP via
  DHCP reservation on the production network).

To allow external access to annieData, add a firewall rule:

```bash
# Allow TCP port 5000 from WAN (optional — only if external consoles need it)
uci add firewall rule
uci set firewall.@rule[-1].src=wan
uci set firewall.@rule[-1].dest_port=5000
uci set firewall.@rule[-1].proto=tcp
uci set firewall.@rule[-1].target=ACCEPT
uci commit firewall
/etc/init.d/firewall restart
```

---

## 11. Firewall Notes

By default the router's firewall **drops** all inbound traffic from the WAN
side.  Traffic within the LAN (192.168.8.0/24) is unrestricted, so sensors
and annieData can communicate freely without any additional firewall changes.

If sensors need to send OSC to an external console on the WAN side, ensure
the console's firewall allows inbound UDP on the configured OSC port.

---

## 12. Troubleshooting

### annieData does not start

Check for errors in the system log:

```bash
logread | grep -i gooey
```

Verify the `gooey` binary is in the PATH:

```bash
which gooey
# Expected: /usr/bin/gooey
```

If it is missing, reinstall:

```bash
pip3 install --force-reinstall gooey-theatergwd
```

### `df` still shows 32 MB after reboot

Extroot did not activate.  Run `block info` to confirm the UUID matches what
is in `/etc/config/fstab`:

```bash
block info /dev/sda1
cat /etc/config/fstab
```

Correct any mismatch and reboot again.

### Sensor does not appear in annieData

1. Confirm the sensor joined the correct WiFi (check the router's **Clients**
   list at `http://192.168.8.1`).
2. Ping the sensor from the router: `ping 192.168.8.101`
3. Ensure the port in annieData's query field matches the port the sensor was
   provisioned with.
4. Check that no firewall rule is blocking UDP between LAN hosts (none should
   be by default).

### Slow performance or high CPU

Budget MIPS-based models (Beryl, Mango) have modest processors.  If annieData
feels sluggish under heavy OSC traffic, reduce the number of active messages or
lower sensor broadcast rates.  ARM-based models (Beryl AX, Slate AX) handle
higher loads.  In all cases the router is designed as a management hub, not a
high-throughput OSC relay.

### Updating annieData

```bash
pip3 install --upgrade gooey-theatergwd
/etc/init.d/gooey restart
```
