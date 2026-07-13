#include <unity.h>

#include "reset_diag/ResetDiagnostics.hpp"

using reset_diag::BootReport;
using reset_diag::classify;
using reset_diag::isValid;
using reset_diag::kMagic;
using reset_diag::kMaxBootCount;
using reset_diag::kVersion;
using reset_diag::label;
using reset_diag::RawResetReason;
using reset_diag::ResetClass;
using reset_diag::SessionState;
using reset_diag::updateSession;

void setUp() {}
void tearDown() {}

// A helper that builds a VALID retained state at a chosen count, so warm-reset
// tests exercise the production isValid()/updateSession() path rather than
// hand-poking bytes the code under test would reject.
static SessionState validState(uint32_t bootCount, ResetClass last) {
    SessionState s;
    s.magic = kMagic;
    s.magicInverse = static_cast<uint32_t>(~kMagic);
    s.version = kVersion;
    s.bootCount = bootCount;
    s.lastResetClass = static_cast<uint8_t>(last);
    return s;
}

// --- Classification: every pinned raw reason maps deliberately (req #1-#10) ---

void test_classify_power_on() {
    TEST_ASSERT_EQUAL(ResetClass::PowerOn, classify(RawResetReason::PowerOn));
}
void test_classify_software() {
    TEST_ASSERT_EQUAL(ResetClass::Software, classify(RawResetReason::Sw));
}
void test_classify_panic() {
    TEST_ASSERT_EQUAL(ResetClass::Panic, classify(RawResetReason::Panic));
}
void test_classify_interrupt_watchdog() {
    TEST_ASSERT_EQUAL(ResetClass::InterruptWatchdog, classify(RawResetReason::IntWdt));
}
void test_classify_task_watchdog() {
    TEST_ASSERT_EQUAL(ResetClass::TaskWatchdog, classify(RawResetReason::TaskWdt));
}
void test_classify_other_watchdog() {
    TEST_ASSERT_EQUAL(ResetClass::OtherWatchdog, classify(RawResetReason::Wdt));
}
void test_classify_brownout() {
    TEST_ASSERT_EQUAL(ResetClass::Brownout, classify(RawResetReason::Brownout));
}
void test_classify_deep_sleep() {
    TEST_ASSERT_EQUAL(ResetClass::DeepSleep, classify(RawResetReason::DeepSleep));
}
void test_classify_external() {
    TEST_ASSERT_EQUAL(ResetClass::External, classify(RawResetReason::Ext));
}
void test_classify_sdio() {
    TEST_ASSERT_EQUAL(ResetClass::Sdio, classify(RawResetReason::Sdio));
}
void test_classify_unknown() {
    TEST_ASSERT_EQUAL(ResetClass::Unknown, classify(RawResetReason::Unknown));
}

// Panic and task-WDT must stay DISTINCT (task brief: a watchdog fault may report
// either on real hardware; R5-b must preserve both).
void test_panic_and_task_wdt_are_distinct_classes() {
    TEST_ASSERT_NOT_EQUAL(classify(RawResetReason::Panic), classify(RawResetReason::TaskWdt));
}

// Every pinned enum value (0..10) maps to a class, and no two RAW reasons that
// the brief lists as separate collapse together unexpectedly. Iterating the
// exact pinned value set proves the switch has no silent fall-through.
void test_every_pinned_raw_value_maps_and_labels() {
    const RawResetReason all[] = {
        RawResetReason::Unknown, RawResetReason::PowerOn, RawResetReason::Ext,
        RawResetReason::Sw,      RawResetReason::Panic,   RawResetReason::IntWdt,
        RawResetReason::TaskWdt, RawResetReason::Wdt,     RawResetReason::DeepSleep,
        RawResetReason::Brownout, RawResetReason::Sdio,
    };
    // 11 pinned values, matching esp_reset_reason_t on the esp32 target.
    TEST_ASSERT_EQUAL_UINT32(11u, sizeof(all) / sizeof(all[0]));
    for (RawResetReason r : all) {
        const char* text = label(classify(r));
        TEST_ASSERT_NOT_NULL(text);
        TEST_ASSERT_TRUE(text[0] != '\0'); // non-empty, concrete label
    }
}

