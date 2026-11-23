#include "ble.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "ui.h"

// 0000fff0-0000-1000-8000-00805f9b34fb as a 16-bit UUID (little-endian).
static const u8 FFF0_16[2] = {0xf0, 0xff};
static const u8 FFF0_128[16] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                                0x00, 0x10, 0x00, 0x00, 0xf0, 0xff, 0x00, 0x00};
// d44bc439-abfd-45a2-b575-9254161296xx, little-endian; [0] = xx.
static const u8 QCAR_CHAR_BASE[16] = {0x00, 0x96, 0x12, 0x16, 0x54, 0x92, 0x75, 0xb5,
                                      0xa2, 0x45, 0xfd, 0xab, 0x39, 0xc4, 0x4b, 0xd4};

static Event g_conn_event;
static Event g_le_event;
static BtmBleDataPath g_path;
static bool g_init, g_path_registered;
static int g_le_logged;
static volatile bool g_abort;

void ble_abort(bool abort) {
	g_abort = abort;
}

static double ms_since(u64 t) {
	return (double)armTicksToNs(armGetSystemTick() - t) / 1e6;
}

Result ble_init(void) {
	Result rc = btmuInitialize();
	if (R_FAILED(rc)) {
		log_msg("btmuInitialize rc=0x%x", rc);
		return rc;
	}
	rc = btInitialize();
	if (R_FAILED(rc)) {
		log_msg("btInitialize rc=0x%x", rc);
		btmuExit();
		return rc;
	}
	rc = btmuAcquireBleConnectionEvent(&g_conn_event);
	if (R_SUCCEEDED(rc))
		rc = btRegisterBleEvent(&g_le_event);
	if (R_FAILED(rc)) {
		log_msg("acquire events rc=0x%x", rc);
		btExit();
		btmuExit();
		return rc;
	}
	// btm completes connections only for registered GATT data paths.
	memset(&g_path, 0, sizeof(g_path));
	g_path.uuid.size = 2;
	memcpy(g_path.uuid.uuid, FFF0_16, 2);
	rc = btmuRegisterBleGattDataPath(&g_path);
	g_path_registered = R_SUCCEEDED(rc);
	log_msg("ble_init ok, data path FFF0 rc=0x%x", rc);
	g_init = true;
	return 0;
}

void ble_exit(void) {
	if (!g_init)
		return;
	ble_scan_stop();
	if (g_path_registered)
		btmuUnregisterBleGattDataPath(&g_path);
	eventClose(&g_le_event);
	eventClose(&g_conn_event);
	btExit();
	btmuExit();
	g_init = false;
}

// ---------------------------------------------------------------------------
// Raw scan
// ---------------------------------------------------------------------------

static Thread g_scan_thread;
static Mutex g_scan_lock;
static volatile bool g_scan_stop, g_scan_running, g_scan_thread_live;
static int g_scan_ms;
static ScanEntry g_found[SCAN_MAX];
static int g_nfound;
static BtdrvAddress g_others[64];
static int g_nothers;

static bool is_shell_mfr(const BtdrvBleAdvertisement *ad) {
	if (ad->type != 0xff || ad->size < 4)
		return false;
	const u8 *p = ad->data;
	return ((p[0] == 0x54 && p[1] == 0x52) || (p[0] == 0x52 && p[1] == 0x54)) && p[2] == 0x00 &&
	       p[3] == 0x3c;
}

