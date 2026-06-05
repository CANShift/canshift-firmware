// usb_comm.cpp — USB serial transport + orchestrator layer.
//
// Protocol: JSON lines over USB serial (UART0 / CP210x bridge), 115200 baud.
// Each message is one JSON object followed by \n.
//
// Desktop → device: {"cmd": <id>, ...fields}\n
// Device → desktop (command response): {"status":"ok"} or {"status":"error","message":"..."}\n
// Device → desktop (telemetry, proactive every ~200ms): {"tele":1,"v":{"rpm":...}}\n
//
// Static memory: only s_rxBuf (USB_RX_BUF_SIZE bytes, see app_config.h).
//   handlePutConfig (in usb_dispatch.cpp) reuses s_rxBuf for serialization after
//   parsing — safe because ArduinoJson 7 copies all values into the JsonDocument
//   during parsing.
//
// Implementation split (#1207 refactor):
//   - usb_comm.cpp        — this file: transport + public API surface.
//                           Sink fan-out (sendLine / setAuxSink / hasAuxSink),
//                           handleLine, RX accumulator, telemetry emit, CAN
//                           scan drain, host-activity tracking, init/tick.
//   - usb_dispatch.cpp    — command parsing + per-command handlers.
//   - usb_config_sync.cpp — typed device.json / input_bindings.json handlers
//                           backed by the pre-allocated response buffer.
//   - usb_comm_internal.h — cross-module forward decls.
//
// The PUBLIC API in usb_comm.h is byte-identical to pre-refactor.

#include "usb_comm.h"
#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "can/signal_map.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "runtime/signal_store.h"
#include "util/format_float.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Telemetry signal table
// ---------------------------------------------------------------------------