// --- Stable labels for every internal class (req #11) ---
void test_labels_are_stable_for_every_class() {
    TEST_ASSERT_EQUAL_STRING("POWER_ON",   label(ResetClass::PowerOn));
    TEST_ASSERT_EQUAL_STRING("SOFTWARE",   label(ResetClass::Software));
    TEST_ASSERT_EQUAL_STRING("PANIC",      label(ResetClass::Panic));
    TEST_ASSERT_EQUAL_STRING("INT_WDT",    label(ResetClass::InterruptWatchdog));
    TEST_ASSERT_EQUAL_STRING("TASK_WDT",   label(ResetClass::TaskWatchdog));
    TEST_ASSERT_EQUAL_STRING("WDT",        label(ResetClass::OtherWatchdog));
    TEST_ASSERT_EQUAL_STRING("BROWNOUT",   label(ResetClass::Brownout));
    TEST_ASSERT_EQUAL_STRING("DEEP_SLEEP", label(ResetClass::DeepSleep));
    TEST_ASSERT_EQUAL_STRING("EXTERNAL",   label(ResetClass::External));
    TEST_ASSERT_EQUAL_STRING("SDIO",       label(ResetClass::Sdio));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",    label(ResetClass::Unknown));
}

// --- Validity policy (req #4, #12) ---

void test_uninitialized_like_bytes_are_invalid() {
    // Simulate indeterminate RTC memory after power loss: all-0xFF.
    SessionState s;
    s.magic = 0xFFFFFFFFu;
    s.magicInverse = 0xFFFFFFFFu;
    s.version = 0xFFFFFFFFu;
    s.bootCount = 0xFFFFFFFFu;
    s.lastResetClass = 0xFF;
    TEST_ASSERT_FALSE(isValid(s));
}

void test_correct_magic_but_bad_inverse_is_invalid() {
    SessionState s = validState(5, ResetClass::Software);
    s.magicInverse = 0; // single-word corruption
    TEST_ASSERT_FALSE(isValid(s));
}

void test_wrong_version_is_invalid() {
    SessionState s = validState(5, ResetClass::Software);
    s.version = kVersion + 1;
    TEST_ASSERT_FALSE(isValid(s));
}

void test_fully_stamped_state_is_valid() {
    TEST_ASSERT_TRUE(isValid(validState(3, ResetClass::TaskWatchdog)));
}

// --- Session update behavior ---

// req #12: invalid RTC magic initializes a fresh session.
void test_invalid_magic_starts_fresh_session() {
    SessionState s;                 // arbitrary garbage
    s.magic = 0xDEADBEEFu;
    s.magicInverse = 0x0BADF00Du;
    s.version = 99u;
    s.bootCount = 12345u;
    s.lastResetClass = 200u;

    // A warm reset (software) that would normally increment -- but the state is
    // invalid, so it must restart, NOT trust the bogus 12345 count.
    const BootReport r = updateSession(s, ResetClass::Software);
    TEST_ASSERT_FALSE(r.incomingValid);
    TEST_ASSERT_TRUE(r.freshSession);
    TEST_ASSERT_EQUAL_UINT32(1u, r.bootCount);
    TEST_ASSERT_EQUAL(ResetClass::Software, r.reason);
    TEST_ASSERT_TRUE(isValid(s)); // state is now stamped valid
    TEST_ASSERT_EQUAL_UINT32(1u, s.bootCount);
}

