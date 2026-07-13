#include <Arduino.h>

#include <cstdio>

// Pinned ESP-IDF 4.4 Task Watchdog Timer (TWDT) C API (R5-a). Used DIRECTLY --
// deliberately NOT via Arduino's enableLoopWDT()/feedLoopWDT() wrappers (see the
// setup() and control-tick comments). This header also pulls in ESP_ERROR_CHECK
// (via esp_err.h), the fail-fatal wrapper used on every TWDT call below.
#include "esp_task_wdt.h"

// Pinned ESP-IDF 4.4 reset-diagnostics primitives (R5-b): esp_reset_reason()
// lives in esp_system.h, and RTC_NOINIT_ATTR (the .rtc_noinit placement used for
// the retained session struct) lives in esp_attr.h. Both are read-only queries /
// a section attribute -- neither writes NVS or flash.
#include "esp_attr.h"
#include "esp_system.h"

#include "channels/ArmGate.hpp"
#include "channels/ChannelDecoder.hpp"
#include "config/PinMap.hpp"
#include "crsf/CrsfFrameBuilder.hpp"
#include "crsf/CrsfReceiver.hpp"
#include "crsf_hal_esp32/Esp32CrsfUart.hpp"
#include "ers/ErsSystem.hpp"
#include "failsafe/FailsafeStateMachine.hpp"
#include "gearbox/Gearbox.hpp"
#include "hal/IClock.hpp"
#include "link2/Link2Sender.hpp"
#include "link2_hal_esp32/Esp32Link2Uart.hpp"
#include "outputs/DrsOutput.hpp"
#include "outputs/EscOutput.hpp"
#include "outputs/ServoOutput.hpp"
#include "outputs_hal_esp32/Esp32LedcPwm.hpp"
#include "reset_diag/ResetDiagnostics.hpp"
#include "telemetry/BatteryMonitor.hpp"
#include "telemetry/WheelSpeed.hpp"
#include "telemetry_hal_esp32/Esp32BatteryAdc.hpp"
#include "telemetry_hal_esp32/Esp32HallPulseCounter.hpp"

#ifdef W17_SIM_CRSF_FEEDER
#include "SimCrsfFeeder.hpp" // Wokwi Stage-2 harness, absent from real firmware
#endif

// Persisted tuning is LOADED on every build (delivery included): read-only NVS
// via the shared validated loader. The console that MUTATES it is tuning-only,
// pulled in behind the flag (its UART0 char-IO lives in console_hal_esp32, so
// the delivery build never compiles that translation unit).
#include "settings/Settings.hpp"
#include "settings/SettingsLoader.hpp"
#include "settings_hal_esp32/Esp32NvsStore.hpp"

#ifdef W17_TUNING_CONSOLE
#include "console/ConsoleRunner.hpp"
#include "console_hal_esp32/Esp32SerialConsole.hpp"
#endif

