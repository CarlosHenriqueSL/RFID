#include "pico/stdlib.h"
#include <string.h>
#include "mfrc522.h"

#define LED_GREEN 11
#define LED_BLUE 12
#define LED_RED 13

#define MAX_KNOWN 3
#define MAX_UID_STRLEN 24

static char known_uids[MAX_KNOWN][MAX_UID_STRLEN];
static int known_count = 0;
static char last_uid[MAX_UID_STRLEN] = "";
static uint64_t last_uid_time_ms = 0;

static void leds_init(void) {
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_put(LED_GREEN, 0);

    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_put(LED_BLUE, 0);

    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_put(LED_RED, 0);
}

static void leds_all_off(void) {
    gpio_put(LED_GREEN, 0);
    gpio_put(LED_BLUE, 0);
    gpio_put(LED_RED, 0);
}

static void blink_led(uint pin, int times, int ms) {
    for (int i = 0; i < times; ++i) {
        gpio_put(pin, 1);
        sleep_ms(ms);
        gpio_put(pin, 0);
        sleep_ms(ms);
    }
}

static int find_known(const char *uid) {
    for (int i = 0; i < known_count; ++i) {
        if (strcmp(known_uids[i], uid) == 0) return i;
    }
    return -1;
}

static bool add_known(const char *uid) {
    if (!uid || uid[0] == '\0') return false;
    if (find_known(uid) >= 0) return false;
    if (known_count >= MAX_KNOWN) return false;
    strncpy(known_uids[known_count], uid, MAX_UID_STRLEN - 1);
    known_uids[known_count][MAX_UID_STRLEN - 1] = '\0';
    known_count++;
    return true;
}

static bool remove_known(const char *uid) {
    int idx = find_known(uid);
    if (idx < 0) return false;
    for (int i = idx; i < known_count - 1; ++i) {
        strncpy(known_uids[i], known_uids[i + 1], MAX_UID_STRLEN);
    }
    known_count--;
    known_uids[known_count][0] = '\0';
    return true;
}

static void indicate_known_index(int idx) {
    // index 0 -> green, 1 -> blue, 2 -> red
    leds_all_off();
    if (idx == 0) gpio_put(LED_GREEN, 1);
    else if (idx == 1) gpio_put(LED_BLUE, 1);
    else if (idx == 2) gpio_put(LED_RED, 1);
}

int main() {
    stdio_init_all(); // left for compatibility; not used for interaction

    // RFID init
    MFRC522Ptr_t mfrc = MFRC522_Init();
    PCD_Init(mfrc, spi0);
    PCD_AntennaOn(mfrc);
    sleep_ms(200);

    // LEDs
    leds_init();

    // State
    leds_all_off();

    // Main loop: wait card, read UID, act only using LEDs
    char uid_str[MAX_UID_STRLEN];

    for (;;) {
        // Wait for card present
        while (!PICC_IsNewCardPresent(mfrc)) {
            sleep_ms(50);
        }

        // Try read
        if (!PICC_ReadCardSerial(mfrc)) {
            // indicate read failure: quick red blink
            blink_led(LED_RED, 2, 120);
            continue;
        }

        // format UID
        int ofs = 0;
        memset(uid_str, 0, sizeof(uid_str));
        for (int i = 0; i < mfrc->uid.size && ofs < (int)sizeof(uid_str) - 4; ++i) {
            ofs += sprintf(&uid_str[ofs], "%02X", mfrc->uid.uidByte[i]);
            if (i < mfrc->uid.size - 1) ofs += sprintf(&uid_str[ofs], " ");
        }

        uint64_t now = to_ms_since_boot(get_absolute_time());

        int idx = find_known(uid_str);
        if (idx >= 0) {
            // known card: indicate by lighting corresponding LED for 2s
            indicate_known_index(idx);

            // if presented twice rapidly (within 4000 ms) remove it (delete mode)
            if (strcmp(last_uid, uid_str) == 0 && (now - last_uid_time_ms) <= 4000ULL) {
                // remove
                bool ok = remove_known(uid_str);
                leds_all_off();
                if (ok) {
                    // blink blue twice to show removal
                    blink_led(LED_BLUE, 2, 180);
                } else {
                    blink_led(LED_RED, 2, 120);
                }
            }

            // remember
            strncpy(last_uid, uid_str, MAX_UID_STRLEN - 1);
            last_uid[MAX_UID_STRLEN - 1] = '\0';
            last_uid_time_ms = now;

            sleep_ms(2000);
            leds_all_off();
        } else {
            // unknown card
            if (known_count < MAX_KNOWN) {
                // auto-enroll and indicate with green blink
                bool ok = add_known(uid_str);
                if (ok) {
                    // short double green blink
                    blink_led(LED_GREEN, 2, 140);
                } else {
                    blink_led(LED_RED, 2, 120);
                }
            } else {
                // list full: indicate with three red blinks
                blink_led(LED_RED, 3, 120);
            }

            // store last seen for potential removal workflow
            strncpy(last_uid, uid_str, MAX_UID_STRLEN - 1);
            last_uid[MAX_UID_STRLEN - 1] = '\0';
            last_uid_time_ms = now;
        }

        // small cooldown before next read
        sleep_ms(300);
    }

    return 0;
}