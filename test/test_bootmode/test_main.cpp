#include <unity.h>

#include "bootmode/BootMode.hpp"
#include "channels/ArmGate.hpp"
#include "failsafe/FailsafeStateMachine.hpp"

using bootmode::BootMode;
using bootmode::StrapReading;
using channels::ArmGate;
using failsafe::FailsafeStateMachine;
using failsafe::State;

void setUp() {}
void tearDown() {}

// --- Resolver truth table ---------------------------------------------------

// Fail-toward-drive (BT design 2.2-A doctrine): ONLY an unambiguous SHOW
// reading selects Showcase. Floating (the compile-time default every
// delivery-lineage build injects), the LAPTOP position, and any garbage
// value a corrupted read could cast in all resolve to Drive -- a selector
// fault makes the car less entertaining, never more armed.
void test_resolver_truth_table_floats_to_drive() {
    TEST_ASSERT_EQUAL(BootMode::Drive, bootmode::resolve(StrapReading::Floating));
    TEST_ASSERT_EQUAL(BootMode::Drive, bootmode::resolve(StrapReading::DrivePosition));
    TEST_ASSERT_EQUAL(BootMode::Showcase, bootmode::resolve(StrapReading::ShowPosition));
    TEST_ASSERT_EQUAL(BootMode::BtSolo, bootmode::resolve(StrapReading::SoloPosition));

    // Out-of-range readings (defensive: a broken classifier or bit flip) are
    // Drive too -- resolve() is total over the underlying byte.
    for (int raw = 4; raw <= 255; ++raw) {
        TEST_ASSERT_EQUAL(BootMode::Drive,
                          bootmode::resolve(static_cast<StrapReading>(raw)));
    }

    // Compile-time resolvable: the whole seam folds away in today's builds,
    // where main.cpp injects the constexpr Floating reading.
    static_assert(bootmode::resolve(StrapReading::Floating) == BootMode::Drive,
                  "floating strap must resolve to Drive at compile time");
    static_assert(bootmode::resolve(StrapReading::ShowPosition) == BootMode::Showcase,
                  "SHOW strap must resolve to Showcase at compile time");
    static_assert(bootmode::resolve(StrapReading::SoloPosition) == BootMode::BtSolo,
                  "SOLO strap must resolve to BtSolo at compile time");
}

// --- Strap classification (the SP3T selector the BT envs sample) -------------
// Recast twice: the former btpad::resolveBootMode suite (2026-08-17 three-mode
// unification) became levels -> position; D3-SHOW-SELECT (owner-ratified
// 2026-08-20) split it again into per-pin majority (classifyStrapPin) + SP3T
// combination (combineStrapPins), so the electrical layer, the selector
// truth table, and the mode layer are separately testable. Fail directions
// unchanged: no data / tie / anything ambiguous -> Floating -> Drive.

namespace {
// Canned per-pin sample arrays, shared by the classification tests below.
const bool kAllHigh[9] = {true, true, true, true, true, true, true, true, true};
const bool kAllLow[9] = {};
// 5 lows of 9: strict majority despite bounce.
const bool kBouncyLow[9] = {true, false, false, true, false, false, true, false, true};
// 4 lows of 9: minority -- still Open.
const bool kMinorityLow[9] = {false, true, true, false, true, false, true, false, true};
const bool kTie[4] = {false, false, true, true};
} // namespace

void test_classify_pin_majority_and_faults() {
    using bootmode::StrapPinRead;
    TEST_ASSERT_EQUAL(StrapPinRead::Open, bootmode::classifyStrapPin(kAllHigh, 9));
    TEST_ASSERT_EQUAL(StrapPinRead::Grounded, bootmode::classifyStrapPin(kAllLow, 9));
    TEST_ASSERT_EQUAL(StrapPinRead::Grounded, bootmode::classifyStrapPin(kBouncyLow, 9));
    TEST_ASSERT_EQUAL(StrapPinRead::Open, bootmode::classifyStrapPin(kMinorityLow, 9));
    // Faults: null, empty, tie -> Ambiguous.
    TEST_ASSERT_EQUAL(StrapPinRead::Ambiguous, bootmode::classifyStrapPin(nullptr, 9));
    TEST_ASSERT_EQUAL(StrapPinRead::Ambiguous, bootmode::classifyStrapPin(kAllHigh, 0));
    TEST_ASSERT_EQUAL(StrapPinRead::Ambiguous, bootmode::classifyStrapPin(kTie, 4));
}

