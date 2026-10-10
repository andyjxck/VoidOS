# VoidOS — PineTime product and engineering plan

Status: proposed architecture and roadmap, 29 September 2026. This is a plan for a **new project**, not a description of implemented software. The existing `/Users/andrewblewett/VoidOS` is outside this project and must not be changed or copied implicitly.

## 1. Product definition

VoidOS is a **fitness-first, independent daily-wear watch** for the PineTime and a first-party device in the gym-void ecosystem. The watch captures workouts, movement, sleep and heart-rate samples *itself*, even with the phone absent; gym-void is the rich planning, nutrition, history and analysis surface. It also needs everyday watch essentials: time, alarms, settings, discreet notifications, a reliable phone link and safe firmware updates. The intended experience is quiet, glanceable and touch-first, with the side button always providing a predictable way home. Product positioning is a proposal for owner confirmation, not an assumption that the existing Apple Watch app should be copied.

It is *not* desktop Linux, a general-purpose app sandbox, or a medical device. “OS” means the complete embedded firmware, system services, UI and applications running on the microcontroller. The Apple Watch app in `gym-void/targets/watch/` is a useful **feature inventory**, not an architecture or screen-design template: its `WatchSessionManager` receives phone-owned snapshots over WatchConnectivity and sends commands; VoidOS must replace those platform services with its own on-watch state, storage, sensors and BLE protocol. gym-void already imports `sleep_hours` from phone health platforms, but its Apple Watch target does **not** record overnight sleep independently. `gym-void/targets/watch/WatchSessionManager.swift` and `gym-void/src/lib/healthImport.ts` are referenced for research only; no code is copied or changed there.

**Product principles:** original UI rather than cloning the Apple Watch app or InfiniTime; legible at arm's length; no cloud account required; offline-first personal data; low power by default; no critical action reachable only by a swipe; safe recovery before installing on a sealed watch; all hardware-dependent operations behind explicit interfaces.

## 2. Verified device facts and their design consequences

| PineTime capability | Verified fact | Consequence for VoidOS |
| --- | --- | --- |
| MCU and memory | nRF52832, 64 MHz Cortex-M4; PineTime has 512 KiB internal flash and 64 KiB RAM | Budget BLE, stacks, UI, and graphics together; produce linker map and peak-RAM measurements on every firmware build. No Linux, process isolation, or unrestricted third-party apps. |
| Display | 1.3-inch 240 × 240 RGB565 IPS panel, ST7789 controller; display variants ST7789V2 before May 2026 and ST7789P3 from May 2026 | One pixel coordinate system across simulator/hardware; identify the controller variant from board/batch information or verified behavior, configure the correct init path, and verify both before broad release. 240 × 240 × 2 = 115,200 bytes, so a full RGB565 framebuffer cannot reside in internal RAM. |
| Display bus | Display uses SPI; documented mode 3 and up to 8 MHz, with chip select shared with external flash bus | Central SPI arbitration, bounded transfers, display dirty-region rendering, and careful flash/display mutual exclusion. Do not assume desktop render throughput on hardware. |
| Input | CST816S capacitive touch on I²C (address 0x15) with interrupt; one side button | Normalize raw taps, gestures, button press/release in HAL; handle touch-controller sleep, bus timeouts, debounce and screen wake. Button must work when touch fails. |
| Sensors | HRS3300 optical heart-rate sensor; Bosch BMA421 on early units / BMA425 on current units | Detect/abstract accelerometer variant, calibrate motion features, gate HR sampling for power; mark HR/steps as wellness estimates, not medical readings. |
| Storage | 4 MiB external SPI NOR in addition to internal flash | Separate firmware update slots from settings/history/assets; do not assume an agreed universal partition layout. Inspect installed bootloader layout before assigning offsets or erasing anything. |
| Power and output | 170–180 mAh battery, charging/battery sensing, three backlight control lines, vibration motor, BLE radio | Screen-off sleep and event-driven wake are fundamental; brightness/vibration/HR/BLE duty cycles are explicit policies, not UI afterthoughts. |
| Connectivity | Bluetooth Low Energy; phone integration supplied by software, not cellular/Wi-Fi hardware | Time sync, notifications, weather, music controls, phone location and OTA require a companion/compatible BLE central. GPS routes, calls, audio playback, camera, Wi-Fi and standalone internet are not native watch features. |
| Deployment | Common sealed watches ship with InfiniTime and an MCUBoot-based bootloader; devkit exposes SWD | Bring up on an open devkit with a debugger first; preserve the bootloader and recovery path. Sealed OTA is a later gated release, never the first hardware test. |

