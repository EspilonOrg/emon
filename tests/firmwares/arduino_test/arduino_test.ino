/*
 * arduino_test.ino — emon hardware test for Arduino Uno/Nano (ATmega328P)
 *
 * Cycles through 5 phases across reboots using EEPROM for state persistence.
 * Each phase generates serial output that arduino_test.pat should detect.
 * Run with emon --wait-for ALL_DONE --timeout 60 to monitor the full sequence.
 *
 *   Phase 0: arm watchdog → hang → WDT fires → hardware reset
 *   Phase 1: detect WDT reset → print "Watchdog Reset" → assert → reboot
 *   Phase 2: print "Assertion 0 failed" → reboot
 *   Phase 3: print "panic:" → reboot
 *   Phase 4: print "low memory" + "ALL_DONE" → clear phase → done
 *
 * WDT detection uses a .noinit magic value in RAM (preserved across warm/WDT
 * resets, not across power-on resets). This works on clone boards whose
 * bootloader clears MCUSR before our .init3 hook can read it.
 */

#include <avr/wdt.h>
#include <avr/io.h>
#include <EEPROM.h>

/*
 * .noinit RAM is never zeroed by the C runtime. A WDT reset is a warm reset —
 * RAM content survives. A power-on or manual reset is cold — RAM is random,
 * so WDT_MAGIC won't be present by chance (2^16 = 1/65536 false positive).
 */
#define WDT_MAGIC 0xCAFE
volatile uint16_t _wdt_flag __attribute__((section(".noinit")));

/* Arm: stamp magic before enabling WDT so the next boot can detect it. */
static void wdt_arm(void) {
    _wdt_flag = WDT_MAGIC;
    wdt_enable(WDTO_2S);
}

/* Detect and consume: returns true once, then clears the flag. */
static bool wdt_was_reset(void) {
    if (_wdt_flag != WDT_MAGIC) return false;
    _wdt_flag = 0;
    return true;
}

#define EEPROM_PHASE_ADDR 0
#define SERIAL_BAUD       115200

static void reboot_now(void) {
    wdt_enable(WDTO_15MS);
    while (1) { }
}

void setup() {
    wdt_disable();   /* always disable WDT first — bootloader may leave it armed */
    Serial.begin(SERIAL_BAUD);
    delay(300);

    uint8_t phase = EEPROM.read(EEPROM_PHASE_ADDR);
    if (phase > 4 || phase == 0xFF) {
        phase = 0;
        EEPROM.write(EEPROM_PHASE_ADDR, 0);
    }

    Serial.print(F("=== arduino_test phase "));
    Serial.print(phase);
    Serial.println(F("/4 ==="));
    delay(100);

    switch (phase) {

    case 0:
        Serial.println(F("[phase 0] arming watchdog - board will reset"));
        EEPROM.write(EEPROM_PHASE_ADDR, 1);
        delay(100);
        wdt_arm();
        while (1) { }   /* hang — WDT fires after 2s */
        break;

    case 1:
        if (wdt_was_reset()) {
            Serial.println(F("Watchdog Reset"));
        } else {
            /* Should not happen — EEPROM phase advanced only after wdt_arm() */
            Serial.println(F("Watchdog Reset (magic mismatch — cold start?)"));
        }
        EEPROM.write(EEPROM_PHASE_ADDR, 2);
        delay(1000);
        reboot_now();
        break;

    case 2:
        Serial.println(F("Assertion 0 failed at arduino_test.ino:57"));
        EEPROM.write(EEPROM_PHASE_ADDR, 3);
        delay(1000);
        reboot_now();
        break;

    case 3:
        Serial.println(F("panic: forced hardware test trigger"));
        EEPROM.write(EEPROM_PHASE_ADDR, 4);
        delay(1000);
        reboot_now();
        break;

    case 4:
        Serial.println(F("low memory: heap fragment 6 bytes free"));
        delay(300);
        Serial.println(F("ALL_DONE"));
        EEPROM.write(EEPROM_PHASE_ADDR, 0);   /* reset for next run */
        break;
    }
}

void loop() { }
