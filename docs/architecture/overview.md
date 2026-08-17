# Overview

The firmware is one Arduino-ESP32 binary that wakes up four FreeRTOS tasks, each owning a clearly-defined slice of the runtime. Nothing in here is exotic — the load-bearing decisions are heap budgets, mutex ownership, and a handful of sequencing rules. Get those right and the rest is widgets.

## The runtime, in one picture

<pre class="cs-ascii">{`
                      ┌────────────────────────────────────────┐
                      │              taskUI  (core 1)          │
                      │  lv_task_handler · widget tick · gesture│
                      │  owns g_lvglMutex                      │
                      └──┬──────────────┬────────────────┬─────┘
                         │              │                │
                  reads  │       reads  │       reads    │ tick / draw
                         ▼              ▼                ▼
                ┌─────────────┐ ┌──────────────┐ ┌──────────────┐
                │ SignalStore │ │  ErrorStore  │ │  PageManager │
                │  portMUX    │ │  portMUX     │ │  LVGL trees  │
                └──────▲──────┘ └──────▲───────┘ └──────────────┘
                       │ writes        │ writes
       ┌───────────────┴────────────┐  │
       │                            │  │
┌──────┴─────────┐         ┌────────┴──┴────────┐         ┌──────────────────┐
│ taskCAN core 0 │         │   taskUSB  core 0   │         │   taskBLE core 0 │
│ TWAI → decode  │         │ JSON lines · 10 Hz  │         │ NimBLE GATT      │
│ ECU profiles   │         │ PUT_CONFIG · scan   │         │ telemetry mirror │
└────────┬───────┘         └─────────┬───────────┘         └────────┬─────────┘
         │                           │                              │
         ▼                           ▼                              ▼
     CAN bus                  Web Serial (tuner)                Mobile app
`}</pre>

Tasks on **core 0** handle I/O — CAN, USB, BLE. **Core 1** is reserved for the UI loop so a stalled host link cannot cost a frame. Cross-task state lives in the two stores (`SignalStore`, `ErrorStore`) and is guarded by `portMUX_TYPE` — RMW under a critical section, no LOG / no ALLOC / no LOCK inside it.

## Where to look next

<ul class="cs-doorways not-content">
  <li>
    <a href="/technical/architecture/boot-sequence/">
      <div class="kicker">Step 1 · cold start</div>
      <h3>Boot sequence</h3>
      <p>
        Heap reservation order, the contiguous-block budget, why <code>lv_init</code> goes after BLE
        and before USB rxBuf.
      </p>
      <div class="arrow">Read →</div>
    </a>
  </li>
  <li>
    <a href="/technical/architecture/lvgl-ownership/">
      <div class="kicker">Step 2 · concurrency</div>
      <h3>LVGL ownership</h3>
      <p>
        Who may call <code>lv_*</code>, who must take <code>g_lvglMutex</code>, and where the
        deadlocks live.
      </p>
      <div class="arrow">Read →</div>
    </a>
  </li>
  <li>
    <a href="/technical/architecture/page-lifecycle/">
      <div class="kicker">Step 3 · the UI</div>
      <h3>Page lifecycle</h3>
      <p>Build, lazy build, release. Why a fresh page hesitates and how the swipe stays smooth.</p>
      <div class="arrow">Read →</div>
    </a>
  </li>
  <li>
    <a href="/technical/architecture/usb-transport/">
      <div class="kicker">Step 4 · host link</div>
      <h3>USB transport</h3>
      <p>
        Sinks, the per-task dispatch shortcut, the PUT_CONFIG burn flow, and why a custom brace walk
        parses the envelope.
      </p>
      <div class="arrow">Read →</div>
    </a>
  </li>
  <li>
    <a href="/technical/architecture/ble-transport/">
      <div class="kicker">Step 5 · mobile link</div>
      <h3>BLE transport</h3>
      <p>
        NimBLE topology, GATT layout, and the stop-race snapshot pattern that keeps disconnect
        deterministic.
      </p>
      <div class="arrow">Read →</div>
    </a>
  </li>
  <li>
    <a href="/technical/architecture/cruise-template/">
      <div class="kicker">Step 6 · a hard widget</div>
      <h3>Cruise template</h3>
      <p>
        The L-shape buttons, the LVGL convex-polygon workaround, and the per-corner Bezier sampling.
      </p>
      <div class="arrow">Read →</div>
    </a>
  </li>
  <li>
    <a href="/technical/architecture/error-store/">
      <div class="kicker">Cross-cutting</div>
      <h3>ErrorStore</h3>
      <p>
        The ring buffer that backs the ErrorBar, with the same critical-section invariant as
        SignalStore.
      </p>
      <div class="arrow">Read →</div>
    </a>
  </li>
  <li>
    <a href="/technical/architecture/signal-store/">
      <div class="kicker">Cross-cutting</div>
      <h3>SignalStore</h3>
      <p>
        Runtime signal table — <code>taskCAN</code> writes, the rest read. Critical-section
        invariant in detail.
      </p>
      <div class="arrow">Read →</div>
    </a>
  </li>
</ul>

## Three rules that explain most of the source

<div class="cs-spec-sheet">
  <div class="title">Invariants worth memorising</div>
  <dl>
    <dt>Heap order</dt>
    <dd>
      Anything contiguous &gt; 16 KB must be reserved <strong>before</strong> <code>lv_init</code>.
      The post-init largest free block drops to ~13–18 KB on a no-PSRAM WROOM.
    </dd>
    <dt>LVGL ownership</dt>
    <dd>
      Any <code>lv_*</code> call from outside <code>taskUI</code> must hold <code>g_lvglMutex</code>
      . taskUI never needs to take it — it owns the lock by being the only writer.
    </dd>
    <dt>Store critical sections</dt>
    <dd>
      SignalStore and ErrorStore mutate under <code>portMUX_TYPE</code>.{' '}
      <strong>No LOG, no ALLOC, no LOCK</strong> inside <code>portENTER_CRITICAL</code>.
    </dd>
  </dl>
</div>

## Reference shortcuts

- [Pinout](../reference/pinout.md) — GPIO contract for the reference build.
- [Build flags](../reference/build-flags.md) — compile-time switches (panel, CAN baud, BLE on/off).
- [Signals](../reference/signals.md) — every signal the firmware knows about, with units and source.
- [Dev setup](../contributing/dev-setup.md) — sibling clones, per-repo build and CI.
