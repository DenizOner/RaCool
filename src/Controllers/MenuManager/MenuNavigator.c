#include "Controllers/MenuManager/MenuNavigator.h"

// Define tree structure
typedef struct {
    UIState parent;
    const UIState* children;
    uint8_t childCount;
} MenuCategoryDef;

static const UIState children_MONITOR[] = {UI_STATE_CAT_TEMP, UI_STATE_CAT_LIGHT, UI_STATE_CAT_DEVICE, UI_STATE_CAT_POWER, UI_STATE_EXIT};
static const UIState children_CAT_TEMP[] = {UI_STATE_FAN_MODE, UI_STATE_SET_LOW_TEMP, UI_STATE_FAN_DELTA_OFF, UI_STATE_BACK};
static const UIState children_CAT_LIGHT[] = {UI_STATE_CAT_BUILTIN_LED, UI_STATE_CAT_EXTERNAL_LED, UI_STATE_BACK};
static const UIState children_CAT_BUILTIN_LED[] = {UI_STATE_SET_BUILTIN_MODE, UI_STATE_SET_BUILTIN_BRIGHTNESS, UI_STATE_CAT_BUILTIN_ANIMATIONS, UI_STATE_BACK};
static const UIState children_CAT_BUILTIN_ANIMATIONS[] = {UI_STATE_SET_BUILTIN_STARTUP, UI_STATE_SET_BUILTIN_IDLE, UI_STATE_SET_BUILTIN_CONNECTING, UI_STATE_SET_BUILTIN_FAULT, UI_STATE_BACK};
static const UIState children_CAT_EXTERNAL_LED[] = {UI_STATE_SET_LED_MODE, UI_STATE_SET_RGB_BRIGHTNESS, UI_STATE_SET_LED_ANIM, UI_STATE_CAT_RGB_REGIONS, UI_STATE_BACK};
static const UIState children_CAT_RGB_REGIONS[] = {UI_STATE_CAT_ZONE_FREEZE, UI_STATE_CAT_ZONE_COLD, UI_STATE_CAT_ZONE_WARM, UI_STATE_CAT_ZONE_HOT, UI_STATE_CAT_ZONE_DANGER, UI_STATE_BACK};
static const UIState children_CAT_ZONE_FREEZE[] = {UI_STATE_SET_RGB_TEMP_FREEZE, UI_STATE_SET_RGB_FREEZE, UI_STATE_BACK};
static const UIState children_CAT_ZONE_COLD[] = {UI_STATE_SET_RGB_TEMP_COLD, UI_STATE_SET_RGB_COLD, UI_STATE_BACK};
static const UIState children_CAT_ZONE_WARM[] = {UI_STATE_SET_RGB_TEMP_WARM, UI_STATE_SET_RGB_WARM, UI_STATE_BACK};
static const UIState children_CAT_ZONE_HOT[] = {UI_STATE_SET_RGB_TEMP_HOT, UI_STATE_SET_RGB_HOT, UI_STATE_BACK};
static const UIState children_CAT_ZONE_DANGER[] = {UI_STATE_SET_RGB_TEMP_DANGER, UI_STATE_SET_RGB_DANGER, UI_STATE_BACK};
static const UIState children_CAT_DEVICE[] = {UI_STATE_CAT_SCREEN, UI_STATE_SET_LANGUAGE, UI_STATE_SET_TEMP_UNIT, UI_STATE_CAT_CALIBRATION, UI_STATE_BACK};
static const UIState children_CAT_SCREEN[] = {UI_STATE_SET_DISPLAY_MODE, UI_STATE_SET_TIMEOUT, UI_STATE_SET_MENU_AUTO_EXIT, UI_STATE_BACK};
static const UIState children_CAT_CALIBRATION[] = {UI_STATE_CALIBRATE_OBJECT, UI_STATE_CALIBRATE_AMBIENT, UI_STATE_BACK};
static const UIState children_CAT_POWER[] = {UI_STATE_SYSTEM_STATUS, UI_STATE_CAT_ZIGBEE, UI_STATE_CAT_ENERGY, UI_STATE_CAT_ADVANCED, UI_STATE_CAT_MAINTENANCE, UI_STATE_BACK};
static const UIState children_CAT_ZIGBEE[] = {UI_STATE_ZIGBEE_PAIR, UI_STATE_SET_ZIGBEE_TX_PROFILE, UI_STATE_CAT_ZIGBEE_REP, UI_STATE_BACK};
static const UIState children_CAT_ZIGBEE_REP[] = {UI_STATE_ZB_REP_FAN, UI_STATE_ZB_REP_AMBIENT, UI_STATE_ZB_REP_CPU, UI_STATE_ZB_REP_HEALTH, UI_STATE_ZB_REP_MEMORY, UI_STATE_ZB_REP_UPTIME, UI_STATE_BACK};
static const UIState children_CAT_ENERGY[] = {UI_STATE_SET_POWER_PROFILE, UI_STATE_SHUTDOWN, UI_STATE_BACK};
static const UIState children_CAT_ADVANCED[] = {UI_STATE_SET_TELEMETRY, UI_STATE_BACK};
static const UIState children_CAT_MAINTENANCE[] = {UI_STATE_RESTART, UI_STATE_FACTORY_RESET, UI_STATE_BACK};

