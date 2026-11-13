#ifndef MENU_NAVIGATOR_H
#define MENU_NAVIGATOR_H

#include "Controllers/MenuManager/MenuManagerTypes.h"
#include <stdbool.h>
#include <stdint.h>
#include "Management/ConfigurationManager/SystemConfigs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the immutable menu topology. */
void MenuNavigator_Init(void);

/* Value-only policy shared by navigation and rendering. No live settings are mutated. */
typedef struct {
    bool valid;
    bool fan_manual;
    uint8_t fan_strategy;
    uint8_t external_led_mode;
    uint8_t builtin_led_mode;
    uint8_t display_mode;
    bool zigbee_connected;
} MenuVisibilityContext;
static inline MenuVisibilityContext MenuNavigator_Context(const SystemSettings *settings,bool connected) {
    MenuVisibilityContext context={0};
    if(settings) context=(MenuVisibilityContext){.valid=true,
        .fan_manual=settings->control.fan_manual_mode,
        .fan_strategy=settings->control.fan_strategy,
        .external_led_mode=settings->hardware.ext_led_mode,
        .builtin_led_mode=settings->hardware.builtin_led_anim_mode,
        .display_mode=settings->system.display_mode,.zigbee_connected=connected};
    return context;
}
static inline bool MenuNavigator_IsVisible(UIState node,const MenuVisibilityContext *context) {
    if (node==UI_STATE_SET_HIGH_TEMP || node==UI_STATE_FAN_DELTA_ON) return false;
    if (node==UI_STATE_SET_RGB_TEMP_COLD_MAX || node==UI_STATE_SET_RGB_TEMP_WARM_MAX || node==UI_STATE_SET_RGB_TEMP_HOT_MAX) return false;
    if (node == UI_STATE_CAT_RGB_XY || (node >= UI_STATE_RGB_COLD_X && node <= UI_STATE_RGB_FREEZE_Y)) return false;
    if((unsigned)node>=UI_STATE_COUNT) return false;
    /* External entity assignment belongs to HA. Keep enum IDs for protocol compatibility. */
    if(node==UI_STATE_OBJECT_SOURCE || node==UI_STATE_AMBIENT_SOURCE) return false;
    if(!context || !context->valid) return true;
    if(node==UI_STATE_SET_TIMEOUT) return true;
    if(node==UI_STATE_CAT_BUILTIN_ANIMATIONS || node==UI_STATE_SET_BUILTIN_BRIGHTNESS ||
        node==UI_STATE_SET_BUILTIN_STARTUP || node==UI_STATE_SET_BUILTIN_IDLE ||
        node==UI_STATE_SET_BUILTIN_CONNECTING || node==UI_STATE_SET_BUILTIN_FAULT)
        return context->builtin_led_mode!=0;
    if(node==UI_STATE_CAT_RGB_REGIONS ||
        (node>=UI_STATE_CAT_ZONE_FREEZE && node<=UI_STATE_CAT_ZONE_DANGER))
        return context->external_led_mode==EXT_LED_MODE_TEMPERATURE;
    if(node==UI_STATE_SET_LOW_TEMP || node==UI_STATE_SET_HIGH_TEMP)
        return !context->fan_manual && context->fan_strategy==0;
    if(node==UI_STATE_FAN_DELTA_ON || node==UI_STATE_FAN_DELTA_OFF)
        return !context->fan_manual && context->fan_strategy==1;
    /* System telemetry also supplies local event consumers; only ZCL reports require a link. */
    if(node==UI_STATE_CAT_ZIGBEE_REP ||
        (node>=UI_STATE_ZB_REP_FAN && node<=UI_STATE_ZB_REP_UPTIME)) return context->zigbee_connected;
    if(node==UI_STATE_SET_LED_ANIM || node==UI_STATE_SET_RGB_BRIGHTNESS)
        return context->external_led_mode!=EXT_LED_MODE_OFF;
    if(node==UI_STATE_SET_RGB_DANGER || node==UI_STATE_SET_RGB_FREEZE ||
        (node>=UI_STATE_SET_RGB_COLD && node<=UI_STATE_SET_RGB_HOT) ||
        (node>=UI_STATE_SET_RGB_TEMP_COLD && node<=UI_STATE_SET_RGB_TEMP_FREEZE) ||
        node==UI_STATE_CAT_RGB_XY || (node>=UI_STATE_RGB_COLD_X && node<=UI_STATE_RGB_FREEZE_Y))
        return context->external_led_mode==EXT_LED_MODE_TEMPERATURE;
    return true;
}

UIState MenuNavigator_FirstVisible(UIState parent,const MenuVisibilityContext *context);
UIState MenuNavigator_PreferredChild(UIState parent,const MenuVisibilityContext *context);
void MenuNavigator_RememberFocus(UIState parent,UIState child);
UIState MenuNavigator_LastVisible(UIState parent,const MenuVisibilityContext *context);
UIState MenuNavigator_NextVisible(UIState node,UIState parent,bool circular,const MenuVisibilityContext *context);
UIState MenuNavigator_PreviousVisible(UIState node,UIState parent,bool circular,const MenuVisibilityContext *context);


/**
 * @brief Return a node parent.
 * 
 * @param node Target node
 * @return Parent, or UI_STATE_COUNT for the root.
 */
UIState MenuNavigator_GetParent(UIState node);

/**
 * @brief Return the first category or root child.
 */
UIState MenuNavigator_GetFirstChild(UIState parent);

/**
 * @brief Return the last category or root child.
 */
UIState MenuNavigator_GetLastChild(UIState parent);

/**
 * @brief Return the next sibling.
 * 
 * @param node Current node
 * @param circular Wrap at the end
 * @return Next node
 */
UIState MenuNavigator_GetNextSibling(UIState node, bool circular);

/**
 * @brief Context-aware next sibling lookup.
 *
 * Uses `parentContext` instead of the node-registry parent when the
 * registered parent may be stale (e.g. the shared UI_STATE_BACK node
 * whose nodeRegistry parent is set to the last category that contains
 * it, not the one the user is actually navigating in).
 *
 * @param node          Current node.
 * @param parentContext The category the user is currently inside.
 *                      Pass UI_STATE_COUNT to fall back to nodeRegistry.
 * @param circular      Wrap-around behaviour.
 */
UIState MenuNavigator_GetNextSiblingIn(UIState node, UIState parentContext, bool circular);

/**
 * @brief Return the previous sibling.
 * 
 * @param node Current node
 * @param circular Wrap at the beginning
 * @return Previous node
 */
UIState MenuNavigator_GetPrevSibling(UIState node, bool circular);

/**
 * @brief Context-aware previous sibling lookup.
 *
 * See MenuNavigator_GetNextSiblingIn for rationale.
 */
UIState MenuNavigator_GetPrevSiblingIn(UIState node, UIState parentContext, bool circular);

/**
 * @brief Return the node type.
 */
MenuNodeType MenuNavigator_GetNodeType(UIState node);

/**
 * @brief Return whether the node is a leaf or action.
 */
bool MenuNavigator_IsLeaf(UIState node);

#ifdef __cplusplus
}
#endif

#endif // MENU_NAVIGATOR_H
