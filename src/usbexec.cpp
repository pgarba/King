#include "usbexec.h"
#include "dfu.h"
#include <iostream>

static void printBuffer(std::vector<uint8_t> &V) {
#ifndef DEBUG
  return;
#endif
  printf("Buffer (%d): ", (int)V.size());
  for (int i = 0; i < V.size(); i++) {
    printf("%02X", V[i]);
  }
  printf("\n");
}

static void printBuffer(uint8_t *V, int Size) {
#ifndef DEBUG
  return;
#endif
  printf("Buffer (%d): ", (int)Size);
  for (int i = 0; i < Size; i++) {
    printf("%02X", V[i]);
  }
  printf("\n");
}

USBEXEC::USBEXEC(string serial_number) {
  this->serial_number = serial_number;
  char match_string[64];


  for (const DevicePlatform& dp : all_platforms)
  {
    sprintf(match_string, "CPID:%04x CPRV:%02x ", dp.cpid, dp.cprv);
    if (this->serial_number.find(match_string) == 0)
    {
      this->platform = &dp;
      break;
    }
  }

  if (this->platform == nullptr)
  {
    printf("[!] No matching platform found for this device!\n");
    exit(0);
  }

  vector<uint8_t> info = this->read_memory(this->image_base() + 0x200, 0x100);

  for (const ExecConfig& config : configs)
  {
    if (config.match(info.data()))
    {
      this->config = &config;
      break;
    }
  }

  if (this->config == nullptr)
  {
    printf("[!] No matching config found for this image!\n");
    exit(0);
  }
}

uint64_t USBEXEC::getDemotionReg() { return this->platform->demotion_reg; }


uint64_t USBEXEC::load_base() {
  if (serial_number.find("SRTG") != string::npos) {
    return this->platform->dfu_image_base;
  } else {
    return this->platform->dfu_load_base;
  }
}

uint64_t USBEXEC::rom_base() {
    return platform->rom_base;
}

uint64_t USBEXEC::rom_size() {
    return platform->rom_size;
}

uint64_t USBEXEC::image_base() {
  if (serial_number.find("SRTG") != string::npos) {
    return this->platform->rom_base;
  }
  else {
    return this->platform->dfu_image_base;
  }
}

uint64_t USBEXEC::cmd_arg_size() {
  if (this->platform->arch == "arm64")
    return 8;
  else
    return 4;
}

// To be removed maybe ...
uint64_t USBEXEC::cmd_arg_type() {
  if (this->platform->arch == "arm64")
    return 8;
  else
    return 4;
}

uint64_t USBEXEC::cmd_data_offset(int index) {
  return 16 + (uint64_t)index * this->cmd_arg_size();
};

uint64_t USBEXEC::cmd_data_address(int index) {
  return this->load_base() + this->cmd_data_offset(index);
}

vector<uint8_t> USBEXEC::cmd_memcpy(uint64_t dest, uint64_t src,
                                    size_t length) {
  vector<uint8_t> cmd;

  // MEMC_MAGIC [0 - 8]
  uint8_t *Start = (uint8_t *)&MEMC_MAGIC;
  uint8_t *End = Start + 8;
  cmd.insert(cmd.end(), Start, End);

  // SPACE [8 - 16]
  cmd.insert(cmd.end(), 8, 0);

  // DEST, SRC, LENGTH
  if (this->platform->arch == "arm64") {
    append(cmd, dest);
    append(cmd, src);
    append(cmd, length);
    assert(cmd.size() == 40);
  } else {
    append(cmd, (uint32_t)dest);
    append(cmd, (uint32_t)src);
    append(cmd, (uint32_t)length);
    assert(cmd.size() == 28);
  }

  return cmd;
}

vector<uint8_t> USBEXEC::command(vector<uint8_t> request_data,
                                 size_t response_length) {
  // assert(0 <= response_length <= USB_READ_LIMIT);

  DFU d;
  d.acquire_device(true);

  vector<uint8_t> Zeros;
  Zeros.insert(Zeros.end(), 16, 0);
  d.send_data(Zeros);
  d.ctrl_transfer(0x21, 1, 0, 0, 0, 0, 100);
  d.ctrl_transfer(0xA1, 3, 0, 0, 0, 6, 100);
  d.ctrl_transfer(0xA1, 3, 0, 0, 0, 6, 100);
  d.send_data(request_data);

  // HACK ?!
  vector<uint8_t> response;
  if (response_length == 0) {
    response = d.ctrl_transfer(0xA1, 2, 0xFFFF, 0, nullptr, response_length + 1,
                               CMD_TIMEOUT);
  } else {
    response = d.ctrl_transfer(0xA1, 2, 0xFFFF, 0, nullptr, response_length,
                               CMD_TIMEOUT);
  }

  d.release_device();

  return response;
}

