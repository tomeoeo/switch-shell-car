// shellscan — BLE scan diagnostics for the Shell RC module, in C/libnx.
//
// Why: nx.js's Web Bluetooth only scans by *128-bit* service UUID, and the
// Shell module advertises 0xFFF0 as a *16-bit* UUID — the Switch found
// nothing. libnx lets us try the variants nx.js can't:
//   A. smart-device scan, 16-bit UUID 0xFFF0
//   B. smart-device scan, 128-bit UUID (what nx.js does — control)
//   C/D. general scan with a manufacturer-data filter (company 0x5452 and
//        0x5254, since byte order is ambiguous; payload 00 3c)
//   E. raw btdrv scan with filters disabled, reading scan results from
//      btdrv's BLE event state (runs first).
// On hardware 2026-11-22, A-D found nothing at all (no errors).
// F. connection test with the car found: neutral frames only.
// Every device found is printed with its address; if exactly one Shell car
// is found, its address is written to sdmc:/switch/shellrc/config.json so
// shellrc.nro can connect to it directly.
//
// Output: screen + sdmc:/switch/shellrc/scan.txt.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#define DIR_PATH "sdmc:/switch/shellrc"
#define LOG_PATH DIR_PATH "/scan.txt"
#define CONFIG_PATH DIR_PATH "/config.json"
#define PASS_MS 5000
#define RAW_MS 20000
#define MAX_DEVICES 64

static FILE *g_log;
static PadState g_pad;
static bool g_abort;

static void out(const char *fmt, ...) {
	va_list a;
	va_start(a, fmt);
	vprintf(fmt, a);
	va_end(a);
	if (g_log) {
		va_start(a, fmt);
		vfprintf(g_log, fmt, a);
		va_end(a);
		fflush(g_log);
	}
	consoleUpdate(NULL);
}

static void file_only(const char *fmt, ...) {
	if (!g_log)
		return;
	va_list a;
	va_start(a, fmt);
	vfprintf(g_log, fmt, a);
	va_end(a);
	fflush(g_log);
}

typedef struct {
	BtdrvAddress addr;
	char name[64];
	unsigned passes; // bitmask of passes that saw it
	bool has_shell_mfr;
	int addr_type; // -1 unknown, else BLE address type from btdrv (0 public, 1 random)
	int rssi;
} Device;

static Device g_dev[MAX_DEVICES];
static int g_ndev;

static void addr_str(const BtdrvAddress *a, char *out) {
	snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a->address[0], a->address[1],
	         a->address[2], a->address[3], a->address[4], a->address[5]);
}

// Same heuristic as nx.js: find an AD structure of type 0x08/0x09 (local
// name) with printable payload somewhere in the raw result.
static bool raw_name(const BtdrvBleScanResult *r, char *out, size_t out_size) {
	const u8 *raw = (const u8 *)r;
	const size_t n = sizeof(*r);
	for (size_t i = 0; i + 2 < n; i++) {
		u8 len = raw[i], type = raw[i + 1];
		if ((type != 0x08 && type != 0x09) || len < 2 || len > 0x1e)
			continue;
		size_t nl = (size_t)len - 1;
		if (i + 2 + nl > n)
			continue;
		bool ok = true;
		for (size_t j = 0; j < nl; j++) {
			u8 c = raw[i + 2 + j];
			if (c < 0x20 || c > 0x7e) {
				ok = false;
				break;
			}
		}
		if (!ok)
			continue;
		if (nl >= out_size)
			nl = out_size - 1;
		memcpy(out, raw + i + 2, nl);
		out[nl] = 0;
		return true;
	}
	return false;
}

