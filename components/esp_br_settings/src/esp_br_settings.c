/*
 * SPDX-FileCopyrightText: 2026 Sonoff Dongle-M OTBR contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Persisted border router settings, radio/link status and backup/restore over the
 * border router web server.
 */

#include "esp_br_settings.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_br_web.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_types.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#include "openthread/backbone_router_ftd.h"
#include "openthread/border_routing.h"
#include "openthread/dataset.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/link.h"
#include "openthread/logging.h"
#include "openthread/nat64.h"
#include "openthread/platform/radio.h"
#include "openthread/srp_server.h"
#include "openthread/thread.h"
#include "openthread/thread_ftd.h"
#if CONFIG_OPENTHREAD_RADIO_TREL
#include "openthread/trel.h"
#endif

#define TAG "br_settings"
#define NVS_NAMESPACE "br_settings"
#define MAX_BODY_LEN 4096
#define BACKUP_FORMAT "esp-br-backup"
#define BACKUP_VERSION 1
#define JSON_CONTENT_TYPE "application/json"

/*-----------------------------------------------------------------------------------------------
 Setting definitions
-----------------------------------------------------------------------------------------------*/
typedef enum {
    SETTING_BOOL,
    SETTING_INT,
    SETTING_ENUM,
} setting_type_t;

typedef struct {
    const char *key;
    const char *nvs_key; /* NVS keys are limited to 15 characters */
    const char *group;
    const char *label;
    const char *unit;
    const char *description;
    setting_type_t type;
    int32_t min;
    int32_t max;
    const char *const *options; /* SETTING_ENUM: names of the values min..max */
    bool requires_restart;
    otError (*set)(otInstance *instance, int32_t value);
    otError (*get)(otInstance *instance, int32_t *value);
    void (*clear)(otInstance *instance); /* optional: return to the stack's automatic behaviour */
    /* The ESP border router library changes this setting on role changes, so re-assert overrides afterwards. */
    bool reassert_on_role_change;
    /* The effective stack default when it is set after boot (the recorded value would be misleading). */
    bool has_stack_default;
    int32_t stack_default;
} setting_def_t;

static otError set_tx_power(otInstance *ins, int32_t v)
{
    return otPlatRadioSetTransmitPower(ins, (int8_t)v);
}

static otError get_tx_power(otInstance *ins, int32_t *v)
{
    int8_t power;
    otError error = otPlatRadioGetTransmitPower(ins, &power);
    *v = power;
    return error;
}

static otError set_cca_threshold(otInstance *ins, int32_t v)
{
    return otPlatRadioSetCcaEnergyDetectThreshold(ins, (int8_t)v);
}

static otError get_cca_threshold(otInstance *ins, int32_t *v)
{
    int8_t threshold;
    otError error = otPlatRadioGetCcaEnergyDetectThreshold(ins, &threshold);
    *v = threshold;
    return error;
}

