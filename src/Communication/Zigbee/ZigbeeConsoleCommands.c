#include "Management/ConfigurationManager/ZigbeeReportingSettings.h"
#include <errno.h>
#include "Core/Localization/Localization.h"
#include "Communication/Zigbee/ZigbeeConsoleCommands.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/Core/DashboardManager.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "ezbee/nwk.h"
#include "ezbee/zdo/zdo_dev_srv_disc.h"
#include <stdlib.h>
#include "esp_zigbee.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// --- Dashboard Providers ---

static void provider_zigbee_status(char *buffer, size_t max_len) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    ServiceLocator *locator = GetServiceLocator();
    IZigbee *zigbee = locator && locator->GetService ? (IZigbee *)locator->GetService(SVC_NAME_ZIGBEE) : NULL;
    if (!zigbee) {
        snprintf(buffer, max_len, "%s", Localization_Text(catalog,LOC_CLI_ZIGBEE_NOT_AVAILABLE));
        return;
    }

    ZigbeeNetworkInfo info;
    if (zigbee->GetNetworkInfo && zigbee->GetNetworkInfo(zigbee, &info) == 0) {
        snprintf(buffer, max_len, "PAN:0x%04X Ch:%d EP:%d", info.pan_id, info.channel, info.endpoint_count);
    } else {
        snprintf(buffer, max_len, "%s", Localization_Text(catalog,LOC_CLI_ZIGBEE_DISCONNECTED));
    }
}

static void provider_zigbee_rssi(char *buffer, size_t max_len) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    ServiceLocator *locator = GetServiceLocator();
    IZigbee *zigbee = locator && locator->GetService ? (IZigbee *)locator->GetService(SVC_NAME_ZIGBEE) : NULL;
    ZigbeeNetworkInfo info;
    if (zigbee && zigbee->GetNetworkInfo && zigbee->GetNetworkInfo(zigbee, &info) == 0 && info.coordinator_link_valid) {
        snprintf(buffer, max_len, "%d dBm (LQI:%u)", info.rssi, (unsigned)info.lqi);
    } else {
        snprintf(buffer, max_len, "%s", Localization_Text(catalog,LOC_CLI_ZIGBEE_NOT_AVAILABLE));
    }
}

static void provider_zigbee_uptime(char *buffer, size_t max_len) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    ServiceLocator *locator = GetServiceLocator();
    IZigbee *zigbee = locator && locator->GetService ? (IZigbee *)locator->GetService(SVC_NAME_ZIGBEE) : NULL;
    ZigbeeNetworkInfo info;
    if (zigbee && zigbee->GetNetworkInfo && zigbee->GetNetworkInfo(zigbee, &info) == 0) {
        uint32_t h = info.uptime_sec / 3600;
        uint32_t m = (info.uptime_sec % 3600) / 60;
        uint32_t s = info.uptime_sec % 60;
        snprintf(buffer, max_len, "%luh%02lum%02lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    } else {
        snprintf(buffer, max_len, "%s", Localization_Text(catalog,LOC_CLI_ZIGBEE_NOT_AVAILABLE));
    }
}

// --- Console Commands ---

