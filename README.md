# King

Port of axi0mX's check-m8 exploit (<https://github.com/axi0mX/ipwndfu>) to C/C++

It works fine on Ubuntu 19.10 and compiles on Windows.

So far I only ported the t8010(iBoot-2696.0.0.1.33) exploit which is working fine on my iPhone 7


# Compilation

```bash
cd King
mkdir build
cmake ..
make
cp -R ../bin .
```

# Run check-m8 exploit
```
sudo ./king
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

# Limitations

I only ported the t8010 exploit and so far it fails on Windows (WIP!)