// The full SP3T position truth table, levels end to end: neither pin
// grounded = LAPTOP/Drive, GPIO27 grounded = SOLO, GPIO32 grounded = SHOW,
// both grounded = harness fault -> Floating -> Drive.
void test_sp3t_position_truth_table() {
    // Center: both pins idle HIGH on their pull-ups.
    TEST_ASSERT_EQUAL(StrapReading::DrivePosition,
                      bootmode::classifyStrapLevels(kAllHigh, kAllHigh, 9));
    // SOLO throw: GPIO27 grounded, GPIO32 idle (bouncy majority still counts).
    TEST_ASSERT_EQUAL(StrapReading::SoloPosition,
                      bootmode::classifyStrapLevels(kAllLow, kAllHigh, 9));
    TEST_ASSERT_EQUAL(StrapReading::SoloPosition,
                      bootmode::classifyStrapLevels(kBouncyLow, kAllHigh, 9));
    // SHOW throw: GPIO32 grounded, GPIO27 idle.
    TEST_ASSERT_EQUAL(StrapReading::ShowPosition,
                      bootmode::classifyStrapLevels(kAllHigh, kAllLow, 9));
    TEST_ASSERT_EQUAL(StrapReading::ShowPosition,
                      bootmode::classifyStrapLevels(kAllHigh, kBouncyLow, 9));
    // BOTH grounded: the part cannot do this -- harness fault, ambiguous.
    TEST_ASSERT_EQUAL(StrapReading::Floating,
                      bootmode::classifyStrapLevels(kAllLow, kAllLow, 9));

    // End to end through resolve(): the four positions land on the four
    // outcomes, with both-grounded on Drive.
    TEST_ASSERT_EQUAL(BootMode::Drive,
                      bootmode::resolve(bootmode::classifyStrapLevels(kAllHigh, kAllHigh, 9)));
    TEST_ASSERT_EQUAL(BootMode::BtSolo,
                      bootmode::resolve(bootmode::classifyStrapLevels(kAllLow, kAllHigh, 9)));
    TEST_ASSERT_EQUAL(BootMode::Showcase,
                      bootmode::resolve(bootmode::classifyStrapLevels(kAllHigh, kAllLow, 9)));
    TEST_ASSERT_EQUAL(BootMode::Drive,
                      bootmode::resolve(bootmode::classifyStrapLevels(kAllLow, kAllLow, 9)));
}

// Every fault injection lands on Drive: null/empty/tied samples on either
// pin (or both), and out-of-range StrapPinRead values cast straight into the
// combiner (it is total over the underlying byte, like resolve()).
void test_sp3t_fault_injections_fail_toward_drive() {
    using bootmode::StrapPinRead;
    // Per-pin data faults, each side, both sides -- including the nasty
    // asymmetric ones where the OTHER pin reads a clean throw.
    const StrapReading faultReads[] = {
        bootmode::classifyStrapLevels(nullptr, nullptr, 9),
        bootmode::classifyStrapLevels(nullptr, kAllHigh, 9),
        bootmode::classifyStrapLevels(kAllHigh, nullptr, 9),
        bootmode::classifyStrapLevels(nullptr, kAllLow, 9), // SHOW looks thrown, SOLO unknown
        bootmode::classifyStrapLevels(kAllLow, nullptr, 9), // SOLO looks thrown, SHOW unknown
        bootmode::classifyStrapLevels(kAllHigh, kAllHigh, 0),
        bootmode::classifyStrapLevels(kTie, kAllHigh, 4),
        bootmode::classifyStrapLevels(kAllHigh, kTie, 4),
        bootmode::classifyStrapLevels(kTie, kTie, 4),
        bootmode::classifyStrapLevels(kTie, kAllLow, 4), // ambiguous + thrown: still no
    };
    for (const StrapReading r : faultReads) {
        TEST_ASSERT_EQUAL(StrapReading::Floating, r);
        TEST_ASSERT_EQUAL(BootMode::Drive, bootmode::resolve(r));
    }
    // Corrupted pin-read bytes: any value that is not exactly the two clean
    // one-throw patterns (or clean center) combines to Floating.
    for (int solo = 0; solo <= 255; ++solo) {
        for (int show = 0; show <= 255; show += (show < 8 ? 1 : 37)) {
            const auto s = static_cast<StrapPinRead>(solo);
            const auto h = static_cast<StrapPinRead>(show);
            const StrapReading combined = bootmode::combineStrapPins(s, h);
            if (s == StrapPinRead::Open && h == StrapPinRead::Open) {
                TEST_ASSERT_EQUAL(StrapReading::DrivePosition, combined);
            } else if (s == StrapPinRead::Grounded && h == StrapPinRead::Open) {
                TEST_ASSERT_EQUAL(StrapReading::SoloPosition, combined);
            } else if (s == StrapPinRead::Open && h == StrapPinRead::Grounded) {
                TEST_ASSERT_EQUAL(StrapReading::ShowPosition, combined);
            } else {
                TEST_ASSERT_EQUAL(StrapReading::Floating, combined);
            }
        }
    }
}