// req #13: power-on starts a fresh session even if retained bytes were valid.
void test_power_on_starts_fresh_even_from_valid_state() {
    SessionState s = validState(7, ResetClass::Software);
    const BootReport r = updateSession(s, ResetClass::PowerOn);
    TEST_ASSERT_TRUE(r.incomingValid);  // bytes were valid...
    TEST_ASSERT_TRUE(r.freshSession);   // ...but power-on restarts anyway
    TEST_ASSERT_EQUAL_UINT32(1u, r.bootCount);
    TEST_ASSERT_EQUAL(ResetClass::PowerOn, r.reason);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(ResetClass::PowerOn), s.lastResetClass);
}

// req #14: a valid warm reset increments the boot counter.
void test_valid_warm_reset_increments_counter() {
    SessionState s = validState(4, ResetClass::PowerOn);
    const BootReport r = updateSession(s, ResetClass::Software);
    TEST_ASSERT_TRUE(r.incomingValid);
    TEST_ASSERT_FALSE(r.freshSession);
    TEST_ASSERT_EQUAL_UINT32(5u, r.bootCount);
    TEST_ASSERT_EQUAL_UINT32(5u, s.bootCount);
}

// req #15: task-WDT reset increments and records TASK_WDT.
void test_task_wdt_reset_increments_and_records() {
    SessionState s = validState(2, ResetClass::PowerOn);
    const BootReport r = updateSession(s, classify(RawResetReason::TaskWdt));
    TEST_ASSERT_FALSE(r.freshSession);
    TEST_ASSERT_EQUAL_UINT32(3u, r.bootCount);
    TEST_ASSERT_EQUAL(ResetClass::TaskWatchdog, r.reason);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(ResetClass::TaskWatchdog), s.lastResetClass);
}

// req #16: panic reset increments and records PANIC.
void test_panic_reset_increments_and_records() {
    SessionState s = validState(9, ResetClass::TaskWatchdog);
    const BootReport r = updateSession(s, classify(RawResetReason::Panic));
    TEST_ASSERT_FALSE(r.freshSession);
    TEST_ASSERT_EQUAL_UINT32(10u, r.bootCount);
    TEST_ASSERT_EQUAL(ResetClass::Panic, r.reason);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint8_t>(ResetClass::Panic), s.lastResetClass);
}

// Other warm resets also continue the session (int-WDT / deep-sleep / external
// / other-WDT / SDIO / unknown), proving the increment path is not special-cased
// to only two reasons.
void test_other_warm_resets_continue_session() {
    const ResetClass warm[] = {
        ResetClass::InterruptWatchdog, ResetClass::OtherWatchdog, ResetClass::DeepSleep,
        ResetClass::External,          ResetClass::Sdio,          ResetClass::Unknown,
    };
    for (ResetClass c : warm) {
        SessionState s = validState(1, ResetClass::PowerOn);
        const BootReport r = updateSession(s, c);
        TEST_ASSERT_FALSE(r.freshSession);
        TEST_ASSERT_EQUAL_UINT32(2u, r.bootCount);
        TEST_ASSERT_EQUAL(c, r.reason);
    }
}

// req #17: brownout follows the cautious policy -- valid retained bytes are NOT
// trusted; the session restarts rather than incrementing.
void test_brownout_restarts_session_even_when_incoming_valid() {
    SessionState s = validState(6, ResetClass::Software);
    const BootReport r = updateSession(s, ResetClass::Brownout);
    TEST_ASSERT_TRUE(r.incomingValid);  // bytes looked valid...
    TEST_ASSERT_TRUE(r.freshSession);   // ...but brownout may corrupt RTC -> distrust
    TEST_ASSERT_EQUAL_UINT32(1u, r.bootCount);
    TEST_ASSERT_EQUAL(ResetClass::Brownout, r.reason);
}

// req #18: counter overflow saturates deterministically at kMaxBootCount.
void test_counter_saturates_at_max() {
    SessionState s = validState(kMaxBootCount, ResetClass::Software);
    const BootReport r = updateSession(s, ResetClass::Software);
    TEST_ASSERT_FALSE(r.freshSession);
    TEST_ASSERT_EQUAL_UINT32(kMaxBootCount, r.bootCount); // clamped, did not wrap to 0
}