namespace {

struct TeleEntry {
    SignalId id;
    const char *name;
};

// All signals exposed over USB telemetry (must match signals.json)
const TeleEntry TELE_SIGNALS[] = {
    {SignalIds::RPM, "rpm"},
    {SignalIds::THROTTLE_POS, "throttle_pos"},
    {SignalIds::MAP_KPA, "map_kpa"},
    {SignalIds::BOOST_BAR, "boost_bar"},
    {SignalIds::IAT_C, "iat_c"},
    {SignalIds::COOLANT_TEMP_C, "coolant_temp_c"},
    {SignalIds::OIL_TEMP_C, "oil_temp_c"},
    {SignalIds::OIL_PRESS_BAR, "oil_press_bar"},
    {SignalIds::FUEL_PRESS_BAR, "fuel_press_bar"},
    {SignalIds::LAMBDA_1, "lambda_1"},
    {SignalIds::AFR_1, "afr_1"},
    {SignalIds::SPEED_KPH, "speed_kph"},
    {SignalIds::GEAR, "gear"},
    {SignalIds::BATTERY_VOLTS, "battery_volts"},
    {SignalIds::FLAG_MIL, "flag_mil"},
    {SignalIds::FLAG_LAUNCH_CTRL, "flag_launch_ctrl"},
    {SignalIds::MAP_NUMBER, "map_number"},
};

constexpr size_t TELE_SIGNAL_COUNT = sizeof(TELE_SIGNALS) / sizeof(TELE_SIGNALS[0]);

// ---------------------------------------------------------------------------
// Output sink — abstracts UART0 vs. an alternate transport (WiFi TCP / WS).
//
// `s_sink` defaults to the Serial sink and is used as the fallback target
// for proactive telemetry (sendLine() called outside any handleLine()
// dispatch). Command replies are routed through a per-task thread_local
// `t_dispatchSink` set by handleLine() for the duration of one command, so
// concurrent dispatches on different transports never block each other on
// the sink mutex. The aux sink, when non-null, receives a parallel copy of
// every sendLine() write so telemetry / acks reach both transports
// concurrently — used by the WiFi TCP / WS servers to mirror the proactive
// telemetry stream out to a connected Studio.
//
// A single mutex serialises every sink read/write. Held only across the brief
// pointer snapshot or the aux-sink registration, never across user-supplied
// I/O that may block (no nested logger locks inside the critical section)
// and never across handleCommand() (#1286).
// ---------------------------------------------------------------------------

#ifdef ARDUINO
void serialSink(const char *data, size_t len) {
    if (!data || len == 0)
        return;
    const bool needsNewline = data[len - 1] != '\n';
    // Logger mutex protects against interleaving with LOG_* from other tasks;
    // dropping it through on failure keeps a command ack from being silently
    // lost when the logger is busy (same trade-off the pre-sink sendLine made).
    const bool locked = Logger::lockUart(pdMS_TO_TICKS(50));
    Serial.write(reinterpret_cast<const uint8_t *>(data), len);
    if (needsNewline)
        Serial.write('\n');
    if (locked)
        Logger::unlockUart();
}
#else
void serialSink(const char *, size_t) {
    // Native test build — no Serial. Tests exercise handlers via injected sinks.
}
#endif

// Recursive — kept recursive so a handler that calls sendLine() while a
// caller already holds the lock (e.g. nested error paths inside an aux-sink
// registration window) never self-deadlocks. The lock is no longer held
// across handleCommand() since #1286 — see t_dispatchSink below — but the
// recursive semantics stay so future call paths remain safe by construction.
SemaphoreHandle_t s_sinkMutex = nullptr;
UsbComm::SendSink s_sink = &serialSink;
UsbComm::SendSink s_auxSink = nullptr;

// BurnOverlay observer callbacks (#1207 #1314). Slots are written by
// UsbComm::setBurnOverlayShow{,Error}Callback and read by
// UsbCommInternal::invokeBurnOverlay{Show,ShowError}. Single writer (boot
// setup), multiple readers (any task that drives a PUT_CONFIG dispatch), and
// pointer-sized so the relaxed read is torn-free on ESP32 — no extra mutex.
UsbComm::BurnOverlayShowCb s_burnOverlayShowCb = nullptr;
UsbComm::BurnOverlayShowErrorCb s_burnOverlayShowErrorCb = nullptr;

// Per-task dispatch sink — set by handleLine() while a command is in flight on
// the current task. sendLine() consults this BEFORE the global s_sink so we no
// longer need to hold the sink mutex across the whole handler dispatch (which
// could block other tasks' sendLine for the ~100-200 ms a PUT_CONFIG burn
// takes to acquire the LVGL mutex). thread_local gives each FreeRTOS task its
// own pointer so two transports dispatching concurrently route their replies
// to the correct caller. Issue #1286.
thread_local UsbComm::SendSink t_dispatchSink = nullptr;

// Acquire / release the sink mutex with a bounded timeout. Returns true on
// success; callers fall through unprotected on timeout to mirror the
// degrade-don't-drop policy of the legacy Logger::lockUart path.
bool lockSink() {
    if (!s_sinkMutex)
        return false;
    return xSemaphoreTakeRecursive(s_sinkMutex, pdMS_TO_TICKS(50)) == pdTRUE;
}

void unlockSink() {
    if (s_sinkMutex)
        xSemaphoreGiveRecursive(s_sinkMutex);
}

// Single RX buffer — also reused as TX buffer in handlePutConfig after parsing.
// Size defined in app_config.h: CONFIG_JSON_DOC_DASHBOARD + 256.
//
// F-LO-2: pin the sizing contract at compile time so a future raise of
// CONFIG_JSON_DOC_DASHBOARD (the worst-case payload that streams in-place
// out of this buffer on the TX path) without a matching bump of
// USB_RX_BUF_SIZE is caught at build instead of at runtime. The +256 is the
// envelope overhead `{"cmd":2,"payload":...}` that the host wraps around
// the dashboard JSON before sending it down the wire.
static_assert(USB_RX_BUF_SIZE >= CONFIG_JSON_DOC_DASHBOARD + 256,
              "USB_RX_BUF_SIZE must hold CONFIG_JSON_DOC_DASHBOARD + envelope overhead");

size_t s_rxPos = 0;

// Tick counter for telemetry scheduling (tick() runs every 20ms)
uint8_t s_tickCount = 0;

// How many tick() calls between telemetry pushes: 10 × 20ms = 200ms
constexpr uint8_t TELE_PERIOD_TICKS = 10;

// ---------------------------------------------------------------------------
// CAN health stats
// ---------------------------------------------------------------------------

// Written by CAN task (core 0), read by USB task (core 1).
// A portMUX spinlock guards both writes and the snapshot read so the
// two-word struct is never torn across a core boundary (#1160).
struct CanHealthStats {
    uint32_t fpsX10;
    uint32_t errors;
};
CanHealthStats s_canStats = {0, 0};
portMUX_TYPE s_canStatsMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_canStatsPending = false;

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------

// Telemetry buffer lives on the stack (768 B, fits in USB task 4096 B stack).
// Format: {"tele":1,"v":{"rpm":1234.5,...}}\n
// Only valid (non-timed-out) signals are included.
constexpr size_t TELE_BUF_SIZE = 768;

void sendTelemetry() {
    char buf[TELE_BUF_SIZE];
    char *p = buf;
    const char *end = buf + TELE_BUF_SIZE - 2; // reserve \n\0

    const char *prefix = "{\"tele\":1,\"v\":{";
    size_t prefixLen = strlen(prefix);
    if (p + prefixLen >= end)
        return;
    memcpy(p, prefix, prefixLen);
    p += prefixLen;

    bool first = true;
    for (size_t i = 0; i < TELE_SIGNAL_COUNT; i++) {
        if (!SignalStore::isValid(TELE_SIGNALS[i].id))
            continue;
        float val = SignalStore::read(TELE_SIGNALS[i].id);

        if (!first) {
            if (p >= end)
                break;
            *p++ = ',';
        }
        first = false;

        // Compose `"<name>":<number>` without `%f`/`%g` so the firmware can
        // drop newlib's float printf family. FloatFormat::formatGeneral mimics
        // `%.3g` (significant digits, trailing zeros stripped).
        int n = snprintf(p, static_cast<size_t>(end - p), "\"%s\":", TELE_SIGNALS[i].name);
        if (n <= 0 || p + n >= end)
            break;
        p += n;
        char numBuf[16];
        size_t numLen = FloatFormat::formatGeneral(numBuf, sizeof(numBuf), val, 3);
        if (numLen == 0 || p + numLen >= end)
            break;
        memcpy(p, numBuf, numLen);
        p += numLen;
    }

    if (p + 3 <= end) {
        *p++ = '}';
        *p++ = '}';
        *p++ = '\n';
        *p = '\0';
        UsbComm::sendLine(buf);
    }
}

// Drain queued CAN scan frames and write them to Serial.
// Each frame is serialized as: {"can":1,"id":<id>,"len":<n>,"d":[b0,...,bn]}\n
// Called from tick() on the USB task — single writer, safe to call Serial.print().
void drainCanScanQueue() {
    UsbComm::CanScanFrame frame;
    // Drain at most 32 frames per tick to avoid blocking telemetry
    uint8_t drained = 0;
    while (drained < 32 && UsbCommInternal::canScanQueueTryReceive(frame)) {
        // Max line length: {"can":1,"id":536870911,"len":8,"d":[255,255,255,255,255,255,255,255]}
        // ≈ 70 chars — 96-byte buffer is safe.
        char buf[96];
        char *p = buf;
        const char *limit = buf + sizeof(buf) - 4; // reserve \n\0

        p += snprintf(p, static_cast<size_t>(limit - p), "{\"can\":1,\"id\":%lu,\"len\":%u,\"d\":[",
                      static_cast<unsigned long>(frame.id), static_cast<unsigned>(frame.len));

        for (uint8_t i = 0; i < frame.len && p < limit; i++) {
            if (i > 0 && p < limit)
                *p++ = ',';
            p += snprintf(p, static_cast<size_t>(limit - p), "%u",
                          static_cast<unsigned>(frame.data[i]));
        }

        if (p < limit)
            *p++ = ']';
        if (p < limit)
            *p++ = '}';
        if (p < limit)
            *p++ = '\n';
        *p = '\0';

        UsbComm::sendLine(buf);
        drained++;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Cross-module storage owned by this TU — declared in usb_comm_internal.h so
// usb_dispatch.cpp + usb_config_sync.cpp can reach the RX buffer and the
// host-activity timestamp without going through accessor calls on the hot
// path.
// ---------------------------------------------------------------------------

namespace UsbCommInternal {
char *s_rxBuf = nullptr;
volatile uint32_t s_lastHostCmdMs = 0;
} // namespace UsbCommInternal

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void UsbComm::reserveRxBuf() {
    if (UsbCommInternal::s_rxBuf) {
        return;
    }
    // Prefer PSRAM (16 KB is cheap there) so internal DRAM stays available
    // for the WiFi / NimBLE stacks. Fall back to internal DRAM on no-PSRAM
    // boards (WROOM) — the per-byte Serial.read() in tick() can absorb the
    // small PSRAM cache penalty. Halt on alloc failure: USB is a hard
    // dependency for provisioning and silently degrading would lock the
    // user out.
    UsbCommInternal::s_rxBuf =
        static_cast<char *>(heap_caps_malloc(USB_RX_BUF_SIZE, MALLOC_CAP_SPIRAM));
    if (!UsbCommInternal::s_rxBuf) {
        UsbCommInternal::s_rxBuf = static_cast<char *>(
            heap_caps_malloc(USB_RX_BUF_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    }
    if (!UsbCommInternal::s_rxBuf) {
        // Used to halt here, but on heap-starved WROOM boots (empty SPIFFS →
        // font/image cache fragmentation) the BLE realloc fails afterwards
        // anyway. Logging + leaving rxBuf NULL lets the rest of the system
        // boot; init()/tick() degrade USB CDC silently so the user can still
        // reach Settings via BLE / WiFi and recover.
        LOG_ERROR("USB", "rxBuf reserve (%u B) failed — USB CDC config disabled", USB_RX_BUF_SIZE);
    }
}

void UsbComm::init() {
    s_rxPos = 0;
    s_tickCount = 0;
    // Defensive — happy path is reserveRxBuf() from setup() before lv_init();
    // this fallback only fires if main.cpp skipped that call.
    if (!UsbCommInternal::s_rxBuf) {
        reserveRxBuf();
    }
    if (!UsbCommInternal::s_rxBuf) {
        // Heap exhausted (WROOM + missing SPIFFS) — skip rest of init; tick()
        // will short-circuit on null rxBuf too.
        return;
    }
    memset(UsbCommInternal::s_rxBuf, 0, USB_RX_BUF_SIZE);
    if (!s_sinkMutex) {
        s_sinkMutex = xSemaphoreCreateRecursiveMutex();
        if (!s_sinkMutex) {
            // No fatal halt — sendLine() falls back to the unprotected sink
            // call on lock failure, so a missing mutex degrades to "no
            // serialisation against handleLine()" which is still correct as
            // long as TCP and USB never dispatch concurrently.
            LOG_WARN("USB", "Sink mutex alloc failed — TCP/USB will not serialise");
        }
    }
    UsbCommInternal::initResponseBufferMutex();
    // s_canScanQueue (owned by usb_dispatch.cpp) is allocated lazily on first
    // CMD_CAN_SCAN_START so we don't carry ~1 KB of DRAM for a feature that's
    // rarely used (#976).
    LOG_INFO("USB", "USB comm initialized");
}

void UsbComm::sendLine(const char *line) {
    if (!line)
        return;
    const size_t len = strlen(line);

    // Per-task dispatch sink shortcut: if the current task is inside a
    // handleLine() dispatch, route the reply to that caller's sink directly.
    // Reading a thread_local pointer is race-free without the sink mutex,
    // which lets the dispatch hold no global locks across the command body
    // (#1286 — was a 50 ms tail on every other transport's telemetry).
    const SendSink dispatch = t_dispatchSink;

    // Snapshot the global sink pointers under the sink mutex so a concurrent
    // setAuxSink() never exposes a half-written value. The actual sink
    // invocations run AFTER releasing the mutex so a slow sink (e.g. a TCP
    // write to a saturated client) cannot block another task's logger
    // output behind us.
    //
    // On lock-timeout we drop the message rather than reading s_sink /
    // s_auxSink unprotected. A concurrent setAuxSink() write could otherwise
    // expose a torn / alien pointer to other tasks during one dispatch
    // window. Dropping one telemetry tick is preferable to invoking a sink
    // we cannot prove is current. Issue #1286.
    if (!lockSink()) {
        LOG_DEBUG("USB", "sendLine: sink lock timeout — dropping (%u B)",
                  static_cast<unsigned>(len));
        return;
    }
    const SendSink primary = dispatch ? dispatch : s_sink;
    const SendSink aux = s_auxSink;
    unlockSink();

    if (primary)
        primary(line, len);
    if (aux && aux != primary)
        aux(line, len);
}

void UsbComm::handleLine(const char *line, size_t len, SendSink sink) {
    if (!line || len == 0 || !sink)
        return;

    // Record the per-call sink in a thread_local so sendLine() routes replies
    // back to this caller without us having to hold the sink mutex across the
    // whole command body. Previously we swapped the global s_sink under the
    // mutex and kept it locked until handleCommand() returned — that put a
    // 100-200 ms tail on every other transport's sendLine when a PUT_CONFIG
    // hit the LVGL mutex. The thread_local is per-task (USB tick vs WiFi
    // TCP/WS tasks each get their own pointer) so concurrent dispatches do
    // not stomp each other. Issue #1286.
    const SendSink saved = t_dispatchSink;
    t_dispatchSink = sink;

    UsbCommInternal::handleCommand(line);

    t_dispatchSink = saved;

    (void)len; // reserved for future length-aware dispatch paths
}

bool UsbComm::hasAuxSink() {
    const bool locked = lockSink();
    const bool hasIt = (s_auxSink != nullptr);
    if (locked)
        unlockSink();
    return hasIt;
}

void UsbComm::setAuxSink(SendSink sink) {
    const bool locked = lockSink();
    s_auxSink = sink;
    if (locked)
        unlockSink();
}

void UsbComm::setBurnOverlayShowCallback(BurnOverlayShowCb cb) {
    s_burnOverlayShowCb = cb;
}

void UsbComm::setBurnOverlayShowErrorCallback(BurnOverlayShowErrorCb cb) {
    s_burnOverlayShowErrorCb = cb;
}

void UsbCommInternal::invokeBurnOverlayShow() {
    // Snapshot to a local so a concurrent setter never races a non-null check
    // against a tear-down. Pointer reads are atomic on ESP32; no mutex needed.
    const UsbComm::BurnOverlayShowCb cb = s_burnOverlayShowCb;
    if (cb)
        cb();
}

void UsbCommInternal::invokeBurnOverlayShowError(int reason) {
    const UsbComm::BurnOverlayShowErrorCb cb = s_burnOverlayShowErrorCb;
    if (cb)
        cb(reason);
}

bool UsbComm::isHostActive() {
    constexpr uint32_t HOST_TIMEOUT_MS = 5000;
    if (UsbCommInternal::s_lastHostCmdMs == 0)
        return false;
    return (millis() - UsbCommInternal::s_lastHostCmdMs) < HOST_TIMEOUT_MS;
}

void UsbComm::updateCanStats(uint32_t fpsX10, uint32_t errors) {
    portENTER_CRITICAL(&s_canStatsMux);
    s_canStats.fpsX10 = fpsX10;
    s_canStats.errors = errors;
    portEXIT_CRITICAL(&s_canStatsMux);
    s_canStatsPending = true;
}

bool UsbComm::pushCanFrame(const CanScanFrame &frame) {
    if (!UsbCommInternal::canScanModeActive())
        return false;
    // Hold the spinlock across xQueueSend so CMD_CAN_SCAN_STOP on the USB task
    // cannot vQueueDelete the handle between our null-check and our send
    // (#1042 — closes the residual µs window #1041 left). Safe because
    // xQueueSend with timeout=0 never yields, and ESP-IDF's queue internal
    // spinlock is acquired strictly *after* this outer MUX in both paths
    // (here and in CMD_CAN_SCAN_STOP's vQueueDelete) — same nesting order,
    // no AB-BA. drainCanScanQueue's xQueueReceive only takes the inner queue
    // spinlock, so it can never block while holding a lock we also need.
    return UsbCommInternal::canScanQueueTrySend(frame);
}

void UsbComm::tick() {
    // USB CDC config push disabled (rxBuf alloc failed at boot). Stay silent
    // — the BLE / WiFi paths remain available for user recovery.
    if (!UsbCommInternal::s_rxBuf) {
        return;
    }

    // Abort any chunked transfer that has stalled (host crashed mid-stream
    // or unplugged) — leaves storage in a clean state. Driven from the
    // dispatch module which owns the chunk-transfer state.
    UsbCommInternal::tickChunkTransferTimeout();

    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        // Update host activity on any byte received, not just complete commands.
        // This keeps the top-bar USB icon green while the studio's serial port
        // is open even between command lines.
        UsbCommInternal::s_lastHostCmdMs = millis();

        if (c == '\n') {
            UsbCommInternal::s_rxBuf[s_rxPos] = '\0';
            if (s_rxPos > 0) {
                UsbCommInternal::handleCommand(UsbCommInternal::s_rxBuf);
            }
            s_rxPos = 0;
        } else if (s_rxPos < USB_RX_BUF_SIZE - 1) {
            UsbCommInternal::s_rxBuf[s_rxPos++] = c;
        } else {
            LOG_WARN("USB", "RX buffer overflow — discarding packet");
            s_rxPos = 0;
        }
    }

    // Drain CAN scan queue — send queued frames before telemetry
    drainCanScanQueue();

    // Emit CAN health stats if the CAN task pushed new data
    if (s_canStatsPending) {
        s_canStatsPending = false;
        // Format: {"can_stat":1,"fps":12.5,"errors":0}\n
        // Worst-case with all UINT32_MAX operands:
        //   prefix 20 + fpsX10/10 (9) + '.' (1) + fpsX10%10 (1)
        //   + "," "errors":"  (10) + errors (10) + "}\n" (2) + '\0' (1) = 54
        // 72 bytes gives headroom for future protocol additions (#1161).
        static constexpr size_t CAN_STAT_BUF_WORST_CASE = 54;
        static_assert(72 >= CAN_STAT_BUF_WORST_CASE,
                      "statBuf too small for worst-case can_stat payload");
        char statBuf[72];
        CanHealthStats stats;
        portENTER_CRITICAL(&s_canStatsMux);
        stats = s_canStats;
        portEXIT_CRITICAL(&s_canStatsMux);
        const int n =
            snprintf(statBuf, sizeof(statBuf), "{\"can_stat\":1,\"fps\":%lu.%lu,\"errors\":%lu}\n",
                     static_cast<unsigned long>(stats.fpsX10 / 10),
                     static_cast<unsigned long>(stats.fpsX10 % 10),
                     static_cast<unsigned long>(stats.errors));
        if (n > 0 && static_cast<size_t>(n) < sizeof(statBuf)) {
            UsbComm::sendLine(statBuf);
        } else {
            LOG_WARN("USB", "can_stat payload truncated (n=%d, cap=%u)", n,
                     static_cast<unsigned>(sizeof(statBuf)));
            ErrorStore::push(ERROR_SRC_SYSTEM, "USB_TRUNC", "can_stat payload truncated");
        }
    }

    if (++s_tickCount >= TELE_PERIOD_TICKS) {
        s_tickCount = 0;
        sendTelemetry();
    }
}