// The no-strap delivery pin: with nothing wired (or the compile-time
// Floating injection every delivery-lineage build keeps), the resolved mode
// is Drive -- bit-for-bit today's shipped behavior. Compile-time provable,
// so it is pinned with static_asserts as well as runtime checks.
void test_no_straps_resolves_to_todays_drive() {
    // The delivery seam: main.cpp's constexpr Floating injection.
    static_assert(bootmode::resolve(bootmode::StrapReading::Floating) == BootMode::Drive,
                  "delivery builds (Floating injection) must constant-fold to Drive");
    // The runtime seam with an unwired/floating harness: ambiguous pins.
    static_assert(bootmode::combineStrapPins(bootmode::StrapPinRead::Ambiguous,
                                             bootmode::StrapPinRead::Ambiguous) ==
                      bootmode::StrapReading::Floating,
                  "unwired straps must classify Floating");
    // And the center position (straps wired, switch at LAPTOP) is Drive too.
    static_assert(bootmode::combineStrapPins(bootmode::StrapPinRead::Open,
                                             bootmode::StrapPinRead::Open) ==
                      bootmode::StrapReading::DrivePosition,
                  "center/LAPTOP must classify DrivePosition");
    TEST_ASSERT_EQUAL(BootMode::Drive,
                      bootmode::resolve(bootmode::classifyStrapLevels(kAllHigh, kAllHigh, 9)));
}

// Latch-once-at-boot doctrine, mirrored at main.cpp's composition level: the
// mode is resolved from the BOOT-TIME sample arrays exactly once; a selector
// that later reads differently changes NOTHING because nothing ever
// re-classifies (g_bootMode is written once in setup() and never again --
// changing mode = key cycle). The test plays main.cpp's exact composition:
// resolve once from boot samples, then prove the post-boot samples WOULD
// classify differently while the latched mode is untouched.
void test_boot_mode_latches_once_at_boot() {
    // Boot: switch at SHOW.
    const BootMode latched =
        bootmode::resolve(bootmode::classifyStrapLevels(kAllHigh, kAllLow, 9));
    TEST_ASSERT_EQUAL(BootMode::Showcase, latched);

    // Mid-session the giftee slides the switch to SOLO. The samples would
    // classify differently...
    TEST_ASSERT_EQUAL(StrapReading::SoloPosition,
                      bootmode::classifyStrapLevels(kAllLow, kAllHigh, 9));
    // ...but no re-resolution exists: the latched mode is still Showcase,
    // and every policy keeps answering for Showcase.
    TEST_ASSERT_EQUAL(BootMode::Showcase, latched);
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(latched, true));
    TEST_ASSERT_TRUE(bootmode::link2ShowcaseFlag(latched));
}

// --- Policy 1: the arm input ------------------------------------------------

