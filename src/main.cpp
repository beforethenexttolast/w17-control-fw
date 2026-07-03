#include <Arduino.h>

#include "channels/ArmGate.hpp"
#include "channels/ChannelDecoder.hpp"
#include "config/PinMap.hpp"
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
#include "telemetry/BatteryMonitor.hpp"
#include "telemetry/WheelSpeed.hpp"
#include "telemetry_hal_esp32/Esp32BatteryAdc.hpp"
#include "telemetry_hal_esp32/Esp32HallPulseCounter.hpp"

#ifdef W17_SIM_CRSF_FEEDER
#include "SimCrsfFeeder.hpp" // Wokwi Stage-2 harness, absent from real firmware
#endif

#ifdef W17_TUNING_CONSOLE
#include "console/ConsoleRunner.hpp"
#include "settings/Settings.hpp"
#include "settings_hal_esp32/Esp32NvsStore.hpp"
#include "settings_hal_esp32/Esp32SerialConsole.hpp"
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

// Drive modes (3-pos switch): 0 = Training, 1 = Gearbox (mid = default, also
// what an absent channel decodes to), 2 = Gearbox+ERS. There is deliberately
// NO raw pass-through mode: gearbox top gear (cap 1000, expo 0) already IS
// full power, and power stays monotone along the switch so a bumped switch
// changes authority by one gentle step.
// Training: one fixed gentle shape, gear shifts have no effect.
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

outputs_hal_esp32::Esp32LedcPwm steeringPwm(pinmap::kSteeringServoPin, /*channel=*/0);
outputs_hal_esp32::Esp32LedcPwm escPwm(pinmap::kEscThrottlePin, /*channel=*/1);
outputs_hal_esp32::Esp32LedcPwm drsPwm(pinmap::kDrsServoPin, /*channel=*/2);

outputs::ServoOutput steering(steeringPwm, steeringConfig);
outputs::EscOutput esc(escPwm, clock, escConfig);
outputs::DrsOutput drs(drsPwm, drsConfig);

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

// Loop cadences (CLAUDE.md 2.8: fixed >=50Hz control, no delay()).
constexpr uint32_t kControlPeriodMs = 20;         // 50 Hz: failsafe + outputs
constexpr uint32_t kLink2PeriodMs = 50;           // 20 Hz: state frame to board #2
constexpr uint32_t kBatterySampleIntervalMs = 100;
uint32_t lastControlTickMs = 0;
uint32_t lastLink2TickMs = 0;
uint32_t lastBatterySampleMs = 0;

// RC-frame arrivals accumulated between control ticks, consumed by the
// failsafe update. Distinct from the per-pass decode flag: merging them would
// either double-decode or drop frame events that land between ticks.
bool rcFrameSinceTick = false;

// What the last control tick actually commanded, for link2 reporting.
// Boot-safe defaults: the first frame must never report a phantom Active.
link2::ControlSnapshot controlSnapshot;

#ifdef W17_TUNING_CONSOLE
// Bench tuning console (opt-in build only; the delivered firmware ships
// without this flag and opens no UART0). Compile-time net: the tunable
// defaults must be valid, composed from every sub-config's own valid().
static_assert(settings::kDefaults.valid(), "default tunable Settings are invalid");
settings_hal_esp32::Esp32NvsStore nvsStore;
settings_hal_esp32::Esp32SerialConsole serialConsole;
console::ConsoleRunner consoleRunner(serialConsole, nvsStore);

// Push the runtime Settings into the live modules (pure config-copies; no
// state reset -- ESC arm anchor + gearbox current gear are preserved). ESC
// endpoints, failsafe, channel map are deliberately NOT tunable.
void applyTuning() {
    steering.setConfig(consoleRunner.settings().steering);
    virtualGearbox.setConfig(consoleRunner.settings().gearbox);
    batteryMonitor.setConfig(consoleRunner.settings().battery);
}
#endif

} // namespace

void setup() {
#ifdef W17_SIM_CRSF_FEEDER
    Serial.begin(115200); // sim status/narration only; real firmware opens no UART0
    Serial.println("[sim] W17 control firmware -- Wokwi Stage-2 demo build");
#endif

    crsfUart.begin();
    link2Uart.begin();
    batteryAdc.begin();
    hallSensor.begin();

    // Attach PWM with an explicit safe initial pulse (center/neutral/closed)
    // so the outputs never depend on the ordering of the calls below.
    steeringPwm.begin(steeringConfig.centerMicros);
    escPwm.begin(escConfig.neutralMicros);
    drsPwm.begin(drsConfig.closedMicros);

    steering.setPosition(0);
    esc.setThrottle(0); // first command: starts the ESC boot-arm hold window
    drs.setOpen(false);

#ifdef W17_TUNING_CONSOLE
    // Load persisted tuning (guard chain -> defaults on any failure) and push
    // it into the live modules. UART0 is opened ONLY in this build.
    serialConsole.begin(115200);
    consoleRunner.loadAtBoot();
    applyTuning();
#endif
}

void loop() {
    const uint32_t nowMs = millis();

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
        applyTuning();
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