namespace {

// Real-clock hal::IClock backed by millis(), used by EscOutput's boot-arm hold.
class Esp32MillisClock : public hal::IClock {
public:
    uint32_t nowMs() const override { return millis(); }
};

crsf_hal_esp32::Esp32CrsfUart crsfUart(pinmap::kCrsfUartRxPin, pinmap::kCrsfUartTxPin);

// Owns frame assembly + typed dispatch; exposes channels, link stats, and the
// RX failsafe signal (latched uplink-LQ==0 from LINK_STATISTICS frames).
crsf::CrsfReceiver crsfReceiver;

// The channel map lives here, in one place; a bad index fails the build.
constexpr channels::ChannelMapConfig kChannelMap{};
static_assert(kChannelMap.valid(), "channel map: index out of range or bad thresholds");

channels::ChannelDecoder channelDecoder(kChannelMap);
channels::ArmGate armGate;
channels::Controls controls; // most recently decoded controls (all-neutral until a frame)

constexpr gearbox::GearboxConfig kGearboxConfig{};
static_assert(kGearboxConfig.valid(), "gearbox: bad gear table (range or non-monotonic)");
gearbox::Gearbox virtualGearbox(kGearboxConfig);

// Drive modes (3-pos switch): 0 = TRAINING, 1 = RACE (gearbox; mid = default,
// also what an absent channel decodes to), 2 = ERS (gearbox+ERS). There is
// deliberately NO raw pass-through mode: gearbox top gear (cap 1000, expo 0)
// already IS full power, and power stays monotone along the switch so a bumped
// switch changes authority by one gentle step.
// TRAINING: one fixed gentle shape, gear shifts have no effect.
constexpr gearbox::GearParams kTrainingGearParams{400, 50};

constexpr ers::ErsConfig kErsConfig{};
static_assert(kErsConfig.valid(), "ers: bad rate/bonus/threshold values");
ers::ErsSystem ersSystem(kErsConfig);

failsafe::FailsafeStateMachine failsafeStateMachine;

Esp32MillisClock clock;

// Default configs, named so setup() can hand their safe positions to the PWM
// layer as the initial pulse.
constexpr outputs::ServoConfig steeringConfig{};
constexpr outputs::EscConfig escConfig{};
constexpr outputs::DrsConfig drsConfig{};

// Camera gimbal servos (MG90S positional): default ServoConfig endpoints work.
constexpr outputs::ServoConfig gimbalConfig{};

outputs_hal_esp32::Esp32LedcPwm steeringPwm(pinmap::kSteeringServoPin, /*channel=*/0);
outputs_hal_esp32::Esp32LedcPwm escPwm(pinmap::kEscThrottlePin, /*channel=*/1);
outputs_hal_esp32::Esp32LedcPwm drsPwm(pinmap::kDrsServoPin, /*channel=*/2);
outputs_hal_esp32::Esp32LedcPwm panPwm(pinmap::kGimbalPanPin, /*channel=*/3);
outputs_hal_esp32::Esp32LedcPwm tiltPwm(pinmap::kGimbalTiltPin, /*channel=*/4);

outputs::ServoOutput steering(steeringPwm, steeringConfig);
outputs::EscOutput esc(escPwm, clock, escConfig);
outputs::DrsOutput drs(drsPwm, drsConfig);
outputs::ServoOutput panServo(panPwm, gimbalConfig);
outputs::ServoOutput tiltServo(tiltPwm, gimbalConfig);

constexpr telemetry::BatteryConfig kBatteryConfig{};
static_assert(kBatteryConfig.valid(), "battery config: bad divider/trim/EMA/warn values");
constexpr telemetry::WheelSpeedConfig kWheelSpeedConfig{};
static_assert(kWheelSpeedConfig.valid(), "wheel speed config: bad values");

telemetry_hal_esp32::Esp32BatteryAdc batteryAdc(pinmap::kBatterySenseAdcPin);
telemetry_hal_esp32::Esp32HallPulseCounter hallSensor(pinmap::kWheelSpeedHallPin);
telemetry::BatteryMonitor batteryMonitor(batteryAdc, kBatteryConfig);
telemetry::WheelSpeed wheelSpeed(hallSensor, kWheelSpeedConfig);

constexpr link2::Link2SenderConfig kLink2Config{};
static_assert(kLink2Config.valid(), "link2: brake hysteresis thresholds inverted");
link2_hal_esp32::Esp32Link2Uart link2Uart(pinmap::kBoard2UartTxPin);
link2::Link2Sender link2Sender(link2Uart, kLink2Config);

// --- Reset diagnostics (remediation R5-b) ------------------------------------
// RTC-retained boot-session state. RTC_NOINIT_ATTR places this in the ESP32
// .rtc_noinit segment: its bytes SURVIVE software / watchdog / deep-sleep resets
// but are INDETERMINATE after a full power loss, so a power-cycle starts a brand
// new session. C++ startup deliberately does NOT touch it (SessionState is a
// plain aggregate declared with no initializer here on purpose), so a warm reset
// sees the previous boot's values and reset_diag::isValid() decides whether to
// trust them. This is NOT persistent storage: NO NVS and NO flash write occur,
// so there is no flash-wear risk. Magic + inverted-magic + version validation is
// required because uninitialized/corrupted RTC bytes must never be trusted, and
// a brownout (which may corrupt RTC memory) is treated as a fresh session.
RTC_NOINIT_ATTR reset_diag::SessionState g_rtcSessionState;

// Pin the real esp_reset_reason_t values onto our portable RawResetReason so a
// framework enum renumber breaks the BUILD instead of silently mis-classifying.
// Values verified against Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 esp32-target
// esp_system.h (UNKNOWN=0 POWERON=1 EXT=2 SW=3 PANIC=4 INT_WDT=5 TASK_WDT=6
// WDT=7 DEEPSLEEP=8 BROWNOUT=9 SDIO=10).
static_assert(static_cast<int>(ESP_RST_UNKNOWN) == static_cast<int>(reset_diag::RawResetReason::Unknown), "ESP_RST_UNKNOWN value drift");
static_assert(static_cast<int>(ESP_RST_POWERON) == static_cast<int>(reset_diag::RawResetReason::PowerOn), "ESP_RST_POWERON value drift");
static_assert(static_cast<int>(ESP_RST_EXT) == static_cast<int>(reset_diag::RawResetReason::Ext), "ESP_RST_EXT value drift");
static_assert(static_cast<int>(ESP_RST_SW) == static_cast<int>(reset_diag::RawResetReason::Sw), "ESP_RST_SW value drift");
static_assert(static_cast<int>(ESP_RST_PANIC) == static_cast<int>(reset_diag::RawResetReason::Panic), "ESP_RST_PANIC value drift");
static_assert(static_cast<int>(ESP_RST_INT_WDT) == static_cast<int>(reset_diag::RawResetReason::IntWdt), "ESP_RST_INT_WDT value drift");
static_assert(static_cast<int>(ESP_RST_TASK_WDT) == static_cast<int>(reset_diag::RawResetReason::TaskWdt), "ESP_RST_TASK_WDT value drift");
static_assert(static_cast<int>(ESP_RST_WDT) == static_cast<int>(reset_diag::RawResetReason::Wdt), "ESP_RST_WDT value drift");
static_assert(static_cast<int>(ESP_RST_DEEPSLEEP) == static_cast<int>(reset_diag::RawResetReason::DeepSleep), "ESP_RST_DEEPSLEEP value drift");
static_assert(static_cast<int>(ESP_RST_BROWNOUT) == static_cast<int>(reset_diag::RawResetReason::Brownout), "ESP_RST_BROWNOUT value drift");
static_assert(static_cast<int>(ESP_RST_SDIO) == static_cast<int>(reset_diag::RawResetReason::Sdio), "ESP_RST_SDIO value drift");

// Thin ESP32 adapter: real esp_reset_reason_t -> portable RawResetReason. Every
// pinned enum value is handled explicitly; a future/unmapped value falls to
// Unknown (the classifier then labels it UNKNOWN rather than guessing).
reset_diag::RawResetReason mapResetReason(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_UNKNOWN:   return reset_diag::RawResetReason::Unknown;
        case ESP_RST_POWERON:   return reset_diag::RawResetReason::PowerOn;
        case ESP_RST_EXT:       return reset_diag::RawResetReason::Ext;
        case ESP_RST_SW:        return reset_diag::RawResetReason::Sw;
        case ESP_RST_PANIC:     return reset_diag::RawResetReason::Panic;
        case ESP_RST_INT_WDT:   return reset_diag::RawResetReason::IntWdt;
        case ESP_RST_TASK_WDT:  return reset_diag::RawResetReason::TaskWdt;
        case ESP_RST_WDT:       return reset_diag::RawResetReason::Wdt;
        case ESP_RST_DEEPSLEEP: return reset_diag::RawResetReason::DeepSleep;
        case ESP_RST_BROWNOUT:  return reset_diag::RawResetReason::Brownout;
        case ESP_RST_SDIO:      return reset_diag::RawResetReason::Sdio;
    }
    return reset_diag::RawResetReason::Unknown;
}

