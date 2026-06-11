/*
 * arduino_interactive.ino
 * Interactive demo firmware for espilon-monitor -i mode.
 *
 * Commands (send via serial, \r or \n to confirm):
 *   help     - list commands
 *   ping     - replies pong
 *   version  - firmware version
 *   crash    - triggers panic: (emon detects PANIC event)
 *   assert   - triggers Assertion failed (emon detects ASSERT event)
 *   oom      - triggers low memory warning (emon detects LOW_MEMORY event)
 *   reset    - soft reset via watchdog
 *   Ctrl+C   - same as reset
 */

#include <avr/wdt.h>

#define PROMPT "> "

static String s_buf;

static void soft_reset(void)
{
    Serial.println(F("\r\nresetting..."));
    Serial.flush();
    delay(100);
    wdt_enable(WDTO_15MS);
    while (1) {}
}

void setup(void)
{
    wdt_disable();
    Serial.begin(115200);
    while (!Serial) {}
    Serial.println(F("espilon-interactive v1.0 - type 'help'"));
    Serial.print(F(PROMPT));
    s_buf = "";
}

void loop(void)
{
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == 0x03) {          /* Ctrl+C → reset */
            soft_reset();
            return;
        }

        if (c == '\b' || c == 127) {   /* backspace */
            if (s_buf.length() > 0) {
                s_buf.remove(s_buf.length() - 1);
                Serial.print(F("\b \b"));
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            Serial.println();
            s_buf.trim();

            if (s_buf == "help") {
                Serial.println(F("Commands:"));
                Serial.println(F("  ping    - pong"));
                Serial.println(F("  version - firmware info"));
                Serial.println(F("  crash   - panic: forced crash"));
                Serial.println(F("  assert  - Assertion 0 failed"));
                Serial.println(F("  oom     - low memory warning"));
                Serial.println(F("  reset   - soft reset"));
                Serial.println(F("  Ctrl+C  - soft reset"));

            } else if (s_buf == "ping") {
                Serial.println(F("pong"));

            } else if (s_buf == "version") {
                Serial.println(F("espilon-interactive v1.0 (Arduino)"));
                Serial.print(F("free heap: "));
                Serial.print((int)SP - (int)__malloc_heap_start);
                Serial.println(F(" bytes"));

            } else if (s_buf == "crash") {
                Serial.println(F("panic: forced crash trigger"));
                Serial.flush();
                delay(200);
                soft_reset();
                return;

            } else if (s_buf == "assert") {
                Serial.println(F("Assertion 0 failed at arduino_interactive.ino:99"));
                Serial.flush();
                delay(200);
                soft_reset();
                return;

            } else if (s_buf == "oom") {
                Serial.println(F("low memory: heap fragment 4 bytes free"));

            } else if (s_buf == "reset") {
                soft_reset();
                return;

            } else if (s_buf.length() > 0) {
                Serial.print(F("unknown command: "));
                Serial.println(s_buf);
                Serial.println(F("type 'help' for commands"));
            }

            s_buf = "";
            Serial.print(F(PROMPT));

        } else if (c >= 32 && c < 127) {
            s_buf += c;
            /* no echo — emon handles local echo in -i mode */
        }
    }
}
