/* nsemu - LGPL - Copyright 2018 rkx1209<rkx1209dev@gmail.com> */
/* TODO: Move host dependent functions to host_util.hpp */
#ifdef _WIN32
#include <winsock2.h>
#else
#include <errno.h>
#include <netinet/in.h>
#include <sys / types.h>
#include <sys/socket.h>
#endif

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdio.h>

#include "GdbStub.h"

namespace GdbStub {

static int server_fd, client_fd;

uint8_t cmd_buf[GDB_BUFFER_SIZE], last_packet[GDB_BUFFER_SIZE + 4];

static int buf_index, line_sum, checksum, last_packet_len;

volatile bool enabled = false;
volatile bool step = false;
volatile bool cont = false;

static RSState state = RS_IDLE;

static inline bool IsXdigit(char ch) {
  return ('a' <= ch && ch <= 'f') || ('A' <= ch && ch <= 'F') ||
         ('0' <= ch && ch <= '9');
}

static inline int FromHex(int v) {
  if (v >= '0' && v <= '9')
    return v - '0';
  else if (v >= 'A' && v <= 'F')
    return v - 'A' + 10;
  else if (v >= 'a' && v <= 'f')
    return v - 'a' + 10;
  else
    return 0;
}

static inline int ToHex(int v) {
  if (v < 10)
    return v + '0';
  else
    return v - 10 + 'a';
}

static void MemToHex(char *buf, const uint8_t *mem, int len) {
  char *q;
  q = buf;
  for (int i = 0; i < len; i++) {
    int c = mem[i];
    *q++ = ToHex(c >> 4);
    *q++ = ToHex(c & 0xf);
  }
  *q = '\0';
}

static void hextomem(uint8_t *mem, const char *buf, int len) {
  int i;

  for (i = 0; i < len; i++) {
    mem[i] = (FromHex(buf[0]) << 4) | FromHex(buf[1]);
    buf += 2;
  }
}

void Init() {
  struct sockaddr_in addr;

  // Initialize Winsock
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    printf("WSAStartup failed with error: %d\n", iResult);
    return;
  }

  server_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    printf("socket failed Error: %i\n", WSAGetLastError());
    return;
  }
  addr.sin_family = AF_INET;
  addr.sin_port = htons(GDB_SERVER_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (::bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    printf("bind failed\n");
    return;
  }

  if (listen(server_fd, 5) != 0) {
    printf("listen failed\n");
    return;
  }

  printf("Waiting for connection on port %i ...\n", GDB_SERVER_PORT);

  client_fd = accept(server_fd, nullptr, nullptr);

  closesocket(server_fd);

  if (client_fd < 0) {
    printf("Failed to accept gdb client\n");
    iResult = shutdown(client_fd, SD_SEND);
    return;
  }
  enabled = true;
}

uint8_t ReadByte() {
  uint8_t byte;
  auto size = recv(client_fd, (char *)&byte, 1, MSG_WAITALL);
  if (size != 1) {
    printf("Failed recv :%ld\n", size);
    enabled = false;
    shutdown(client_fd, SD_SEND);
    return 0;
  }
  return byte;
}

void WriteBytes(uint8_t *buf, size_t len) {
  if (send(client_fd, (const char *)buf, len, 0) == -1) {
    printf("Failed send\n");
    enabled = false;
    shutdown(client_fd, SD_SEND);
  }
}

void SendPacket(uint8_t *buf, size_t len) {
  int csum;
  uint8_t *p;
  for (;;) {
    p = last_packet;
    *(p++) = '$';
    memcpy(p, buf, len);
    p += len;
    csum = 0;
    for (int i = 0; i < len; i++) {
      csum += buf[i];
    }
    *(p++) = '#';
    *(p++) = ToHex((csum >> 4) & 0xf);
    *(p++) = ToHex((csum)&0xf);

    last_packet_len = p - last_packet;
    WriteBytes(last_packet, last_packet_len);
    break;
  }
}

void WritePacket(std::string buf) {
  SendPacket((uint8_t *)buf.c_str(), buf.size());
}

static int IsQueryPacket(const char *p, const char *query, char separator) {
  unsigned int query_len = strlen(query);

  return strncmp(p, query, query_len) == 0 &&
         (p[query_len] == '\0' || p[query_len] == separator);
}

static int ReadRegister(uint8_t *buf, int reg, AAUC &U) {
  /*
  if (reg < 31) {
    uc_reg_read(U.uc, UC_ARM64_REG_X0 + reg, buf);
  } else {
    switch (reg) {
    case 31:
      uc_reg_read(U.uc, U.StackRegister, buf);
      break;
    case 32:
      uc_reg_read(U.uc, U.ProgramCounterRegister, buf);
      break;
    default:
      printf("Unknown reg %i\n", reg);
      *(uint64_t *)buf = 0xdeadbeef;
      break;
    }
  }
  */
  return sizeof(uint64_t);
}

