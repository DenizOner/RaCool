#include "Core/Localization/Localization.h"
#include "Communication/DebugConsole/Commands/NetworkCommands.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/Core/DashboardManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include <stdio.h>
#include <string.h>
#include <esp_system.h>
#include <esp_mac.h>

/**
 * @file NetworkCommands.c
 * @brief Zigbee network diagnostic commands for the Home Assistant endpoint.
 */

static int cmd_mac_info(int argc, char **argv, const LocalizationCatalog *catalog) {
    uint8_t mac[8];

    LOC_PRINTF(catalog, LOC_CLI_NETWORK_MAC_TITLE,  "\n--- MAC Adresleri ---\n" );

    if (esp_read_mac(mac, ESP_MAC_IEEE802154) == ESP_OK) {
        printf(" IEEE 802.15.4:  %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    }

    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        printf(" WiFi STA:       %02X:%02X:%02X:%02X:%02X:%02X\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
        printf(" Bluetooth:      %02X:%02X:%02X:%02X:%02X:%02X\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    printf("---------------------\n");
    return 0;
}

static int cmd_net_diag(int argc, char **argv, const LocalizationCatalog *catalog) {
    LOC_PRINTF(catalog, LOC_CLI_NETWORK_TITLE,  "\n--- Ag Teshis ---\n" );

    /* Zigbee service status. */
    IService *zbSvc = (IService *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (zbSvc) {
        ServiceState st = zbSvc->GetState(zbSvc);
        LOC_PRINTF(catalog, LOC_CLI_NETWORK_STATE, " Zigbee Servis Durumu: %s%s\n" ,
               st == SERVICE_STATE_STARTED ? CLI_COLOR_GREEN : CLI_COLOR_RED,
               Localization_Text(catalog, st == SERVICE_STATE_STARTED ? LOC_CLI_NETWORK_ACTIVE : LOC_CLI_NETWORK_INACTIVE));
    } else {
        LOC_PRINTF(catalog, LOC_CLI_NETWORK_MISSING, " Zigbee:     "  "Yuklenmedi\n" );
    }

    /* General system information. */
    LOC_PRINTF(catalog, LOC_CLI_NETWORK_HEAP, " Free Heap:  %u byte\n", (unsigned int)esp_get_free_heap_size());
    printf("-----------------\n");
    return 0;
}

static int cmd_net(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_NETWORK_USAGE, "Kullanim: NET <subcommand>\n");
        LOC_PRINTF(catalog, LOC_CLI_NETWORK_SUBCOMMANDS, "Alt komutlar: MAC, DIAG\n");
        return 1;
    }

    if (strcasecmp(argv[1], "MAC") == 0) return cmd_mac_info(argc - 1, &argv[1], catalog);
    if (strcasecmp(argv[1], "DIAG") == 0) return cmd_net_diag(argc - 1, &argv[1], catalog);

    LOC_PRINTF(catalog, LOC_CLI_NETWORK_UNKNOWN,  "Bilinmeyen alt komut: %s\n" , argv[1]);
    return 1;
}

void NetworkCommands_Register(IDebugConsole *console) {
    if (!console) return;

    static const esp_console_cmd_t net_cmds[] = {
        {
            .command = "NETWORK",
            .help    = "Ag yonetim komutlari",
            .func    = &cmd_net,
            .hint    = "<subcmd>"
        },
        {
            .command = "NET",
            .help    = "Ag yonetim komutlari (kisa)",
            .func    = &cmd_net,
            .hint    = "<subcmd>"
        }
    };

    for (int i = 0; i < (int)(sizeof(net_cmds) / sizeof(net_cmds[0])); i++) {
        console->RegisterCommandWithCategory(console, &net_cmds[i], CMD_CAT_NETWORK);
    }
}