#if defined(W17_TUNING_CONSOLE) || defined(W17_SIM_CRSF_FEEDER)
// Format the single concise boot line, e.g. "[boot] reset=TASK_WDT boots=3
// retained=yes". retained=yes means the counter carried forward from a valid
// prior session; retained=no means this boot started a fresh session (power-on,
// brownout, or invalid retained state). Compiled ONLY in the tuning/sim builds;
// delivery emits no diagnostic string.
void formatBootLine(const reset_diag::BootReport& r, char* buf, size_t n) {
    std::snprintf(buf, n, "[boot] reset=%s boots=%lu retained=%s",
                  reset_diag::label(r.reason),
                  static_cast<unsigned long>(r.bootCount),
                  r.freshSession ? "no" : "yes");
}
#endif

// --- Control-loop Task Watchdog policy (remediation R5-a) --------------------
// The pinned ESP-IDF 4.4 framework ALREADY initializes exactly one global TWDT
// at boot: 5 s timeout, panic+reset enabled, core-0 idle subscribed, core-1 idle
// NOT subscribed, Arduino loopTask NOT subscribed (sdkconfig CONFIG_ESP_TASK_WDT*
// / CONFIG_ARDUINO_RUNNING_CORE=1). R5-a does NOT create a second watchdog; it
// reconfigures that single global TWDT and subscribes this task (the loopTask).
//
// esp_task_wdt_init() UPDATES the existing global timeout in place (it does not
// spawn a new instance), so lowering it to 2 s ALSO moves the already-subscribed
// core-0 idle task from a 5 s to a 2 s deadline. That global side effect is
// intentional and accepted for R5-a.
//
// 2 s is PROVISIONAL for the software implementation, pending Phase-B timing
// validation. It is NOT a fixed "worst-case unsafe interval": the true bound is
//   TWDT timeout + measured panic/reboot-to-safe-output interval,
// and the second term is hardware-only (panic text prints before reboot, LEDC
// may keep driving the previous duty during panic, and GPIO13/14 reset state +
// real ESC signal-loss behavior are unmeasured). Deployment trust stays gated on
// A2 / Phase B regardless of this value.
constexpr uint32_t kControlTaskWatchdogTimeoutSeconds = 2;