static int WriteRegister(uint64_t *buf, int reg, AAUC &U) {
  /*
  if (reg < 31) {
    uc_reg_write(U.uc, UC_ARM64_REG_X0 + reg, buf);
  } else {
    switch (reg) {
    case 31:
      uc_reg_write(U.uc, U.StackRegister, buf);
      break;
    case 32:
      uc_reg_write(U.uc, U.ProgramCounterRegister, buf);
      break;
    default:
      printf("Unknown reg %i\n", reg);
      break;
    }
  }
  */
  return sizeof(uint64_t);
}

static bool ValidAddress(uint64_t addr, AAUC &U) {
  bool AddrOk = false;

  /*
  uint32_t count;
  uc_mem_region *regions;
  int err_count = 0;
  uc_err err = uc_mem_regions(U.uc, &regions, &count);


  for (int i = 0; i < count; i++) {
    if (addr >= regions[i].begin && addr <= regions[i].end) {
      AddrOk = true;
      break;
    }
  }
  uc_free(regions);
  */
  return AddrOk;
}

static int TargetMemoryRW(uint64_t addr, uint8_t *buf, int len, bool is_write,
                          AAUC &U) {
  if (ValidAddress(addr, U) == false) {
    return -1;
  }
  /*
  if (is_write) {
    uc_mem_write(U.uc, addr, buf, len);
  } else {
    uc_mem_read(U.uc, addr, buf, len);
  }
  */
  return 0;
}

static void SendSignal(uint32_t signal) {
  char buf[GDB_BUFFER_SIZE];
  snprintf(buf, sizeof(buf), "T%02xthread:%02x;", signal, 1);
  WritePacket(buf);
}

void Trap() {
  cont = false;
  SendSignal(GDB_SIGNAL_TRAP);
}

void Interrupt() {
  cont = false;
  SendSignal(GDB_SIGNAL_INT);
}

static void Stepi() { step = true; }

static int SwBreakpointInsert(unsigned long addr, unsigned long len, int type,
                              AAUC &U) {
  std::stringstream stream;
  stream << std::hex << addr;
  std::string strHexAddr(stream.str());

  /*
  Tokens T;
  T.push_back("bp");
  T.push_back(strHexAddr);
  cmd_SetBreakpoint(T, U);
  */

  printf("[Add bp] 0x%lx, %u, %d\n", addr, len, type);

  return 0;
}

static int SwBreakpointRemove(unsigned long addr, unsigned long len, int type,
                              AAUC &U) {
  /*
  int Bp = isBreakpointSet(addr);

  Tokens T;
  T.push_back("bp");
  T.push_back(std::to_string(Bp));
  cmd_ClearBreakpoint(T, U);
  */
  printf("[Remove bp] 0x%lx, %u, %d\n", addr, len, type);

  return 0;
}

void HitBreakpoint(unsigned long addr, int type) {
  cont = false;
  step = false;

  Trap();
}

static int WatchpointInsert(unsigned long addr, unsigned long len, int type) {
  /* Watchpoint wp(addr, len, type);
   wp_list.push_back(wp);
   printf("[Add wp] 0x%lx, %u, %d\n", addr, len, type);
           */
  return 0;
}

static int WatchpointRemove(unsigned long addr, unsigned long len, int type) {
  /*if (wp_list.empty()) {
          return -1;
  }

  auto wp_it = std::find(wp_list.begin(), wp_list.end(), Watchpoint(addr, len,
  type)); if (wp_it == wp_list.end()) { printf ("Watchpoint not found\n");
          return -1;
  }
  printf("[Remove wp] 0x%lx, %u, %d\n", addr, len, type);
  wp_list.erase(wp_it);
          */
  return 0;
}

void HitWatchpoint(unsigned long addr, int type) {
  cont = false;
  step = false;
  char buf[GDB_BUFFER_SIZE];
  const char *type_s;
  if (type == GDB_WATCHPOINT_READ) {
    type_s = "r";
  } else if (type == GDB_WATCHPOINT_ACCESS) {
    type_s = "a";
  } else {
    type_s = "";
  }

  snprintf(buf, sizeof(buf), "T%02xthread:%02x;%swatch:%lx;", GDB_SIGNAL_TRAP,
           1, type_s, addr);
  WritePacket(buf);
}