static otError set_retries_direct(otInstance *ins, int32_t v)
{
    otLinkSetMaxFrameRetriesDirect(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_retries_direct(otInstance *ins, int32_t *v)
{
    *v = otLinkGetMaxFrameRetriesDirect(ins);
    return OT_ERROR_NONE;
}

static otError set_retries_indirect(otInstance *ins, int32_t v)
{
    otLinkSetMaxFrameRetriesIndirect(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_retries_indirect(otInstance *ins, int32_t *v)
{
    *v = otLinkGetMaxFrameRetriesIndirect(ins);
    return OT_ERROR_NONE;
}

#if CONFIG_OPENTHREAD_RADIO_TREL
static otError set_trel(otInstance *ins, int32_t v)
{
    otTrelSetEnabled(ins, v != 0);
    return OT_ERROR_NONE;
}

static otError get_trel(otInstance *ins, int32_t *v)
{
    *v = otTrelIsEnabled(ins);
    return OT_ERROR_NONE;
}
#endif

static otError set_router_eligible(otInstance *ins, int32_t v)
{
    return otThreadSetRouterEligible(ins, v != 0);
}

static otError get_router_eligible(otInstance *ins, int32_t *v)
{
    *v = otThreadIsRouterEligible(ins);
    return OT_ERROR_NONE;
}

static otError set_leader_weight(otInstance *ins, int32_t v)
{
    otThreadSetLocalLeaderWeight(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_leader_weight(otInstance *ins, int32_t *v)
{
    *v = otThreadGetLocalLeaderWeight(ins);
    return OT_ERROR_NONE;
}

static otError set_router_jitter(otInstance *ins, int32_t v)
{
    otThreadSetRouterSelectionJitter(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_router_jitter(otInstance *ins, int32_t *v)
{
    *v = otThreadGetRouterSelectionJitter(ins);
    return OT_ERROR_NONE;
}

static otError set_upgrade_threshold(otInstance *ins, int32_t v)
{
    otThreadSetRouterUpgradeThreshold(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_upgrade_threshold(otInstance *ins, int32_t *v)
{
    *v = otThreadGetRouterUpgradeThreshold(ins);
    return OT_ERROR_NONE;
}

static otError set_downgrade_threshold(otInstance *ins, int32_t v)
{
    otThreadSetRouterDowngradeThreshold(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_downgrade_threshold(otInstance *ins, int32_t *v)
{
    *v = otThreadGetRouterDowngradeThreshold(ins);
    return OT_ERROR_NONE;
}

static otError set_network_id_timeout(otInstance *ins, int32_t v)
{
    otThreadSetNetworkIdTimeout(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_network_id_timeout(otInstance *ins, int32_t *v)
{
    *v = otThreadGetNetworkIdTimeout(ins);
    return OT_ERROR_NONE;
}

static otError set_max_children(otInstance *ins, int32_t v)
{
    return otThreadSetMaxAllowedChildren(ins, (uint16_t)v);
}

static otError get_max_children(otInstance *ins, int32_t *v)
{
    *v = otThreadGetMaxAllowedChildren(ins);
    return OT_ERROR_NONE;
}

static otError set_br_enabled(otInstance *ins, int32_t v)
{
    return otBorderRoutingSetEnabled(ins, v != 0);
}

static otError get_br_enabled(otInstance *ins, int32_t *v)
{
    otBorderRoutingState state = otBorderRoutingGetState(ins);
    *v = (state == OT_BORDER_ROUTING_STATE_STOPPED || state == OT_BORDER_ROUTING_STATE_RUNNING);
    return OT_ERROR_NONE;
}

static otError set_rio_preference(otInstance *ins, int32_t v)
{
    otBorderRoutingSetRouteInfoOptionPreference(ins, (otRoutePreference)v);
    return OT_ERROR_NONE;
}

static otError get_rio_preference(otInstance *ins, int32_t *v)
{
    *v = otBorderRoutingGetRouteInfoOptionPreference(ins);
    return OT_ERROR_NONE;
}

static void clear_rio_preference(otInstance *ins)
{
    otBorderRoutingClearRouteInfoOptionPreference(ins);
}

static otError set_route_preference(otInstance *ins, int32_t v)
{
    otBorderRoutingSetRoutePreference(ins, (otRoutePreference)v);
    return OT_ERROR_NONE;
}

static otError get_route_preference(otInstance *ins, int32_t *v)
{
    *v = otBorderRoutingGetRoutePreference(ins);
    return OT_ERROR_NONE;
}

static void clear_route_preference(otInstance *ins)
{
    otBorderRoutingClearRoutePreference(ins);
}

static otError set_dhcp6_pd(otInstance *ins, int32_t v)
{
    otBorderRoutingDhcp6PdSetEnabled(ins, v != 0);
    return OT_ERROR_NONE;
}

static otError get_dhcp6_pd(otInstance *ins, int32_t *v)
{
    *v = otBorderRoutingDhcp6PdGetState(ins) != OT_BORDER_ROUTING_DHCP6_PD_STATE_DISABLED;
    return OT_ERROR_NONE;
}

static otError set_nat64(otInstance *ins, int32_t v)
{
    otNat64SetEnabled(ins, v != 0);
    return OT_ERROR_NONE;
}

static otError get_nat64(otInstance *ins, int32_t *v)
{
    *v = otNat64GetPrefixManagerState(ins) != OT_NAT64_STATE_DISABLED;
    return OT_ERROR_NONE;
}

static otError set_bbr(otInstance *ins, int32_t v)
{
    otBackboneRouterSetEnabled(ins, v != 0);
    return OT_ERROR_NONE;
}

static otError get_bbr(otInstance *ins, int32_t *v)
{
    *v = otBackboneRouterGetState(ins) != OT_BACKBONE_ROUTER_STATE_DISABLED;
    return OT_ERROR_NONE;
}

static otError set_bbr_jitter(otInstance *ins, int32_t v)
{
    otBackboneRouterSetRegistrationJitter(ins, (uint8_t)v);
    return OT_ERROR_NONE;
}

static otError get_bbr_jitter(otInstance *ins, int32_t *v)
{
    *v = otBackboneRouterGetRegistrationJitter(ins);
    return OT_ERROR_NONE;
}

static otError set_ot_log_level(otInstance *ins, int32_t v)
{
    (void)ins;
    return otLoggingSetLevel((otLogLevel)v);
}

static otError get_ot_log_level(otInstance *ins, int32_t *v)
{
    (void)ins;
    *v = otLoggingGetLevel();
    return OT_ERROR_NONE;
}

static otError set_esp_log_level(otInstance *ins, int32_t v)
{
    (void)ins;
    esp_log_level_set("*", (esp_log_level_t)v);
    return OT_ERROR_NONE;
}

static otError get_esp_log_level(otInstance *ins, int32_t *v)
{
    (void)ins;
    *v = esp_log_level_get("*");
    return OT_ERROR_NONE;
}

static const char *const s_route_preference_names[] = {"low", "medium", "high"};
static const char *const s_ot_log_level_names[] = {"none", "crit", "warn", "note", "info", "debg"};
static const char *const s_esp_log_level_names[] = {"none", "error", "warn", "info", "debug", "verbose"};

static const setting_def_t s_settings[] = {
    {"radio.tx_power", "tx_power", "Radio", "Transmit power", "dBm",
     "IEEE 802.15.4 transmit power requested from the radio co-processor. The stock Sonoff MG24 RCP starts at "
     "0 dBm. Respect local limits (EU: 20 dBm EIRP including antenna gain).",
     SETTING_INT, -20, 20, NULL, false, set_tx_power, get_tx_power, NULL, false, false, 0},
    {"radio.cca_threshold", "cca_thresh", "Radio", "CCA energy-detect threshold", "dBm",
     "Clear-channel assessment threshold. Lower values make the radio back off earlier on busy channels.",
     SETTING_INT, -100, -10, NULL, false, set_cca_threshold, get_cca_threshold, NULL, false, false, 0},
    {"link.max_frame_retries_direct", "retry_direct", "Link", "Max frame retries (direct)", NULL,
     "MAC retransmissions for frames to rx-on neighbors.", SETTING_INT, 0, 15, NULL, false, set_retries_direct,
     get_retries_direct, NULL, false, false, 0},
    {"link.max_frame_retries_indirect", "retry_indirect", "Link", "Max frame retries (indirect)", NULL,
     "MAC retransmissions for frames to sleepy children.", SETTING_INT, 0, 15, NULL, false, set_retries_indirect,
     get_retries_indirect, NULL, false, false, 0},
#if CONFIG_OPENTHREAD_RADIO_TREL
    {"trel.enabled", "trel", "TREL", "TREL (Thread over Ethernet/Wi-Fi)", NULL,
     "Carry Thread frames over the backbone link so border routers on the same LAN stay connected even when "
     "they cannot hear each other over the radio.",
     SETTING_BOOL, 0, 1, NULL, false, set_trel, get_trel, NULL, false, false, 0},
#endif
    {"thread.router_eligible", "router_elig", "Thread", "Router eligible", NULL,
     "Allow this device to become a Thread router or leader.", SETTING_BOOL, 0, 1, NULL, false, set_router_eligible,
     get_router_eligible, NULL, false, false, 0},
    {"thread.leader_weight", "leader_weight", "Thread", "Local leader weight", NULL,
     "Weight used when a new partition is formed. Give the preferred leader the highest weight.", SETTING_INT, 0,
     255, NULL, false, set_leader_weight, get_leader_weight, NULL, false, false, 0},
    {"thread.router_selection_jitter", "router_jitter", "Thread", "Router selection jitter", "s",
     "Random delay before upgrading to or downgrading from router.", SETTING_INT, 1, 255, NULL, false,
     set_router_jitter, get_router_jitter, NULL, false, false, 0},
    {"thread.router_upgrade_threshold", "rtr_upgrade", "Thread", "Router upgrade threshold", "routers",
     "Upgrade to router while the partition has fewer active routers than this.", SETTING_INT, 1, 32, NULL, false,
     set_upgrade_threshold, get_upgrade_threshold, NULL, false, false, 0},
    {"thread.router_downgrade_threshold", "rtr_downgrade", "Thread", "Router downgrade threshold", "routers",
     "Consider downgrading to child while the partition has more active routers than this.", SETTING_INT, 1, 32,
     NULL, false, set_downgrade_threshold, get_downgrade_threshold, NULL, false, false, 0},
    {"thread.network_id_timeout", "netid_timeout", "Thread", "Network ID timeout", "s",
     "Time without leader connectivity before a router forms a new partition.", SETTING_INT, 10, 255, NULL, false,
     set_network_id_timeout, get_network_id_timeout, NULL, false, false, 0},
    {"thread.max_children", "max_children", "Thread", "Maximum children", NULL,
     "Maximum number of end devices attached directly to this router.", SETTING_INT, 1,
     CONFIG_OPENTHREAD_MLE_MAX_CHILDREN, NULL, true, set_max_children, get_max_children, NULL, false, false, 0},
    {"border_routing.enabled", "br_enabled", "Border routing", "Border routing", NULL,
     "Advertise routes between the Thread network and the backbone link.", SETTING_BOOL, 0, 1, NULL, false,
     set_br_enabled, get_br_enabled, NULL, false, false, 0},
    {"border_routing.rio_preference", "br_rio_prf", "Border routing", "RIO preference", NULL,
     "Preference of the Route Information Options advertised to the backbone. Lets LAN hosts prefer one border "
     "router.",
     SETTING_ENUM, -1, 1, s_route_preference_names, false, set_rio_preference, get_rio_preference,
     clear_rio_preference, false, false, 0},
    {"border_routing.route_preference", "br_route_prf", "Border routing", "Network Data route preference", NULL,
     "Preference of the external route this border router publishes in Thread Network Data.", SETTING_ENUM, -1, 1,
     s_route_preference_names, false, set_route_preference, get_route_preference, clear_route_preference, false, false, 0},
    {"border_routing.dhcp6_pd", "br_dhcp6_pd", "Border routing", "DHCPv6 prefix delegation", NULL,
     "Request the OMR prefix from an upstream DHCPv6-PD server instead of generating a ULA prefix.", SETTING_BOOL,
     0, 1, NULL, false, set_dhcp6_pd, get_dhcp6_pd, NULL, false, false, 0},
    {"border_routing.nat64", "nat64", "Border routing", "NAT64 prefix publishing", NULL,
     "Publish a NAT64 prefix in Network Data.", SETTING_BOOL, 0, 1, NULL, false, set_nat64, get_nat64, NULL, true,
     true, 1},
    {"bbr.enabled", "bbr_enabled", "Backbone router", "Backbone Router", NULL,
     "Thread 1.2+ Backbone Router for multicast across the backbone.", SETTING_BOOL, 0, 1, NULL, false, set_bbr,
     get_bbr, NULL, true, true, 1},
    {"bbr.registration_jitter", "bbr_jitter", "Backbone router", "BBR registration jitter", "s",
     "Random delay before registering as Backbone Router.", SETTING_INT, 1, 255, NULL, false, set_bbr_jitter,
     get_bbr_jitter, NULL, false, false, 0},
    {"log.ot_level", "ot_log_level", "Logging", "OpenThread log level", NULL, "OpenThread core log level.",
     SETTING_ENUM, 0, 5, s_ot_log_level_names, false, set_ot_log_level, get_ot_log_level, NULL, false, false, 0},
    {"log.esp_level", "esp_log_level", "Logging", "ESP-IDF log level", NULL,
     "Global ESP-IDF log level for all tags (limited by the build's maximum level).", SETTING_ENUM, 0,
     CONFIG_LOG_MAXIMUM_LEVEL, s_esp_log_level_names, false, set_esp_log_level, get_esp_log_level, NULL, false, false, 0},
};

#define NUM_SETTINGS (sizeof(s_settings) / sizeof(s_settings[0]))

/* All fields below are protected by the OpenThread API lock. */
static int32_t s_override[NUM_SETTINGS];
static bool s_has_override[NUM_SETTINGS];
static int32_t s_default[NUM_SETTINGS];
static bool s_has_default[NUM_SETTINGS];
static otError s_apply_error[NUM_SETTINGS];
static bool s_booted;

/*-----------------------------------------------------------------------------------------------
 Settings core
-----------------------------------------------------------------------------------------------*/
static int find_setting(const char *key)
{
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        if (strcmp(s_settings[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static esp_err_t persist_setting(size_t index)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS");
    esp_err_t err;
    if (s_has_override[index]) {
        err = nvs_set_i32(handle, s_settings[index].nvs_key, s_override[index]);
    } else {
        err = nvs_erase_key(handle, s_settings[index].nvs_key);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void load_settings(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to open settings: %s", esp_err_to_name(err));
        }
        return;
    }
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        int32_t value;
        if (nvs_get_i32(handle, s_settings[i].nvs_key, &value) == ESP_OK && value >= s_settings[i].min &&
            value <= s_settings[i].max) {
            s_override[i] = value;
            s_has_override[i] = true;
        }
    }
    nvs_close(handle);
}

/* Caller holds the OpenThread lock. */
static otError apply_setting(otInstance *ins, size_t index, bool boot)
{
    const setting_def_t *def = &s_settings[index];
    otError error = OT_ERROR_NONE;

    if (s_has_override[index]) {
        error = def->set(ins, s_override[index]);
    } else if (!boot && def->clear) {
        def->clear(ins);
    } else if (!boot && s_has_default[index]) {
        error = def->set(ins, s_default[index]);
    }
    s_apply_error[index] = error;
    if (error != OT_ERROR_NONE) {
        ESP_LOGW(TAG, "Failed to apply %s: %s", def->key, otThreadErrorToString(error));
    }
    return error;
}

void esp_br_settings_apply_boot(void)
{
    otInstance *ins = esp_openthread_get_instance();

    esp_openthread_lock_acquire(portMAX_DELAY);
#if CONFIG_ESP_BR_SETTINGS_TREL_DEFAULT
    otTrelSetEnabled(ins, true);
#elif CONFIG_OPENTHREAD_RADIO_TREL
    otTrelSetEnabled(ins, false);
#endif
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        int32_t value;
        if (s_settings[i].clear) {
            continue; /* default is the stack's automatic behaviour */
        }
        if (s_settings[i].has_stack_default) {
            s_default[i] = s_settings[i].stack_default;
            s_has_default[i] = true;
        } else if (s_settings[i].get(ins, &value) == OT_ERROR_NONE) {
            s_default[i] = value;
            s_has_default[i] = true;
        }
    }
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        if (s_has_override[i] && apply_setting(ins, i, true) == OT_ERROR_NONE) {
            ESP_LOGI(TAG, "Applied %s = %" PRId32, s_settings[i].key, s_override[i]);
        }
    }
    s_booted = true;
    esp_openthread_lock_release();
}

static cJSON *value_to_json(const setting_def_t *def, int32_t value)
{
    switch (def->type) {
    case SETTING_BOOL:
        return cJSON_CreateBool(value != 0);
    case SETTING_ENUM:
        return cJSON_CreateString(def->options[value - def->min]);
    default:
        return cJSON_CreateNumber(value);
    }
}

/* Parse a JSON value for a setting. Returns false and sets *reason when invalid. */
static bool json_to_value(const setting_def_t *def, const cJSON *item, int32_t *value, const char **reason)
{
    switch (def->type) {
    case SETTING_BOOL:
        if (!cJSON_IsBool(item)) {
            *reason = "expected true or false";
            return false;
        }
        *value = cJSON_IsTrue(item) ? 1 : 0;
        return true;
    case SETTING_ENUM:
        if (cJSON_IsString(item)) {
            for (int32_t v = def->min; v <= def->max; v++) {
                if (strcmp(item->valuestring, def->options[v - def->min]) == 0) {
                    *value = v;
                    return true;
                }
            }
        }
        *reason = "unknown option";
        return false;
    default:
        if (!cJSON_IsNumber(item) || item->valuedouble != (double)(int32_t)item->valuedouble) {
            *reason = "expected an integer";
            return false;
        }
        *value = (int32_t)item->valuedouble;
        if (*value < def->min || *value > def->max) {
            *reason = "out of range";
            return false;
        }
        return true;
    }
}

/* Caller holds the OpenThread lock. */
static cJSON *settings_to_json(otInstance *ins)
{
    cJSON *array = cJSON_CreateArray();
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        const setting_def_t *def = &s_settings[i];
        cJSON *item = cJSON_CreateObject();
        int32_t current;

        cJSON_AddStringToObject(item, "key", def->key);
        cJSON_AddStringToObject(item, "group", def->group);
        cJSON_AddStringToObject(item, "label", def->label);
        cJSON_AddStringToObject(item, "description", def->description);
        cJSON_AddStringToObject(item, "type",
                                def->type == SETTING_BOOL ? "bool" : (def->type == SETTING_ENUM ? "enum" : "int"));
        if (def->unit) {
            cJSON_AddStringToObject(item, "unit", def->unit);
        }
        if (def->type == SETTING_INT) {
            cJSON_AddNumberToObject(item, "min", def->min);
            cJSON_AddNumberToObject(item, "max", def->max);
        } else if (def->type == SETTING_ENUM) {
            cJSON *options = cJSON_AddArrayToObject(item, "options");
            for (int32_t v = def->min; v <= def->max; v++) {
                cJSON_AddItemToArray(options, cJSON_CreateString(def->options[v - def->min]));
            }
        }
        cJSON_AddBoolToObject(item, "requires_restart", def->requires_restart);
        cJSON_AddItemToObject(item, "value",
                              def->get(ins, &current) == OT_ERROR_NONE ? value_to_json(def, current) : cJSON_CreateNull());
        cJSON_AddItemToObject(item, "configured",
                              s_has_override[i] ? value_to_json(def, s_override[i]) : cJSON_CreateNull());
        cJSON_AddItemToObject(item, "default",
                              def->clear          ? cJSON_CreateString("auto")
                              : s_has_default[i] ? value_to_json(def, s_default[i])
                                                 : cJSON_CreateNull());
        cJSON_AddItemToObject(item, "apply_error", s_apply_error[i] == OT_ERROR_NONE
                                                       ? cJSON_CreateNull()
                                                       : cJSON_CreateString(otThreadErrorToString(s_apply_error[i])));
        cJSON_AddItemToArray(array, item);
    }
    return array;
}

/*
 * Validate and apply a {key: value|null} object. When replace is true, every setting that is not
 * listed is reset to its default. Invalid input is rejected before anything is changed.
 * Caller holds the OpenThread lock.
 */
static bool update_settings(otInstance *ins, const cJSON *changes, bool replace, cJSON *errors, cJSON *results)
{
    int32_t values[NUM_SETTINGS];
    bool present[NUM_SETTINGS] = {false};
    bool clear[NUM_SETTINGS] = {false};
    bool valid = true;
    const cJSON *item;

    cJSON_ArrayForEach(item, changes)
    {
        int index = find_setting(item->string);
        const char *reason = NULL;
        if (index < 0) {
            cJSON_AddStringToObject(errors, item->string, "unknown setting");
            valid = false;
        } else if (cJSON_IsNull(item)) {
            present[index] = true;
            clear[index] = true;
        } else if (json_to_value(&s_settings[index], item, &values[index], &reason)) {
            present[index] = true;
        } else {
            cJSON_AddStringToObject(errors, item->string, reason);
            valid = false;
        }
    }
    if (!valid) {
        return false;
    }

    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        if (!present[i] && !(replace && s_has_override[i])) {
            continue;
        }
        bool has_override = present[i] && !clear[i];
        if (has_override == s_has_override[i] && (!has_override || values[i] == s_override[i])) {
            continue;
        }
        bool old_has_override = s_has_override[i];
        int32_t old_override = s_override[i];
        s_has_override[i] = has_override;
        s_override[i] = has_override ? values[i] : 0;
        esp_err_t err = persist_setting(i);
        const char *result;
        if (err != ESP_OK) {
            /* Keep memory consistent with NVS so a retry is not mistaken for "unchanged". */
            s_has_override[i] = old_has_override;
            s_override[i] = old_override;
            result = esp_err_to_name(err);
        } else if (s_settings[i].requires_restart || !s_booted) {
            result = "restart_required";
        } else {
            result = apply_setting(ins, i, false) == OT_ERROR_NONE ? "applied" : "apply_failed";
        }
        cJSON_AddStringToObject(results, s_settings[i].key, result);
    }
    return true;
}

/*-----------------------------------------------------------------------------------------------
 Status
-----------------------------------------------------------------------------------------------*/
static void add_hex(cJSON *obj, const char *name, const uint8_t *bytes, size_t len)
{
    char buf[2 * OT_OPERATIONAL_DATASET_MAX_LENGTH + 1];
    if (len * 2 + 1 > sizeof(buf)) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        snprintf(&buf[i * 2], 3, "%02x", bytes[i]);
    }
    buf[len * 2] = '\0';
    cJSON_AddStringToObject(obj, name, buf);
}

static void add_prefix(cJSON *obj, const char *name, const otIp6Prefix *prefix)
{
    char buf[OT_IP6_PREFIX_STRING_SIZE];
    otIp6PrefixToString(prefix, buf, sizeof(buf));
    cJSON_AddStringToObject(obj, name, buf);
}

static double error_rate_percent(uint16_t rate)
{
    return (double)rate * 100.0 / 0xffff;
}

static const char *route_preference_name(otRoutePreference preference)
{
    return (preference >= -1 && preference <= 1) ? s_route_preference_names[preference + 1] : "unknown";
}

static cJSON *status_node(otInstance *ins)
{
    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "role", otThreadDeviceRoleToString(otThreadGetDeviceRole(ins)));
    cJSON_AddNumberToObject(node, "rloc16", otThreadGetRloc16(ins));
    add_hex(node, "ext_address", otLinkGetExtendedAddress(ins)->m8, OT_EXT_ADDRESS_SIZE);
    cJSON_AddNumberToObject(node, "partition_id", otThreadGetPartitionId(ins));
    cJSON_AddNumberToObject(node, "leader_router_id", otThreadGetLeaderRouterId(ins));
    cJSON_AddNumberToObject(node, "local_leader_weight", otThreadGetLocalLeaderWeight(ins));
    cJSON_AddNumberToObject(node, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(node, "min_free_heap", esp_get_minimum_free_heap_size());
    return node;
}

static cJSON *status_radio(otInstance *ins)
{
    cJSON *radio = cJSON_CreateObject();
    int8_t value;
    cJSON_AddNumberToObject(radio, "channel", otLinkGetChannel(ins));
    if (otPlatRadioGetTransmitPower(ins, &value) == OT_ERROR_NONE) {
        cJSON_AddNumberToObject(radio, "tx_power", value);
    }
    if (otPlatRadioGetCcaEnergyDetectThreshold(ins, &value) == OT_ERROR_NONE) {
        cJSON_AddNumberToObject(radio, "cca_threshold", value);
    }
    cJSON_AddNumberToObject(radio, "cca_failure_rate_percent", error_rate_percent(otLinkGetCcaFailureRate(ins)));
    cJSON_AddNumberToObject(radio, "receive_sensitivity", otPlatRadioGetReceiveSensitivity(ins));
    cJSON_AddStringToObject(radio, "rcp_version", otPlatRadioGetVersionString(ins));
    return radio;
}

static cJSON *status_mac_counters(otInstance *ins)
{
    const otMacCounters *c = otLinkGetCounters(ins);
    cJSON *obj = cJSON_CreateObject();
#define ADD_COUNTER(name, field) cJSON_AddNumberToObject(obj, name, c->field)
    ADD_COUNTER("tx_total", mTxTotal);
    ADD_COUNTER("tx_unicast", mTxUnicast);
    ADD_COUNTER("tx_broadcast", mTxBroadcast);
    ADD_COUNTER("tx_ack_requested", mTxAckRequested);
    ADD_COUNTER("tx_acked", mTxAcked);
    ADD_COUNTER("tx_retry", mTxRetry);
    ADD_COUNTER("tx_err_cca", mTxErrCca);
    ADD_COUNTER("tx_err_abort", mTxErrAbort);
    ADD_COUNTER("tx_err_busy_channel", mTxErrBusyChannel);
    ADD_COUNTER("tx_direct_max_retry_expiry", mTxDirectMaxRetryExpiry);
    ADD_COUNTER("tx_indirect_max_retry_expiry", mTxIndirectMaxRetryExpiry);
    ADD_COUNTER("rx_total", mRxTotal);
    ADD_COUNTER("rx_unicast", mRxUnicast);
    ADD_COUNTER("rx_broadcast", mRxBroadcast);
    ADD_COUNTER("rx_err_no_frame", mRxErrNoFrame);
    ADD_COUNTER("rx_err_unknown_neighbor", mRxErrUnknownNeighbor);
    ADD_COUNTER("rx_err_invalid_src_addr", mRxErrInvalidSrcAddr);
    ADD_COUNTER("rx_err_sec", mRxErrSec);
    ADD_COUNTER("rx_err_fcs", mRxErrFcs);
    ADD_COUNTER("rx_err_other", mRxErrOther);
    ADD_COUNTER("rx_duplicated", mRxDuplicated);
#undef ADD_COUNTER
    return obj;
}

static cJSON *status_mle_counters(otInstance *ins)
{
    const otMleCounters *c = otThreadGetMleCounters(ins);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "disabled_role", c->mDisabledRole);
    cJSON_AddNumberToObject(obj, "detached_role", c->mDetachedRole);
    cJSON_AddNumberToObject(obj, "child_role", c->mChildRole);
    cJSON_AddNumberToObject(obj, "router_role", c->mRouterRole);
    cJSON_AddNumberToObject(obj, "leader_role", c->mLeaderRole);
    cJSON_AddNumberToObject(obj, "attach_attempts", c->mAttachAttempts);
    cJSON_AddNumberToObject(obj, "partition_id_changes", c->mPartitionIdChanges);
    cJSON_AddNumberToObject(obj, "better_partition_attach_attempts", c->mBetterPartitionAttachAttempts);
    cJSON_AddNumberToObject(obj, "parent_changes", c->mParentChanges);
    return obj;
}

static void add_rssi(cJSON *obj, const char *name, int8_t rssi)
{
    if (rssi == OT_RADIO_RSSI_INVALID) {
        cJSON_AddNullToObject(obj, name);
    } else {
        cJSON_AddNumberToObject(obj, name, rssi);
    }
}

static cJSON *status_neighbors(otInstance *ins)
{
    cJSON *array = cJSON_CreateArray();
    otNeighborInfoIterator iterator = OT_NEIGHBOR_INFO_ITERATOR_INIT;
    otNeighborInfo info;
    while (otThreadGetNextNeighborInfo(ins, &iterator, &info) == OT_ERROR_NONE) {
        cJSON *n = cJSON_CreateObject();
        add_hex(n, "ext_address", info.mExtAddress.m8, OT_EXT_ADDRESS_SIZE);
        cJSON_AddNumberToObject(n, "rloc16", info.mRloc16);
        cJSON_AddStringToObject(n, "role", info.mIsChild ? "child" : "router");
        add_rssi(n, "average_rssi", info.mAverageRssi);
        add_rssi(n, "last_rssi", info.mLastRssi);
        cJSON_AddNumberToObject(n, "link_margin", info.mLinkMargin);
        cJSON_AddNumberToObject(n, "link_quality_in", info.mLinkQualityIn);
        cJSON_AddNumberToObject(n, "frame_error_rate_percent", error_rate_percent(info.mFrameErrorRate));
        cJSON_AddNumberToObject(n, "message_error_rate_percent", error_rate_percent(info.mMessageErrorRate));
        cJSON_AddNumberToObject(n, "age", info.mAge);
        cJSON_AddNumberToObject(n, "connection_time", info.mConnectionTime);
        cJSON_AddNumberToObject(n, "version", info.mVersion);
        cJSON_AddBoolToObject(n, "rx_on_when_idle", info.mRxOnWhenIdle);
        cJSON_AddBoolToObject(n, "full_thread_device", info.mFullThreadDevice);
        cJSON_AddItemToArray(array, n);
    }
    return array;
}

static cJSON *status_routers(otInstance *ins)
{
    cJSON *array = cJSON_CreateArray();
    uint8_t max_router_id = otThreadGetMaxRouterId(ins);
    uint16_t own_rloc16 = otThreadGetRloc16(ins);
    for (uint8_t id = 0; id <= max_router_id; id++) {
        otRouterInfo info;
        if (otThreadGetRouterInfo(ins, id, &info) != OT_ERROR_NONE || !info.mAllocated) {
            continue;
        }
        /* OpenThread reports no extended address or link data for the device's own entry. */
        bool self = info.mRloc16 == own_rloc16;
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "router_id", info.mRouterId);
        cJSON_AddNumberToObject(r, "rloc16", info.mRloc16);
        add_hex(r, "ext_address", self ? otLinkGetExtendedAddress(ins)->m8 : info.mExtAddress.m8,
                OT_EXT_ADDRESS_SIZE);
        cJSON_AddBoolToObject(r, "self", self);
        cJSON_AddNumberToObject(r, "next_hop", info.mNextHop);
        cJSON_AddNumberToObject(r, "path_cost", info.mPathCost);
        cJSON_AddNumberToObject(r, "link_quality_in", info.mLinkQualityIn);
        cJSON_AddNumberToObject(r, "link_quality_out", info.mLinkQualityOut);
        cJSON_AddNumberToObject(r, "age", info.mAge);
        cJSON_AddBoolToObject(r, "link_established", info.mLinkEstablished);
        cJSON_AddItemToArray(array, r);
    }
    return array;
}

static cJSON *status_children(otInstance *ins)
{
    cJSON *array = cJSON_CreateArray();
    uint16_t max_children = otThreadGetMaxAllowedChildren(ins);
    for (uint16_t index = 0; index < max_children; index++) {
        otChildInfo info;
        if (otThreadGetChildInfoByIndex(ins, index, &info) != OT_ERROR_NONE) {
            continue;
        }
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "child_id", info.mChildId);
        cJSON_AddNumberToObject(c, "rloc16", info.mRloc16);
        add_hex(c, "ext_address", info.mExtAddress.m8, OT_EXT_ADDRESS_SIZE);
        cJSON_AddNumberToObject(c, "timeout", info.mTimeout);
        cJSON_AddNumberToObject(c, "age", info.mAge);
        add_rssi(c, "average_rssi", info.mAverageRssi);
        add_rssi(c, "last_rssi", info.mLastRssi);
        cJSON_AddNumberToObject(c, "link_quality_in", info.mLinkQualityIn);
        cJSON_AddNumberToObject(c, "frame_error_rate_percent", error_rate_percent(info.mFrameErrorRate));
        cJSON_AddNumberToObject(c, "message_error_rate_percent", error_rate_percent(info.mMessageErrorRate));
        cJSON_AddNumberToObject(c, "queued_messages", info.mQueuedMessageCnt);
        cJSON_AddNumberToObject(c, "version", info.mVersion);
        cJSON_AddBoolToObject(c, "rx_on_when_idle", info.mRxOnWhenIdle);
        cJSON_AddBoolToObject(c, "full_thread_device", info.mFullThreadDevice);
        cJSON_AddItemToArray(array, c);
    }
    return array;
}

static cJSON *status_trel(otInstance *ins)
{
    cJSON *trel = cJSON_CreateObject();
#if CONFIG_OPENTHREAD_RADIO_TREL
    cJSON_AddBoolToObject(trel, "supported", true);
    cJSON_AddBoolToObject(trel, "enabled", otTrelIsEnabled(ins));
    cJSON_AddNumberToObject(trel, "udp_port", otTrelGetUdpPort(ins));
    cJSON *peers = cJSON_AddArrayToObject(trel, "peers");
    otTrelPeerIterator iterator;
    const otTrelPeer *peer;
    otTrelInitPeerIterator(ins, &iterator);
    while ((peer = otTrelGetNextPeer(ins, &iterator)) != NULL) {
        cJSON *p = cJSON_CreateObject();
        char addr[OT_IP6_SOCK_ADDR_STRING_SIZE];
        add_hex(p, "ext_address", peer->mExtAddress.m8, OT_EXT_ADDRESS_SIZE);
        add_hex(p, "ext_panid", peer->mExtPanId.m8, OT_EXT_PAN_ID_SIZE);
        otIp6SockAddrToString(&peer->mSockAddr, addr, sizeof(addr));
        cJSON_AddStringToObject(p, "address", addr);
        cJSON_AddItemToArray(peers, p);
    }
    const otTrelCounters *counters = otTrelGetCounters(ins);
    if (counters) {
        cJSON *c = cJSON_AddObjectToObject(trel, "counters");
        cJSON_AddNumberToObject(c, "tx_packets", counters->mTxPackets);
        cJSON_AddNumberToObject(c, "tx_bytes", counters->mTxBytes);
        cJSON_AddNumberToObject(c, "tx_failure", counters->mTxFailure);
        cJSON_AddNumberToObject(c, "rx_packets", counters->mRxPackets);
        cJSON_AddNumberToObject(c, "rx_bytes", counters->mRxBytes);
    }
#else
    (void)ins;
    cJSON_AddBoolToObject(trel, "supported", false);
#endif
    return trel;
}

static const char *br_state_name(otBorderRoutingState state)
{
    switch (state) {
    case OT_BORDER_ROUTING_STATE_UNINITIALIZED:
        return "uninitialized";
    case OT_BORDER_ROUTING_STATE_DISABLED:
        return "disabled";
    case OT_BORDER_ROUTING_STATE_STOPPED:
        return "stopped";
    case OT_BORDER_ROUTING_STATE_RUNNING:
        return "running";
    default:
        return "unknown";
    }
}

static const char *dhcp6_pd_state_name(otBorderRoutingDhcp6PdState state)
{
    switch (state) {
    case OT_BORDER_ROUTING_DHCP6_PD_STATE_DISABLED:
        return "disabled";
    case OT_BORDER_ROUTING_DHCP6_PD_STATE_STOPPED:
        return "stopped";
    case OT_BORDER_ROUTING_DHCP6_PD_STATE_RUNNING:
        return "running";
    default:
        return "unknown";
    }
}

static const char *nat64_state_name(otNat64State state)
{
    switch (state) {
    case OT_NAT64_STATE_DISABLED:
        return "disabled";
    case OT_NAT64_STATE_NOT_RUNNING:
        return "not_running";
    case OT_NAT64_STATE_IDLE:
        return "idle";
    case OT_NAT64_STATE_ACTIVE:
        return "active";
    default:
        return "unknown";
    }
}

static const char *bbr_state_name(otBackboneRouterState state)
{
    switch (state) {
    case OT_BACKBONE_ROUTER_STATE_DISABLED:
        return "disabled";
    case OT_BACKBONE_ROUTER_STATE_SECONDARY:
        return "secondary";
    case OT_BACKBONE_ROUTER_STATE_PRIMARY:
        return "primary";
    default:
        return "unknown";
    }
}

static const char *srp_state_name(otSrpServerState state)
{
    switch (state) {
    case OT_SRP_SERVER_STATE_DISABLED:
        return "disabled";
    case OT_SRP_SERVER_STATE_RUNNING:
        return "running";
    case OT_SRP_SERVER_STATE_STOPPED:
        return "stopped";
    default:
        return "unknown";
    }
}

static cJSON *status_border_routing(otInstance *ins)
{
    cJSON *br = cJSON_CreateObject();
    otIp6Prefix prefix;
    otRoutePreference preference;

    cJSON_AddStringToObject(br, "state", br_state_name(otBorderRoutingGetState(ins)));
    if (otBorderRoutingGetOmrPrefix(ins, &prefix) == OT_ERROR_NONE) {
        add_prefix(br, "local_omr_prefix", &prefix);
    }
    if (otBorderRoutingGetFavoredOmrPrefix(ins, &prefix, &preference) == OT_ERROR_NONE) {
        add_prefix(br, "favored_omr_prefix", &prefix);
        cJSON_AddStringToObject(br, "favored_omr_preference", route_preference_name(preference));
    }
    if (otBorderRoutingGetOnLinkPrefix(ins, &prefix) == OT_ERROR_NONE) {
        add_prefix(br, "local_on_link_prefix", &prefix);
    }
    if (otBorderRoutingGetFavoredOnLinkPrefix(ins, &prefix) == OT_ERROR_NONE) {
        add_prefix(br, "favored_on_link_prefix", &prefix);
    }
    cJSON_AddStringToObject(br, "rio_preference",
                            route_preference_name(otBorderRoutingGetRouteInfoOptionPreference(ins)));
    cJSON_AddStringToObject(br, "route_preference", route_preference_name(otBorderRoutingGetRoutePreference(ins)));
    cJSON_AddStringToObject(br, "dhcp6_pd_state", dhcp6_pd_state_name(otBorderRoutingDhcp6PdGetState(ins)));
    cJSON_AddStringToObject(br, "nat64_state", nat64_state_name(otNat64GetPrefixManagerState(ins)));

    cJSON *prefixes = cJSON_AddArrayToObject(br, "prefix_table");
    otBorderRoutingPrefixTableIterator iterator;
    otBorderRoutingPrefixTableEntry entry;
    otBorderRoutingPrefixTableInitIterator(ins, &iterator);
    while (otBorderRoutingGetNextPrefixTableEntry(ins, &iterator, &entry) == OT_ERROR_NONE) {
        cJSON *e = cJSON_CreateObject();
        char router[OT_IP6_ADDRESS_STRING_SIZE];
        add_prefix(e, "prefix", &entry.mPrefix);
        otIp6AddressToString(&entry.mRouter.mAddress, router, sizeof(router));
        cJSON_AddStringToObject(e, "router", router);
        cJSON_AddBoolToObject(e, "on_link", entry.mIsOnLink);
        cJSON_AddNumberToObject(e, "valid_lifetime", entry.mValidLifetime);
        cJSON_AddNumberToObject(e, "preferred_lifetime", entry.mPreferredLifetime);
        cJSON_AddStringToObject(e, "route_preference", route_preference_name(entry.mRoutePreference));
        cJSON_AddItemToArray(prefixes, e);
    }
    return br;
}

static cJSON *status_services(otInstance *ins)
{
    cJSON *services = cJSON_CreateObject();
    cJSON_AddStringToObject(services, "backbone_router", bbr_state_name(otBackboneRouterGetState(ins)));
    cJSON_AddStringToObject(services, "srp_server", srp_state_name(otSrpServerGetState(ins)));
    cJSON_AddNumberToObject(services, "srp_server_port", otSrpServerGetPort(ins));
    return services;
}

/*-----------------------------------------------------------------------------------------------
 Backup and restore
-----------------------------------------------------------------------------------------------*/
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool hex_to_dataset(const char *hex, otOperationalDatasetTlvs *tlvs)
{
    size_t len = strlen(hex);
    if (len == 0 || len % 2 != 0 || len / 2 > OT_OPERATIONAL_DATASET_MAX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < len / 2; i++) {
        int hi = hex_value(hex[2 * i]);
        int lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        tlvs->mTlvs[i] = (uint8_t)((hi << 4) | lo);
    }
    tlvs->mLength = (uint8_t)(len / 2);
    return true;
}

/* Caller holds the OpenThread lock. */
static cJSON *create_backup(otInstance *ins)
{
    cJSON *backup = cJSON_CreateObject();
    const esp_app_desc_t *app = esp_app_get_description();
    otOperationalDatasetTlvs tlvs;

    cJSON_AddStringToObject(backup, "format", BACKUP_FORMAT);
    cJSON_AddNumberToObject(backup, "version", BACKUP_VERSION);
    cJSON *firmware = cJSON_AddObjectToObject(backup, "firmware");
    cJSON_AddStringToObject(firmware, "project", app->project_name);
    cJSON_AddStringToObject(firmware, "version", app->version);
    cJSON_AddStringToObject(firmware, "idf", app->idf_ver);
    cJSON *node = cJSON_AddObjectToObject(backup, "node");
    add_hex(node, "ext_address", otLinkGetExtendedAddress(ins)->m8, OT_EXT_ADDRESS_SIZE);

    cJSON *dataset = cJSON_AddObjectToObject(backup, "dataset");
    if (otDatasetGetActiveTlvs(ins, &tlvs) == OT_ERROR_NONE) {
        add_hex(dataset, "active", tlvs.mTlvs, tlvs.mLength);
    } else {
        cJSON_AddNullToObject(dataset, "active");
    }

    cJSON *settings = cJSON_AddObjectToObject(backup, "settings");
    for (size_t i = 0; i < NUM_SETTINGS; i++) {
        if (s_has_override[i]) {
            cJSON_AddItemToObject(settings, s_settings[i].key, value_to_json(&s_settings[i], s_override[i]));
        }
    }
    return backup;
}

/* Caller holds the OpenThread lock. */
static otError restore_dataset(otInstance *ins, const otOperationalDatasetTlvs *tlvs, const char **result)
{
    otOperationalDatasetTlvs current;
    if (otDatasetGetActiveTlvs(ins, &current) == OT_ERROR_NONE && current.mLength == tlvs->mLength &&
        memcmp(current.mTlvs, tlvs->mTlvs, tlvs->mLength) == 0) {
        *result = "unchanged";
        return OT_ERROR_NONE;
    }

    bool was_running = otThreadGetDeviceRole(ins) != OT_DEVICE_ROLE_DISABLED;
    otError error = otThreadSetEnabled(ins, false);
    if (error == OT_ERROR_NONE) {
        error = otDatasetSetActiveTlvs(ins, tlvs);
    }
    if (error == OT_ERROR_NONE && was_running) {
        if (!otIp6IsEnabled(ins)) {
            error = otIp6SetEnabled(ins, true);
        }
        if (error == OT_ERROR_NONE) {
            error = otThreadSetEnabled(ins, true);
        }
    }
    *result = error == OT_ERROR_NONE ? "applied" : otThreadErrorToString(error);
    return error;
}

/*-----------------------------------------------------------------------------------------------
 HTTP handlers
-----------------------------------------------------------------------------------------------*/
static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *root)
{
    esp_err_t ret = ESP_FAIL;
    char *body = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, JSON_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    ret = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return ret;
}

static cJSON *envelope(int error, cJSON *result, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "error", error);
    cJSON_AddItemToObject(root, "result", result ? result : cJSON_CreateNull());
    cJSON_AddStringToObject(root, "message", message);
    return root;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message, cJSON *details)
{
    return send_json(req, status, envelope(OT_ERROR_INVALID_ARGS, details, message));
}

/* Returns a parsed JSON body, or NULL after sending an error response. Empty bodies yield {}. */
static cJSON *read_json_body(httpd_req_t *req)
{
    if (req->content_len == 0) {
        return cJSON_CreateObject();
    }
    if (req->content_len > MAX_BODY_LEN) {
        send_error(req, "413 Payload Too Large", "Request body too large", NULL);
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return NULL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
            return NULL;
        }
        received += n;
    }
    buf[received] = '\0';
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        send_error(req, "400 Bad Request", "Body must be a JSON object", NULL);
        return NULL;
    }
    return json;
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    otInstance *ins = esp_openthread_get_instance();
    cJSON *result = cJSON_CreateObject();
    esp_openthread_lock_acquire(portMAX_DELAY);
    cJSON_AddItemToObject(result, "settings", settings_to_json(ins));
    esp_openthread_lock_release();
    return send_json(req, "200 OK", envelope(OT_ERROR_NONE, result, "Settings: Success"));
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    cJSON *request = read_json_body(req);
    if (!request) {
        return ESP_OK;
    }
    otInstance *ins = esp_openthread_get_instance();
    cJSON *errors = cJSON_CreateObject();
    cJSON *results = cJSON_CreateObject();

    esp_openthread_lock_acquire(portMAX_DELAY);
    bool ok = update_settings(ins, request, false, errors, results);
    cJSON *settings = ok ? settings_to_json(ins) : NULL;
    esp_openthread_lock_release();
    cJSON_Delete(request);

    if (!ok) {
        cJSON_Delete(results);
        return send_error(req, "400 Bad Request", "Settings: Invalid", errors);
    }
    cJSON_Delete(errors);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "changes", results);
    cJSON_AddItemToObject(result, "settings", settings);
    return send_json(req, "200 OK", envelope(OT_ERROR_NONE, result, "Settings: Saved"));
}

static esp_err_t settings_reset_handler(httpd_req_t *req)
{
    cJSON *request = read_json_body(req);
    if (!request) {
        return ESP_OK;
    }
    cJSON *changes = cJSON_CreateObject();
    const cJSON *keys = cJSON_GetObjectItemCaseSensitive(request, "keys");
    const cJSON *key;
    if (keys && !cJSON_IsArray(keys)) {
        cJSON_Delete(changes);
        cJSON_Delete(request);
        return send_error(req, "400 Bad Request", "\"keys\" must be an array of setting keys", NULL);
    }
    cJSON_ArrayForEach(key, keys)
    {
        if (!cJSON_IsString(key)) {
            cJSON_Delete(changes);
            cJSON_Delete(request);
            return send_error(req, "400 Bad Request", "\"keys\" must be an array of setting keys", NULL);
        }
        cJSON_AddNullToObject(changes, key->valuestring);
    }
    bool reset_all = keys == NULL;
    cJSON_Delete(request);

    otInstance *ins = esp_openthread_get_instance();
    cJSON *errors = cJSON_CreateObject();
    cJSON *results = cJSON_CreateObject();
    esp_openthread_lock_acquire(portMAX_DELAY);
    bool ok = update_settings(ins, changes, reset_all, errors, results);
    cJSON *settings = ok ? settings_to_json(ins) : NULL;
    esp_openthread_lock_release();
    cJSON_Delete(changes);

    if (!ok) {
        cJSON_Delete(results);
        return send_error(req, "400 Bad Request", "Settings: Invalid", errors);
    }
    cJSON_Delete(errors);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "changes", results);
    cJSON_AddItemToObject(result, "settings", settings);
    return send_json(req, "200 OK", envelope(OT_ERROR_NONE, result, "Settings: Reset"));
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    otInstance *ins = esp_openthread_get_instance();
    cJSON *result = cJSON_CreateObject();
    esp_openthread_lock_acquire(portMAX_DELAY);
    cJSON_AddItemToObject(result, "node", status_node(ins));
    cJSON_AddItemToObject(result, "radio", status_radio(ins));
    cJSON_AddItemToObject(result, "mac_counters", status_mac_counters(ins));
    cJSON_AddItemToObject(result, "mle_counters", status_mle_counters(ins));
    cJSON_AddItemToObject(result, "neighbors", status_neighbors(ins));
    cJSON_AddItemToObject(result, "routers", status_routers(ins));
    cJSON_AddItemToObject(result, "children", status_children(ins));
    cJSON_AddItemToObject(result, "trel", status_trel(ins));
    cJSON_AddItemToObject(result, "border_routing", status_border_routing(ins));
    cJSON_AddItemToObject(result, "services", status_services(ins));
    esp_openthread_lock_release();
    return send_json(req, "200 OK", envelope(OT_ERROR_NONE, result, "Status: Success"));
}

static esp_err_t counters_reset_handler(httpd_req_t *req)
{
    otInstance *ins = esp_openthread_get_instance();
    esp_openthread_lock_acquire(portMAX_DELAY);
    otLinkResetCounters(ins);
    otThreadResetMleCounters(ins);
#if CONFIG_OPENTHREAD_RADIO_TREL
    otTrelResetCounters(ins);
#endif
    esp_openthread_lock_release();
    return send_json(req, "200 OK", envelope(OT_ERROR_NONE, NULL, "Counters: Reset"));
}

static esp_err_t backup_get_handler(httpd_req_t *req)
{
    otInstance *ins = esp_openthread_get_instance();
    char disposition[64];

    esp_openthread_lock_acquire(portMAX_DELAY);
    cJSON *backup = create_backup(ins);
    otExtAddress ext = *otLinkGetExtendedAddress(ins);
    esp_openthread_lock_release();

    snprintf(disposition, sizeof(disposition), "attachment; filename=\"otbr-backup-%02x%02x%02x%02x.json\"",
             ext.m8[4], ext.m8[5], ext.m8[6], ext.m8[7]);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    return send_json(req, "200 OK", backup);
}

static esp_err_t restore_post_handler(httpd_req_t *req)
{
    cJSON *request = read_json_body(req);
    if (!request) {
        return ESP_OK;
    }

    const cJSON *format = cJSON_GetObjectItemCaseSensitive(request, "format");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(request, "version");
    const cJSON *dataset = cJSON_GetObjectItemCaseSensitive(request, "dataset");
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(request, "settings");
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(request, "restore");
    bool want_dataset = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(options, "dataset"));
    bool want_settings = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(options, "settings"));
    otOperationalDatasetTlvs tlvs;
    otOperationalDataset parsed;
    const char *active = NULL;

    if (!cJSON_IsString(format) || strcmp(format->valuestring, BACKUP_FORMAT) != 0 || !cJSON_IsNumber(version) ||
        version->valueint != BACKUP_VERSION) {
        cJSON_Delete(request);
        return send_error(req, "400 Bad Request", "Not a supported backup (format/version)", NULL);
    }
    if (want_dataset) {
        active = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(dataset, "active"));
        if (!active || !hex_to_dataset(active, &tlvs) || otDatasetParseTlvs(&tlvs, &parsed) != OT_ERROR_NONE ||
            !parsed.mComponents.mIsNetworkKeyPresent || !parsed.mComponents.mIsChannelPresent ||
            !parsed.mComponents.mIsPanIdPresent || !parsed.mComponents.mIsExtendedPanIdPresent ||
            !parsed.mComponents.mIsActiveTimestampPresent) {
            cJSON_Delete(request);
            return send_error(req, "400 Bad Request", "Backup does not contain a complete Active Dataset", NULL);
        }
    }
    if (want_settings && settings && !cJSON_IsObject(settings)) {
        cJSON_Delete(request);
        return send_error(req, "400 Bad Request", "Backup settings must be an object", NULL);
    }

    otInstance *ins = esp_openthread_get_instance();
    cJSON *result = cJSON_CreateObject();
    cJSON *errors = cJSON_CreateObject();
    cJSON *changes = cJSON_CreateObject();
    const char *dataset_result = "skipped";
    bool settings_valid = true;
    bool ok = true;

    esp_openthread_lock_acquire(portMAX_DELAY);
    if (want_settings) {
        cJSON *empty = settings ? NULL : cJSON_CreateObject();
        settings_valid = update_settings(ins, settings ? settings : empty, true, errors, changes);
        ok = settings_valid;
        cJSON_Delete(empty);
    }
    if (ok && want_dataset && restore_dataset(ins, &tlvs, &dataset_result) != OT_ERROR_NONE) {
        ok = false;
    }
    esp_openthread_lock_release();
    cJSON_Delete(request);

    cJSON_AddStringToObject(result, "dataset", dataset_result);
    cJSON_AddItemToObject(result, "settings", changes);
    if (!ok) {
        cJSON_AddItemToObject(result, "errors", errors);
        return send_json(req, settings_valid ? "409 Conflict" : "400 Bad Request",
                         envelope(OT_ERROR_FAILED, result, "Restore: Failure"));
    }
    cJSON_Delete(errors);
    return send_json(req, "200 OK", envelope(OT_ERROR_NONE, result, "Restore: Success"));
}