vector<uint8_t> USBEXEC::read_memory(uint64_t address, int length) {
  vector<uint8_t> data;

  while (data.size() < length) {
    uint64_t part_length = lmin((length - data.size()),
                                (USB_READ_LIMIT - this->cmd_data_offset(0)));

    auto cmd_mcp = this->cmd_memcpy(cmd_data_address(0), address + data.size(),
                                    part_length);

    // Send to iPhone
    auto result = this->command(cmd_mcp, cmd_data_offset(0) + part_length);

    // Verify result
    printBuffer(result);

    if (result.size() < 8 && (*(uint64_t *)result.data()) != DONE_MAGIC) {
      cout << "[!] Wrong response retrieved. Aborting!\n";
    }

    // Append result
    data.insert(data.end(), result.data() + cmd_data_offset(0),
                result.data() + result.size());
  }

  printBuffer(data);

  return data;
}

void USBEXEC::write_memory(uint64_t address, vector<uint8_t> data) {
  auto cmd_mcp = this->cmd_memcpy(address, cmd_data_address(3), data.size());
  appendV(cmd_mcp, data);
  printBuffer(cmd_mcp);

  auto R = this->command(cmd_mcp, 0);
}

uint32_t USBEXEC::read_memory_uint32(uint64_t address) {
  auto value = read_memory(address, 4);
  printBuffer(value);

  assert(value.size() == 4);

  return *(uint32_t *)value.data();
}

uint64_t USBEXEC::read_memory_uint64(uint64_t address) {
  auto value = read_memory(address, 8);
  printBuffer(value);

  assert(value.size() == 8);

  return *(uint64_t *)value.data();
}

void USBEXEC::write_memory_uint32(uint64_t address, uint32_t value) {
  vector<uint8_t> Mem;
  append(Mem, value);

  this->write_memory(address, Mem);
}

void USBEXEC::aes(vector<uint8_t> data, int action, int key,
                  vector<uint8_t> &Out) {
  if (data.size() % AES_BLOCK_SIZE) {
    printf("[!] Data size is not aligned!\n");
    exit(0);
  }

  // call execute
  vector<vector<uint8_t>> args;

  // self.config.aes_crypto_cmd
  vector<uint8_t> aes_crypto_cmd;
  append(aes_crypto_cmd, this->config->aes_crypto_cmd);
  args.push_back(aes_crypto_cmd);

  // action
  vector<uint8_t> actionV;
  append(actionV, (uint64_t)action);
  args.push_back(actionV);

  // self.cmd_data_address(7)
  vector<uint8_t> cmd_data_address7;
  append(cmd_data_address7, this->cmd_data_address(7));
  args.push_back(cmd_data_address7);

  // self.cmd_data_address(0)
  vector<uint8_t> cmd_data_address0;
  append(cmd_data_address0, this->cmd_data_address(0));
  args.push_back(cmd_data_address0);

  // len(data)
  vector<uint8_t> lendata;
  append(lendata, (uint64_t)data.size());
  args.push_back(lendata);

  // len(data)
  vector<uint8_t> keyV;
  append(keyV, (uint64_t)key);
  args.push_back(keyV);

  // 0
  vector<uint8_t> Zero;
  append(Zero, (uint64_t)0);
  args.push_back(Zero);
  args.push_back(Zero);

  // data
  args.push_back(data);

  // execute command
  execute(data.size(), args, Out);
}

void USBEXEC::execute(size_t response_length, vector<vector<uint8_t>> args,
                      vector<uint8_t> &Out) {
  // Build up cmd buffer
  vector<uint8_t> cmd;

  // MEMC_MAGIC [0 - 8]
  uint8_t *Start = (uint8_t *)&EXEC_MAGIC;
  uint8_t *End = Start + 8;
  cmd.insert(cmd.end(), Start, End);

  for (auto &arg : args) {
    printBuffer(arg);
    appendV(cmd, arg);
  }

  printBuffer(cmd);

  auto result = command(cmd, cmd_data_offset(0) + response_length);

  // Verify result
  printBuffer(result);

  if (result.size() < 8 && (*(uint64_t *)result.data()) != DONE_MAGIC) {
    cout << "[!] Wrong response retrieved. Aborting!\n";
  }

  // Append result
  Out.clear();
  Out.insert(Out.end(), result.data() + cmd_data_offset(0),
             result.data() + result.size());
}