void test_counter_at_max_minus_one_reaches_max() {
    SessionState s = validState(kMaxBootCount - 1u, ResetClass::Software);
    const BootReport r = updateSession(s, ResetClass::Software);
    TEST_ASSERT_EQUAL_UINT32(kMaxBootCount, r.bootCount);
}

// req #19: updateSession touches ONLY the SessionState it is given -- a canary
// placed immediately after it in memory must be unchanged (no over-write).
void test_update_does_not_modify_unrelated_memory() {
    struct Framed {
        SessionState state;
        uint32_t canary;
    } f;
    f.state = validState(3, ResetClass::Software);
    f.canary = 0xA5A5A5A5u;
    updateSession(f.state, ResetClass::Panic);
    TEST_ASSERT_EQUAL_UINT32(0xA5A5A5A5u, f.canary);
}

// Multi-boot session walk: power-on -> two warm resets -> the printed count and
// last reason evolve exactly as the boot line would show.
void test_session_walk_across_multiple_boots() {
    SessionState s;
    s.magic = 0; s.magicInverse = 0; s.version = 0; s.bootCount = 0; s.lastResetClass = 0;

    BootReport r = updateSession(s, ResetClass::PowerOn);
    TEST_ASSERT_EQUAL_UINT32(1u, r.bootCount);
    TEST_ASSERT_TRUE(r.freshSession);

    r = updateSession(s, ResetClass::TaskWatchdog);
    TEST_ASSERT_EQUAL_UINT32(2u, r.bootCount);
    TEST_ASSERT_FALSE(r.freshSession);
    TEST_ASSERT_EQUAL(ResetClass::TaskWatchdog, r.reason);

    r = updateSession(s, ResetClass::Panic);
    TEST_ASSERT_EQUAL_UINT32(3u, r.bootCount);
    TEST_ASSERT_FALSE(r.freshSession);
    TEST_ASSERT_EQUAL(ResetClass::Panic, r.reason);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_classify_power_on);
    RUN_TEST(test_classify_software);
    RUN_TEST(test_classify_panic);
    RUN_TEST(test_classify_interrupt_watchdog);
    RUN_TEST(test_classify_task_watchdog);
    RUN_TEST(test_classify_other_watchdog);
    RUN_TEST(test_classify_brownout);
    RUN_TEST(test_classify_deep_sleep);
    RUN_TEST(test_classify_external);
    RUN_TEST(test_classify_sdio);
    RUN_TEST(test_classify_unknown);
    RUN_TEST(test_panic_and_task_wdt_are_distinct_classes);
    RUN_TEST(test_every_pinned_raw_value_maps_and_labels);
    RUN_TEST(test_labels_are_stable_for_every_class);
    RUN_TEST(test_uninitialized_like_bytes_are_invalid);
    RUN_TEST(test_correct_magic_but_bad_inverse_is_invalid);
    RUN_TEST(test_wrong_version_is_invalid);
    RUN_TEST(test_fully_stamped_state_is_valid);
    RUN_TEST(test_invalid_magic_starts_fresh_session);
    RUN_TEST(test_power_on_starts_fresh_even_from_valid_state);
    RUN_TEST(test_valid_warm_reset_increments_counter);
    RUN_TEST(test_task_wdt_reset_increments_and_records);
    RUN_TEST(test_panic_reset_increments_and_records);
    RUN_TEST(test_other_warm_resets_continue_session);
    RUN_TEST(test_brownout_restarts_session_even_when_incoming_valid);
    RUN_TEST(test_counter_saturates_at_max);
    RUN_TEST(test_counter_at_max_minus_one_reaches_max);
    RUN_TEST(test_update_does_not_modify_unrelated_memory);
    RUN_TEST(test_session_walk_across_multiple_boots);
    return UNITY_END();
}
