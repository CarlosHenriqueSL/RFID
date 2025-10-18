#include <stdio.h>
#include "pico/stdlib.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "mfrc522.h"
#include <string.h>
#include <stdbool.h>

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

#define LED_BLUE 12
#define LED_GREEN 13

// Botões para interação (ativos low com pull-up)
#define BTN_NEXT 5
#define BTN_SELECT 6

#define MAX_KNOWN 8
#define MAX_UID_STRLEN 24

static char known_uids[MAX_KNOWN][MAX_UID_STRLEN];
static int known_count = 0;
static char last_uid[MAX_UID_STRLEN] = "";

static const char *menu_items[] = {"Scan card", "Add UID", "Remove UID", "List UIDs", "Toggle inv"};
static const int menu_len = sizeof(menu_items) / sizeof(menu_items[0]);

static bool btn_pressed(uint pin);
static void oled_show_menu(ssd1306_t *ssd, int sel, bool invert);
static bool scan_card(MFRC522Ptr_t mfrc, ssd1306_t *ssd);
static bool add_known_uid(const char *uid);
static bool remove_known_uid(const char *uid);
static void list_known_on_oled(ssd1306_t *ssd);

void main()
{
    stdio_init_all();

    // Inicializa o RFID
    MFRC522Ptr_t mfrc = MFRC522_Init();
    PCD_Init(mfrc, spi0);
    PCD_AntennaOn(mfrc); // Liga antena
    sleep_ms(500);

    // Inicializa display
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_put(LED_BLUE, 0);
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_put(LED_GREEN, 0);

    gpio_init(BTN_NEXT);
    gpio_set_dir(BTN_NEXT, GPIO_IN);
    gpio_pull_up(BTN_NEXT);

    gpio_init(BTN_SELECT);
    gpio_set_dir(BTN_SELECT, GPIO_IN);
    gpio_pull_up(BTN_SELECT);

    bool invert = false;
    int sel = 0;

    for (;;)
    {
        ssd1306_fill(&ssd, !invert);
        ssd1306_draw_string(&ssd, "RFID MENU", 20, 0);
        // mostra item selecionado com marcador
        char line[20];
        snprintf(line, sizeof(line), "> %s", menu_items[sel]);
        ssd1306_draw_string(&ssd, line, 0, 24);

        if (last_uid[0])
        {
            ssd1306_draw_string(&ssd, "Last:", 0, 40);
            ssd1306_draw_string(&ssd, last_uid, 0, 52);
        }
        else
        {
            ssd1306_draw_string(&ssd, "No UID read", 0, 52);
        }
        ssd1306_send_data(&ssd);

        // navega com NEXT, seleciona com SELECT
        if (btn_pressed(BTN_NEXT))
        {
            sel = (sel + 1) % menu_len;
            oled_show_menu(&ssd, sel, invert);
        }

        if (btn_pressed(BTN_SELECT))
        {
            // executa ação
            switch (sel)
            {
            case 0: // Scan
                scan_card(mfrc, &ssd);
                break;
            case 1: // Add last_uid
                if (last_uid[0])
                {
                    bool ok = add_known_uid(last_uid);
                    ssd1306_fill(&ssd, false);
                    if (ok)
                        ssd1306_draw_string(&ssd, "UID added", 16, 28);
                    else
                        ssd1306_draw_string(&ssd, "Add failed", 12, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                else
                {
                    ssd1306_fill(&ssd, false);
                    ssd1306_draw_string(&ssd, "No UID to add", 4, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                break;
            case 2: // Remove last_uid
                if (last_uid[0])
                {
                    bool ok = remove_known_uid(last_uid);
                    ssd1306_fill(&ssd, false);
                    if (ok)
                        ssd1306_draw_string(&ssd, "UID removed", 12, 28);
                    else
                        ssd1306_draw_string(&ssd, "Remove fail", 12, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                else
                {
                    ssd1306_fill(&ssd, false);
                    ssd1306_draw_string(&ssd, "No UID to rm", 8, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                break;
            case 3: // List
                list_known_on_oled(&ssd);
                break;
            case 4: // Toggle invert
                invert = !invert;
                oled_show_menu(&ssd, sel, invert);
                break;
            }
        }

        sleep_ms(100);
    }
}

static void gpio_setup_buttons(void)
{
    gpio_init(BTN_NEXT);
    gpio_set_dir(BTN_NEXT, GPIO_IN);
    gpio_pull_up(BTN_NEXT);

    gpio_init(BTN_SELECT);
    gpio_set_dir(BTN_SELECT, GPIO_IN);
    gpio_pull_up(BTN_SELECT);
}

static bool btn_pressed(uint pin)
{
    // ativo baixo
    if (!gpio_get(pin))
    {
        // debounce simples
        sleep_ms(20);
        if (!gpio_get(pin))
        {
            // espera soltar
            while (!gpio_get(pin))
                sleep_ms(10);
            return true;
        }
    }
    return false;
}

static int find_known_uid(const char *uid)
{
    for (int i = 0; i < known_count; ++i)
    {
        if (strcmp(known_uids[i], uid) == 0)
            return i;
    }
    return -1;
}

static bool add_known_uid(const char *uid)
{
    if (!uid || uid[0] == '\0')
        return false;
    if (find_known_uid(uid) >= 0)
        return false;
    if (known_count >= MAX_KNOWN)
        return false;
    strncpy(known_uids[known_count], uid, MAX_UID_STRLEN - 1);
    known_uids[known_count][MAX_UID_STRLEN - 1] = '\0';
    known_count++;
    return true;
}

static bool remove_known_uid(const char *uid)
{
    int idx = find_known_uid(uid);
    if (idx < 0)
        return false;
    for (int i = idx; i < known_count - 1; ++i)
    {
        strncpy(known_uids[i], known_uids[i + 1], MAX_UID_STRLEN);
    }
    known_count--;
    known_uids[known_count][0] = '\0';
    return true;
}

static void oled_show_menu(ssd1306_t *ssd, int sel, bool invert)
{
    ssd1306_fill(ssd, !invert);
    ssd1306_draw_string(ssd, "RFID MENU", 20, 0);
    // mostra item selecionado com marcador
    char line[20];
    snprintf(line, sizeof(line), "> %s", menu_items[sel]);
    ssd1306_draw_string(ssd, line, 0, 24);

    if (last_uid[0])
    {
        ssd1306_draw_string(ssd, "Last:", 0, 40);
        ssd1306_draw_string(ssd, last_uid, 0, 52);
    }
    else
    {
        ssd1306_draw_string(ssd, "No UID read", 0, 52);
    }
    ssd1306_send_data(ssd);
}

// Espera e lê um cartão; retorna true se leu e preenche last_uid
static bool scan_card(MFRC522Ptr_t mfrc, ssd1306_t *ssd)
{
    ssd1306_fill(ssd, false);
    ssd1306_draw_string(ssd, "Scanning...", 10, 24);
    ssd1306_send_data(ssd);

    // aguarda novo cartão
    for (;;)
    {
        if (PICC_IsNewCardPresent(mfrc))
            break;
        sleep_ms(100);
    }

    if (!PICC_ReadCardSerial(mfrc))
    {
        ssd1306_fill(ssd, true);
        ssd1306_draw_string(ssd, "Read fail", 20, 28);
        ssd1306_send_data(ssd);
        sleep_ms(800);
        return false;
    }

    // formata UID
    int offset = 0;
    memset(last_uid, 0, sizeof(last_uid));
    for (int i = 0; i < mfrc->uid.size && offset < (int)sizeof(last_uid) - 4; ++i)
    {
        offset += sprintf(&last_uid[offset], "%02X ", mfrc->uid.uidByte[i]);
    }

    // mostra no OLED
    ssd1306_fill(ssd, false);
    ssd1306_draw_string(ssd, "Card read:", 0, 24);
    ssd1306_draw_string(ssd, last_uid, 0, 40);
    ssd1306_send_data(ssd);

    // feedback: pisca LED
    gpio_put(LED_BLUE, 1);
    sleep_ms(150);
    gpio_put(LED_BLUE, 0);

    return true;
}

static void list_known_on_oled(ssd1306_t *ssd)
{
    if (known_count == 0)
    {
        ssd1306_fill(ssd, false);
        ssd1306_draw_string(ssd, "Known UIDs:", 0, 24);
        ssd1306_draw_string(ssd, "(none)", 0, 40);
        ssd1306_send_data(ssd);
        sleep_ms(1000);
        return;
    }

    int idx = 0;
    for (;;)
    {
        ssd1306_fill(ssd, false);
        ssd1306_draw_string(ssd, "Known UIDs:", 0, 0);
        ssd1306_draw_string(ssd, known_uids[idx], 0, 24);
        char buf[20];
        snprintf(buf, sizeof(buf), "%d/%d", idx + 1, known_count);
        ssd1306_draw_string(ssd, buf, 96, 0);
        ssd1306_send_data(ssd);

        // espera ação: next avança, select volta ao menu
        while (true)
        {
            if (btn_pressed(BTN_SELECT))
                return; // volta ao menu
            if (btn_pressed(BTN_NEXT))
                break;    // mostra próximo
            sleep_ms(50);
        }
        idx = (idx + 1) % known_count;
    }
}