static int cmd_zinfo(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc!=1 || !argv) return 1;
    ServiceLocator *locator = GetServiceLocator();
    IZigbee *zigbee = locator && locator->GetService ? (IZigbee *)locator->GetService(SVC_NAME_ZIGBEE) : NULL;
    if (!zigbee) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_UNAVAILABLE,"Zigbee servisi bulunamadi.\n");
        return -1;
    }

    ZigbeeNetworkInfo info;
    if (zigbee->GetNetworkInfo && zigbee->GetNetworkInfo(zigbee, &info) == 0) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_NETWORK_TITLE,"\n--- Zigbee Ag Bilgileri ---\n");
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_PAN,"PAN ID       : 0x%04X\n", info.pan_id);
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_CHANNEL,"Kanal        : %d\n", info.channel);
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ADDRESS,"Adres        : 0x%04X\n", info.short_address);
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ROLE,"Rol          : %s\n", info.is_router ? Localization_Text(catalog,LOC_CLI_ZIGBEE_ROUTER) : Localization_Text(catalog,LOC_CLI_ZIGBEE_END_DEVICE));
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_EXTENDED_PAN,"ExtPAN       : %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
               info.ext_pan_id[7], info.ext_pan_id[6], info.ext_pan_id[5], info.ext_pan_id[4],
               info.ext_pan_id[3], info.ext_pan_id[2], info.ext_pan_id[1], info.ext_pan_id[0]);
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_FIRMWARE,"FW Version   : 0x%08lx\n", (unsigned long)info.firmware_version);
        printf("RaCool firmware: %s\n", RACOOL_PRODUCT_VERSION);
        if (info.coordinator_link_valid) {
            LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_RSSI,"RSSI         : %d dBm\n", info.rssi);
            LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_LQI,"LQI          : %u\n", (unsigned)info.lqi);
        } else {
            LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_NO_COORDINATOR,"RSSI / LQI   : N/A (dogrudan koordinator komsusu yok)\n");
        }
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ENDPOINTS,"Endpoint'ler : %d\n", info.endpoint_count);
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_UPTIME,"Uptime       : %lu saniye\n", (unsigned long)info.uptime_sec);
        printf("----------------------------\n\n");
    } else {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_NETWORK_UNAVAILABLE,"Zigbee ag bilgisi alinamiyor (Bagli olmayabilir).\n");
        return 1;
    }
    return 0;
}

static int cmd_zreset(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc != 2 || !argv || !argv[1] || strcmp(argv[1], "CONFIRM") != 0) {
        printf("Zigbee network reset requires: ZB RESET CONFIRM\n");
        return 1;
    }
    IZigbee *zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!zigbee || !zigbee->ResetToFactorySettings) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_UNAVAILABLE,"Zigbee servisi bulunamadi.\n");
        return -1;
    }

    int32_t result = zigbee->ResetToFactorySettings(zigbee);
    if (result != ZIGBEE_ERROR_OK) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_RESET_FAILED,"Zigbee reset request failed: %ld\n", (long)result);
        return 1;
    }
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_RESET_QUEUED,"Zigbee local reset requested; completion is reported by the LEAVE signal.\n");
    return 0;
}

static int cmd_zsteer(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc!=1 || !argv) return 1;
    ServiceLocator *locator = GetServiceLocator();
    IZigbee *zigbee = locator && locator->GetService ? (IZigbee *)locator->GetService(SVC_NAME_ZIGBEE) : NULL;
    if (!zigbee) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_UNAVAILABLE,"Zigbee servisi bulunamadi.\n");
        return -1;
    }

    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_STEERING,"Manuel network steering baslatiliyor...\n");
    return zigbee->ConnectToNetwork && zigbee->ConnectToNetwork(zigbee)==0 ? 0 : 1;
}