static const MenuCategoryDef categories[] = {
    {UI_STATE_MONITOR, children_MONITOR, sizeof(children_MONITOR)/sizeof(UIState)},
    {UI_STATE_CAT_TEMP, children_CAT_TEMP, sizeof(children_CAT_TEMP)/sizeof(UIState)},
    {UI_STATE_CAT_LIGHT, children_CAT_LIGHT, sizeof(children_CAT_LIGHT)/sizeof(UIState)},
    {UI_STATE_CAT_BUILTIN_LED, children_CAT_BUILTIN_LED, sizeof(children_CAT_BUILTIN_LED)/sizeof(UIState)},
    {UI_STATE_CAT_BUILTIN_ANIMATIONS, children_CAT_BUILTIN_ANIMATIONS, sizeof(children_CAT_BUILTIN_ANIMATIONS)/sizeof(UIState)},
    {UI_STATE_CAT_EXTERNAL_LED, children_CAT_EXTERNAL_LED, sizeof(children_CAT_EXTERNAL_LED)/sizeof(UIState)},
    {UI_STATE_CAT_RGB_REGIONS, children_CAT_RGB_REGIONS, sizeof(children_CAT_RGB_REGIONS)/sizeof(UIState)},
    {UI_STATE_CAT_ZONE_FREEZE, children_CAT_ZONE_FREEZE, sizeof(children_CAT_ZONE_FREEZE)/sizeof(UIState)},
    {UI_STATE_CAT_ZONE_COLD, children_CAT_ZONE_COLD, sizeof(children_CAT_ZONE_COLD)/sizeof(UIState)},
    {UI_STATE_CAT_ZONE_WARM, children_CAT_ZONE_WARM, sizeof(children_CAT_ZONE_WARM)/sizeof(UIState)},
    {UI_STATE_CAT_ZONE_HOT, children_CAT_ZONE_HOT, sizeof(children_CAT_ZONE_HOT)/sizeof(UIState)},
    {UI_STATE_CAT_ZONE_DANGER, children_CAT_ZONE_DANGER, sizeof(children_CAT_ZONE_DANGER)/sizeof(UIState)},
    {UI_STATE_CAT_DEVICE, children_CAT_DEVICE, sizeof(children_CAT_DEVICE)/sizeof(UIState)},
    {UI_STATE_CAT_SCREEN, children_CAT_SCREEN, sizeof(children_CAT_SCREEN)/sizeof(UIState)},
    {UI_STATE_CAT_CALIBRATION, children_CAT_CALIBRATION, sizeof(children_CAT_CALIBRATION)/sizeof(UIState)},
    {UI_STATE_CAT_POWER, children_CAT_POWER, sizeof(children_CAT_POWER)/sizeof(UIState)},
    {UI_STATE_CAT_ZIGBEE, children_CAT_ZIGBEE, sizeof(children_CAT_ZIGBEE)/sizeof(UIState)},
    {UI_STATE_CAT_ZIGBEE_REP, children_CAT_ZIGBEE_REP, sizeof(children_CAT_ZIGBEE_REP)/sizeof(UIState)},
    {UI_STATE_CAT_ENERGY, children_CAT_ENERGY, sizeof(children_CAT_ENERGY)/sizeof(UIState)},
    {UI_STATE_CAT_ADVANCED, children_CAT_ADVANCED, sizeof(children_CAT_ADVANCED)/sizeof(UIState)},
    {UI_STATE_CAT_MAINTENANCE, children_CAT_MAINTENANCE, sizeof(children_CAT_MAINTENANCE)/sizeof(UIState)},
};
static const uint8_t categoryCount = sizeof(categories)/sizeof(MenuCategoryDef);

