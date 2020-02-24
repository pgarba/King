#ifndef USBEXEC_H
#define USBEXEC_H

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

using namespace std;

const uint64_t EXEC_MAGIC = 0x6578656365786563; // 'execexec';
const uint64_t DONE_MAGIC = 0x646f6e65646f6e65; // "donedone";
const uint64_t MEMC_MAGIC = 0x6D656D636D656D63; // 'memcmemc';
const uint64_t MEMS_MAGIC = 0x6D656D736D656D73; // "memsmems";

// Patch to make it work on ubuntu
const int USB_READ_LIMIT = 0xFF0;
const int CMD_TIMEOUT = 5000;
const int AES_BLOCK_SIZE = 16;
const int AES_ENCRYPT = 16;
const int AES_DECRYPT = 17;
const int AES_GID_KEY = 0x20000200;
const int AES_UID_KEY = 0x20000201;

class DevicePlatform {
public:
  DevicePlatform(int cpid, int cprv, int scep, string arch, string srtg,
                 uint64_t rom_base, uint64_t rom_size, string rom_sha1,
                 uint64_t sram_base, uint64_t sram_size, uint64_t dram_base,
                 int nonce_length, int sep_nonce_length,
                 uint64_t demotion_reg) {
    this->cpid = cpid;
    this->cprv = cprv;
    this->scep = scep;
    this->arch = arch;
    this->srtg = srtg;
    this->rom_base = rom_base;
    this->rom_size = rom_size;
    this->rom_sha1 = rom_sha1;
    this->sram_base = sram_base;
    this->sram_size = sram_size;
    this->dram_base = dram_base;
    this->nonce_length = nonce_length;
    this->sep_nonce_length = sep_nonce_length;
    this->demotion_reg = demotion_reg;

    if (cpid == 0x8010)
    {
      this->dfu_image_base = 0x1800B0000;
      this->dfu_load_base = 0x800000000;
      this->recovery_image_base = 0x1800B0000;
      this->recovery_load_base = 0x800000000;
    }
    else if (cpid == 0x7000 || cpid == 0x8000 || cpid == 0x8003)
    {
      this->dfu_image_base = 0x180380000;
      this->dfu_load_base = 0x180000000;
      this->recovery_image_base = 0x83D7F7000;
      this->recovery_load_base = 0x800000000;
    }
    else
    {
      assert(0 && "Not implemented");
    }
  }

  int cpid;
  int cprv;
  int scep;
  string arch;
  string srtg;
  uint64_t rom_base;
  uint64_t rom_size;
  string rom_sha1;
  uint64_t sram_base;
  uint64_t sram_size;
  uint64_t dram_base;
  int nonce_length;
  int sep_nonce_length;
  uint64_t demotion_reg;
  uint64_t dfu_image_base;
  uint64_t dfu_load_base;
  uint64_t recovery_image_base;
  uint64_t recovery_load_base;
};

class ExecConfig {
public:
  ExecConfig(string SecureROMVersion, string Type, string IBootVersion,
             uint64_t aes_crypto_cmd) {
    this->SecureROMVersion = SecureROMVersion;
    this->Type = Type;
    this->IBootVersion = IBootVersion;
    this->aes_crypto_cmd = aes_crypto_cmd;
  }

  bool match(uint8_t * info) const {
    uint8_t self_match[0x100];
    memset(self_match, 0, sizeof(self_match));
    strcpy((char*)&self_match[0], this->SecureROMVersion.c_str());
    strcpy((char*)&self_match[0x40], this->Type.c_str());
    strcpy((char*)&self_match[0x80], this->IBootVersion.c_str());
    return memcmp(self_match, info, sizeof(self_match)) == 0;
  }

  string SecureROMVersion;
  string Type;
  string IBootVersion;
  uint64_t aes_crypto_cmd;
};

class USBEXEC {
public:
  USBEXEC(string serial_number);

  void write_memory(uint64_t address, vector<uint8_t> data);
  vector<uint8_t> read_memory(uint64_t address, int length);

  vector<uint8_t> command(vector<uint8_t> request_data, size_t response_length);

  uint64_t cmd_arg_type();
  uint64_t cmd_arg_size();

  uint64_t cmd_data_offset(int index);
  uint64_t cmd_data_address(int index);

  /*
        Build up the memcpy command that will be send to the iPhone
  */
  vector<uint8_t> cmd_memcpy(uint64_t dest, uint64_t src, size_t length);

  /*
        Build up the memset command that will be send to the iPhone
  */
  vector<uint8_t> cmd_memset(uint8_t *address, uint8_t c, size_t length);

  /*
        Read memory helper
  */
  uint32_t read_memory_uint32(uint64_t address);
  uint64_t read_memory_uint64(uint64_t address);

  /*
        Write memory helper
  */
  void write_memory_uint32(uint64_t address, uint32_t value);

  uint64_t rom_base();
  uint64_t rom_size();
  /*
        Load the image base
  */
  uint64_t load_base();

  /*
        Image base address
  */
  uint64_t image_base();

  /*
        Get the demotion reg offset
  */
  uint64_t getDemotionReg();

  /*
        Run the aes engine
  */
  void aes(vector<uint8_t> data, int action, int key, vector<uint8_t> &Out);

  /*
        Execute code
  */
  void execute(size_t response_length, vector<vector<uint8_t>> args,
               vector<uint8_t> &Out);

private:
  string serial_number;
  vector<ExecConfig> configs = {
      ExecConfig("SecureROM for t8010si, Copyright 2007-2015, Apple Inc.",
                 "ROMRELEASE", "iBoot-2696.0.0.1.33", 0x10000C8F4),
      ExecConfig("SecureROM for t7000si, Copyright 2013, Apple Inc.",
                 "RELEASE", "iBoot-1992.0.0.1.19", 0x10000DA90),
      ExecConfig("SecureROM for s8000si, Copyright 2007-2014, Apple Inc.",
                 "RELEASE", "iBoot-2234.0.0.3.3", 0x10000DAA0),
      ExecConfig("SecureROM for s8003si, Copyright 2007-2014, Apple Inc.",
                 "RELEASE", "iBoot-2234.0.0.2.22", 0x10000DAA0),
  };
  vector<DevicePlatform> all_platforms = {
      DevicePlatform(0x8010, 0x11, 0x01, "arm64", "iBoot-2696.0.0.1.33",
                     0x100000000, 0x20000, "41a488b3c46ff06c1a2376f3405b079fb0f15316",
                     0x180000000, 0x200000, 0x800000000,
                     32, 20, 0x2102BC000),
      DevicePlatform(0x7000, 0x11, 0x01, "arm64", "iBoot-1992.0.0.1.19",
                     0x100000000, 0x80000, "c4dcd22ae135c14244fc2b62165c85effa566bfe",
                     0x180000000, 0x400000, 0x800000000,
                     20, 20, 0x20E02A000),
      DevicePlatform(0x8000, 0x20, 0x01, "arm64", "iBoot-2234.0.0.3.3",
                     0x100000000, 0x80000, "9979dce30e913c888cf77234c7a7e2a7fa676f4c",
                     0x180000000, 0x400000, 0x800000000,
                     32, 20, 0x2102BC000),
      DevicePlatform(0x8000, 0x20, 0x01, "arm64", "iBoot-2234.0.0.2.22",
                     0x100000000, 0x80000, "93d69e2430e2f0c161e3e1144b69b4da1859169b",
                     0x180000000, 0x400000, 0x800000000,
                     32, 20, 0x2102BC000)
  };

  const ExecConfig *config;
  const DevicePlatform *platform;
};

#endif