// The pinned API takes a WHOLE-second timeout. Bound it so a future typo can
// neither disable protection (< 1 s rounds toward nonsense on this API) nor
// silently relax it past the framework's previous 5 s shipped default.
static_assert(kControlTaskWatchdogTimeoutSeconds >= 1,
              "control TWDT timeout must be at least 1 whole second");
static_assert(kControlTaskWatchdogTimeoutSeconds <= 5,
              "control TWDT timeout must not exceed the framework's previous 5 s default");

// Loop cadences (CLAUDE.md 2.8: fixed >=50Hz control, no delay()).
constexpr uint32_t kControlPeriodMs = 20;         // 50 Hz: failsafe + outputs
constexpr uint32_t kLink2PeriodMs = 50;           // 20 Hz: state frame to board #2
constexpr uint32_t kBatterySampleIntervalMs = 100;
constexpr uint32_t kTelemetryPeriodMs = 200;      // 5 Hz: CRSF battery telemetry up to RP1
uint32_t lastControlTickMs = 0;
uint32_t lastLink2TickMs = 0;
uint32_t lastBatterySampleMs = 0;
uint32_t lastTelemetryMs = 0;

// Rough 2S remaining-percent estimate from resting pack voltage (6.6V empty ->
// 8.4V full), for the CRSF battery frame's percent field. Voltage itself is
// the honest value; percent is a coarse gauge (no coulomb counting).
uint8_t estimate2sPercent(uint16_t batteryMv) {
    constexpr int32_t kEmptyMv = 6600;
    constexpr int32_t kFullMv = 8400;
    if (batteryMv <= kEmptyMv) return 0;
    if (batteryMv >= kFullMv) return 100;
    return static_cast<uint8_t>((static_cast<int32_t>(batteryMv) - kEmptyMv) * 100 /
                                (kFullMv - kEmptyMv));
}

// RC-frame arrivals accumulated between control ticks, consumed by the
// failsafe update. Distinct from the per-pass decode flag: merging them would
// either double-decode or drop frame events that land between ticks.
bool rcFrameSinceTick = false;

// What the last control tick actually commanded, for link2 reporting.
// Boot-safe defaults: the first frame must never report a phantom Active.
link2::ControlSnapshot controlSnapshot;

// Persisted tuning is loaded on EVERY build, delivery included: the delivery
// esp32dev firmware READS validated NVS at boot but exposes no console to change
// it. Compile-time net: the tunable defaults must be valid, composed from every
// sub-config's own valid().
static_assert(settings::kDefaults.valid(), "default tunable Settings are invalid");
settings_hal_esp32::Esp32NvsStore nvsStore; // read-only load here; save only via the console

// Copy a validated Settings object into the live modules (pure config-copies;
// no state reset -- ESC arm anchor + gearbox current gear are preserved). ESC
// endpoints, failsafe, channel map are deliberately NOT tunable. The argument is
// always a whole, validated object (loader output or the console's RAM copy),
// never a partial merge.
void applySettings(const settings::Settings& s) {
    steering.setConfig(s.steering);
    virtualGearbox.setConfig(s.gearbox);
    batteryMonitor.setConfig(s.battery);
}

#ifdef W17_TUNING_CONSOLE
// Bench tuning console (opt-in build only; the delivered firmware ships without
// this flag, opens no UART0, and carries no command surface). It shares the one
// nvsStore above and loads through the same validated loader as delivery.
console_hal_esp32::Esp32SerialConsole serialConsole;
console::ConsoleRunner consoleRunner(serialConsole, nvsStore);
#endif

} // namespace

