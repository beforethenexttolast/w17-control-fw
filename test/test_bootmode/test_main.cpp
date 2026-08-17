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
// reading selects Showcase. Floating (the compile-time default while the
// strap pin stays OWNER-PENDING), the LAPTOP position, and any garbage value
// a corrupted read could cast in all resolve to Drive -- a selector fault
// makes the car less entertaining, never more armed.
void test_resolver_truth_table_floats_to_drive() {
    TEST_ASSERT_EQUAL(BootMode::Drive, bootmode::resolve(StrapReading::Floating));
    TEST_ASSERT_EQUAL(BootMode::Drive, bootmode::resolve(StrapReading::DrivePosition));
    TEST_ASSERT_EQUAL(BootMode::Showcase, bootmode::resolve(StrapReading::ShowPosition));

    // Out-of-range readings (defensive: a broken classifier or bit flip) are
    // Drive too -- resolve() is total over the underlying byte.
    for (int raw = 3; raw <= 255; ++raw) {
        TEST_ASSERT_EQUAL(BootMode::Drive,
                          bootmode::resolve(static_cast<StrapReading>(raw)));
    }

    // Compile-time resolvable: the whole seam folds away in today's builds,
    // where main.cpp injects the constexpr Floating reading.
    static_assert(bootmode::resolve(StrapReading::Floating) == BootMode::Drive,
                  "floating strap must resolve to Drive at compile time");
    static_assert(bootmode::resolve(StrapReading::ShowPosition) == BootMode::Showcase,
                  "SHOW strap must resolve to Showcase at compile time");
}

// --- Policy 1: the arm input ------------------------------------------------

void test_arm_switch_input_pinned_false_in_showcase_passthrough_in_drive() {
    // Showcase: false REGARDLESS of the decoded switch.
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(BootMode::Showcase, false));
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(BootMode::Showcase, true));
    // Drive: transparent pass-through (byte-identical normal behavior).
    TEST_ASSERT_FALSE(bootmode::armSwitchInput(BootMode::Drive, false));
    TEST_ASSERT_TRUE(bootmode::armSwitchInput(BootMode::Drive, true));
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
// gate in normal operation: switch on with the stick neutral, neutral-then-
// displaced, a failsafe episode with the switch held on through recovery.
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
        {true, 0, false},     // recovery, switch STILL on, neutral seen -> Drive re-arms
        {true, 1000, false},  // and full throttle again
        {false, 0, false},    // switch off
        {true, -60, false},   // switch on inside the neutral window (|t| <= 60)
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
    };
    for (const Row& r : rows) {
        TEST_ASSERT_EQUAL(r.expected,
                          bootmode::link2FailsafeFlag(r.mode, r.fsmSafe, r.everLinked));
    }
}

// D4 against the REAL failsafe FSM, whole-boot storyline: never-linked ->
// first link -> lost mid-session -> re-linked. Uses the machine's actual
// timeout (500 ms) and re-arm confirm (150 ms) and the hasEverReceivedFrame()
// latch main.cpp feeds the policy from.
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
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                  fsm.state() == State::Safe,
                                                  fsm.hasEverReceivedFrame()));
    TEST_ASSERT_TRUE(bootmode::link2FailsafeFlag(BootMode::Drive,
                                                 fsm.state() == State::Safe,
                                                 fsm.hasEverReceivedFrame()));

    // Phase 2 -- the table demo begins: frames arrive, the latch sets
    // IMMEDIATELY (first frame), Active after the 150 ms confirm window.
    fsm.update(t, true, false);
    TEST_ASSERT_TRUE(fsm.hasEverReceivedFrame());
    for (uint32_t end = t + 200; t < end;) {
        t += 20;
        fsm.update(t, true, false);
    }
    TEST_ASSERT_EQUAL(State::Active, fsm.state());
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                  fsm.state() == State::Safe,
                                                  fsm.hasEverReceivedFrame()));

    // Phase 3 -- radio dies mid-showcase: frames stop, Safe after 500 ms,
    // and NOW the showcase flag asserts (everLinked latched) -- a dead table
    // radio must look dead (design draft, failsafe table row 3).
    const uint32_t lastFrameMs = t;
    while (t - lastFrameMs < 600) {
        t += 20;
        fsm.update(t, false, false);
    }
    TEST_ASSERT_EQUAL(State::Safe, fsm.state());
    TEST_ASSERT_TRUE(fsm.hasEverReceivedFrame()); // latch survives the loss
    TEST_ASSERT_TRUE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                 fsm.state() == State::Safe,
                                                 fsm.hasEverReceivedFrame()));

    // Phase 4 -- radio returns: continuous validity for the confirm window
    // re-arms the FSM and the flag clears again (recovery is automatic).
    for (uint32_t end = t + 200; t < end;) {
        t += 20;
        fsm.update(t, true, false);
    }
    TEST_ASSERT_EQUAL(State::Active, fsm.state());
    TEST_ASSERT_FALSE(bootmode::link2FailsafeFlag(BootMode::Showcase,
                                                  fsm.state() == State::Safe,
                                                  fsm.hasEverReceivedFrame()));
}

// --- Policy 3: the showcase wire flag ----------------------------------------

void test_showcase_flag_is_the_boot_mode_and_nothing_else() {
    TEST_ASSERT_FALSE(bootmode::link2ShowcaseFlag(BootMode::Drive));
    TEST_ASSERT_TRUE(bootmode::link2ShowcaseFlag(BootMode::Showcase));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_resolver_truth_table_floats_to_drive);
    RUN_TEST(test_arm_switch_input_pinned_false_in_showcase_passthrough_in_drive);
    RUN_TEST(test_showcase_never_arms_where_drive_does);
    RUN_TEST(test_d4_failsafe_flag_truth_table);
    RUN_TEST(test_d4_with_real_fsm_never_linked_then_lost_then_relinked);
    RUN_TEST(test_showcase_flag_is_the_boot_mode_and_nothing_else);
    return UNITY_END();
}