// Manufacturer-specific AD (type 0xFF) starting with 54 52 00 3c or 52 54 00 3c.
static bool raw_shell_mfr(const BtdrvBleScanResult *r) {
	const u8 *raw = (const u8 *)r;
	for (size_t i = 0; i + 5 < sizeof(*r); i++) {
		if (raw[i + 1] != 0xff || raw[i] < 5)
			continue;
		const u8 *p = raw + i + 2;
		if (((p[0] == 0x54 && p[1] == 0x52) || (p[0] == 0x52 && p[1] == 0x54)) && p[2] == 0x00 &&
		    p[3] == 0x3c)
			return true;
	}
	return false;
}

static void record(const BtdrvBleScanResult *r, int pass) {
	for (int i = 0; i < g_ndev; i++) {
		if (memcmp(&g_dev[i].addr, &r->addr, sizeof(BtdrvAddress)) == 0) {
			g_dev[i].passes |= 1u << pass;
			return;
		}
	}
	if (g_ndev >= MAX_DEVICES)
		return;
	Device *d = &g_dev[g_ndev++];
	memset(d, 0, sizeof(*d));
	d->addr = r->addr;
	d->passes = 1u << pass;
	d->addr_type = -1;
	if (!raw_name(r, d->name, sizeof(d->name)))
		strcpy(d->name, "(brak nazwy)");
	d->has_shell_mfr = raw_shell_mfr(r);

	char a[18];
	addr_str(&d->addr, a);
	out("  + %s  %s%s\n", a, d->name, d->has_shell_mfr ? "  [dane producenta Shell]" : "");
	// Raw dump to the file, so the result layout can be studied later.
	const u8 *raw = (const u8 *)r;
	file_only("    raw:");
	for (size_t i = 0; i < sizeof(*r); i++) {
		if (i % 32 == 0)
			file_only("\n     %03zx:", i);
		file_only(" %02x", raw[i]);
	}
	file_only("\n");
}

typedef enum { SCAN_SMART, SCAN_GENERAL } ScanKind;

static void poll_abort(void) {
	padUpdate(&g_pad);
	if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus)
		g_abort = true;
}

static void run_pass(int pass, const char *label, ScanKind kind, const BtdrvGattAttributeUuid *uuid,
                     const BtdrvBleAdvertisePacketParameter *param, Event *scan_event, bool unfiltered) {
	if (g_abort || !appletMainLoop())
		return;
	out("\n[%c] %s (%d s)\n", 'A' + pass, label, PASS_MS / 1000);
	Result rc = kind == SCAN_SMART ? btmuStartBleScanForSmartDevice(uuid)
	                               : btmuStartBleScanForGeneral(*param);
	if (R_FAILED(rc)) {
		out("  start nieudany: rc=0x%x\n", rc);
		return;
	}
	// Trick: btm has just installed its scan-filter conditions in btdrv.
	// Remove them, so the controller reports every advertiser to btm. If btm
	// does no further filtering of its own, its results now include the car.
	bool btdrv_open = false;
	if (unfiltered) {
		Result r2 = btdrvInitialize();
		btdrv_open = R_SUCCEEDED(r2);
		out("  btdrvInitialize rc=0x%x", r2);
		if (btdrv_open) {
			out(", Clear rc=0x%x", btdrvClearBleScanFilters());
			out(", Enable(false) rc=0x%x", btdrvEnableBleScanFilter(false));
		}
		out("\n");
	}
	int before = g_ndev;
	int polls = 0, max_total = 0;
	Result last_err = 0;
	u64 end = armGetSystemTick() + armNsToTicks((u64)PASS_MS * 1000000ULL);
	while (armGetSystemTick() < end && !g_abort && appletMainLoop()) {
		eventWait(scan_event, 250000000ULL); // wake on new results or every 250 ms
		BtdrvBleScanResult res[10];
		u8 total = 0;
		rc = kind == SCAN_SMART ? btmuGetBleScanResultsForSmartDevice(res, 10, &total)
		                        : btmuGetBleScanResultsForGeneral(res, 10, &total);
		polls++;
		if (R_FAILED(rc)) {
			if (rc != last_err)
				out("  odczyt wynikow: rc=0x%x\n", rc);
			last_err = rc;
		} else {
			if (total > max_total)
				max_total = total;
			for (int i = 0; i < total && i < 10; i++)
				record(&res[i], pass);
		}
		poll_abort();
	}
	if (btdrv_open) {
		btdrvEnableBleScanFilter(true);
		btdrvExit();
	}
	rc = kind == SCAN_SMART ? btmuStopBleScanForSmartDevice() : btmuStopBleScanForGeneral();
	if (R_FAILED(rc))
		out("  stop: rc=0x%x\n", rc);
	int seen = 0;
	for (int i = 0; i < g_ndev; i++)
		if (g_dev[i].passes & (1u << pass))
			seen++;
	out("  wynik: %d urzadzen (nowych %d), odczytow %d, max naraz %d\n", seen, g_ndev - before,
	    polls, max_total);
}