// Node mapping for fast lookup
typedef struct {
    MenuNodeType type;
    UIState parent;
} MenuNodeInfo;

static MenuNodeInfo nodeRegistry[UI_STATE_COUNT];
static UIState rememberedChild[UI_STATE_COUNT];

void MenuNavigator_Init(void) {
    // Initialize all to LEAF and parent MONITOR by default as safe fallback
    for (int i = 0; i < UI_STATE_COUNT; i++) {
        nodeRegistry[i].type = MENU_NODE_LEAF;
        rememberedChild[i]=UI_STATE_COUNT;
        nodeRegistry[i].parent = UI_STATE_MONITOR;
    }

    // Set ROOT
    nodeRegistry[UI_STATE_MONITOR].type = MENU_NODE_ROOT;
    nodeRegistry[UI_STATE_MONITOR].parent = UI_STATE_COUNT; // No parent

    // Menu open sentinel (nothing selected): child of MONITOR, root-like for rotation
    nodeRegistry[UI_STATE_MENU_ROOT].type = MENU_NODE_ROOT;
    nodeRegistry[UI_STATE_MENU_ROOT].parent = UI_STATE_MONITOR;

    // Special types
    nodeRegistry[UI_STATE_BACK].type = MENU_NODE_BACK;
    nodeRegistry[UI_STATE_EXIT].type = MENU_NODE_EXIT;

    nodeRegistry[UI_STATE_RESTART].type = MENU_NODE_ACTION;
    nodeRegistry[UI_STATE_SHUTDOWN].type = MENU_NODE_ACTION;
    nodeRegistry[UI_STATE_FACTORY_RESET].type = MENU_NODE_ACTION;
    nodeRegistry[UI_STATE_ZIGBEE_PAIR].type = MENU_NODE_ACTION;

    // Traverse categories to set parent and category types
    for (int i = 0; i < categoryCount; i++) {
        if (categories[i].parent != UI_STATE_MONITOR) {
            nodeRegistry[categories[i].parent].type = MENU_NODE_CATEGORY;
        }
        
        for (int j = 0; j < categories[i].childCount; j++) {
            UIState child = categories[i].children[j];
            nodeRegistry[child].parent = categories[i].parent;
        }
    }
}

UIState MenuNavigator_GetParent(UIState node) {
    if ((unsigned)node >= UI_STATE_COUNT) return UI_STATE_COUNT;
    return nodeRegistry[node].parent;
}

static const MenuCategoryDef* GetCategoryDef(UIState parent) {
    for (int i = 0; i < categoryCount; i++) {
        if (categories[i].parent == parent) {
            return &categories[i];
        }
    }
    return NULL;
}

UIState MenuNavigator_GetFirstChild(UIState parent) {
    const MenuCategoryDef* cat = GetCategoryDef(parent);
    if (cat && cat->childCount > 0) {
        return cat->children[0];
    }
    return UI_STATE_COUNT;
}

UIState MenuNavigator_GetLastChild(UIState parent) {
    const MenuCategoryDef* cat = GetCategoryDef(parent);
    if (cat && cat->childCount > 0) {
        return cat->children[cat->childCount - 1];
    }
    return UI_STATE_COUNT;
}

UIState MenuNavigator_GetNextSiblingIn(UIState node, UIState parentContext, bool circular) {
    if (node == UI_STATE_MONITOR || (unsigned)node >= UI_STATE_COUNT) return node;

    /* Use the provided context; fall back to the registry when not given. */
    UIState parent = (parentContext != UI_STATE_COUNT)
                         ? parentContext
                         : nodeRegistry[node].parent;
    const MenuCategoryDef *cat = GetCategoryDef(parent);
    if (!cat) return node;

    for (int i = 0; i < cat->childCount; i++) {
        if (cat->children[i] == node) {
            if (i + 1 < cat->childCount) {
                return cat->children[i + 1];
            } else if (circular) {
                return cat->children[0];
            } else {
                return node;
            }
        }
    }
    return node;
}

