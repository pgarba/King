#ifndef USBEXEC_H
#define USBEXEC_H

#include <vector>
#include <string>
#include <stdint.h>
#include <assert.h>

using namespace std;


char EXEC_MAGIC[] = "execexec";
char DONE_MAGIC[] = "donedone";
char MEMC_MAGIC[] = "memcmemc";
char MEMS_MAGIC[] = "memsmems";

// Patch to make it work on ubuntu
int USB_READ_LIMIT = 0xFF0;
int CMD_TIMEOUT     = 5000;
int AES_BLOCK_SIZE  = 16;
int AES_ENCRYPT     = 16;
int AES_DECRYPT     = 17;
int AES_GID_KEY     = 0x20000200;
int AES_UID_KEY     = 0x20000201;

class DevicePlatform {
public:
	DevicePlatform(int cpid, int cprv,	int scep, string arch, string srtg,	uint64_t rom_base,	uint64_t rom_size, string rom_sha1, uint64_t sram_base, uint64_t sram_size, uint64_t dram_base, int nonce_length, int sep_nonce_length, uint64_t demotion_reg) {
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

		if (cpid == 0x8010, 0x8011) {
			dfu_image_base      = 0x1800B0000;
			dfu_load_base       = 0x800000000;
			recovery_image_base = 0x1800B0000;
			recovery_load_base  = 0x800000000;
		} else {
			assert("Not implemented");
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
	ExecConfig(string SecureROMVersion, string Type, string IBootVersion, uint64_t aes_crypto_cmd) {
		this->SecureROMVersion = SecureROMVersion;
		this->Type = Type;
		this->IBootVersion = IBootVersion;
		this->aes_crypto_cmd = aes_crypto_cmd;
	}

	bool match(string info) {
		// return info == self.info[0].ljust(0x40, '\0') + self.info[1].ljust(0x40, '\0') + self.info[2].ljust(0x80, '\0')
		return false;
	}

private:
	string SecureROMVersion;
	string Type;
	string IBootVersion;
	uint64_t aes_crypto_cmd;
};

class USBEXEC {
	USBEXEC(string serial_number) {
		this->serial_number = serial_number;
	}

public:
	void write_memory(uint64_t address, vector<uint8_t> data);
	vector<uint8_t> read_memory(uint64_t address, int length);

	vector<uint8_t> command(vector<uint8_t> request_data, int response_length);

	uint64_t cmd_arg_type();
	uint64_t cmd_arg_size();
	uint64_t cmd_data_address(int index);
	vector<uint8_t> cmd_memcpy(uint8_t *dest, uint64_t *src, size_t length);
	vector<uint8_t> cmd_memset(uint8_t *address, uint8_t c, size_t length);

	uint64_t load_base() {
		if (serial_number.find("SRGT") != string::npos) {
			return all_platforms[0].dfu_image_base;
		} else {
			return all_platforms[0].dfu_load_base;
		}
	}
private:
	string serial_number;
	vector<ExecConfig> configs = { ExecConfig("SecureROM for t8010si, Copyright 2007-2015, Apple Inc.", "ROMRELEASE",  "iBoot-2696.0.0.1.33",   0x10000C8F4) };
	vector<DevicePlatform> all_platforms = { DevicePlatform(0x8010, 0x11, 0x01, "arm64", "iBoot-2696.0.0.1.33",
    0x100000000, 0x20000, "41a488b3c46ff06c1a2376f3405b079fb0f15316",
    0x180000000, 0x200000,
    0x800000000,
    32, 20,
    0x2102BC000) };
};

#endif