static int cmd_zdiag(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc!=1 || !argv) return 1;
    ServiceLocator *locator = GetServiceLocator();
    IZigbee *zigbee = locator && locator->GetService ? (IZigbee *)locator->GetService(SVC_NAME_ZIGBEE) : NULL;
    if (!zigbee) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_UNAVAILABLE,"Zigbee servisi bulunamadi.\n");
        return -1;
    }

    IService *svc = &zigbee->base;
    if (!svc->GetState || !svc->CheckHealth || !zigbee->IsConnected || !zigbee->GetEndpointCount) return 1;
    int connected=zigbee->IsConnected(zigbee);
    int endpoint_count=zigbee->GetEndpointCount(zigbee);
    if (connected<0 || endpoint_count<0) return 1;
    printf("RACOOL_CLI v=1 type=zigbee_status state=%d connected=%u endpoints=%d\n",(int)svc->GetState(svc),connected ? 1U : 0U,endpoint_count);
    ServiceState state = svc->GetState(svc);
    const char *stateStr = "?";
    switch (state) {
        case SERVICE_STATE_UNINITIALIZED: stateStr = Localization_Text(catalog,LOC_CLI_ZIGBEE_UNINITIALIZED); break;
        case SERVICE_STATE_INITIALIZED:   stateStr = Localization_Text(catalog,LOC_CLI_ZIGBEE_INITIALIZED); break;
        case SERVICE_STATE_STARTED:       stateStr = Localization_Text(catalog,LOC_CLI_ZIGBEE_STARTED); break;
        case SERVICE_STATE_STOPPED:       stateStr = Localization_Text(catalog,LOC_CLI_ZIGBEE_STOPPED); break;
        case SERVICE_STATE_ERROR:         stateStr = Localization_Text(catalog,LOC_CLI_ZIGBEE_ERROR); break;
    }

    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_DIAGNOSTIC_TITLE,"\n--- Zigbee Diagnostik ---\n");
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_STATE,"Servis Durumu : %s\n", stateStr);
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_CONNECTED,"Baglanti      : %s\n", connected ? Localization_Text(catalog,LOC_CLI_ZIGBEE_YES) : Localization_Text(catalog,LOC_CLI_ZIGBEE_NO));
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ENDPOINT_COUNT,"Endpoint Sayisi: %d\n", endpoint_count);
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_HEALTH,"Saglik Kontrolu: %s\n", svc->CheckHealth(svc) == 0 ? Localization_Text(catalog,LOC_CLI_ZIGBEE_HEALTHY) : Localization_Text(catalog,LOC_CLI_ZIGBEE_FAULT));
    printf("--------------------------\n\n");
    return 0;
}

/* Reservation and completion run under SDK ownership. Successful SDK deinit
 * retires any canceled callbacks before the owner resets this bounded pool. */
typedef struct {
    bool occupied;
    ezb_nwk_neighbor_info_t neighbor;
} RouterRequest;
static RouterRequest routerRequests[ZIGBEE_ROUTER_REQUEST_CAPACITY];

void Zigbee_ConsoleSdkStopped(void) { memset(routerRequests, 0, sizeof(routerRequests)); }

static RouterRequest *ReserveRouterRequest(const ezb_nwk_neighbor_info_t *neighbor) {
    RouterRequest *available = NULL;
    for (unsigned i = 0; i < ZIGBEE_ROUTER_REQUEST_CAPACITY; ++i) {
        if (routerRequests[i].occupied && routerRequests[i].neighbor.short_addr == neighbor->short_addr) return NULL;
        if (!routerRequests[i].occupied && !available) available = &routerRequests[i];
    }
    if (available) { available->neighbor = *neighbor; available->occupied = true; }
    return available;
}

/* Neighbor EUI64 may be unknown until ZDO address discovery completes. */
static void RouterAddressResolved(const ezb_zdo_ieee_addr_req_result_t *result, void *context) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    RouterRequest *request = context;
    if (!request || !request->occupied) return;
    ezb_nwk_neighbor_info_t *neighbor = &request->neighbor;
    if (result && result->error == EZB_ERR_NONE && result->rsp &&
        result->rsp->status == 0 && result->rsp->nwk_addr_remote_dev == neighbor->short_addr &&
        result->rsp->ieee_addr_remote_dev.u64 != UINT64_MAX && result->rsp->ieee_addr_remote_dev.u64 != 0) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ROUTER_ADDRESS,"Router 0x%04X IEEE %016llX LQI %u RSSI %d\n", neighbor->short_addr,
            (unsigned long long)result->rsp->ieee_addr_remote_dev.u64,
            (unsigned)neighbor->lqi, (int)neighbor->rssi);
    } else {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ROUTER_DISCOVERY_FAILED,"Router 0x%04X: IEEE address discovery failed\n", neighbor->short_addr);
    }
    request->occupied = false;
}