UIState MenuNavigator_GetNextSibling(UIState node, bool circular) {
    return MenuNavigator_GetNextSiblingIn(node, UI_STATE_COUNT, circular);
}

UIState MenuNavigator_GetPrevSiblingIn(UIState node, UIState parentContext, bool circular) {
    if (node == UI_STATE_MONITOR || (unsigned)node >= UI_STATE_COUNT) return node;

    UIState parent = (parentContext != UI_STATE_COUNT)
                         ? parentContext
                         : nodeRegistry[node].parent;
    const MenuCategoryDef *cat = GetCategoryDef(parent);
    if (!cat) return node;

    for (int i = 0; i < cat->childCount; i++) {
        if (cat->children[i] == node) {
            if (i > 0) {
                return cat->children[i - 1];
            } else if (circular) {
                return cat->children[cat->childCount - 1];
            } else {
                return node;
            }
        }
    }
    return node;
}

UIState MenuNavigator_GetPrevSibling(UIState node, bool circular) {
    return MenuNavigator_GetPrevSiblingIn(node, UI_STATE_COUNT, circular);
}

MenuNodeType MenuNavigator_GetNodeType(UIState node) {
    if ((unsigned)node >= UI_STATE_COUNT) return MENU_NODE_LEAF;
    return nodeRegistry[node].type;
}

bool MenuNavigator_IsLeaf(UIState node) {
    if ((unsigned)node >= UI_STATE_COUNT) return false;
    return nodeRegistry[node].type == MENU_NODE_LEAF || nodeRegistry[node].type == MENU_NODE_ACTION;
}

UIState MenuNavigator_FirstVisible(UIState parent,const MenuVisibilityContext *context) {
    const MenuCategoryDef *category=GetCategoryDef(parent);
    if(category) for(unsigned i=0;i<category->childCount;++i)
        if(MenuNavigator_IsVisible(category->children[i],context)) return category->children[i];
    return UI_STATE_COUNT;
}
void MenuNavigator_RememberFocus(UIState parent,UIState child) {
    if(child==UI_STATE_BACK || child==UI_STATE_EXIT)return;
    const MenuCategoryDef *category=GetCategoryDef(parent);
    if(category) for(unsigned i=0;i<category->childCount;++i)
        if(category->children[i]==child) {rememberedChild[parent]=child;return;}
}
UIState MenuNavigator_PreferredChild(UIState parent,const MenuVisibilityContext *context) {
    if((unsigned)parent<UI_STATE_COUNT && (unsigned)rememberedChild[parent]<UI_STATE_COUNT &&
        MenuNavigator_IsVisible(rememberedChild[parent],context))return rememberedChild[parent];
    return MenuNavigator_FirstVisible(parent,context);
}
UIState MenuNavigator_LastVisible(UIState parent,const MenuVisibilityContext *context) {
    const MenuCategoryDef *category=GetCategoryDef(parent);
    if(category) for(unsigned i=category->childCount;i>0;--i)
        if(MenuNavigator_IsVisible(category->children[i-1],context)) return category->children[i-1];
    return UI_STATE_COUNT;
}
static UIState VisibleSibling(UIState node,UIState parent,bool circular,bool forward,const MenuVisibilityContext *context) {
    const MenuCategoryDef *category=GetCategoryDef(parent==UI_STATE_COUNT ? MenuNavigator_GetParent(node) : parent);
    if(!category) return node;
    int index=-1;
    for(unsigned i=0;i<category->childCount;++i) if(category->children[i]==node) {index=(int)i;break;}
    if(index<0) return node;
    for(unsigned attempt=0;attempt<category->childCount;++attempt) {
        index+=forward ? 1 : -1;
        if(index<0 || index>=category->childCount) {
            if(!circular) return node;
            index=forward ? 0 : category->childCount-1;
        }
        UIState candidate=category->children[index];
        if(MenuNavigator_IsVisible(candidate,context)) return candidate;
    }
    return node;
}
UIState MenuNavigator_NextVisible(UIState node,UIState parent,bool circular,const MenuVisibilityContext *context) {
    return VisibleSibling(node,parent,circular,true,context);
}
UIState MenuNavigator_PreviousVisible(UIState node,UIState parent,bool circular,const MenuVisibilityContext *context) {
    return VisibleSibling(node,parent,circular,false,context);
}
