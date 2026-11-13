#ifndef MENU_RANGE_EDITOR_H
#define MENU_RANGE_EDITOR_H
#include "Controllers/MenuManager/MenuManagerTypes.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
/* Single menu writer, bounded immutable snapshots for the renderer. Values are deci-Celsius. */
int MenuRangeEditor_Zone(UIState state);
bool MenuRangeEditor_Bounds(UIState state,int16_t *minimum,int16_t *maximum);
bool MenuRangeEditor_Values(UIState state,const SystemSettings *settings,float *low,float *high,size_t *lowOffset,size_t *highOffset);
bool MenuRangeEditor_Begin(UIState state,const SystemSettings *settings);
bool MenuRangeEditor_Read(UIState state,int16_t *low,int16_t *high,unsigned *phase);
bool MenuRangeEditor_Rotate(UIState state,int direction);
bool MenuRangeEditor_Advance(UIState state);
bool MenuRangeEditor_Unchanged(UIState state,const SystemSettings *settings);
void MenuRangeEditor_Clear(void);
void MenuRangeEditor_Failed(UIState state);
#endif