typedef struct {
    ezb_nwk_neighbor_info_t neighbors[ZIGBEE_ROUTER_NEIGHBOR_CAPACITY];
    ezb_nwk_route_info_t routes[ZIGBEE_ROUTER_ROUTE_CAPACITY];
    ezb_nwk_route_record_info_t records[ZIGBEE_ROUTER_RECORD_CAPACITY];
    unsigned neighbor_count, route_count, record_count;
    ezb_err_t neighbor_result, route_result, record_result;
    bool neighbor_truncated, route_truncated, record_truncated;
} RouterInventory;

/* The caller owns the SDK lease; no printing or allocation inside iteration. */
static void ReadRouterInventory(RouterInventory *inventory) {
    ezb_nwk_info_iterator_t iterator = EZB_NWK_INFO_ITERATOR_INIT;
    ezb_nwk_neighbor_info_t neighbor = {0};
    while ((inventory->neighbor_result = ezb_nwk_get_next_neighbor(&iterator, &neighbor)) == EZB_ERR_NONE) {
        if (inventory->neighbor_count == ZIGBEE_ROUTER_NEIGHBOR_CAPACITY) {
            inventory->neighbor_truncated = true; break;
        }
        inventory->neighbors[inventory->neighbor_count++] = neighbor;
    }
    iterator = EZB_NWK_INFO_ITERATOR_INIT;
    ezb_nwk_route_info_t route = {0};
    while ((inventory->route_result = ezb_nwk_get_next_route(&iterator, &route)) == EZB_ERR_NONE) {
        if (inventory->route_count == ZIGBEE_ROUTER_ROUTE_CAPACITY) {
            inventory->route_truncated = true; break;
        }
        inventory->routes[inventory->route_count++] = route;
    }
    iterator = EZB_NWK_INFO_ITERATOR_INIT;
    ezb_nwk_route_record_info_t record = {0};
    while ((inventory->record_result = ezb_nwk_get_next_route_record(&iterator, &record)) == EZB_ERR_NONE) {
        if (inventory->record_count == ZIGBEE_ROUTER_RECORD_CAPACITY) {
            inventory->record_truncated = true; break;
        }
        inventory->records[inventory->record_count++] = record;
    }
}