static void scan_record(const BtdrvBleEventInfo *ev) {
	const BtdrvAddress *addr = &ev->scan_result.address;
	int count = ev->scan_result.count > 10 ? 10 : ev->scan_result.count;
	bool shell = false;
	char name[24] = "";
	for (int i = 0; i < count; i++) {
		const BtdrvBleAdvertisement *ad = &ev->scan_result.ad_list[i];
		if (is_shell_mfr(ad))
			shell = true;
		if ((ad->type == 0x08 || ad->type == 0x09) && !name[0]) {
			size_t k = 0, n = ad->size > sizeof(ad->data) ? sizeof(ad->data) : ad->size;
			while (k < n && k < sizeof(name) - 1 && ad->data[k] >= 0x20 && ad->data[k] <= 0x7e) {
				name[k] = (char)ad->data[k];
				k++;
			}
			name[k] = 0;
		}
	}
	if (!strncmp(name, "QCAR-", 5) || !strncmp(name, "SL-", 3))
		shell = true;

	mutexLock(&g_scan_lock);
	ScanEntry *e = NULL;
	for (int i = 0; i < g_nfound; i++)
		if (!memcmp(&g_found[i].addr, addr, sizeof(*addr)))
			e = &g_found[i];
	if (!e && shell && g_nfound < SCAN_MAX) {
		e = &g_found[g_nfound++];
		memset(e, 0, sizeof(*e));
		e->addr = *addr;
		snprintf(e->name, sizeof(e->name), "QCAR-%02X%02X%02X", addr->address[3], addr->address[4],
		         addr->address[5]);
		char a[18];
		addr_to_str(addr, a);
		log_msg("scan: auto %s rssi=%d typ_adresu=%d", a, (int)ev->scan_result.rssi,
		        ev->scan_result.ble_addr_type);
	}
	if (e) {
		e->rssi = ev->scan_result.rssi;
		e->seen_tick = armGetSystemTick();
		if (name[0] && !e->name_from_adv) {
			snprintf(e->name, sizeof(e->name), "%s", name);
			e->name_from_adv = true;
		}
	} else if (!shell) {
		bool known = false;
		for (int i = 0; i < g_nothers; i++)
			if (!memcmp(&g_others[i], addr, sizeof(*addr)))
				known = true;
		if (!known && g_nothers < 64)
			g_others[g_nothers++] = *addr;
	}
	mutexUnlock(&g_scan_lock);
}

static void scan_thread(void *arg) {
	(void)arg;
	Result rc = btdrvInitialize();
	if (R_FAILED(rc)) {
		log_msg("scan: btdrvInitialize rc=0x%x", rc);
		g_scan_running = false;
		return;
	}
	btdrvClearBleScanFilters();
	btdrvEnableBleScanFilter(false);
	rc = btdrvStartBleScan();
	if (R_FAILED(rc)) {
		log_msg("scan: StartBleScan rc=0x%x", rc);
	} else {
		static BtdrvBleEventInfo ev; // 0x400 bytes
		u64 t0 = armGetSystemTick();
		u32 polls = 0, hits = 0;
		while (!g_scan_stop && ms_since(t0) < g_scan_ms) {
			BtdrvBleEventType type = 0;
			if (R_SUCCEEDED(btdrvGetBleManagedEventInfo(&ev, sizeof(ev), &type)) &&
			    type == BtdrvBleEventType_ScanResult) {
				hits++;
				scan_record(&ev);
			}
			// No sleep: btm-sysmodule consumes these events too. Yield
			// occasionally so same-core threads still run.
			if (++polls % 1000 == 0)
				svcSleepThread(0);
		}
		btdrvStopBleScan();
		log_msg("scan: %u odczytow, %u zdarzen skanu, %d aut, %d innych", polls, hits, g_nfound,
		        g_nothers);
	}
	// Re-enable filtering; btm re-adds its own conditions on its next scan.
	btdrvEnableBleScanFilter(true);
	btdrvExit();
	g_scan_running = false;
}

void ble_scan_start(int duration_ms) {
	ble_scan_stop();
	static bool lock_init;
	if (!lock_init) {
		mutexInit(&g_scan_lock);
		lock_init = true;
	}
	mutexLock(&g_scan_lock);
	g_nfound = 0;
	g_nothers = 0;
	mutexUnlock(&g_scan_lock);
	g_scan_ms = duration_ms;
	g_scan_stop = false;
	g_scan_running = true;
	// Low priority (main thread is 0x2C) on core 1, so the busy poll never
	// starves the UI or the car thread.
	Result rc = threadCreate(&g_scan_thread, scan_thread, NULL, NULL, 0x8000, 0x2D, 1);
	if (R_FAILED(rc))
		rc = threadCreate(&g_scan_thread, scan_thread, NULL, NULL, 0x8000, 0x2D, -2);
	if (R_SUCCEEDED(rc))
		rc = threadStart(&g_scan_thread);
	if (R_FAILED(rc)) {
		log_msg("scan: thread rc=0x%x", rc);
		g_scan_running = false;
		return;
	}
	g_scan_thread_live = true;
}