void setup() {
    // Attach actuator PWM FIRST, before any other init, so GPIO13/14 (steering /
    // ESC signal) spend as little time as possible floating high-Z after reset
    // (audit R04 -- the hardware pull-down/RC decision stays bench-gated). begin()
    // attaches the LEDC channel with an explicit safe pulse (center/neutral/closed);
    // the safe commands below require the channel already attached.
    steeringPwm.begin(steeringConfig.centerMicros);
    escPwm.begin(escConfig.neutralMicros);
    drsPwm.begin(drsConfig.closedMicros);
    panPwm.begin(gimbalConfig.centerMicros);
    tiltPwm.begin(gimbalConfig.centerMicros);

    steering.setPosition(0);
    esc.setThrottle(0); // first command: starts the ESC boot-arm hold window
    drs.setOpen(false);
    panServo.setPosition(0); // camera centered
    tiltServo.setPosition(0);

    // --- R5-b reset diagnostics: capture + retained-session update -----------
    // Capture the reset reason EXACTLY ONCE and update the RTC-retained session
    // here -- after the safe-output block above (safe PWM attach + neutral
    // commands stay the first substantive setup behavior; diagnostics must never
    // delay them) and before any UART / sensor / NVS / watchdog init. This runs
    // in EVERY build so the firmware always records correct state; only the
    // flag-gated prints below emit a string (delivery stays silent). No output,
    // no NVS/flash write, and the R5-a watchdog policy below is untouched.
    const reset_diag::RawResetReason rawResetReason = mapResetReason(esp_reset_reason());
    [[maybe_unused]] const reset_diag::BootReport bootReport =
        reset_diag::updateSession(g_rtcSessionState, reset_diag::classify(rawResetReason));

#ifdef W17_SIM_CRSF_FEEDER
    Serial.begin(115200); // sim status/narration only; real firmware opens no UART0
    Serial.println("[sim] W17 control firmware -- Wokwi Stage-2 demo build");
    {
        // Same concise boot diagnostic as the tuning build, on the sim serial
        // path. Lets the W17_SIM_WDT_STALL run show the reboot's reset class and
        // the incremented retained boot count.
        char bootLine[64];
        formatBootLine(bootReport, bootLine, sizeof(bootLine));
        Serial.println(bootLine);
    }
#endif

    crsfUart.begin();
    link2Uart.begin();
    batteryAdc.begin();
    hallSensor.begin();

    // Load persisted tuning through the shared validated loader (guard chain:
    // length -> CRC -> version -> Settings::valid(); ANY failure -> complete
    // defaults) and push it into the live modules. This runs in EVERY build,
    // including the delivery firmware -- delivery READS validated NVS but has no
    // console to change it. Ordering: after the actuators + sensors are in their
    // safe boot state, before the control loop starts.
#ifdef W17_TUNING_CONSOLE
    // Tuning build additionally opens UART0 and the console; it loads through the
    // same loader into the console's RAM Settings so get/set/save operate on the
    // applied values.
    serialConsole.begin(115200);
    {
        // One concise boot diagnostic line through the existing tuning UART0
        // path, in the console's [tag] banner style, before the [tune] load line.
        char bootLine[64];
        formatBootLine(bootReport, bootLine, sizeof(bootLine));
        serialConsole.write(bootLine);
        serialConsole.write("\r\n");
    }
    consoleRunner.loadAtBoot();
    applySettings(consoleRunner.settings());
#else
    applySettings(settings::loadOrDefault(nvsStore).settings);
#endif

    // --- Control-loop Task Watchdog subscription (R5-a) ----------------------
    // MUST be the last statement in setup(): every potentially long boot step
    // above (PWM attach + safe commands, UART init, sensor init, validated NVS
    // load/apply, and the tuning console's boot load) has already completed, so
    // the first watched control tick is never racing initialization.
    //
    // Reconfigure the framework's single global TWDT to the provisional 2 s /
    // panic timeout, then subscribe THIS task -- the Arduino loopTask -- by
    // passing nullptr (the pinned API reads that as "the currently running
    // task"). esp_task_wdt_status(nullptr) then confirms the subscription took.
    //
    // We call the pinned ESP-IDF API directly and deliberately do NOT use
    // Arduino's enableLoopWDT()/feedLoopWDT(): enableLoopWDT() sets the
    // framework's loopTaskWDTEnabled flag, which makes the framework feed at the
    // TOP of every loop() pass BEFORE application code runs. That would defeat
    // this policy, whose whole point is that a feed proves a COMPLETE actuator-
    // control iteration finished. loopTaskWDTEnabled therefore stays disabled.
    //
    // Every call is fail-fatal via ESP_ERROR_CHECK. An init/subscribe/confirm
    // error means framework or configuration drift; continuing would ship a
    // firmware that is falsely "protected" (unwatched but appearing watched),
    // which is exactly the state this remediation exists to prevent. No retry
    // loop -- a failure here is a build/config fault, not a transient condition.
    ESP_ERROR_CHECK(esp_task_wdt_init(kControlTaskWatchdogTimeoutSeconds, true));
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    ESP_ERROR_CHECK(esp_task_wdt_status(nullptr));
}