static int cmd_zrouters(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    (void)argc;
    (void)argv;
    IZigbee *zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!zigbee) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_UNAVAILABLE,"Zigbee servisi bulunamadi.\n");
        return -1;
    }

    ZigbeeNetworkInfo info;
    if (zigbee->GetNetworkInfo(zigbee, &info) != ZIGBEE_ERROR_OK) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_DISCONNECTED,"Zigbee agina bagli degil.\n");
        return 1;
    }

    RouterInventory *inventory = calloc(1, sizeof(*inventory));
    if (!inventory) { LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ALLOCATION_FAILED,"Router inventory allocation failed.\n"); return 1; }
    if (!zigbee->TrySdkAccess || !zigbee->ReleaseSdkAccess ||
        !zigbee->TrySdkAccess(zigbee, ZIGBEE_LOCK_TIMEOUT_MS)) {
        free(inventory); LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_LOCK_FAILED,"Zigbee SDK lock unavailable.\n"); return 1;
    }
    ReadRouterInventory(inventory);
    if (zigbee->ReleaseSdkAccess(zigbee) != ZIGBEE_ERROR_OK) {
        free(inventory); return 1;
    }
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_NEIGHBORS_TITLE,"Komşu tablosu (router, coordinator ve end-device):\n");
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_NEIGHBOR_COLUMNS,"%-18s %-8s %-8s %-6s %-6s %-6s %-6s\n",
           "IEEE adresi", "NWK", "Rol", "Iliski", "LQI", "RSSI", "Maliyet");

    unsigned count = 0;
    unsigned children = 0;
    for (unsigned i = 0; i < inventory->neighbor_count; ++i) {
        ezb_nwk_neighbor_info_t neighbor = inventory->neighbors[i];
        if (neighbor.relationship == EZB_NWK_RELATIONSHIP_CHILD) ++children;
        printf("NEIGHBOR nwk=0x%04X type=%u relation=%u depth=%u rx_idle=%u age=%u timeout_s=%lu timeout_ms=%lu\n",
            neighbor.short_addr, (unsigned)neighbor.device_type, (unsigned)neighbor.relationship,
            (unsigned)neighbor.depth, (unsigned)neighbor.rx_on_when_idle, (unsigned)neighbor.age,
            (unsigned long)neighbor.device_timeout, (unsigned long)neighbor.timeout_counter);
        if (neighbor.ieee_addr.u64 == UINT64_MAX || neighbor.ieee_addr.u64 == 0) {
            ++count;
            if (!zigbee->TrySdkAccess(zigbee, 0)) {
                LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_IEEE_BUSY,"Neighbor 0x%04X: IEEE unavailable (SDK busy)\n", neighbor.short_addr);
                continue;
            }
            RouterRequest *pending = ReserveRouterRequest(&neighbor);
            if (!pending) {
                if (zigbee->ReleaseSdkAccess(zigbee) != ZIGBEE_ERROR_OK) { free(inventory); return 1; }
                LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_IEEE_PENDING,"Neighbor 0x%04X: IEEE unavailable (discovery pending or capacity reached)\n", neighbor.short_addr);
                continue;
            }
            ezb_zdo_ieee_addr_req_t request = {
                .dst_nwk_addr = neighbor.short_addr,
                .field = {.nwk_addr_of_interest = neighbor.short_addr,
                          .request_type = ZDO_ADDR_REQUEST_TYPE_SINGLE_DEVICE},
                .cb = RouterAddressResolved, .user_ctx = pending,
            };
            ezb_err_t error = ezb_zdo_ieee_addr_req(&request);
            if (error != EZB_ERR_NONE) pending->occupied = false;
            int32_t release = zigbee->ReleaseSdkAccess(zigbee);
            if (error != EZB_ERR_NONE) {
                LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_IEEE_REQUEST_FAILED,"Router 0x%04X: IEEE request failed (%d)\n", neighbor.short_addr, (int)error);
            } else {
                LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_IEEE_DISCOVERY_PENDING,"Router 0x%04X: IEEE discovery pending\n", neighbor.short_addr);
            }
            if (release != ZIGBEE_ERROR_OK) { free(inventory); return 1; }
            continue;
        }
        const char *role = neighbor.device_type == EZB_NWK_DEVICE_TYPE_COORDINATOR
                               ? Localization_Text(catalog,LOC_CLI_ZIGBEE_COORDINATOR)
                               : neighbor.device_type == EZB_NWK_DEVICE_TYPE_ROUTER
                                   ? Localization_Text(catalog,LOC_CLI_ZIGBEE_ROUTER)
                                   : neighbor.device_type == EZB_NWK_DEVICE_TYPE_END_DEVICE
                                       ? Localization_Text(catalog,LOC_CLI_ZIGBEE_END_DEVICE) : Localization_Text(catalog,LOC_CLI_ZIGBEE_UNKNOWN_LABEL);
        const char *relationship = neighbor.relationship == EZB_NWK_RELATIONSHIP_PARENT
                                        ? Localization_Text(catalog,LOC_CLI_ZIGBEE_PARENT)
                                        : neighbor.relationship == EZB_NWK_RELATIONSHIP_CHILD
                                            ? Localization_Text(catalog,LOC_CLI_ZIGBEE_CHILD)
                                            : neighbor.relationship == EZB_NWK_RELATIONSHIP_SIBLING
                                                ? Localization_Text(catalog,LOC_CLI_ZIGBEE_SIBLING) : Localization_Text(catalog,LOC_CLI_ZIGBEE_OTHER);
         LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_NEIGHBOR_ROW,"%016llX      0x%04X   %-8s %-6s %-6u %-6d %-6u\n",
               (unsigned long long)neighbor.ieee_addr.u64,
               neighbor.short_addr, role, relationship, (unsigned)neighbor.lqi,
               (int)neighbor.rssi, (unsigned)neighbor.outgoing_cost);
        ++count;
    }
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_NEIGHBOR_COUNT,"Total neighbors: %u; direct children: %u\n", count, children);
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ROUTES_TITLE,"Yönlendirme tablosu: %u kayıt\n", inventory->route_count);
    for (unsigned i = 0; i < inventory->route_count; ++i) {
        const ezb_nwk_route_info_t *route = &inventory->routes[i];
        printf("ROUTE dst=0x%04X next=0x%04X status=%u expiry=%u many_to_one=%u record_required=%u\n",
            route->dest_addr, route->next_hop_addr, (unsigned)route->flags.status,
            (unsigned)route->expiry, (unsigned)route->flags.many_to_one,
            (unsigned)route->flags.route_record_required);
    }
    bool invalid_record = false;
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_SOURCE_ROUTES_TITLE,"Source-route tablosu: %u kayıt\n", inventory->record_count);
    for (unsigned i = 0; i < inventory->record_count; ++i) {
        const ezb_nwk_route_record_info_t *record = &inventory->records[i];
        printf("SOURCE_ROUTE dst=0x%04X expiry=%u relays=%u path=", record->dest_address,
            (unsigned)record->expiry, (unsigned)record->relay_count);
        if (record->relay_count > EZB_NWK_MAX_SOURCE_ROUTE) {
            invalid_record = true; LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_INVALID,"INVALID\n"); continue;
        }
        for (unsigned n = 0; n < record->relay_count; ++n) LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ROUTE_SEPARATOR,"%s0x%04X", n ? ">" : "", record->path[n]);
        printf("\n");
    }
    bool complete = !inventory->neighbor_truncated && !inventory->route_truncated &&
        !inventory->record_truncated && !invalid_record &&
        inventory->neighbor_result == EZB_ERR_NOT_FOUND && inventory->route_result == EZB_ERR_NOT_FOUND &&
        inventory->record_result == EZB_ERR_NOT_FOUND;
    printf("INVENTORY complete=%u neighbor_status=%d route_status=%d record_status=%d truncated=%u/%u/%u\n",
        (unsigned)complete, (int)inventory->neighbor_result, (int)inventory->route_result,
        (int)inventory->record_result, (unsigned)inventory->neighbor_truncated,
        (unsigned)inventory->route_truncated, (unsigned)inventory->record_truncated);
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_INVENTORY_LIMIT,"Bu tablolar gerçek transit paket geçmişi veya cihaz başına trafik sayacı değildir.\n");
    free(inventory);
    return complete ? 0 : 1;
}