void NotifyMemAccess(unsigned long addr, size_t len, bool read) {
  /*int type = read ? GDB_WATCHPOINT_READ : GDB_WATCHPOINT_WRITE;
  for (int i = 0; i < wp_list.size(); i++) {
          Watchpoint wp = wp_list[i];
          if (addr <= wp.addr && wp.addr + wp.len <= addr + len) {
                  if (wp.type == GDB_WATCHPOINT_ACCESS || wp.type == type) {
                          HitWatchpoint (addr, wp.type);
                          return;
                  }
          }
  }
          */
}

static int BreakpointInsert(unsigned long addr, unsigned long len, int type,
                            AAUC &U) {
  int err = -1;
  switch (type) {
  case GDB_BREAKPOINT_SW:
  case GDB_BREAKPOINT_HW:
    return SwBreakpointInsert(addr, len, type, U);
  case GDB_WATCHPOINT_WRITE:
  case GDB_WATCHPOINT_READ:
  case GDB_WATCHPOINT_ACCESS:
    return WatchpointInsert(addr, len, type);
  default:
    break;
  }
  return err;
}

static int BreakpointRemove(unsigned long addr, unsigned long len, int type,
                            AAUC &U) {
  int err = -1;
  switch (type) {
  case GDB_BREAKPOINT_SW:
  case GDB_BREAKPOINT_HW:
    return SwBreakpointRemove(addr, len, type, U);
  case GDB_WATCHPOINT_WRITE:
  case GDB_WATCHPOINT_READ:
  case GDB_WATCHPOINT_ACCESS:
    return WatchpointRemove(addr, len, type);
    break;
  default:
    break;
  }
  return err;
}

static void Continue() { cont = true; }

static RSState HandleCommand(char *line_buf, AAUC &U) {
  const char *p;
  uint32_t thread;
  int ch, reg_size, type, res;
  char buf[GDB_BUFFER_SIZE];
  uint8_t mem_buf[GDB_BUFFER_SIZE];
  unsigned long addr, len;

  p = line_buf;
  ch = *p++;

  // printf("Command: %c\n", ch);
  // printf("%s\n", line_buf);

  switch (ch) {
  case '?':
    /* XXX: Is it correct to fix thread id to '1'? */
    SendSignal(GDB_SIGNAL_TRAP);
    break;
  case 'c':
    if (*p != '\0') {
      addr = strtol(p, (char **)&p, 16);
    }
    Continue();
    break;
  case 'g':
    len = 0;
    for (addr = 0; addr < 33; addr++) {
      reg_size = ReadRegister(mem_buf + len, addr, U);
      len += reg_size;
    }
    MemToHex(buf, mem_buf, len);
    WritePacket(buf);
    break;
  case 'G':
    len = 0;
    hextomem((uint8_t *)mem_buf, p, strlen(p) / 2);
    for (addr = 0; addr < 33; addr++) {
      reg_size = WriteRegister((uint64_t *)(mem_buf + len), addr, U);
      len += reg_size;
    }
    WritePacket("OK");
  case 'm':
    addr = strtoul(p, (char **)&p, 16);
    if (*p == ',')
      p++;
    len = strtol(p, NULL, 16);

    /* memtohex() doubles the required space */
    if (len > GDB_BUFFER_SIZE / 2) {
      WritePacket("E22");
      break;
    }

    if (TargetMemoryRW(addr, mem_buf, len, false, U) != 0) {
      WritePacket("E14");
    } else {
      MemToHex(buf, mem_buf, len);
      WritePacket(buf);
    }
    break;
  case 'M':
    addr = strtoull(p, (char **)&p, 16);
    if (*p == ',')
      p++;
    len = strtoull(p, (char **)&p, 16);
    if (*p == ':')
      p++;
    hextomem(mem_buf, p, len);
    if (TargetMemoryRW(addr, mem_buf, len, true, U) != 0) {
      WritePacket("E14");
    } else {
      WritePacket("OK");
    }
    break;
  case 'p':
    addr = strtol(p, (char **)&p, 16);
    reg_size = ReadRegister(mem_buf, addr, U);
    if (reg_size) {
      MemToHex(buf, mem_buf, reg_size);
      WritePacket(buf);
    } else {
      WritePacket("E14");
    }
    break;
  case 'q':
  case 'Q':
    if (IsQueryPacket(p, "Supported", ':')) {
      snprintf(buf, sizeof(buf), "PacketSize=%x", GDB_BUFFER_SIZE);
      WritePacket(buf);
      break;
    } else if (strcmp(p, "C") == 0) {
      WritePacket("QC1");
      break;
    }
    goto unknown_command;
  case 'z':
  case 'Z':
    type = strtol(p, (char **)&p, 16);
    if (*p == ',')
      p++;
    addr = strtol(p, (char **)&p, 16);
    if (*p == ',')
      p++;
    len = strtol(p, (char **)&p, 16);
    if (ch == 'Z') {
      res = BreakpointInsert(addr, len, type, U);
    } else {
      res = BreakpointRemove(addr, len, type, U);
    }
    if (res >= 0)
      WritePacket("OK");
    else
      WritePacket("E22");
    break;
  case 's':
    Stepi();
    break;
  case 'H':
    type = *p++;
    thread = strtol(p, (char **)&p, 16);
    if (thread == -1 || thread == 0) {
      WritePacket("OK");
      break;
    }
    switch (type) {
    case 'c':
      WritePacket("OK");
      break;
    case 'g':
      WritePacket("OK");
      break;
    default:
      WritePacket("E22");
      break;
    }
    break;
  case 'T':
    thread = strtol(p, (char **)&p, 16);
    WritePacket("OK");
    break;
  default:
  unknown_command:
    /* put empty packet */
    buf[0] = '\0';
    WritePacket(buf);
    break;
  }
  return RS_IDLE;
}

