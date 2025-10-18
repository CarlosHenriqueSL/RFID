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

#define BUTTON_A 5
#define BUTTON_B 6

// Numero maximo de UIDs
#define MAX_CONHECIDOS 8
#define MAX_UID_STRLEN 24

// Vetor que guarda as strings dos UIDs conhecidos
static char uids_conhecidos[MAX_CONHECIDOS][MAX_UID_STRLEN];
// Quantos UIDs estao atualmente armazenados
static int total_conhecidos = 0;
// ultimo UID lido
static char ultimo_uid[MAX_UID_STRLEN] = "";

// Menu do sistema (texto exibido no OLED)
static const char *itens_menu[] = {"Ler cartao", "Adicionar UID", "Remover UID", "Listar UIDs", "Inverter"};
static const int tamanho_menu = sizeof(itens_menu) / sizeof(itens_menu[0]);

// Prototipos de funcoes 
static bool botao_pressionado(uint pin);
static void menu_oled(ssd1306_t *ssd, int sel, bool invert);
static bool escanear_cartao(MFRC522Ptr_t mfrc, ssd1306_t *ssd);
static bool add_uid(const char *uid);
static bool remove_uid(const char *uid);
static void listar_uids(ssd1306_t *ssd);

// Funcao principal
void main()
{
    stdio_init_all();

    // Inicializacao do leitor RFID 
    MFRC522Ptr_t mfrc = MFRC522_Init();
    PCD_Init(mfrc, spi0);
    PCD_AntennaOn(mfrc);
    sleep_ms(500); 

    // Inicializacao do display OLED via I2C 
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

    // Inicializa LEDs 
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_put(LED_BLUE, 0);
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_put(LED_GREEN, 0);

    // Inicializa botoes
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);

    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);

    bool invert = false; // inverte cores do OLED quando true
    int sel = 0; // item selecionado

    while(true)
    {
        // Desenha a tela principal do menu
        ssd1306_fill(&ssd, !invert);
        ssd1306_draw_string(&ssd, "MENU RFID", 20, 0);
        // mostra item selecionado com marcador
        char line[20];
        snprintf(line, sizeof(line), "> %s", itens_menu[sel]);
        ssd1306_draw_string(&ssd, line, 0, 24);

        // Mostra o ultimo UID lido, se houver
        if (ultimo_uid[0])
        {
            ssd1306_draw_string(&ssd, "ultimo:", 0, 40);
            ssd1306_draw_string(&ssd, ultimo_uid, 0, 52);
        }
        else
        {
            ssd1306_draw_string(&ssd, "Nenhum UID lido", 0, 52);
        }
        ssd1306_send_data(&ssd);

        // Navegacao: BUTTON_A avanca, BUTTON_B executa
        if (botao_pressionado(BUTTON_A))
        {
            sel = (sel + 1) % tamanho_menu;
            menu_oled(&ssd, sel, invert);
        }

        if (botao_pressionado(BUTTON_B))
        {
            // Executa acao dependendo do item selecionado
            switch (sel)
            {
            case 0: // Scan — espera e le um cartao
                escanear_cartao(mfrc, &ssd);
                break;
            case 1: // Add ultimo_uid — adiciona o ultimo UID lido a lista de conhecidos
                if (ultimo_uid[0])
                {
                    bool ok = add_uid(ultimo_uid);
                    ssd1306_fill(&ssd, false);
                    if (ok)
                        ssd1306_draw_string(&ssd, "UID adicionado", 8, 28);
                    else
                        ssd1306_draw_string(&ssd, "Falha ao adicionar", 4, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                else
                {
                    // Nao ha UID para adicionar
                    ssd1306_fill(&ssd, false);
                    ssd1306_draw_string(&ssd, "Nenhum UID para adicionar", 0, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                break;
            case 2: // Remove ultimo_uid — remove o ultimo UID lido da lista de conhecidos
                if (ultimo_uid[0])
                {
                    bool ok = remove_uid(ultimo_uid);
                    ssd1306_fill(&ssd, false);
                    if (ok)
                        ssd1306_draw_string(&ssd, "UID removido", 8, 28);
                    else
                        ssd1306_draw_string(&ssd, "Falha ao remover", 4, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                else
                {
                    ssd1306_fill(&ssd, false);
                    ssd1306_draw_string(&ssd, "Nenhum UID para remover", 0, 28);
                    ssd1306_send_data(&ssd);
                    sleep_ms(800);
                }
                break;
            case 3: // List — percorre e mostra UIDs conhecidos no OLED
                listar_uids(&ssd);
                break;
            case 4: // Toggle invert — inverte as cores do OLED
                invert = !invert;
                menu_oled(&ssd, sel, invert);
                break;
            }
        }

        sleep_ms(100);
    }
}

// botao_pressionado: detecta um pressionamento, com debounce.

static bool botao_pressionado(uint pin)
{
    if (!gpio_get(pin))
    {
        // debounce simples
        sleep_ms(20);
        if (!gpio_get(pin))
        {
            while (!gpio_get(pin))
                sleep_ms(10);
            return true;
        }
    }
    return false;
}

// find_known_uid: procura um UID ja armazenado e retorna o indice
static int find_known_uid(const char *uid)
{
    for (int i = 0; i < total_conhecidos; ++i)
    {
        if (strcmp(uids_conhecidos[i], uid) == 0)
            return i;
    }
    return -1;
}

// add_uid: adiciona um UID a lista de conhecidos
static bool add_uid(const char *uid)
{
    if (!uid || uid[0] == '\0')
        return false;
    if (find_known_uid(uid) >= 0)
        return false; // ja existe
    if (total_conhecidos >= MAX_CONHECIDOS)
        return false; // lista cheia
    // Copia a string (garante terminacao)
    strncpy(uids_conhecidos[total_conhecidos], uid, MAX_UID_STRLEN - 1);
    uids_conhecidos[total_conhecidos][MAX_UID_STRLEN - 1] = '\0';
    total_conhecidos++;
    return true;
}

// remove_uid: remove um UID da lista conhecida
static bool remove_uid(const char *uid)
{
    int idx = find_known_uid(uid);
    if (idx < 0)
        return false; // nao encontrado
    for (int i = idx; i < total_conhecidos - 1; ++i)
    {
        // copia a proxima entrada para a atual
        strncpy(uids_conhecidos[i], uids_conhecidos[i + 1], MAX_UID_STRLEN);
    }
    total_conhecidos--;
    // limpa a ultima posicao para evitar residuos
    uids_conhecidos[total_conhecidos][0] = '\0';
    return true;
}

// menu_oled: desenha a tela do menu no OLED
static void menu_oled(ssd1306_t *ssd, int sel, bool invert)
{
    ssd1306_fill(ssd, !invert);
    ssd1306_draw_string(ssd, "MENU RFID", 20, 0);
    // mostra item selecionado com marcador
    char line[20];
    snprintf(line, sizeof(line), "> %s", itens_menu[sel]);
    ssd1306_draw_string(ssd, line, 0, 24);

    if (ultimo_uid[0])
    {
        ssd1306_draw_string(ssd, "ultimo:", 0, 40);
        ssd1306_draw_string(ssd, ultimo_uid, 0, 52);
    }
    else
    {
        ssd1306_draw_string(ssd, "Nenhum UID lido", 0, 52);
    }
    ssd1306_send_data(ssd);
}

// escanear_cartao: espera por um cartao e le seu UID
static bool escanear_cartao(MFRC522Ptr_t mfrc, ssd1306_t *ssd)
{
    ssd1306_fill(ssd, false);
    ssd1306_draw_string(ssd, "Lendo...", 20, 24);
    ssd1306_send_data(ssd);

    // aguarda novo cartao ser apresentado
    while(true)
    {
        if (PICC_IsNewCardPresent(mfrc))
            break;
        sleep_ms(100);
    }

    // tenta ler o cartao; se falhar, informa no OLED
    if (!PICC_ReadCardSerial(mfrc))
    {
        ssd1306_fill(ssd, true);
        ssd1306_draw_string(ssd, "Falha na leitura", 4, 28);
        ssd1306_send_data(ssd);
        sleep_ms(800);
        return false;
    }

    // Formata o UID lido em ultimo_uid como hex com espacos: "AA BB CC DD "
    int offset = 0;
    memset(ultimo_uid, 0, sizeof(ultimo_uid));
    // Protege para nao escrever alem do buffer
    for (int i = 0; i < mfrc->uid.size && offset < (int)sizeof(ultimo_uid) - 4; ++i)
    {
        // sprintf retorna o numero de caracteres escritos
        offset += sprintf(&ultimo_uid[offset], "%02X ", mfrc->uid.uidByte[i]);
    }

    // mostra no OLED o UID lido
    ssd1306_fill(ssd, false);
    ssd1306_draw_string(ssd, "Cartao lido:", 0, 24);
    ssd1306_draw_string(ssd, ultimo_uid, 0, 40);
    ssd1306_send_data(ssd);

    // feedback: pisca LED para indicar leitura bem-sucedida
    gpio_put(LED_BLUE, 1);
    sleep_ms(150);
    gpio_put(LED_BLUE, 0);

    return true;
}

// listar_uids: exibe, um a um, os UIDs armazenados
static void listar_uids(ssd1306_t *ssd)
{
    if (total_conhecidos == 0)
    {
        ssd1306_fill(ssd, false);
        ssd1306_draw_string(ssd, "UIDs conhecidos:", 0, 24);
        ssd1306_draw_string(ssd, "(nenhum)", 0, 40);
        ssd1306_send_data(ssd);
        sleep_ms(1000);
        return;
    }

    int idx = 0;
    while(true)
    {
        ssd1306_fill(ssd, false);
        ssd1306_draw_string(ssd, "UIDs conhecidos:", 0, 0);
        ssd1306_draw_string(ssd, uids_conhecidos[idx], 0, 24);
        char buf[20];
        snprintf(buf, sizeof(buf), "%d/%d", idx + 1, total_conhecidos);
        ssd1306_draw_string(ssd, buf, 96, 0);
        ssd1306_send_data(ssd);

        // espera acao: BUTTON_A avanca, BUTTON_B volta ao menu
        while (true)
        {
            if (botao_pressionado(BUTTON_B))
                return; // volta ao menu
            if (botao_pressionado(BUTTON_A))
                break;    // mostra proximo
            sleep_ms(50);
        }
        idx = (idx + 1) % total_conhecidos;
    }
}