static int cmd_zreport(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    static const char *const names[] = {"fan", "ambient", "cpu", "health", "memory", "uptime"};
    if (argc != 1 && argc != 2 && argc != 3) return 1;
    int selected = -1;
    if (argc >= 2) {
        for (int i = 0; i < ZIGBEE_REPORT_COUNT; ++i)
            if (strcasecmp(argv[1], names[i]) == 0) selected = i;
        if (selected < 0) { LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_REPORT_UNKNOWN,"Unknown report. Use fan, ambient, cpu, health, memory or uptime.\n"); return 1; }
    }
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    if (argc == 3) {
        if (!argv[2] || argv[2][0] < '0' || argv[2][0] > '9') return 1;
        errno = 0; char *end;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (errno || *end || value > ZB_REPORT_MAX_S) {
            printf("Interval must be an integer from 0 to %u seconds; 0 disables reports.\n", ZB_REPORT_MAX_S);
            return 1;
        }
        esp_err_t result = ZigbeeReporting_SetInterval(config, (ZigbeeReportKind)selected, (uint32_t)value);
        if (result != ESP_OK) { LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_REPORT_SAVE_FAILED,"Reporting setting was not saved: %d\n", result); return 1; }
        IZigbee *zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
        if (zigbee && zigbee->ReconfigureAllReporting && zigbee->ReconfigureAllReporting(zigbee) != 0) {
            printf("Reporting value saved, but refresh scheduling failed; retry the command.\n");
            return 1;
        }
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_REPORT_SAVED,"%s report saved: %lu s%s\n", names[selected], value, value == 0 ? Localization_Text(catalog,LOC_CLI_ZIGBEE_OFF) : "");
        printf("RACOOL_CLI v=1 type=report_saved report=%s seconds=%lu\n",names[selected],value);
        return 0;
    }
    for (int i = 0; i < ZIGBEE_REPORT_COUNT; ++i) {
        if (selected >= 0 && selected != i) continue;
        uint16_t value;
        if (ZigbeeReporting_GetInterval(config, (ZigbeeReportKind)i, &value) != ESP_OK) return 1;
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_REPORT_VALUE,"%s: %u s%s\n", names[i], value, value == 0 ? Localization_Text(catalog,LOC_CLI_ZIGBEE_OFF) : "");
        printf("RACOOL_CLI v=1 type=report_interval report=%s seconds=%u enabled=%u\n",names[i],(unsigned)value,value ? 1U : 0U);
    }
    return 0;
}