static esp_err_t send_page(httpd_req_t *req, const char *start, const char *end)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, start, end - start - 1); /* EMBED_TXTFILES appends a NUL */
}

static esp_err_t settings_page_handler(httpd_req_t *req)
{
    extern const char settings_html_start[] asm("_binary_settings_html_start");
    extern const char settings_html_end[] asm("_binary_settings_html_end");
    return send_page(req, settings_html_start, settings_html_end);
}

static esp_err_t radio_page_handler(httpd_req_t *req)
{
    extern const char radio_html_start[] asm("_binary_radio_html_start");
    extern const char radio_html_end[] asm("_binary_radio_html_end");
    return send_page(req, radio_html_start, radio_html_end);
}

static const httpd_uri_t s_uri_handlers[] = {
    {.uri = "/br/settings", .method = HTTP_GET, .handler = settings_get_handler},
    {.uri = "/br/settings", .method = HTTP_POST, .handler = settings_post_handler},
    {.uri = "/br/settings/reset", .method = HTTP_POST, .handler = settings_reset_handler},
    {.uri = "/br/status", .method = HTTP_GET, .handler = status_get_handler},
    {.uri = "/br/status/counters/reset", .method = HTTP_POST, .handler = counters_reset_handler},
    {.uri = "/br/backup", .method = HTTP_GET, .handler = backup_get_handler},
    {.uri = "/br/restore", .method = HTTP_POST, .handler = restore_post_handler},
    {.uri = "/settings.html", .method = HTTP_GET, .handler = settings_page_handler},
    {.uri = "/radio.html", .method = HTTP_GET, .handler = radio_page_handler},
};

static esp_err_t register_uri_handlers(void *server, void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < sizeof(s_uri_handlers) / sizeof(s_uri_handlers[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler((httpd_handle_t)server, &s_uri_handlers[i]), TAG,
                            "Failed to register %s", s_uri_handlers[i].uri);
    }
    return ESP_OK;
}

static void role_changed_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    otInstance *ins = esp_openthread_get_instance();

    /* Runs after the ESP border router library's state callback, which re-enables BBR and NAT64. */
    esp_openthread_lock_acquire(portMAX_DELAY);
    for (size_t i = 0; s_booted && i < NUM_SETTINGS; i++) {
        if (s_settings[i].reassert_on_role_change && s_has_override[i]) {
            apply_setting(ins, i, true);
        }
    }
    esp_openthread_lock_release();
}

esp_err_t esp_br_settings_init(void)
{
    load_settings();
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(OPENTHREAD_EVENT, OPENTHREAD_EVENT_ROLE_CHANGED, role_changed_handler, NULL), TAG,
        "Failed to register role change handler");
    return esp_br_web_register_extension(register_uri_handlers, NULL,
                                         sizeof(s_uri_handlers) / sizeof(s_uri_handlers[0]));
}