void test_arm_switch_input_pinned_false_in_showcase_passthrough_in_drive() {
    // Showcase: false REGARDLESS of the decoded switch.
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(BootMode::Showcase, false));
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(BootMode::Showcase, true));
    // Drive: transparent pass-through (byte-identical normal behavior).
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(BootMode::Drive, false));
    TEST_ASSERT_TRUE(bootmode::armSwitchInput(BootMode::Drive, true));
    // BtSolo: ALSO pass-through -- the decoded switch there is the pad arm
    // ritual, which gates upstream (test_btpad); no double gate here.
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(BootMode::BtSolo, false));
    TEST_ASSERT_TRUE(bootmode::armSwitchInput(BootMode::BtSolo, true));
}

namespace {

// One hostile arm-attempt scenario, played against a real ArmGate through the
// bootmode arm-input policy, mirroring main.cpp's exact composition:
//
//   armed         = armGate.update(armSwitchInput(mode, switch), throttle, forceDisarm)
//   baseCommanded = (active && armed) ? modeShaped : 0        (src/main.cpp)
//
// Returns true if the gate EVER armed (or any throttle would have been
// commanded) at any step. The scenario includes every trick that arms the
// gate in normal operation: switch on with the stick neutral, a full
// OFF->ON toggle after a failsafe episode (2026-08-20 rule: the episode
// latches, recovery-with-switch-held-on alone no longer re-arms anything).
bool scenarioEverArms(BootMode mode) {
    ArmGate gate;
    bool everArmed = false;

    struct Step {
        bool armSwitch;
        int16_t throttle;
        bool forceDisarm; // failsafe FSM Safe this tick
    };
    const Step steps[] = {
        {false, 0, false},    // idle, disarmed
        {true, 0, false},     // switch ON at neutral -- arms a Drive gate NOW
        {true, 800, false},   // full throttle, still switch ON
        {true, 800, true},    // failsafe episode begins (forceDisarm)
        {true, 0, true},      // stick back to neutral during the episode
        {true, 0, false},     // recovery, switch STILL on: latched, Drive stays DISARMED
        {true, 1000, false},  // (2026-08-20 rule) still disarmed under full stick
        {false, 0, false},    // switch off -- clears the episode latch
        {true, -60, false},   // switch back on inside the neutral window -> Drive re-arms
        {true, 1000, false},  // slam the stick
    };

    for (const Step& s : steps) {
        const bool armed =
            gate.update(bootmode::armSwitchInput(mode, s.armSwitch), s.throttle, s.forceDisarm);
        everArmed |= armed;
        everArmed |= gate.isArmed();

        // The ESC command expression from main.cpp: in Showcase it must be 0
        // on every step, active or not, whatever the shaped throttle is.
        const bool active = !s.forceDisarm;
        const int16_t modeShaped = s.throttle; // stand-in: any nonzero shape
        const int16_t baseCommanded = (active && armed) ? modeShaped : 0;
        if (mode == BootMode::Showcase) {
            TEST_ASSERT_EQUAL_INT16(0, baseCommanded);
        }
    }
    return everArmed;
}

} // namespace

// Structurally never armed: the SAME gate, the SAME hostile switch/stick
// sequence -- the only difference is the bootmode arm-input pin. Drive arms
// (proving the scenario is genuinely arming-capable and the pin, not the
// gate, is what de-fangs showcase); Showcase never arms, so baseCommanded
// stays 0 and the ESC never leaves neutral (esc.setThrottle(0) -> neutral
// pulse is pinned in test_outputs; applyBoost(0) == 0 in test_ers).
void test_showcase_never_arms_where_drive_does() {
    TEST_ASSERT_TRUE(scenarioEverArms(BootMode::Drive));
    TEST_ASSERT_FALSE(scenarioEverArms(BootMode::Showcase));
    // BtSolo arms exactly like Drive through this policy: arming discipline
    // in that mode lives in the pad ritual upstream (strict clear-on-failsafe
    // + release-before-rearm, pinned in test_btpad), not in a second gate.
    TEST_ASSERT_TRUE(scenarioEverArms(BootMode::BtSolo));
}

// --- Policy 2: the D4 failsafe-flag truth table ------------------------------