void loop() {
    const uint32_t nowMs = millis();

#ifdef W17_SIM_WDT_STALL
    // SIM-ONLY deliberate-stall fault injection to VALIDATE that the control-loop
    // Task Watchdog actually fires (R5-a Wokwi test). This macro is intentionally
    // NOT set in any checked-in PlatformIO environment (delivery, tuning, or sim);
    // it is supplied only as an ad-hoc build flag for the watchdog validation run,
    // so this whole block is absent from every normal binary. It adds NO runtime
    // user-accessible command.
    //
    // After ~3 s of simulated runtime -- long enough for a normal boot plus many
    // fed 50 Hz control ticks -- stop the loopTask forever, never reaching the
    // feed above, so the 2 s TWDT must elapse and panic. The unique marker string
    // below lets an ELF/strings scan prove this code is absent from normal builds.
    static const volatile char kW17SimWdtStallMarker[] =
        "W17_SIM_WDT_STALL_DELIBERATE_LOOPTASK_HANG";
    if (nowMs >= 3000) {
        Serial.println(const_cast<const char*>(kW17SimWdtStallMarker));
        Serial.flush();
        for (;;) {
            // Busy-spin: never yield, never feed. Proves the subscribed loopTask
            // (not an idle task) trips the Task Watchdog.
        }
    }
#endif

#ifdef W17_SIM_CRSF_FEEDER
    // Feed the scripted CRSF demo stream into Serial2 TX; the diagram loops
    // it back into the CRSF RX pin, so everything below runs unmodified.
    simfeeder::tick(nowMs);
#endif

    // --- Always: drain the CRSF UART (the RX buffer fills in ~6ms at 420k;
    // bytes must never back up behind the tick guards below). Accumulate (|=)
    // rather than assign: an RC frame and a stats frame can both complete in
    // one drain pass.
    bool frameArrived = false;
    while (crsfUart.available() > 0) {
        const crsf::CrsfReceiver::ByteResult result =
            crsfReceiver.feedByte(static_cast<uint8_t>(crsfUart.read()), nowMs);
        frameArrived |= (result == crsf::CrsfReceiver::ByteResult::NewRcFrame);
    }
    rcFrameSinceTick |= frameArrived;

#ifdef W17_TUNING_CONSOLE
    // Bench console: polled outside the control tick, non-blocking, one capped
    // line per pass. Mutations are gated on DISARMED inside the console; a
    // change is applied to the live modules immediately (RAM-only until save).
    if (consoleRunner.poll(armGate.isArmed())) {
        applySettings(consoleRunner.settings());
    }
#endif

    // --- Event-driven: decode on every new frame, including while failsafe
    // is Safe (pausing decode during an outage would turn a switch moved
    // during the outage into a phantom gear edge on recovery).
    if (frameArrived) {
        controls = channelDecoder.decode(crsfReceiver.channels());

        // Gear edges are consume-on-read and `controls` is cached across
        // loop passes, so shifts MUST happen here, exactly once per edge.
        // A gear shift is state, not actuation, so it is not gated on
        // failsafe (gear deliberately survives failsafe/disarm: ArmGate
        // already forces a fresh throttle-neutral after every episode).
        if (controls.gearUpEdge) {
            virtualGearbox.shiftUp();
        }
        if (controls.gearDownEdge) {
            virtualGearbox.shiftDown();
        }
    }

    // --- 10 Hz: battery sampling (monitoring only, CLAUDE.md 6.4).
    if (nowMs - lastBatterySampleMs >= kBatterySampleIntervalMs) {
        lastBatterySampleMs = nowMs;
        batteryMonitor.sample(nowMs);
    }

    // --- 5 Hz: CRSF battery telemetry up to RP1 (GPIO17 TX). Standard sensor
    // frame; RP1 relays it over the ELRS downlink to the ground station.
    // Outside the control tick, non-blocking (~10 byte write). Link quality
    // is reported by the ground TX module itself, so it needs nothing here.
    if (nowMs - lastTelemetryMs >= kTelemetryPeriodMs) {
        lastTelemetryMs = nowMs;
        const uint16_t mv = batteryMonitor.batteryMv();
        if (mv > 0) { // skip until the monitor has a real reading
            uint8_t frame[4 + crsf::kBatteryPayloadLen];
            const uint16_t deciVolt = static_cast<uint16_t>(mv / 100); // mV -> 0.1V
            const size_t n = crsf::buildBatteryFrame(deciVolt, /*current*/ 0, /*capacity*/ 0,
                                                     estimate2sPercent(mv), frame);
            crsfUart.write(frame, n);
        }

        // Real wheel speed as a standard GPS groundspeed frame (0.1 km/h units).
        // km/h*10 = mm/s * 3.6 / 1000 * 10 = mm/s * 36 / 1000.
        {
            const uint32_t mmPerSec = wheelSpeed.speedMmPerSec();
            const uint16_t speedKmhX10 = static_cast<uint16_t>((mmPerSec * 36u) / 1000u);
            uint8_t frame[4 + crsf::kGpsPayloadLen];
            const size_t n = crsf::buildGpsFrame(/*lat*/ 0, /*lon*/ 0, speedKmhX10, /*heading*/ 0,
                                                 /*altitude*/ 1000, /*sats*/ 0, frame);
            crsfUart.write(frame, n);
        }

        // Car-authoritative gear / drive-mode / ERS% as a FLIGHTMODE status
        // string ("G3 M2 E55"): the ground can't infer these without drift, so
        // the HUD prefers these over its own mirror when a source is live.
        {
            char status[crsf::kFlightModeMaxLen];
            std::snprintf(status, sizeof(status), "G%u M%u E%u",
                          static_cast<unsigned>(controlSnapshot.displayGear),
                          static_cast<unsigned>(controlSnapshot.driveMode),
                          static_cast<unsigned>(controlSnapshot.ersPercent));
            uint8_t frame[4 + crsf::kFlightModeMaxLen];
            const size_t n = crsf::buildFlightModeFrame(status, frame);
            crsfUart.write(frame, n);
        }
    }

    // --- 50 Hz control tick: failsafe + arm gate + outputs (CLAUDE.md 2.8).
    // Tick guard is `last = now` (phase-accumulating) on purpose: `last +=
    // period` would burst to catch up after any stall, and nothing here
    // integrates tick count. Failsafe timestamps land at worst ~20ms late
    // (~540ms worst-case detection vs the 500ms budget) -- and actuation was
    // already quantized to 20ms by the 50 Hz PWM hardware.
    if (nowMs - lastControlTickMs >= kControlPeriodMs) {
        lastControlTickMs = nowMs;

        wheelSpeed.update(nowMs);

        // rxSignalsFailsafe: latched uplink-LQ==0 from LINK_STATISTICS -- an
        // independent loss signal alongside the frame timeout, and the only
        // one that fires if the RX keeps sending hold-position RC frames.
        const failsafe::State state = failsafeStateMachine.update(
            nowMs, rcFrameSinceTick, crsfReceiver.rxSignalsFailsafe());
        rcFrameSinceTick = false;

        // The arm gate runs every control tick so a failsafe episode clears
        // its neutral-seen latch: after recovery, throttle must be observed
        // at neutral again before the motor may run (CLAUDE.md 6.2).
        const bool armed = armGate.update(controls.armSwitch, controls.throttle,
                                          /*forceDisarm=*/state == failsafe::State::Safe);

        // Mode-shaped, post-arm-gate, pre-boost throttle. Training uses one
        // fixed gentle shape (gear shifts inert); other modes use the gearbox.
        const int16_t modeShaped =
            (controls.driveMode == 0)
                ? gearbox::shapeThrottle(controls.throttle, kTrainingGearParams)
                : virtualGearbox.apply(controls.throttle);
        const bool active = state == failsafe::State::Active;
        const int16_t baseCommanded = (active && armed) ? modeShaped : 0;

        // ERS ticks EVERY control tick: outside GearboxErs (or in failsafe)
        // it freezes and re-seeds its clock, so mode switches and outages
        // never produce a dt gap, and a stale boost switch during failsafe
        // can neither drain energy nor report "deploying".
        ersSystem.update(nowMs, /*ersActive=*/active && controls.driveMode == 2,
                         baseCommanded, wheelSpeed.rpm(), controls.boostHeld,
                         controls.overtakeHeld);

        // No early return in the Safe branch: link2 below must keep
        // transmitting during failsafe -- that flag is its whole purpose.
        if (state == failsafe::State::Safe) {
            steering.setPosition(0);
            esc.setThrottle(0);
            drs.setOpen(false);
            controlSnapshot.commandedThrottle = 0;
            controlSnapshot.steering = 0;
            controlSnapshot.drsOpen = false;
            controlSnapshot.armed = false;
            controlSnapshot.failsafe = true;
        } else {
            // Steering stays live while disarmed: CLAUDE.md 6.2 gates
            // throttle only, and bench setup needs steering without arming.
            steering.setPosition(controls.steering);

            // applyBoost is purely multiplicative (applyBoost(0) == 0,
            // test-pinned), so boosting AFTER the arm gate cannot bypass it.
            const int16_t commanded = ersSystem.applyBoost(baseCommanded);
            esc.setThrottle(commanded);
            drs.setOpen(controls.drsSwitch);

            controlSnapshot.commandedThrottle = commanded;
            controlSnapshot.steering = controls.steering;
            controlSnapshot.drsOpen = controls.drsSwitch;
            controlSnapshot.armed = armed;
            controlSnapshot.failsafe = false;
        }

        // Camera gimbal (right stick -> ch9/ch10 in elrs-joystick-control).
        // Not safety-gated: aiming the camera is harmless armed or disarmed,
        // so it just follows the last commanded look direction. During a
        // failsafe `controls` is frozen (no decode), so the camera holds its
        // last position rather than snapping to center.
        panServo.setPosition(controls.pan);
        tiltServo.setPosition(controls.tilt);

        // --- Control-loop Task Watchdog feed (R5-a) -------------------------
        // The ONE and ONLY application feed, placed as the final statement of
        // the 50 Hz control tick. Reaching this line proves a COMPLETE safety-
        // critical iteration ran: CRSF drain (top of loop) -> failsafe update
        // -> arm gate -> throttle/gear/ERS shaping -> steering -> ESC -> DRS ->
        // pan -> tilt. Both the Safe and Active branches above command every
        // actuator before control flow merges here, so either path feeds only
        // after its outputs are written. Feeding here (not at the top of loop(),
        // not in a between-tick pass, and not inside CRSF/telemetry/link2/console
        // code) is what makes the watchdog a proof of loop liveness rather than
        // of mere scheduler ticks.
        //
        // Fail-fatal: a failed reset means this task is unexpectedly no longer
        // subscribed (drift) -- a falsely-protected loop must not keep running.
        //
        // This proves TASK-LEVEL completion ONLY. It does NOT detect silent LEDC
        // failure, out-of-range pulse values, disconnected wires, or peripheral
        // output corruption downstream of the commanded microseconds.
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }

    // --- 20 Hz: vehicle-state frame to the sound/light board.
    if (nowMs - lastLink2TickMs >= kLink2PeriodMs) {
        lastLink2TickMs = nowMs;

        controlSnapshot.lowBattery = batteryMonitor.lowVoltageWarning();
        controlSnapshot.displayGear = static_cast<uint8_t>(virtualGearbox.currentGear() + 1);
        controlSnapshot.rpm = wheelSpeed.rpm();
        controlSnapshot.batteryMv = batteryMonitor.batteryMv();
        controlSnapshot.ersPercent = ersSystem.energyPercent();
        controlSnapshot.ersDeploying = ersSystem.deploying();
        controlSnapshot.driveMode = controls.driveMode;
        link2Sender.send(controlSnapshot);
    }

#ifdef W17_SIM_CRSF_FEEDER
    // Live state readout for the Wokwi serial monitor.
    static uint32_t lastStatusPrintMs = 0;
    if (nowMs - lastStatusPrintMs >= 500) {
        lastStatusPrintMs = nowMs;
        Serial.printf("[state] failsafe=%d armed=%d mode=%u gear=%u thr=%d steer=%d ers=%u%%%s rpm=%u batt=%umV lowBatt=%d\n",
                      controlSnapshot.failsafe, controlSnapshot.armed, controls.driveMode,
                      virtualGearbox.currentGear() + 1, controlSnapshot.commandedThrottle,
                      controlSnapshot.steering, ersSystem.energyPercent(),
                      ersSystem.deploying() ? "(DEPLOY)" : "", wheelSpeed.rpm(),
                      batteryMonitor.batteryMv(), batteryMonitor.lowVoltageWarning());
    }
#endif
}