Sources: [PINE64 specifications](https://pine64.org/documentation/PineTime/Further_information/Specifications/), [PINE64 hardware notes](https://pine64.org/documentation/PineTime/Hardware/), [PINE64 component datasheets and display/sensor revisions](https://pine64.org/documentation/PineTime/Further_information/Datasheets_schematics_and_certifications/), [Nordic nRF52832 product specification](https://infocenter.nordicsemi.com/topic/struct_nrf52/struct/nrf52832_ps.html), [InfiniTime bootloader description](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/bootloader/README.md), [PINE64 flashing FAQ](https://pine64.org/documentation/PineTime/FAQ/). Hardware documentation describes families and revisions; inspect the particular watch before fixing driver assumptions.

### Hard limits and explicit exclusions

- No required full-screen 16-bit RAM framebuffer, desktop-only rendering dependency, dynamic asset loading into large buffers, or heavy runtime on the watch. SDL2 is **only** a simulator dependency.
- No on-watch GPS tracks without phone-provided location; no microphone/speaker features unless hardware revision evidence contradicts current device documentation; no oxygen saturation, ECG, medical diagnosis, or guaranteed continuous HR accuracy.
- BLE compatibility with Gadgetbridge/InfiniTime is **not automatic** just because the radio is compatible. A custom VoidOS service needs a cooperating companion; protocol compatibility is a separate integration project.
- A firmware image that boots over SWD is **not** automatically safe for OTA. Image format, memory map, staging, validation, and rollback must match the installed bootloader.

## 3. Complete user experience and feature map

### Interaction model

- **Sleep/lock:** normally screen off; side button wakes, optional wrist raise/double tap only after reliable motion/touch wake and battery tests. Lock screen shows time, date, battery, and connectivity. Dim/timeout configurable; do-not-disturb suppresses alerts, not alarms.
- **Home/watch face:** full-size analogue dial for the square 240 × 240 display with consistent, evenly positioned numerals and no hour-tick strokes colliding with glyphs; red heart icon with BPM (`--` until measured) and no boxed complication backgrounds. When the white hour hand points into the lower semicircle from 3:00 through 9:00, show BPM in the upper part of the dial; from 9:01 through 2:59, show it below the hand. Battery status and icon belong in the swipe-down control centre rather than a corner of the face, where the physical watch shape may hide them. Selected CC0 Jupiteroid typeface is a simulator TTF until converted into an embedded bitmap. Steps and detailed health metrics belong on the stats page, not a face footer.
- **Navigation:** from face, swipe **down** (top to bottom) for control centre (battery, brightness, DND, connection and settings), swipe **up** (bottom to top) for stats (steps, HR, sleep, HRV availability, workouts), swipe **right** (left to right) for phone notifications, and swipe **left** (right to left) for nutrition/hydration from gym-void. Tap the face or short-press the button while on the face to open the app launcher, so workout, sleep, water, settings and clock remain reachable. In an app the short button goes back one level; from screen-off it wakes; a long press in running firmware may later show power/restart. Bootloader hold gestures remain untouched. Every destination has a clear back/home action. Gesture recognition needs tests for direction, thresholds and accidental taps.
- **Glanceability:** meaningful state at 240 × 240, generous touch targets, truncation/scroll for long text, vibration patterns for confirmation/alerts, and reduced animation mode. Text and palette reviewed on real display in bright and dim conditions.

### What gym-void actually provides today

The phone is an offline-first strength tracker: SQLite is its source of truth and Supabase is optional. Its model contains routines with ordered exercises, planned sets/reps/weights/rest/supersets; live workouts with weight in canonical kg, reps, RPE, hold/carry/cardio variants, rest and pause; cardio sessions; PRs; nutrition and liquid logs; body metrics including `sleep_hours`, resting HR and HRV. Recovery/readiness insights already consume sleep and training data. The phone reads sleep from HealthKit/Health Connect and can log it manually, **but that is not the PineTime collecting sleep data**. See `gym-void/AGENTS.md`, `src/lib/types.ts`, `src/store/activeWorkout.ts`, `src/lib/healthImport.ts`, and `src/lib/insights.ts`.

Its current Apple Watch target shows phone-supplied water/food/step rings, routines/PRs, hydration and nutrition detail; sends commands for quick water, sets, rest, routines, cardio and barcode lookup; uses Apple HealthKit for HR and CoreLocation for GPS. A PineTime has neither HealthKit nor a GPS receiver or barcode camera. Preserve the *tasks users need*, not those UI screens, snapshot architecture or hardware assumptions. See `gym-void/targets/watch/ContentView.swift`, `WatchSessionManager.swift` and `src/lib/watchSync.ts`.

### VoidOS feature commitments and division of labor

| Area | On the independent watch | In gym-void / companion | Priority and proof |
| --- | --- | --- | --- |
| Watch essentials | Clock, date, configurable face, battery/charging, alarms/snooze, timer, stopwatch, vibration, display/sleep settings, DND | Initial time, settings backup | **Foundation**; all tools work offline and survive restart |
| Steps and movement | Track steps continuously through day, goals, hourly movement prompts, history, optional wrist-raise | Show trends, avoid importing identical steps twice from other health sources | **Core**; measured walking/rest tests and sleep-time battery profile |
| Strength training | Start cached routine or quick session without phone; inspect exercise/targets/previous set; log sets, weight, reps, RPE, hold/carry time, rest, pause, skip and end; haptic rest completion; resume after reset | Author/edit full routine library, PR calculation, large history and advanced analysis | **Core differentiator**; offline workout with multiple sets syncs once, correctly, after reconnection |
| Cardio | Walk/run/cycle/indoor timed sessions, pause/resume, elapsed time, HR trend and steps; distance from calibrated steps **only where meaningful** | Optional phone GPS route/distance and activity import | **Core**; watch-only session works with no fictitious GPS trace |
| Sleep and wake | **New capability:** overnight on-watch movement logging, time-in-bed and estimated sleep interval, quiet sleep mode, nightly summary, gentle haptic alarm, manual start/stop/correction | Nightly trends, recovery factors, optional health-platform export/import with source precedence | **Core differentiator**; no phone needed overnight; missing/low-quality readings visible; validate against wearer sleep diary |
| Heart rate and recovery | On-demand HR, sampled workout HR, optional sparse overnight HR after power validation, resting HR trend where defensible; simple morning readiness display with explicit inputs | Compute richer recovery/readiness from sleep, training load, feel, food and imported metrics | **Core**; invalid contact never displayed as valid BPM; no invented HRV from unsupported-quality measurements |
| Hydration and nutrition | Fast water adds, daily water/protein/calorie progress from latest synced phone snapshot, explicit stale badge; optionally quick-add favorite drink/meal *only if locally cached and reconciled* | Full meal recipes, barcode camera/search, macro calculations, serving edits, canonical logs | **Core hydration, later cached food**; offline water logs sync without duplication; no barcode scanner on watch |
| Training guidance | Next planned session, rest countdown, recent PR/high-level progress, today's recovery context, training reminder | Routine planning, large history, insights and recommendations | **Later core**; guidance states why data is absent/stale; no confident advice from incomplete sleep/HR |
| Connected watch | Pairing, discrete notification preview, quick-settings connection status, optionally music control/find-phone/weather | Phone notification relay, media controls, weather and data transfer | **Connected phase**; phone absence never breaks exercise/sleep/clock |
| Settings, privacy and updates | Local metric/imperial display, time, motion wake, brightness, vibration, sleep/HR sampling and retention, health export consent, diagnostics, update progress | Pairing management, archive/export and firmware delivery | **Foundational**; settings persisted and recoverable; safe rollback before sealed-watch release |

**A day with VoidOS:** wake to a gentle vibration and last night's sleep-duration estimate (or `insufficient data`); glance at time, recovery context and today's routine; start the cached routine at the gym without carrying a phone; tap a set complete, adjust weight/reps/RPE, feel the rest timer end; review a compact session summary, log water, see steps through the day; enter quiet sleep mode at night. After reconnection, gym-void shows the same workout, hydration, steps and sleep night with explicit PineTime provenance and no duplicates. Nutrition planning, exercise-library edits and deep analytics stay on the phone. This is the primary end-to-end product scenario that gates release.

**Sleep plan, deliberately honest:** phase A is explicit bedtime/wake capture plus low-duty-cycle accelerometer epochs and optional sparse HR; distinguish *time in bed* from *estimated sleep*, mark gaps and allow corrections. Phase B is measured actigraphy-based automatic start/end and awakenings **only after** diary/comparison validation on real watches. Never label REM/deep/light stages, sleep apnea, a clinical score or reliable HRV without appropriate validation; the HRS3300 is not a medical sensor. Persist nightly data on-device so an overnight phone disconnect loses nothing. Existing PineTime sleep-tracking work demonstrates a feasible direction but has unresolved crash/power questions; [InfiniTime sleep PR #2304](https://github.com/InfiniTimeOrg/InfiniTime/pull/2304) is an open proposal, not shipped behavior.

**Companion strategy:** gym-void is the intended phone-side product, not a replacement watch UI. Define a small, versioned VoidOS BLE service and native Expo integration on the phone; prototype connection, background behavior and permissions on Android **and** iOS before promising automatic sync on either. The PineTime uses its own BLE stack; Apple Watch `WatchConnectivity`, `HealthKit`, `CoreLocation` and watchOS assets cannot be reused. A non-Apple watch cannot obtain iPhone system notifications just by pairing; verify iOS notification transport and limitations as a separate feasibility gate. Gadgetbridge interoperability is optional and requires explicit protocol work. Phone optional cloud sync is not needed for watch operation.

## 4. Software architecture and technology choices

**Language/toolchain:** C11 for portable core, drivers and embedded startup; macOS clang for simulator, arm-none-eabi GCC for firmware. Build configuration will pin known-good SDK/RTOS/BLE versions rather than float dependencies. Use a small, fixed-size C API between subsystems. Initial hardware execution is event-driven; FreeRTOS + NimBLE is the selected BLE-era architecture because it has PineTime precedent in InfiniTime, but its actual memory fit must be demonstrated by a minimal firmware before locking it in. If it fails budgets, revisit scheduling/stack before increasing product scope. Zephyr and embedded Rust remain port possibilities, not simultaneous supported targets.

```text
applications (face, clock tools, notifications, activity, HR, workouts, settings)
       ↓ commands/events, no device-specific headers
system services (navigation, time, alarms, motion, HR, BLE sync, store, power)
       ↓ stable capability-based HAL interfaces
HAL contracts (display, touch/button, RTC/timers, accel/HR, battery/backlight,
               haptics, storage, BLE transport, wake/sleep, diagnostics)
       ↙                                      ↘
simulator backend (SDL2 + deterministic        PineTime backend (nRF52 buses,
virtual clock/sensor/phone/battery fixtures)    ST7789/CST816S/BMA42x/HRS3300,
                                               NOR, BLE, boot integration)
```

- **Event model:** timestamped, typed events with bounded queues; input normalization and gesture recognition separated from screens; slow I²C/SPI/BLE operations cannot block UI responsiveness indefinitely. Explicit ownership and cancellation on screen exit.
- **Display:** portable RGB565 drawing command list or bounded line/tile rasterization, clipping and dirty regions; identical geometry/font assets on simulator and device. Choose tile size and maximum command count after memory/performance profiling. Hardware backend owns SPI transactions and display variant initialization. Desktop may hold a full texture, but core never assumes it exists. Pre-bake small bitmap/vector-derived fonts and icons in ROM with licensing tracked.
- **State and persistence:** versioned records with length/CRC, atomic or journaled writes and migration tests; settings, workouts, sleep epochs and sync outbox separated from DFU staging. Flash writes are infrequent, batched, bounded, and power-loss tested. Use an existing lightweight flash filesystem only if it fits measured budgets and respects bootloader partitions; otherwise use narrow records, not an ad-hoc general filesystem.
- **Workout ownership and sync:** the watch owns an offline session started on the watch; the phone imports its events and presents a read-through view until reconciliation. A workout started on the phone is phone-owned and the watch acts as a remote controller only while connected. Never run two authoritative live state machines for the same session. Each watch-originated event has stable `(device_id, event_id)` identity, workout/set IDs, UTC timestamp, sequence and protocol version. Persist outbox before acknowledging locally, transfer in bounded batches, ingest transactionally and idempotently on phone, acknowledge checkpoint only after phone storage commits. Reconnect/retry, time correction, paired-device changes, duplicate imports and simultaneous edits need explicit conflict policy. Canonical kg, metres and ml cross BLE; display-unit conversions live at the UI edge. The existing gym-void SQLite repositories and merge/sync model stay the phone's authority for imported logs; any phone schema changes are a separate reviewed task, not part of this planning edit.
- **Health provenance:** on-watch sleep needs a session/epoch record; gym-void's current `sleep_hours` measurement alone cannot represent nights, confidence, gaps and corrections. Introduce versioned phone sleep session/summary storage after agreeing a schema migration. Derived `sleep_hours` should reference the watch source ID; reconcile with HealthKit/Health Connect/manual values by identity, source priority and user correction, **not** by blindly summing or suppressing a whole date. Likewise distinguish PineTime steps/cardio/HR from Apple Watch and other imported device sources; do not silently replace manual health logs.
- **Time:** RTC monotonic ticks for timers and scheduled wakes; wall-clock time, timezone and DST supplied/configured separately. Timestamp sleep by wake date with timezone metadata and retain the original UTC instants. Test clock corrections, leap days, timezone changes and disconnected use. Avoid treating host `time()` as portable watch code.
- **Power:** display-off idle as default, tickless/event sleep where supported, wake from button/RTC/sensor interrupt, peripheral suspend, bounded BLE advertising/connection policy. Battery telemetry and charging status are separate from estimated percentage. Measure with debug circuitry disabled under a documented profile.
- **BLE/privacy:** standard GATT services where appropriate (Device Information, Battery, Heart Rate, Current Time role as supported), versioned custom UUIDs for VoidOS features, pairing/bonding and explicit trust boundary for notifications/settings/DFU. Authenticate/authorize sensitive writes, bounds-check all BLE payloads, and do not persist notification bodies by default. Bootloader image acceptance/security depends on the **actual installed bootloader configuration**; verify rather than assume signed-image enforcement.
- **Fault handling:** watchdog, bounded queue/buffer behavior, safe defaults on sensor failure, recoverable storage corruption, cold-boot and post-update self-test, and diagnostics that do not leak private notification contents.

### Proposed repository structure (create incrementally, not all at once)

```text
VoidOS-New/
├── PLAN.md
├── README.md
├── CMakeLists.txt                 shared targets, simulator and later firmware presets
├── include/voidos/              HAL contracts, event types, model APIs
├── core/                        renderer, navigation, services, apps
├── assets/                      compact fonts/icons and source/licensing manifests
├── platform/
│   ├── sdl/                     macOS simulator, virtual hardware and scenarios
│   └── pinetime/                board definition, drivers, startup, linker/boot config
├── protocol/                    versioned BLE messages and test vectors
├── companion/                   integration client, then product companion if chosen
├── tests/                       native unit, scenario, protocol, fault-injection tests
└── tools/                       assets, size reports, packaging and test utilities
```

CMake is the long-term multi-target build; the first simulator can use a minimal Makefile if it keeps onboarding simpler, but no independent duplicated source lists after firmware work begins. Third-party code is reviewed for license, footprint and pinning before vendoring.

## 5. Delivery roadmap: dependencies and exit gates

Each stage adds to the **same product**, not a disposable demo. A stage is not complete merely because the desktop window opens.

### Stage 0 — hardware and release baseline

- Record the exact PineTime revision, open devkit/sealed status, display/accelerometer IDs, installed bootloader version, flash map and known recovery method. Obtain an **open devkit and SWD debugger** before writing hardware deployment instructions.
- Capture source-of-truth pin map, bootloader image/DFU requirements, license inventory and an external-flash usage map that explicitly reserves bootloader staging/rollback space. Document a safe test-only flash procedure; do not overwrite the bootloader.
- Exit: hardware matrix and recovery procedure reviewed; no unresolved assumptions about target slots or drivers before flashing.

### Stage 1 — shared engine and macOS simulator

- Create the new project and HAL contracts; SDL2 240 × 240 logical display enlarged 2×, touch drag/swipe/button mappings, simulated clock/battery/sensors/BLE events and record/replay scenarios. Implement small font/primitive renderer and original design tokens.
- Build an original full-dial watch face and four swipe destinations (control centre, health/training stats, phone notifications and gym-void nutrition/hydration) plus tap/button app launcher, strength session, sleep/night mode, morning summary, clock tools and settings *flows* on simulated state. Phone-dependent values must show an explicit disconnected state, and HRV must show unavailable until measurement quality is validated. Model gym-void routine/set semantics without copying the Apple Watch views. Simulate offline workouts, overnight sleep epochs, reconnection and bad sensor data. Add screenshot baselines and deterministic scenario tests.
- Exit: native build and tests pass; no SDL include or desktop API in core; each major flow has a route/back path, disconnected/empty/error states, and a visible 240 × 240 layout. Completing a multi-set workout and a sleep night without a phone produces durable, replayable watch events in simulation. This stage includes the originally requested minimal test-frame bootstrap as its **first task**, not its final product.

### Stage 2 — safe on-device foundation

- Build nRF52832 firmware on open devkit using verified boot layout; bring up clocks, watchdog, GPIO/button, I²C/SPI with timeouts, ST7789 variant handling, CST816S touch, battery/charging, haptics, sleep/wake, external NOR read-only probe, RTC. Run shared UI with a bounded renderer.
- Exit: cold boot, touch and button navigation, sleep/wake, charging, 30-minute interaction stability and repeatable SWD recovery demonstrated on devkit; firmware map/stack/RAM usage captured; no sealed-watch installation.

### Stage 3 — complete offline watch

- Implement reliable time/date handling, faces, settings/persistence, alarm/timer/stopwatch, battery estimation, step counting with BMA variant tests, on-demand HR with invalid-signal UX, **full routine-based strength logging** (sets, units, RPE, rest, pause and resume), timed cardio, hydration, offline history, and overnight sleep logging with gentle alarm. Cache selected gym-void routines only after import format is settled; allow a locally defined simple routine and quick workout while disconnected.
- Exit: alarms wake from sleep; time tools and settings survive reset; corrupt/power-loss storage tests recover; sensor errors do not freeze UI; sleep/night recording, workout and hydration records remain bounded and recover after reset; a documented workout + overnight + morning-summary user journey works entirely disconnected.

### Stage 4 — connected watch and companion integration

- Prove FreeRTOS/NimBLE memory/power budget in a minimal board build, then implement BLE pairing, time sync and **gym-void bridge first**: download compact routine targets/settings to watch, upload watch-owned workouts/sets/sleep/steps/HR/hydration, replay and acknowledgements, reconcile phone-owned live workouts, import source attribution and sleep summary/epoch schema. Prototype native Expo BLE bridge on actual Android/iOS devices before relying on background transfer; keep phone SQLite authoritative for phone views and optional Supabase sync downstream. Only then implement notifications, weather/music/find-phone transport and third-party compatibility investigation.
- Exit: offline watch-owned workout and night synchronize to gym-void exactly once after reconnect; repeated packets, crashed phone, wrong clock, duplicate health imports, routine revisions and concurrent edits have specified outcomes; binding reset, privacy and phone-feature offline states tested; measured firmware size, RAM headroom and current profile still fit. Document which mobile OS integrations actually work.

### Stage 5 — safe update pipeline and reliability

- Implement tested application-side DFU staging using actual MCUBoot-compatible image/flash layout, progress and integrity validation, post-boot confirmation and rollback. Test interrupted transfer, reset during update, invalid image, storage full and retained settings on an open devkit. Do not change the bootloader merely to simplify development.
- Exit: reproducible DFU package, recovered devkit after every injected failure, validated rollback, written upgrade/recovery runbook; only then consider a sealed-watch beta with explicit user consent.

### Stage 6 — polish and release

- Optimize UI and power on real watches: typography, animation budget, wrist wake, brightness, accessibility/readability, localized date/time policy, sensor accuracy, sleep interval validation and battery characterization. Ship recovery/readiness only with transparent inputs and missing-data states; evaluate automatic sleep detection against sleep diaries before enabling it by default. Verify gym-void onboarding/export, docs, licenses, security review and release packaging.
- Exit: strength workout, sleep night, morning recap, hydration and subsequent phone sync demonstrated on real hardware; release measurements and known limitations published; no critical boot, recovery, data-loss or pairing defects; approved sealed-device upgrade path.

## 6. Quantitative budgets and validation protocol

These are **design gates**, not claims already achieved. Record measured values with build commit, hardware revision, firmware configuration and reproducible profile.

- **Memory:** hard limit is 64 KiB RAM and 512 KiB internal flash *before bootloader/app partitioning*. Set provisional firmware gate at ≤56 KiB measured peak RAM (≥8 KiB headroom) including BLE stacks, worst-case screen and interrupt/RTOS stacks; linker map must fit the *measured* application slot, not all 512 KiB. Revisit exact gate after the bootloader/BLE baseline. No 115,200-byte framebuffer.
- **Graphics:** test region clipping, off-screen draws, RGB565 parity and font truncation on native unit tests and actual display. Record cold-boot-to-watch-face and touch-to-visible-response latency; proposed UX gate <200 ms while awake for common actions, measured under BLE load. Tune frame cadence by activity rather than fixed always-on 30 FPS.
- **Battery:** do not promise a week on assumptions. At 180 mAh, 72 hours implies ~2.5 mA average and 5 days implies ~1.5 mA average (idealized, ignoring usable capacity/aging). First target ≥72 hours for a documented mixed-use profile, then aim at 5 days after current measurements. Specify screen wake count/duration, BLE pairing state, HR and notification frequency, brightness and ambient conditions. Compare idle, connected, workout and worst-case modes; measure real current without debugger leakage.
- **Storage:** test interrupted writes, CRC failures, schema migration, wear and full history capacity. Keep user data out of DFU/rollback partitions. No erase/write on an unknown external flash map. Size watch outbox for a phone-free workout plus multiple nights and document what happens when full; never silently discard unacknowledged user logs.
- **Sleep/fitness validation:** compare bedtime/wake estimates with a wearer diary across multiple nights, awake-in-bed periods and naps; label unobserved intervals, sensor-off wrists and low confidence instead of asserting stages. Validate step counts on walking/rest fixtures and HR only against a reference under stated conditions. Monitor overnight battery drain against a no-sleep-tracking baseline; make periodic HR opt-in or adapt duty cycle if power target fails. No recovery score should imply certainty when sleep/HR data is unavailable.
- **Cross-device sync:** test an offline multi-exercise workout, offline night, local water additions, reconnection, duplicate BLE packets, phone process death before/after commit, incorrect clock, phone-originated edits and cloud-disabled guest mode. Assert one phone workout per watch session, no dropped sets, no double-counted water/steps/sleep, and no data loss on repeated retries.
- **Security/recovery:** fuzz parsers with maximum lengths and malformed BLE packets; exercise pairing removal, unauthorized writes, DFU corruption, post-update confirmation and rollback. Never claim image authenticity unless the deployed bootloader actually verifies it.
- **CI/manual matrix:** native core tests and deterministic SDL screenshots per screen/state; ARM compile/link and size report; protocol compatibility tests against a real client; hardware bench tests on both known display/accelerometer revisions where available; human checks for legibility, gestures and wake/sleep.

## 7. Open decisions to settle with the owner before feature implementation

1. Confirm product direction: **fitness-first standalone VoidOS paired with gym-void**, including independent sleep/workout capture, instead of a generic smartwatch or Apple Watch port. Which proposed feature is missing or over-scoped?
2. Which gym-void platform must sync first: Android, iOS, or both? iOS BLE background behavior and notification relay need real-device investigation; do not promise identical OS support without it.
3. Which PineTime revision(s) and access are available: open devkit with SWD, sealed watch only, or both? Hardware development and safe OTA depend on this.
4. Is overnight sleep tracking a launch-critical feature, and is manual bedtime mode acceptable initially while automatic detection is validated? Commit to duration and movement trends without promising REM/deep-stage classification.
5. What is the desired release posture: personal firmware, open-source community project, or distributable consumer firmware? This determines compatibility, documentation and security obligations.
6. Which visual direction and priority order should be approved before implementing full screen designs? Prepare a small set of 240 × 240 design explorations for selection, rather than porting the Apple Watch rings UI.

Until these are answered, development can proceed on portable infrastructure and simulator scenarios, but we should not imply finalized product positioning or deploy unverified firmware.

## Sources and confidence notes

- Hardware/specs: [PINE64 specifications](https://pine64.org/documentation/PineTime/Further_information/Specifications/), [hardware notes](https://pine64.org/documentation/PineTime/Hardware/), [datasheets and component revision list](https://pine64.org/documentation/PineTime/Further_information/Datasheets_schematics_and_certifications/), [Nordic product specification](https://infocenter.nordicsemi.com/topic/struct_nrf52/struct/nrf52832_ps.html).
- Firmware patterns, not mandatory dependencies: [InfiniTime architecture](https://docs.infinitime.io/en/latest/developer-documentation/index.html), [InfiniTime BLE protocol](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/doc/ble.md), [InfiniTime build and image types](https://docs.infinitime.io/en/latest/build-flash-and-debug/index.html).
- Update/power risks: [MCUBoot behavior](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/bootloader/README.md), [PINE64 FAQ on sealed devices](https://pine64.org/documentation/PineTime/FAQ/), [battery-friendly software](https://pine64.org/documentation/PineTime/Development/Write_battery-friendly_software/).
- PINE64's [external-flash partition discussion](https://pine64.org/documentation/PineTime/Flashing/External_flash_partitioning/) is a **draft**, not proof of a standardized layout. Confirm actual partition offsets for the installed bootloader/firmware on the specific watch before implementation.