// Owner decision D4, full table. Drive: the wire flag IS the FSM state --
// UNCHANGED semantics, everLinked never consulted. Showcase: assert only
// when Safe AND a link existed this boot (shelf never hazards; a dead table
// radio is still told).
void test_d4_failsafe_flag_truth_table() {
    struct Row {
        BootMode mode;
        bool fsmSafe;
        bool everLinked;
        bool expected;
    };
    const Row rows[] = {
        // Drive-mode wire semantics byte-unchanged: flag == fsmSafe.
        {BootMode::Drive, false, false, false},
        {BootMode::Drive, false, true, false},
        {BootMode::Drive, true, false, true}, // never-linked drive boot DOES hazard (today's behavior)
        {BootMode::Drive, true, true, true},
        // Showcase: Safe && everLinkedThisBoot.
        {BootMode::Showcase, false, false, false},
        {BootMode::Showcase, false, true, false},
        {BootMode::Showcase, true, false, false}, // shelf: no CRSF ever -> NO hazard
        {BootMode::Showcase, true, true, true},   // radio died mid-session -> told
        // BtSolo: plain fsmSafe, like Drive -- the pad modes HAVE drive
        // authority, and awaiting-a-controller SHOULD read as failsafe until
        // the modeFlags-bit1 surface is emitted (OWNER-DECIDED(BT-7) slice).
        {BootMode::BtSolo, false, false, false},
        {BootMode::BtSolo, false, true, false},
        {BootMode::BtSolo, true, false, true},
        {BootMode::BtSolo, true, true, true},
    };
    for (const Row& r : rows) {
        TEST_ASSERT_EQUAL(r.expected,
                          bootmode::link2FailsafeFlag(r.mode, r.fsmSafe, r.everLinked));
    }
}

// D4 against the REAL failsafe FSM, whole-boot storyline: never-linked ->
// noise frame -> first PROVEN link -> lost mid-session -> re-linked. Uses
// the machine's actual timeout (500 ms), re-arm confirm (150 ms link proof),
// and the hasEverLinked() latch main.cpp feeds the policy from (latched at
// proof completion, NOT at first raw frame -- 2026-08-20 hardening).
void test_d4_with_real_fsm_never_linked_then_lost_then_relinked() {
    FailsafeStateMachine fsm;

    // Phase 1 -- shelf: 1 s of ticks, no frame ever. Safe, but everLinked
    // stays false, so the SHOWCASE wire flag is CLEAR (no hazard on the
    // shelf) while the DRIVE wire flag is SET (unchanged today-behavior).
    uint32_t t = 0;
    for (; t <= 1000; t += 20) {
        TEST_ASSERT_EQUAL(State::Safe, fsm.update(t, false, false));
    }
    TEST_ASSERT_FALSE(fsm.hasEverReceivedFrame());
    TEST_ASSERT_FALSE(fsm.hasEverLinked());
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                  fsm.state() == State::Safe,
                                                  fsm.hasEverLinked()));
    TEST_ASSERT_TRUE(bootmode::link2FailsafeFlag(BootMode::Drive,
                                                 fsm.state() == State::Safe,
                                                 fsm.hasEverLinked()));

    // Phase 1b -- a lone CRC-colliding noise frame on the shelf: bytes were
    // seen, but no link was PROVEN, so the showcase wire flag must STAY
    // clear (with the first-frame latch this hazard-blinked forever).
    fsm.update(t, true, false);
    for (uint32_t end = t + 1000; t < end;) {
        t += 20;
        TEST_ASSERT_EQUAL(State::Safe, fsm.update(t, false, false));
    }
    TEST_ASSERT_TRUE(fsm.hasEverReceivedFrame());
    TEST_ASSERT_FALSE(fsm.hasEverLinked());
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                  fsm.state() == State::Safe,
                                                  fsm.hasEverLinked()));

    // Phase 2 -- the table demo begins: frames at the real 20 ms cadence,
    // Active once the 150 ms link proof lands; the latch sets exactly THEN,
    // so the flag never blips during the very first confirm window.
    for (uint32_t end = t + 200; t < end;) {
        t += 20;
        fsm.update(t, true, false);
        TEST_ASSERT_EQUAL(fsm.state() == State::Active, fsm.hasEverLinked());
    }
    TEST_ASSERT_EQUAL(State::Active, fsm.state());
    TEST_ASSERT_TRUE(fsm.hasEverLinked());
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                  fsm.state() == State::Safe,
                                                  fsm.hasEverLinked()));

    // Phase 3 -- radio dies mid-showcase: frames stop, Safe after 500 ms,
    // and NOW the showcase flag asserts (everLinked latched) -- a dead table
    // radio must look dead (design draft, failsafe table row 3).
    const uint32_t lastFrameMs = t;
    while (t - lastFrameMs < 600) {
        t += 20;
        fsm.update(t, false, false);
    }
    TEST_ASSERT_EQUAL(State::Safe, fsm.state());
    TEST_ASSERT_TRUE(fsm.hasEverLinked()); // latch survives the loss
    TEST_ASSERT_TRUE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                 fsm.state() == State::Safe,
                                                 fsm.hasEverLinked()));

    // Phase 4 -- radio returns: a full link proof (continuous validity +
    // frame count) re-arms the FSM and the flag clears again (recovery is
    // automatic).
    for (uint32_t end = t + 200; t < end;) {
        t += 20;
        fsm.update(t, true, false);
    }
    TEST_ASSERT_EQUAL(State::Active, fsm.state());
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                  fsm.state() == State::Safe,
                                                  fsm.hasEverLinked()));
}

