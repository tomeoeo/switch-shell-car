// BLE for the Shell module on the Switch (libnx btm.u + bt + btdrv).
//
// What works (measured 2026-11-22, docs/faza1-wyniki.md):
//  - btm.u application scans never report the module -> we scan raw:
//    btdrv with filters disabled, busy-polling btdrvGetBleManagedEventInfo
//    (btm-sysmodule consumes most events, so poll without sleeping).
//  - btmuBleConnect works with a 16-bit FFF0 GATT data path registered.
//  - btLeClientWriteCharacteristic without response, ~0.2 ms per call.
#pragma once
#include <stdbool.h>
#include <switch.h>

#define SCAN_MAX 16

typedef struct {
	BtdrvAddress addr;
	char name[24];
	bool name_from_adv; // false: derived "QCAR-XXXXXX" from the address
	int rssi;
	u64 seen_tick;
} ScanEntry;

Result ble_init(void);
void ble_exit(void);

// Raw scan in a background thread. Results are Shell modules only.
void ble_scan_start(int duration_ms);
void ble_scan_stop(void);
bool ble_scan_running(void);
int ble_scan_results(ScanEntry *out, int max); // sorted by RSSI, strongest first
int ble_scan_other_count(void);               // non-Shell devices seen

typedef struct {
	u32 handle;
	BtdrvAddress addr;
	bool primary;
	BtdrvGattId svc, ctrl, batt, cccd;
	bool has_batt, has_cccd;
} BleLink;

// Makes a running ble_connect give up quickly (called from another thread).
void ble_abort(bool abort);
// Blocking. Requires the device in the stack's cache (seen by a raw scan
// since boot). err gets a short human-readable reason on failure.
Result ble_connect(const BtdrvAddress *addr, BleLink *out, int timeout_ms, char *err, size_t errlen);
Result ble_write(const BleLink *l, const u8 frame[16]);
bool ble_connected(const BleLink *l);
void ble_disconnect(const BleLink *l, int wait_ms);
// Drain pending LE events. Returns true and fills out if a battery
// notification (16 bytes from ...9601) arrived.
bool ble_poll_battery(const BleLink *l, u8 out[16]);
