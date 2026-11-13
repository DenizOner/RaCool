#include "Controllers/MenuManager/MenuRangeEditor.h"
#include <stdatomic.h>
#include <math.h>
#include <string.h>
static atomic_uint rangeVersion, rangeState, rangePair, rangePhase;
static int16_t initialLow, initialHigh;
static float initialLowValue,initialHighValue;
int MenuRangeEditor_Zone(UIState state) {
    return state==UI_STATE_SET_LOW_TEMP ? 3 : state==UI_STATE_FAN_DELTA_OFF ? 4 : state==UI_STATE_SET_RGB_TEMP_COLD ? 0 : state==UI_STATE_SET_RGB_TEMP_WARM ? 1 : state==UI_STATE_SET_RGB_TEMP_HOT ? 2 : -1;
}
bool MenuRangeEditor_Bounds(UIState state, int16_t *minimum, int16_t *maximum) {
    int zone=MenuRangeEditor_Zone(state);
    if (zone<0 || !minimum || !maximum) return false;
    const float lows[]={RGB_COLD_MIN_LIMIT,RGB_WARM_MIN_LIMIT,RGB_HOT_MIN_LIMIT,FAN_SETPOINT_MIN_C,FAN_DELTA_MIN_DECIC/10.0f};
    const float highs[]={RGB_COLD_MAX_LIMIT,RGB_WARM_MAX_LIMIT,RGB_HOT_MAX_LIMIT,FAN_SETPOINT_MAX_C,FAN_DELTA_MAX_DECIC/10.0f};
    *minimum=(int16_t)(lows[zone]*10); *maximum=(int16_t)(highs[zone]*10); return true;
}
bool MenuRangeEditor_Values(UIState state,const SystemSettings *settings,float *low,float *high,size_t *lowOffset,size_t *highOffset) {
    int zone=MenuRangeEditor_Zone(state);
    if (zone<0 || !settings || !low || !high || !lowOffset || !highOffset) return false;
    if (zone==3 || zone==4) {
        *lowOffset=zone==3 ? offsetof(ControlConfigs,relay_close_temp) : offsetof(ControlConfigs,fan_delta_off_decic);
        *highOffset=zone==3 ? offsetof(ControlConfigs,relay_open_temp) : offsetof(ControlConfigs,fan_delta_on_decic);
        *low=zone==3 ? settings->control.relay_close_temp : settings->control.fan_delta_off_decic/10.0f;
        *high=zone==3 ? settings->control.relay_open_temp : settings->control.fan_delta_on_decic/10.0f;
        return isfinite(*low) && isfinite(*high);
    }
    const HardwareConfigs *hardware=&settings->hardware;
    const size_t offsets[][2]={
        {offsetof(HardwareConfigs,ext_led_temp_cold_min),offsetof(HardwareConfigs,ext_led_temp_cold_max)},
        {offsetof(HardwareConfigs,ext_led_temp_warm_min),offsetof(HardwareConfigs,ext_led_temp_warm_max)},
        {offsetof(HardwareConfigs,ext_led_temp_hot_min),offsetof(HardwareConfigs,ext_led_temp_hot_max)}};
    *lowOffset=offsets[zone][0]; *highOffset=offsets[zone][1];
    memcpy(low,(const uint8_t *)hardware+*lowOffset,sizeof(*low));
    memcpy(high,(const uint8_t *)hardware+*highOffset,sizeof(*high));
    return isfinite(*low) && isfinite(*high);
}
static void PublishRange(UIState state,int16_t low,int16_t high,unsigned phase) {
    atomic_fetch_add(&rangeVersion,1);
    atomic_store(&rangePair,(uint16_t)low | ((uint32_t)(uint16_t)high<<16));
    atomic_store(&rangePhase,phase); atomic_store(&rangeState,(unsigned)state+1);
    atomic_fetch_add(&rangeVersion,1);
}
bool MenuRangeEditor_Begin(UIState state,const SystemSettings *settings) {
    float low,high; size_t a,b; int16_t minimum,maximum;
    if (!MenuRangeEditor_Values(state,settings,&low,&high,&a,&b) || !MenuRangeEditor_Bounds(state,&minimum,&maximum) ||
        low*10<minimum || high*10>maximum || high-low<0.099f) { MenuRangeEditor_Clear(); return false; }
    initialLowValue=low;initialHighValue=high;
    initialLow=(int16_t)lroundf(low*10); initialHigh=(int16_t)lroundf(high*10);
    PublishRange(state,initialLow,initialHigh,0); return true;
}
bool MenuRangeEditor_Read(UIState state,int16_t *low,int16_t *high,unsigned *phase) {
    if (!low || !high || !phase) return false;
    for (unsigned attempt=0;attempt<3;++attempt) {
        unsigned version=atomic_load(&rangeVersion);
        if (version&1) continue;
        unsigned owner=atomic_load(&rangeState),pair=atomic_load(&rangePair),selection=atomic_load(&rangePhase);
        if (version!=atomic_load(&rangeVersion)) continue;
        if (owner!=(unsigned)state+1) return false;
        *low=(int16_t)pair; *high=(int16_t)(pair>>16); *phase=selection; return true;
    }
    return false;
}
bool MenuRangeEditor_Rotate(UIState state,int direction) {
    int16_t low,high,minimum,maximum; unsigned phase;
    if (!MenuRangeEditor_Read(state,&low,&high,&phase) || !MenuRangeEditor_Bounds(state,&minimum,&maximum)) return false;
    if (phase>1) { PublishRange(state,low,high,direction<0 ? 1 : 0); return true; }
    int64_t next=(phase ? high : low)+(int64_t)direction;
    int lower=phase ? low+1 : minimum,upper=phase ? maximum : high-1;
    if (next<lower) next=lower;
    if (next>upper) next=upper;
    if (phase) high=(int16_t)next; else low=(int16_t)next;
    PublishRange(state,low,high,phase); return true;
}
bool MenuRangeEditor_Advance(UIState state) {
    int16_t low,high; unsigned phase;
    if (!MenuRangeEditor_Read(state,&low,&high,&phase)) return false;
    if (phase>=1) return true;
    PublishRange(state,low,high,phase+1); return false;
}
bool MenuRangeEditor_Unchanged(UIState state,const SystemSettings *settings) {
    float low,high; size_t a,b;
    return MenuRangeEditor_Values(state,settings,&low,&high,&a,&b) && low==initialLowValue && high==initialHighValue;
}
void MenuRangeEditor_Clear(void) { atomic_store(&rangeState,0); }

void MenuRangeEditor_Failed(UIState state) {
    int16_t low,high; unsigned phase;
    if (MenuRangeEditor_Read(state,&low,&high,&phase)) PublishRange(state,low,high,3);
}