// D3 -> D4 regression: SHOWCASE selected via the NEW physical strap path
// (GPIO32 throw) reaches exactly the ratified showcase semantics -- the same
// mode value the compile-time bench injection produces, so every D4/policy
// behavior already pinned above applies verbatim to a strap-selected boot.
void test_showcase_via_strap_path_has_d4_semantics() {
    const BootMode mode =
        bootmode::resolve(bootmode::classifyStrapLevels(kAllHigh, kAllLow, 9));
    TEST_ASSERT_EQUAL(BootMode::Showcase, mode);
    // Identical to the bench-injection route (one mode model, no parallel path).
    TEST_ASSERT_EQUAL(bootmode::resolve(StrapReading::ShowPosition), mode);

    // Policy 1: structurally disarmed -- the hostile arming scenario that
    // arms Drive never arms a strap-selected Showcase.
    TEST_ASSERT_FALSE(scenarioEverArms(mode));
    // Policy 2: the D4 flag rows for showcase (shelf never hazards; a dead
    // table radio is still told).
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(mode, true, false));
    TEST_ASSERT_TRUE(bootmode::link2FailsafeFlag(mode, true, true));
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(mode, false, true));
    // Policy 3: the showcase wire bit rides.
    TEST_ASSERT_TRUE(bootmode::link2ShowcaseFlag(mode));
}

// --- Policy 3: the showcase wire flag ----------------------------------------

void test_showcase_flag_is_the_boot_mode_and_nothing_else() {
    TEST_ASSERT_FALSE(bootmode::link2ShowcaseFlag(BootMode::Drive));
    TEST_ASSERT_TRUE(bootmode::link2ShowcaseFlag(BootMode::Showcase));
    // The ratified bit split: bit0 belongs to showcase; a BT boot NEVER sets
    // it (bit1, reserved-unemitted, is that mode's own future surface).
    TEST_ASSERT_FALSE(bootmode::link2ShowcaseFlag(BootMode::BtSolo));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_resolver_truth_table_floats_to_drive);
    RUN_TEST(test_classify_pin_majority_and_faults);
    RUN_TEST(test_sp3t_position_truth_table);
    RUN_TEST(test_sp3t_fault_injections_fail_toward_drive);
    RUN_TEST(test_no_straps_resolves_to_todays_drive);
    RUN_TEST(test_boot_mode_latches_once_at_boot);
    RUN_TEST(test_arm_switch_input_pinned_false_in_showcase_passthrough_in_drive);
    RUN_TEST(test_showcase_never_arms_where_drive_does);
    RUN_TEST(test_d4_failsafe_flag_truth_table);
    RUN_TEST(test_d4_with_real_fsm_never_linked_then_lost_then_relinked);
    RUN_TEST(test_showcase_via_strap_path_has_d4_semantics);
    RUN_TEST(test_showcase_flag_is_the_boot_mode_and_nothing_else);
    return UNITY_END();
}