// ---------------------------------------------------------------------------
// Connection test (pass E): connect, dump GATT, 100 x neutral frame, disconnect.
// Uses the UUID structs exactly as discovered (16-bit stays 16-bit), which
// nx.js cannot do. Sends ONLY the neutral (IDLE) frame — the car never moves.
// ---------------------------------------------------------------------------

static const u8 FFF0_128[16] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                                0x00, 0x10, 0x00, 0x00, 0xf0, 0xff, 0x00, 0x00};
// d44bc439-abfd-45a2-b575-925416129600, little-endian.
static const u8 CONTROL_128[16] = {0x00, 0x96, 0x12, 0x16, 0x54, 0x92, 0x75, 0xb5,
                                   0xa2, 0x45, 0xfd, 0xab, 0x39, 0xc4, 0x4b, 0xd4};
// AES-128 ciphertext of the neutral frame 00 'CTL' 00 00 00 00 01 50 00..
// (protocol/drivers/qcar.ts, tests/qcar.test.ts "IDLE").
static const u8 IDLE_FRAME[16] = {0x02, 0x5e, 0x69, 0x5a, 0x48, 0xff, 0x2a, 0x43,
                                  0x8c, 0xa6, 0x80, 0xf8, 0x3e, 0x04, 0xe4, 0x5d};

static void uuid_str(const BtdrvGattAttributeUuid *u, char *out /*[48]*/) {
	if (u->size == 2) {
		snprintf(out, 48, "%02x%02x (16-bit)", u->uuid[1], u->uuid[0]);
	} else if (u->size == 4) {
		snprintf(out, 48, "%02x%02x%02x%02x (32-bit)", u->uuid[3], u->uuid[2], u->uuid[1], u->uuid[0]);
	} else {
		const u8 *b = u->uuid;
		snprintf(out, 48, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		         b[15], b[14], b[13], b[12], b[11], b[10], b[9], b[8], b[7], b[6], b[5], b[4], b[3],
		         b[2], b[1], b[0]);
	}
}

static bool uuid_is_fff0(const BtdrvGattAttributeUuid *u) {
	if (u->size == 2)
		return u->uuid[0] == 0xf0 && u->uuid[1] == 0xff;
	return u->size == 0x10 && memcmp(u->uuid, FFF0_128, 16) == 0;
}

static double ticks_ms(u64 t) {
	return (double)armTicksToNs(t) / 1e6;
}