static int cmd_zsync(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    (void)argv;
    if (argc != 1) return 1;
    IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    ZigbeeAttributeStatus status;
    if (!radio || !radio->GetAttributeStatus || radio->GetAttributeStatus(radio, &status) != ZIGBEE_ERROR_OK) return 1;
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_ATTRIBUTE_SYNC,"Zigbee attribute sync: pending=%u applied=%lu failed=%lu lock_deferrals=%lu retries=%lu dropped=%lu\n",
        (unsigned)status.pending, (unsigned long)status.applied, (unsigned long)status.failed,
        (unsigned long)status.lock_deferrals, (unsigned long)status.retries, (unsigned long)status.dropped);
    return 0;
}

static int cmd_zrestart(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc != 1 || !argv) return 1;
    IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!radio || !radio->base.Stop || !radio->base.Start || !radio->base.GetState ||
        radio->base.GetState(&radio->base) != SERVICE_STATE_STARTED) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_RESTART_REJECTED,"Zigbee restart rejected: service must be started.\n");
        return 1;
    }
    int32_t result = radio->base.Stop(&radio->base);
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_LIFECYCLE_STOP,"Zigbee lifecycle stop: result=%ld state=%d\n", (long)result,
        (int)radio->base.GetState(&radio->base));
    printf("RACOOL_CLI v=1 type=zigbee_lifecycle operation=stop result=%ld state=%d\n",(long)result,(int)radio->base.GetState(&radio->base));
    if (result != 0 || radio->base.GetState(&radio->base) != SERVICE_STATE_STOPPED) return 1;
    result = radio->base.Start(&radio->base);
    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_LIFECYCLE_START,"Zigbee lifecycle start: result=%ld state=%d\n", (long)result,
        (int)radio->base.GetState(&radio->base));
    printf("RACOOL_CLI v=1 type=zigbee_lifecycle operation=start result=%ld state=%d\n",(long)result,(int)radio->base.GetState(&radio->base));
    /* Startup admission is distinct from subsequent network rejoin. */
    return result == 0 && radio->base.GetState(&radio->base) == SERVICE_STATE_STARTED ? 0 : 1;
}