void ble_scan_stop(void) {
	if (!g_scan_thread_live)
		return;
	g_scan_stop = true;
	threadWaitForExit(&g_scan_thread);
	threadClose(&g_scan_thread);
	g_scan_thread_live = false;
	g_scan_running = false;
}

bool ble_scan_running(void) {
	return g_scan_running;
}

static int cmp_rssi(const void *a, const void *b) {
	return ((const ScanEntry *)b)->rssi - ((const ScanEntry *)a)->rssi;
}

int ble_scan_results(ScanEntry *out, int max) {
	mutexLock(&g_scan_lock);
	int n = g_nfound < max ? g_nfound : max;
	memcpy(out, g_found, n * sizeof(ScanEntry));
	mutexUnlock(&g_scan_lock);
	qsort(out, n, sizeof(ScanEntry), cmp_rssi);
	return n;
}

int ble_scan_other_count(void) {
	return g_nothers;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

static u32 find_connection(const BtdrvAddress *addr) {
	BtdrvBleConnectionInfo infos[8];
	u8 total = 0;
	memset(infos, 0, sizeof(infos));
	if (R_FAILED(btmuBleGetConnectionState(infos, 8, &total)))
		return 0xFFFFFFFF;
	for (int i = 0; i < total && i < 8; i++)
		if (infos[i].connection_handle != 0xFFFFFFFF && !memcmp(&infos[i].addr, addr, sizeof(*addr)))
			return infos[i].connection_handle;
	return 0xFFFFFFFF;
}

static bool uuid_is_fff0(const BtdrvGattAttributeUuid *u) {
	if (u->size == 2)
		return !memcmp(u->uuid, FFF0_16, 2);
	return u->size == 0x10 && !memcmp(u->uuid, FFF0_128, 16);
}

static bool uuid_is_qcar_char(const BtdrvGattAttributeUuid *u, u8 xx) {
	return u->size == 0x10 && u->uuid[0] == xx && !memcmp(u->uuid + 1, QCAR_CHAR_BASE + 1, 15);
}

static void gatt_id(BtdrvGattId *id, u16 instance, const BtdrvGattAttributeUuid *uuid) {
	memset(id, 0, sizeof(*id));
	id->instance_id = (u8)instance;
	id->uuid = *uuid;
}

Result ble_connect(const BtdrvAddress *addr, BleLink *out, int timeout_ms, char *err, size_t errlen) {
	char a[18];
	addr_to_str(addr, a);
	memset(out, 0, sizeof(*out));
	out->addr = *addr;
	u64 t0 = armGetSystemTick();

	u32 h = find_connection(addr);
	if (h == 0xFFFFFFFF) {
		Result rc = btmuBleConnect(*addr);
		log_msg("connect %s: BleConnect rc=0x%x", a, rc);
		while (ms_since(t0) < timeout_ms && !g_abort) {
			eventWait(&g_conn_event, 100000000ULL);
			h = find_connection(addr);
			if (h != 0xFFFFFFFF)
				break;
		}
	}
	if (h == 0xFFFFFFFF) {
		snprintf(err, errlen, TR("brak polaczenia po %d ms", "no connection after %d ms"), timeout_ms);
		log_msg("connect %s: timeout", a);
		return MAKERESULT(Module_Libnx, LibnxError_Timeout);
	}
	out->handle = h;
	log_msg("connect %s: polaczono po %.0f ms, handle=0x%x", a, ms_since(t0), h);

	// Service discovery runs automatically after connecting; wait for it.
	BtmGattService svcs[16];
	u8 nsvc = 0;
	while (ms_since(t0) < timeout_ms + 5000 && !g_abort) {
		memset(svcs, 0, sizeof(svcs));
		if (R_SUCCEEDED(btmuGetGattServices(h, svcs, 16, &nsvc)) && nsvc > 0)
			break;
		svcSleepThread(100000000ULL);
	}
	bool have_ctrl = false;
	for (int i = 0; i < nsvc && i < 16; i++) {
		if (!uuid_is_fff0(&svcs[i].uuid))
			continue;
		BtmGattCharacteristic ch[32];
		u8 nch = 0;
		memset(ch, 0, sizeof(ch));
		if (R_FAILED(btmuGetGattCharacteristics(h, svcs[i].handle, ch, 32, &nch)))
			continue;
		gatt_id(&out->svc, svcs[i].instance_id, &svcs[i].uuid);
		out->primary = svcs[i].primary_service != 0;
		for (int j = 0; j < nch && j < 32; j++) {
			if (uuid_is_qcar_char(&ch[j].uuid, 0x00)) {
				gatt_id(&out->ctrl, ch[j].instance_id, &ch[j].uuid);
				have_ctrl = true;
			} else if (uuid_is_qcar_char(&ch[j].uuid, 0x01)) {
				gatt_id(&out->batt, ch[j].instance_id, &ch[j].uuid);
				out->has_batt = true;
				BtmGattDescriptor d[8];
				u8 nd = 0;
				memset(d, 0, sizeof(d));
				if (R_SUCCEEDED(btmuGetGattDescriptors(h, ch[j].handle, d, 8, &nd))) {
					for (int k = 0; k < nd && k < 8; k++)
						if (d[k].uuid.size == 2 && d[k].uuid.uuid[0] == 0x02 && d[k].uuid.uuid[1] == 0x29) {
							gatt_id(&out->cccd, 0, &d[k].uuid);
							out->has_cccd = true;
						}
				}
			}
		}
	}
	log_msg("connect %s: serwisow %u, sterowanie %s, bateria %s, cccd %s (%.0f ms)", a, nsvc,
	        have_ctrl ? "tak" : "NIE", out->has_batt ? "tak" : "nie", out->has_cccd ? "tak" : "nie",
	        ms_since(t0));
	if (!have_ctrl) {
		snprintf(err, errlen, "%s", TR("brak charakterystyki sterowania", "no control characteristic"));
		btmuBleDisconnect(h);
		return MAKERESULT(Module_Libnx, LibnxError_NotFound);
	}

	if (out->has_batt) {
		Result rc = btLeClientRegisterNotification(h, out->primary, &out->svc, &out->batt);
		log_msg("bateria: RegisterNotification rc=0x%x", rc);
		if (out->has_cccd) {
			static const u8 enable[2] = {0x01, 0x00};
			rc = btLeClientWriteDescriptor(h, out->primary, &out->svc, &out->batt, &out->cccd, enable,
			                               2, 0);
			log_msg("bateria: CCCD rc=0x%x", rc);
		}
	}
	g_le_logged = 0;
	return 0;
}

Result ble_write(const BleLink *l, const u8 frame[16]) {
	return btLeClientWriteCharacteristic(l->handle, l->primary, &l->svc, &l->ctrl, frame, 16, 0, false);
}

bool ble_connected(const BleLink *l) {
	return find_connection(&l->addr) == l->handle;
}

void ble_disconnect(const BleLink *l, int wait_ms) {
	Result rc = btmuBleDisconnect(l->handle);
	log_msg("BleDisconnect rc=0x%x", rc);
	u64 t0 = armGetSystemTick();
	while (find_connection(&l->addr) != 0xFFFFFFFF && ms_since(t0) < wait_ms)
		eventWait(&g_conn_event, 50000000ULL);
}

bool ble_poll_battery(const BleLink *l, u8 out[16]) {
	bool got = false;
	static u8 buf[0x400];
	for (int spin = 0; spin < 32; spin++) {
		if (R_FAILED(eventWait(&g_le_event, 0)))
			break;
		BtdrvBleEventType type = 0;
		if (R_FAILED(btGetLeEventInfo(buf, sizeof(buf), &type)))
			break;
		const BtdrvLeEventInfo *le = (const BtdrvLeEventInfo *)buf;
		// op flags (verified by nx.js on-device): bit0 write, bit1 descriptor,
		// bit2 notify. Write completions arrive here too.
		bool notify = (le->unk_x8 & 0x4) != 0;
		if (g_le_logged < 12 && (notify || (le->unk_x8 & 0x2))) {
			g_le_logged++;
			log_msg("LE event typ=%d status=0x%x conn=%u op=0x%x uuid1=%u/%02x size=%u", (int)type,
			        le->unk_x0, le->unk_x4, le->unk_x8, le->uuid1.size, le->uuid1.uuid[0], le->size);
		}
		if (notify && l->has_batt && le->size >= 16 && uuid_is_qcar_char(&le->uuid1, 0x01)) {
			memcpy(out, le->data, 16);
			got = true;
		}
	}
	return got;
}
