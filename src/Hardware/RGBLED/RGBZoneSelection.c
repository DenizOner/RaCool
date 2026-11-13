#include "Hardware/RGBLED/RGBZoneSelection.h"
#include <stdatomic.h>
#include <math.h>
#include <string.h>
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
static atomic_uint draftVersion, draftState, draftXY;
/* Initial custom slot belongs to the single menu writer, not frame readers. */
static uint16_t initialX,initialY;
static int selectedSlot,slotCount;
int RGBZoneSelection_Zone(UIState state) {
    switch (state) {
    case UI_STATE_SET_RGB_COLD: return 0;
    case UI_STATE_SET_RGB_WARM: return 1;
    case UI_STATE_SET_RGB_HOT: return 2;
    case UI_STATE_SET_RGB_DANGER: return 3;
    case UI_STATE_SET_RGB_FREEZE: return 4;
    default: return -1;
    }
}
void RGBZoneSelection_Begin(UIState state, uint16_t x, uint16_t y) {
    initialX=x; initialY=y;
    selectedSlot=RGBLEDColorEngine_XYtoExactPreset(x,y);
    slotCount=RGB_COLOR_COUNT+(selectedSlot<0);
    if (selectedSlot<0) selectedSlot=RGB_COLOR_COUNT;
    /* Single menu writer; retain exact custom HA coordinates until Save. */
    atomic_fetch_add(&draftVersion,1);
    atomic_store(&draftXY,((uint32_t)y << 16) | x);
    atomic_store(&draftState,(unsigned)state+1u);
    atomic_fetch_add(&draftVersion,1);
}
bool RGBZoneSelection_Read(UIState state, uint16_t *x, uint16_t *y) {
    if (!x || !y) return false;
    for (unsigned attempt=0;attempt<3;++attempt) {
        unsigned first=atomic_load(&draftVersion);
        if (first & 1u) continue;
        unsigned owner=atomic_load(&draftState), value=atomic_load(&draftXY);
        if (first!=atomic_load(&draftVersion)) continue;
        if (owner!=(unsigned)state+1u) return false;
        *x=(uint16_t)value; *y=(uint16_t)(value>>16); return true;
    }
    return false;
}
void RGBZoneSelection_Clear(void) { atomic_store(&draftState,0); }

bool RGBZoneSelection_Rotate(UIState state,int direction,uint16_t *x,uint16_t *y) {
    if (!RGBZoneSelection_Read(state,x,y) || !slotCount) return false;
    selectedSlot=(selectedSlot+direction)%slotCount;
    if (selectedSlot<0) selectedSlot+=slotCount;
    if (selectedSlot==RGB_COLOR_COUNT) { *x=initialX; *y=initialY; }
    else RGBLEDColorEngine_PresetToXY((PresetColorIndex)selectedSlot,x,y);
    atomic_fetch_add(&draftVersion,1);
    atomic_store(&draftXY,((uint32_t)*y<<16)|*x);
    atomic_fetch_add(&draftVersion,1);
    return true;
}