static int cmd_ztx(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    static const char *const names[] = {"Eco", "Normal", "Boost"};
    if (argc < 1 || argc > 2 || !argv) return 1;
    IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!radio || !radio->GetTxStatus) return 1;
    if (argc == 2) {
        int mode = -1;
        for (unsigned i = 0; i < ZIGBEE_TX_PROFILE_COUNT; ++i)
            if (argv[1] && strcasecmp(argv[1], names[i]) == 0) mode = (int)i;
        if (mode < 0) { LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_TX_USAGE,"Usage: ZB TX [Eco|Normal|Boost]\n"); return 1; }
        int32_t result = radio->SetTxProfile ? radio->SetTxProfile(radio, (ZigbeeTxProfile)mode) : ESP_ERR_INVALID_STATE;
        if (result != ESP_OK) { LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_TX_FAILED,"TX profile was not applied: %ld\n", (long)result); return 1; }
    }
    ZigbeeTxStatus status;
    if (radio->GetTxStatus(radio, &status) != ESP_OK || (unsigned)status.profile >= ZIGBEE_TX_PROFILE_COUNT) return 1;
    printf("Zigbee TX profile: %s; SDK power: %d dBm\n", names[status.profile], status.dbm);
    printf("RACOOL_CLI v=1 type=zigbee_tx profile=%u dbm=%d\n",(unsigned)status.profile,status.dbm);
    return 0;
}

static int dispatch_zigbee(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!argv || argc<1 || argc>4) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !argv[i][0]) return 1;
    if (argc < 2) {
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_USAGE,"Kullanim: ZB <subcommand>\n");
        LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_COMMANDS,"Alt komutlar: INFO, RESET, RESTART, STEER, DIAG, ROUTERS, SYNC, TX [Eco|Normal|Boost], REPORT [fan|ambient|cpu|health|memory|uptime] [0-3600]\n");
        return 1;
    }

    if (strcasecmp(argv[1], "REPORT") == 0) return cmd_zreport(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "TX") == 0) return cmd_ztx(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "SYNC") == 0) return cmd_zsync(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "RESTART") == 0) return cmd_zrestart(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "INFO") == 0) return cmd_zinfo(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "RESET") == 0) return cmd_zreset(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "STEER") == 0) return cmd_zsteer(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "DIAG") == 0) return cmd_zdiag(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "ROUTERS") == 0) return cmd_zrouters(argc - 1, &argv[1]);

    LOC_PRINTF(catalog,LOC_CLI_ZIGBEE_UNKNOWN,"Bilinmeyen alt komut: %s\n", argv[1]);
    return 1;
}

static int cmd_zigbee(int argc,char **argv) {
    if (!argv || argc<1 || argc>5) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !argv[i][0]) return 1;
    bool machine=strcmp(argv[argc-1],"--machine")==0;
    if (machine) --argc;
    int result=dispatch_zigbee(argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=zigbee_command result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}

// --- Self-Registration ---

esp_err_t Zigbee_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommand) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t z_cmds[] = {
        {.command = "ZIGBEE", .help = "Zigbee yonetim komutlari",          .func = &cmd_zigbee, .hint = "<subcmd>"},
        {.command = "ZB",     .help = "Zigbee yonetim komutlari (kisa)",   .func = &cmd_zigbee, .hint = "<subcmd>"}
    };

    for (int i = 0; i < sizeof(z_cmds) / sizeof(z_cmds[0]); i++) {
        esp_err_t result = console->RegisterCommand(console, &z_cmds[i]);
        if (result != ESP_OK) return result;
    }

    // Dashboard
    IDashboard *dash = DashboardManager_GetModule();
    if (dash) {
        if (!dash->RegisterField ||
            !dash->RegisterField(dash, "ZIGBEE", provider_zigbee_status) ||
            !dash->RegisterField(dash, "ZB RSSI", provider_zigbee_rssi) ||
            !dash->RegisterField(dash, "ZB UPTIME", provider_zigbee_uptime)) return ESP_FAIL;
    }
    return ESP_OK;
}
