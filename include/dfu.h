#ifndef DFU_H
#define DFU_H

#include <assert.h>
#include <libusb-1.0/libusb.h>
#include <string>
#include <vector>

using namespace std;

// Some template helpers
template <typename T> void append(vector<T> &V, uint8_t *Data, size_t Size) {
  uint8_t *Start = Data;
  uint8_t *End = Start + Size;
  V.insert(V.end(), Start, End);
};

template <typename T, typename C> void append(vector<T> &V, C Value) {
  int Size = sizeof(C);
  uint8_t *Start = (uint8_t *)&Value;
  uint8_t *End = Start + Size;
  V.insert(V.end(), Start, End);
};

template <typename T, typename C> void appendV(vector<T> &V, vector<C> Value) {
  size_t Size = Value.size() * sizeof(C);
  uint8_t *Start = (uint8_t *)Value.data();
  uint8_t *End = Start + Size;
  V.insert(V.end(), Start, End);
};

const int MAX_PACKET_SIZE = 0x800;

class DFU {
private:
  const int idVendor = 0x5AC;
  const int idProduct = 0x1227;

  libusb_device_handle *devh;
  libusb_device *device;
  struct libusb_device_descriptor desc;
  std::string SerialNumber;

public:
  DFU() {
    devh = nullptr;
    int r = libusb_init(nullptr);
    assert(r == 0);
  };

  string getSerialNumber();

  bool isExploited();

  bool acquire_device(bool Silent=false);
  void release_device();
  void usb_reset();

  void stall();
  void no_leak();
  void usb_req_leak();
  void usb_req_stall();

  void send_data(vector<uint8_t> data);

  struct libusb_transfer *
  libusb1_create_ctrl_transfer(std::vector<uint8_t> &request, int timeout);

  bool libusb1_async_ctrl_transfer(int bmRequestType, int bRequest, int wValue,
                                   int wIndex, std::vector<uint8_t> &data,
                                   double timeout);

  bool libusb1_no_error_ctrl_transfer(uint8_t bmRequestType, uint8_t bRequest,
                                      uint16_t wValue, uint16_t wIndex,
                                      uint8_t *data, size_t length,
                                      int timeout);

  vector<uint8_t> ctrl_transfer(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue,
                    uint16_t wIndex, uint8_t *data, size_t length, int timeout);
};

#endif