void HandlePacket(AAUC &U) {
  uint8_t ch = ReadByte();

  uint8_t reply;
  switch (state) {
  case RS_IDLE:
    if (ch == '$') {
      /* start of command packet */
      buf_index = 0;
      line_sum = 0;
      state = RS_GETLINE;
    } else {
      // printf("%s: received garbage between packets: 0x%x\n", __func__, ch);
    }
    break;
  case RS_GETLINE:
    if (ch == '}') {
      state = RS_GETLINE_ESC;
      line_sum += ch;
    } else if (ch == '*') {
      /* start run length encoding sequence */
      state = RS_GETLINE_RLE;
      line_sum += ch;
    } else if (ch == '#') {
      /* end of command, start of checksum*/
      state = RS_CHKSUM1;
    } else if (buf_index >= sizeof(cmd_buf) - 1) {
      state = RS_IDLE;
      printf("Gdb: command buffer overrun, dropping command\n");
    } else {
      /* unescaped command character */
      cmd_buf[buf_index++] = ch;
      line_sum += ch;
    }
    break;
  case RS_GETLINE_ESC:
    if (ch == '#') {
      /* unexpected end of command in escape sequence */
      state = RS_CHKSUM1;
    } else if (buf_index >= sizeof(cmd_buf) - 1) {
      /* command buffer overrun */
      state = RS_IDLE;
      printf("Gdb: command buffer overrun, dropping command\n");
    } else {
      /* parse escaped character and leave escape state */
      cmd_buf[buf_index++] = ch ^ 0x20;
      line_sum += ch;
      state = RS_GETLINE;
    }
    break;
  case RS_GETLINE_RLE:
    if (ch < ' ') {
      /* invalid RLE count encoding */
      state = RS_GETLINE;
      printf("Gdb: got invalid RLE count: 0x%x\n", ch);
    } else {
      /* decode repeat length */
      int repeat = (unsigned char)ch - ' ' + 3;
      if (buf_index + repeat >= sizeof(cmd_buf) - 1) {
        /* that many repeats would overrun the command buffer */
        printf("Gdb: command buffer overrun, dropping command\n");
        state = RS_IDLE;
      } else if (buf_index < 1) {
        /* got a repeat but we have nothing to repeat */
        printf("Gdb: got invalid RLE sequence\n");
        state = RS_GETLINE;
      } else {
        /* repeat the last character */
        memset(cmd_buf + buf_index, cmd_buf[buf_index - 1], repeat);
        buf_index += repeat;
        line_sum += ch;
        state = RS_GETLINE;
      }
    }
    break;
  case RS_CHKSUM1:
    /* get high hex digit of checksum */
    if (!IsXdigit(ch)) {
      printf("Gdb: got invalid command checksum digit\n");
      state = RS_GETLINE;
      break;
    }
    cmd_buf[buf_index] = '\0';
    checksum = FromHex(ch) << 4;
    state = RS_CHKSUM2;
    break;
  case RS_CHKSUM2:
    /* get low hex digit of checksum */
    if (!IsXdigit(ch)) {
      printf("Gdb: got invalid command checksum digit\n");
      state = RS_GETLINE;
      break;
    }
    checksum |= FromHex(ch);

    if (checksum != (line_sum & 0xff)) {
      printf("Gdb: got command packet with incorrect checksum\n");
      /* send NAK reply */
      reply = '-';
      WriteBytes(&reply, 1);
      state = RS_IDLE;
    } else {
      /* send ACK reply */
      reply = '+';
      WriteBytes(&reply, 1);
      state = HandleCommand((char *)cmd_buf, U);
    }
    break;
  default:
    printf("Gdb: got unknown status\n");
    break;
  }
}

}; // namespace GdbStub