static int cmp_double(const void *a, const void *b) {
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static void summary(const char *label, double *v, int n) {
	if (n == 0) {
		out("  %s: brak danych\n", label);
		return;
	}
	qsort(v, n, sizeof(double), cmp_double);
	int i50 = (n * 50 + 99) / 100 - 1, i95 = (n * 95 + 99) / 100 - 1;
	out("  %s: n=%d min=%.2f p50=%.2f p95=%.2f max=%.2f ms\n", label, n, v[0], v[i50 < 0 ? 0 : i50],
	    v[i95 < 0 ? 0 : i95], v[n - 1]);
}

static u32 find_connection(const BtdrvAddress *addr) {
	BtdrvBleConnectionInfo infos[8];
	u8 total = 0;
	memset(infos, 0, sizeof(infos));
	if (R_FAILED(btmuBleGetConnectionState(infos, 8, &total)))
		return 0xFFFFFFFF;
	for (int i = 0; i < total && i < 8; i++)
		if (infos[i].connection_handle != 0xFFFFFFFF &&
		    memcmp(&infos[i].addr, addr, sizeof(BtdrvAddress)) == 0)
			return infos[i].connection_handle;
	return 0xFFFFFFFF;
}

static u32 try_connect(const BtdrvAddress *addr, Event *conn_ev, int timeout_ms) {
	Result rc = btmuBleConnect(*addr);
	out("  BleConnect rc=0x%x\n", rc);
	u64 start = armGetSystemTick();
	while (ticks_ms(armGetSystemTick() - start) < timeout_ms && appletMainLoop() && !g_abort) {
		eventWait(conn_ev, 250000000ULL);
		u32 h = find_connection(addr);
		if (h != 0xFFFFFFFF) {
			out("  polaczono po %.0f ms, handle=0x%x\n", ticks_ms(armGetSystemTick() - start), h);
			return h;
		}
		poll_abort();
	}
	out("  brak polaczenia po %d ms\n", timeout_ms);
	return 0xFFFFFFFF;
}

static void connect_test(const BtdrvAddress *addr) {
	out("\n[F] Test polaczenia (tylko ramki NEUTRALNE, auto nie ruszy)\n");
	Result rc = btInitialize();
	if (R_FAILED(rc)) {
		out("  btInitialize rc=0x%x\n", rc);
		return;
	}
	Event conn_ev;
	rc = btmuAcquireBleConnectionEvent(&conn_ev);
	if (R_FAILED(rc)) {
		out("  AcquireBleConnectionEvent rc=0x%x\n", rc);
		btExit();
		return;
	}

	// Data path: btm only completes connections for registered services.
	// Try the 16-bit form first (what the module advertises), then 128-bit.
	BtmBleDataPath p16 = {0}, p128 = {0};
	p16.uuid.size = 2;
	p16.uuid.uuid[0] = 0xf0;
	p16.uuid.uuid[1] = 0xff;
	p128.uuid.size = 0x10;
	memcpy(p128.uuid.uuid, FFF0_128, 16);
	bool reg128 = false;
	out("  data path 16-bit FFF0: rc=0x%x\n", btmuRegisterBleGattDataPath(&p16));
	u32 h = try_connect(addr, &conn_ev, 10000);
	if (h == 0xFFFFFFFF && !g_abort) {
		out("  data path 128-bit FFF0: rc=0x%x\n", btmuRegisterBleGattDataPath(&p128));
		reg128 = true;
		h = try_connect(addr, &conn_ev, 10000);
	}
	if (h == 0xFFFFFFFF)
		goto cleanup;

	u16 mtu = 0;
	if (R_SUCCEEDED(btmuGetBleMtu(h, &mtu)))
		out("  MTU=%u\n", mtu);

	// Service discovery runs automatically after connecting; wait for it.
	BtmGattService svcs[16];
	u8 nsvc = 0;
	u64 start = armGetSystemTick();
	while (ticks_ms(armGetSystemTick() - start) < 10000 && appletMainLoop()) {
		memset(svcs, 0, sizeof(svcs));
		if (R_SUCCEEDED(btmuGetGattServices(h, svcs, 16, &nsvc)) && nsvc > 0)
			break;
		svcSleepThread(200000000ULL);
	}
	out("  serwisy: %u (po %.0f ms)\n", nsvc, ticks_ms(armGetSystemTick() - start));

	BtdrvGattId serv_id, char_id;
	bool have_ctrl = false, primary = true;
	for (int i = 0; i < nsvc && i < 16; i++) {
		char us[48];
		uuid_str(&svcs[i].uuid, us);
		out("   serwis %s handle=0x%x inst=%u primary=%u\n", us, svcs[i].handle, svcs[i].instance_id,
		    svcs[i].primary_service);
		BtmGattCharacteristic ch[32];
		u8 nch = 0;
		memset(ch, 0, sizeof(ch));
		rc = btmuGetGattCharacteristics(h, svcs[i].handle, ch, 32, &nch);
		if (R_FAILED(rc)) {
			out("    GetGattCharacteristics rc=0x%x\n", rc);
			continue;
		}
		for (int j = 0; j < nch && j < 32; j++) {
			char cs[48];
			uuid_str(&ch[j].uuid, cs);
			const u8 *raw = (const u8 *)&ch[j];
			out("    %s inst=%u props@1e=0x%02x props@20=0x%02x\n", cs, ch[j].instance_id, raw[0x1e],
			    raw[0x20]);
			if (uuid_is_fff0(&svcs[i].uuid) && ch[j].uuid.size == 0x10 &&
			    memcmp(ch[j].uuid.uuid, CONTROL_128, 16) == 0) {
				memset(&serv_id, 0, sizeof(serv_id));
				memset(&char_id, 0, sizeof(char_id));
				serv_id.instance_id = (u8)svcs[i].instance_id;
				serv_id.uuid = svcs[i].uuid;
				char_id.instance_id = (u8)ch[j].instance_id;
				char_id.uuid = ch[j].uuid;
				primary = svcs[i].primary_service != 0;
				have_ctrl = true;
			}
		}
	}

	if (!have_ctrl) {
		out("  !! Nie znaleziono charakterystyki sterowania ...9600\n");
	} else {
		out("  100 x IDLE co 10 ms (write without response)...\n");
		double call_ms[100], gap_ms[100];
		int ncall = 0, ngap = 0, errors = 0;
		Result first_err = 0;
		u64 last = 0;
		for (int i = 0; i < 100 && appletMainLoop(); i++) {
			u64 t0 = armGetSystemTick();
			if (last)
				gap_ms[ngap++] = ticks_ms(t0 - last);
			last = t0;
			rc = btLeClientWriteCharacteristic(h, primary, &serv_id, &char_id, IDLE_FRAME, 16, 0, false);
			u64 t1 = armGetSystemTick();
			if (R_FAILED(rc)) {
				if (!errors)
					first_err = rc;
				errors++;
			} else {
				call_ms[ncall++] = ticks_ms(t1 - t0);
			}
			svcSleepThread(10000000ULL);
		}
		summary("czas wywolania zapisu", call_ms, ncall);
		summary("odstep miedzy zapisami", gap_ms, ngap);
		out("  bledy: %d%s", errors, errors ? "" : "\n");
		if (errors)
			out(" (pierwszy rc=0x%x)\n", first_err);
		out("  polaczenie po tescie: %s\n", find_connection(addr) == h ? "OK" : "ZERWANE");
	}

	// ~100 ms more neutral, then disconnect.
	if (have_ctrl)
		for (int i = 0; i < 10; i++) {
			btLeClientWriteCharacteristic(h, primary, &serv_id, &char_id, IDLE_FRAME, 16, 0, false);
			svcSleepThread(10000000ULL);
		}
	out("  BleDisconnect rc=0x%x\n", btmuBleDisconnect(h));
	start = armGetSystemTick();
	while (find_connection(addr) != 0xFFFFFFFF && ticks_ms(armGetSystemTick() - start) < 3000)
		eventWait(&conn_ev, 250000000ULL);

cleanup:
	btmuUnregisterBleGattDataPath(&p16);
	if (reg128)
		btmuUnregisterBleGattDataPath(&p128);
	eventClose(&conn_ev);
	btExit();
}

// ---------------------------------------------------------------------------
// Pass E: raw, unfiltered btdrv scan. btm's application scans (A-D) apply
// system filters the Shell module doesn't pass. Here we clear btdrv's scan
// filters (as nx.js does to prime the device cache) and read scan results
// straight from btdrv's BLE event state. Read-only; filters are re-enabled
// afterwards and btm re-adds its conditions on its next scan.
// ---------------------------------------------------------------------------

#define RAW_PASS 4

static void record_raw(const BtdrvBleEventInfo *ev) {
	const BtdrvAddress *addr = &ev->scan_result.address;
	Device *d = NULL;
	for (int i = 0; i < g_ndev; i++)
		if (memcmp(&g_dev[i].addr, addr, sizeof(BtdrvAddress)) == 0)
			d = &g_dev[i];
	bool fresh = d == NULL;
	if (fresh) {
		if (g_ndev >= MAX_DEVICES)
			return;
		d = &g_dev[g_ndev++];
		memset(d, 0, sizeof(*d));
		d->addr = *addr;
		strcpy(d->name, "(brak nazwy)");
	}
	d->passes |= 1u << RAW_PASS;
	d->addr_type = ev->scan_result.ble_addr_type;
	d->rssi = ev->scan_result.rssi;

	// Advertisement entries: name, manufacturer data. Names/mfr data may
	// arrive in a later packet (scan response), so keep merging.
	bool learned = false;
	int count = ev->scan_result.count;
	if (count > 10)
		count = 10;
	for (int i = 0; i < count; i++) {
		const BtdrvBleAdvertisement *ad = &ev->scan_result.ad_list[i];
		size_t n = ad->size > sizeof(ad->data) ? sizeof(ad->data) : ad->size;
		if ((ad->type == 0x08 || ad->type == 0x09) && strcmp(d->name, "(brak nazwy)") == 0 && n > 0) {
			size_t k = 0;
			while (k < n && k < sizeof(d->name) - 1 && ad->data[k] >= 0x20 && ad->data[k] <= 0x7e) {
				d->name[k] = (char)ad->data[k];
				k++;
			}
			if (k > 0) {
				d->name[k] = 0;
				learned = true;
			} else {
				strcpy(d->name, "(brak nazwy)");
			}
		}
		if (ad->type == 0xff && n >= 4 && !d->has_shell_mfr) {
			const u8 *p = ad->data;
			if (((p[0] == 0x54 && p[1] == 0x52) || (p[0] == 0x52 && p[1] == 0x54)) && p[2] == 0x00 &&
			    p[3] == 0x3c) {
				d->has_shell_mfr = true;
				learned = true;
			}
		}
	}

	if (fresh || learned) {
		char a[18];
		addr_str(&d->addr, a);
		out("  %c %s typ=%d rssi=%d  %s%s\n", fresh ? '+' : '~', a, d->addr_type, d->rssi, d->name,
		    d->has_shell_mfr ? "  [dane producenta Shell]" : "");
		file_only("    ad (%d):", count);
		for (int i = 0; i < count; i++) {
			const BtdrvBleAdvertisement *ad = &ev->scan_result.ad_list[i];
			file_only(" [t=%02x n=%u:", ad->type, ad->size);
			for (int j = 0; j < ad->size && j < (int)sizeof(ad->data); j++)
				file_only("%02x", ad->data[j]);
			file_only("]");
		}
		file_only("\n");
	}
}

static void raw_pass(int ms) {
	if (g_abort || !appletMainLoop())
		return;
	out("\n[E] surowy skan btdrv bez filtrow (%d s)\n", ms / 1000);
	Result rc = btdrvInitialize();
	if (R_FAILED(rc)) {
		out("  btdrvInitialize rc=0x%x\n", rc);
		return;
	}
	out("  ClearBleScanFilters rc=0x%x\n", btdrvClearBleScanFilters());
	out("  EnableBleScanFilter(false) rc=0x%x\n", btdrvEnableBleScanFilter(false));
	rc = btdrvStartBleScan();
	out("  StartBleScan rc=0x%x\n", rc);
	if (R_SUCCEEDED(rc)) {
		int polls = 0, scan_events = 0, errors = 0;
		int types[16] = {0};
		Result first_err = 0;
		static BtdrvBleEventInfo ev; // 0x400 bytes, keep off the stack
		u64 start = armGetSystemTick();
		while (ticks_ms(armGetSystemTick() - start) < ms && !g_abort && appletMainLoop()) {
			BtdrvBleEventType type = 0;
			memset(&ev, 0, sizeof(ev));
			rc = btdrvGetBleManagedEventInfo(&ev, sizeof(ev), &type);
			polls++;
			if (R_FAILED(rc)) {
				if (!errors)
					first_err = rc;
				errors++;
			} else {
				if ((unsigned)type < 16)
					types[type]++;
				if (type == BtdrvBleEventType_ScanResult) {
					scan_events++;
					record_raw(&ev);
				}
			}
			// No sleep: btm-sysmodule consumes these events too, so we
			// only see the ones we read first. Poll as fast as possible.
			if (polls % 2000 == 0)
				poll_abort();
		}
		out("  odczytow %d, zdarzen skanu %d, bledow %d", polls, scan_events, errors);
		if (errors)
			out(" (pierwszy rc=0x%x)", first_err);
		out("\n  typy zdarzen:");
		for (int i = 0; i < 16; i++)
			if (types[i])
				out(" %d:%d", i, types[i]);
		out("\n");
		out("  StopBleScan rc=0x%x\n", btdrvStopBleScan());
	}
	out("  EnableBleScanFilter(true) rc=0x%x\n", btdrvEnableBleScanFilter(true));
	btdrvExit();
	int seen = 0;
	for (int i = 0; i < g_ndev; i++)
		if (g_dev[i].passes & (1u << RAW_PASS))
			seen++;
	out("  wynik: %d urzadzen\n", seen);
}

static bool is_shell(const Device *d) {
	return d->has_shell_mfr || strncmp(d->name, "QCAR-", 5) == 0 || strncmp(d->name, "SL-", 3) == 0;
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	consoleInit(NULL);
	padConfigureInput(8, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&g_pad);

	mkdir("sdmc:/switch", 0777);
	mkdir(DIR_PATH, 0777);
	g_log = fopen(LOG_PATH, "w");

	out("Shell RC - diagnostyka skanu BLE (C/libnx)\n");
	out("Autko wlaczone, aplikacja Shell na telefonie zamknieta. + = przerwij\n");
	out("appletType=%d (0 = aplikacja)\n", (int)appletGetAppletType());
	if (!g_log)
		out("! Nie moge pisac do %s\n", LOG_PATH);

	Result rc = btmuInitialize();
	if (R_FAILED(rc)) {
		out("btmuInitialize nieudany: rc=0x%x\n", rc);
		goto wait_exit;
	}
	Event scan_event;
	rc = btmuAcquireBleScanEvent(&scan_event);
	if (R_FAILED(rc)) {
		out("btmuAcquireBleScanEvent nieudany: rc=0x%x\n", rc);
		btmuExit();
		goto wait_exit;
	}

	raw_pass(RAW_MS);

	// A: 16-bit 0xFFF0 (little-endian bytes).
	BtdrvGattAttributeUuid u16 = {.size = 2};
	u16.uuid[0] = 0xf0;
	u16.uuid[1] = 0xff;
	// First run (2026-11-22, MissionControl off): A-D found nothing, so only
	// their "unfiltered" variants run now: G (general) and H (smart 16-bit).
	BtdrvBleAdvertisePacketParameter pg = {0};
	pg.company_id = 0x5452;
	pg.pattern_data[0] = 0x00;
	pg.pattern_data[1] = 0x3c;
	run_pass(6, "general + filtr btdrv wylaczony", SCAN_GENERAL, NULL, &pg, &scan_event, true);
	run_pass(7, "smart device 16-bit FFF0 + filtr btdrv wylaczony", SCAN_SMART, &u16, NULL, &scan_event,
	         true);
#if 0
	run_pass(0, "smart device, UUID 16-bit FFF0", SCAN_SMART, &u16, NULL, &scan_event, false);

	// B: 128-bit 0000fff0-0000-1000-8000-00805f9b34fb, little-endian (as nx.js).
	static const u8 base128[16] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
	                               0x00, 0x10, 0x00, 0x00, 0xf0, 0xff, 0x00, 0x00};
	BtdrvGattAttributeUuid u128 = {.size = 0x10};
	memcpy(u128.uuid, base128, 16);
	run_pass(1, "smart device, UUID 128-bit FFF0 (jak nx.js)", SCAN_SMART, &u128, NULL, &scan_event, false);

	// C/D: manufacturer data filter, both byte orders of the company id.
	BtdrvBleAdvertisePacketParameter p = {0};
	p.company_id = 0x5452;
	p.pattern_data[0] = 0x00;
	p.pattern_data[1] = 0x3c;
	run_pass(2, "general, firma 0x5452 + 00 3c", SCAN_GENERAL, NULL, &p, &scan_event, false);
	p.company_id = 0x5254;
	run_pass(3, "general, firma 0x5254 + 00 3c", SCAN_GENERAL, NULL, &p, &scan_event, false);
#endif

	eventClose(&scan_event);

	out("\n=== Podsumowanie: %d urzadzen ===\n", g_ndev);
	const Device *car = NULL;
	int ncars = 0;
	for (int i = 0; i < g_ndev; i++) {
		const Device *d = &g_dev[i];
		char a[18];
		addr_str(&d->addr, a);
		out(" %s  %-20s przebiegi:%s%s%s%s%s%s%s%s\n", a, d->name, d->passes & 1 ? " A" : "",
		    d->passes & 2 ? " B" : "", d->passes & 4 ? " C" : "", d->passes & 8 ? " D" : "",
		    d->passes & 16 ? " E" : "", d->passes & 64 ? " G" : "", d->passes & 128 ? " H" : "",
		    is_shell(d) ? "  <- AUTO" : "");
		if (is_shell(d)) {
			car = d;
			ncars++;
		}
	}
	// If no device looks like a car by name/mfr data but pass A/C/D found
	// exactly one device, that is almost certainly it.
	if (ncars == 0) {
		int n = 0;
		for (int i = 0; i < g_ndev; i++)
			if (g_dev[i].passes & (1u | 4u | 8u)) {
				car = &g_dev[i];
				n++;
			}
		ncars = n;
		if (n == 1)
			out("(jedyne urzadzenie z FFF0 / danymi Shell - biore je jako auto)\n");
	}

	if (ncars == 1 && car) {
		char a[18];
		addr_str(&car->addr, a);
		FILE *f = fopen(CONFIG_PATH, "w");
		if (f) {
			fprintf(f, "{\"deviceId\": \"%s\"}\n", a);
			fclose(f);
			out("\nAUTO: %s - zapisane do %s\n", a, CONFIG_PATH);
			out("Uruchom teraz shellrc.nro - polaczy sie po tym adresie.\n");
		} else {
			out("\nAUTO: %s - NIE udalo sie zapisac %s\n", a, CONFIG_PATH);
		}
		if (!g_abort)
			connect_test(&car->addr);
	} else if (ncars > 1) {
		out("\nZnaleziono kilka aut - config.json NIE zmieniony. Wybierz adres recznie.\n");
	} else {
		out("\nNie znaleziono auta. Wyslij %s.\n", LOG_PATH);
	}
	btmuExit();

wait_exit:
	out("\nLog: %s\n+ = wyjscie\n", LOG_PATH);
	if (g_log)
		fclose(g_log);
	g_log = NULL;
	while (appletMainLoop()) {
		padUpdate(&g_pad);
		if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus)
			break;
		consoleUpdate(NULL);
	}
	consoleExit(NULL);
	return 0;
}
