#include "Controllers/MenuManager/MenuEditSession.h"
#include <freertos/FreeRTOS.h>
#include <string.h>

#define MENU_EDIT_FIELD_CAPACITY 8U
typedef struct { size_t offset,size; } EditedField;
typedef struct {
    bool active,fanRange,committing,cancelRequested;
    UIState state;
    IConfigurationManager *owner;
    SystemSettings initial,draft;
    unsigned block,count;
    EditedField fields[MENU_EDIT_FIELD_CAPACITY];
} EditSession;
static EditSession session;
/* Reserved by session.committing until the synchronous owner call drains.
 * UI readers keep using session; nested saves cannot overwrite this payload. */
static EditSession commitSnapshot;
static portMUX_TYPE editLock=portMUX_INITIALIZER_UNLOCKED;
static UIState errorState=UI_STATE_COUNT;
static esp_err_t lastError;
static const char *const blockNames[]={"system","control","hardware","calibration","zigbee_reporting","localization"};

static void *Block(SystemSettings *settings,unsigned block,size_t *size) {
    switch(block) {
    case 0: *size=sizeof(settings->system);return &settings->system;
    case 1: *size=sizeof(settings->control);return &settings->control;
    case 2: *size=sizeof(settings->hardware);return &settings->hardware;
    case 3: *size=sizeof(settings->calibration);return &settings->calibration;
    case 4: *size=sizeof(settings->zigbee_reporting);return &settings->zigbee_reporting;
    case 5: *size=sizeof(settings->localization);return &settings->localization;
    default: *size=0;return NULL;
    }
}
bool MenuEditSession_Supports(UIState state) {
    switch(state) {
    case UI_STATE_SET_LOW_TEMP: case UI_STATE_FAN_DELTA_OFF:
    case UI_STATE_SET_RGB_TEMP_COLD: case UI_STATE_SET_RGB_TEMP_WARM: case UI_STATE_SET_RGB_TEMP_HOT:
    case UI_STATE_SET_RGB_COLD: case UI_STATE_SET_RGB_WARM: case UI_STATE_SET_RGB_HOT:
    case UI_STATE_SET_RGB_DANGER: case UI_STATE_SET_RGB_FREEZE:
    case UI_STATE_FAN_MODE: case UI_STATE_SET_TEMP_UNIT:
    case UI_STATE_CALIBRATE_OBJECT: case UI_STATE_CALIBRATE_AMBIENT:
    case UI_STATE_SET_BUILTIN_MODE: case UI_STATE_SET_BUILTIN_BRIGHTNESS:
    case UI_STATE_SET_BUILTIN_STARTUP: case UI_STATE_SET_BUILTIN_IDLE:
    case UI_STATE_SET_BUILTIN_CONNECTING: case UI_STATE_SET_BUILTIN_FAULT:
    case UI_STATE_SET_LED_MODE: case UI_STATE_SET_LED_ANIM: case UI_STATE_SET_RGB_BRIGHTNESS:
    case UI_STATE_SET_RGB_TEMP_FREEZE: case UI_STATE_SET_RGB_TEMP_DANGER:
    case UI_STATE_SET_DISPLAY_MODE: case UI_STATE_SET_TIMEOUT: case UI_STATE_SET_MENU_AUTO_EXIT: case UI_STATE_SET_LANGUAGE:
    case UI_STATE_SET_POWER_PROFILE: case UI_STATE_SET_ZIGBEE_TX_PROFILE: case UI_STATE_SET_TELEMETRY:
    case UI_STATE_ZB_REP_FAN: case UI_STATE_ZB_REP_AMBIENT: case UI_STATE_ZB_REP_CPU:
    case UI_STATE_ZB_REP_HEALTH: case UI_STATE_ZB_REP_MEMORY: case UI_STATE_ZB_REP_UPTIME:
        return true;
    default:return false;
    }
}
bool MenuEditSession_Begin(UIState state,IConfigurationManager *owner,const SystemSettings *snapshot) {
    if (!MenuEditSession_Supports(state) || !owner || !owner->UpdateFieldsIfUnchanged || !snapshot) return false;
    portENTER_CRITICAL(&editLock);
    bool available=!session.active;
    if(available) {
        session=(EditSession){.active=true,.state=state,.owner=owner,.initial=*snapshot,.draft=*snapshot};
        session.fanRange=state==UI_STATE_SET_LOW_TEMP || state==UI_STATE_FAN_DELTA_OFF ||
            state==UI_STATE_SET_RGB_TEMP_COLD || state==UI_STATE_SET_RGB_TEMP_WARM || state==UI_STATE_SET_RGB_TEMP_HOT ||
            state==UI_STATE_SET_RGB_COLD || state==UI_STATE_SET_RGB_WARM || state==UI_STATE_SET_RGB_HOT ||
            state==UI_STATE_SET_RGB_DANGER || state==UI_STATE_SET_RGB_FREEZE;
        errorState=state;lastError=ESP_OK;
        if(state==UI_STATE_FAN_MODE) {
            session.block=1;session.count=3;
            session.fields[0]=(EditedField){offsetof(ControlConfigs,fan_manual_mode),sizeof(snapshot->control.fan_manual_mode)};
            session.fields[1]=(EditedField){offsetof(ControlConfigs,fan_manual_state),sizeof(snapshot->control.fan_manual_state)};
            session.fields[2]=(EditedField){offsetof(ControlConfigs,fan_strategy),sizeof(snapshot->control.fan_strategy)};
        }
    }
    portEXIT_CRITICAL(&editLock);
    return available;
}
static esp_err_t DraftRead(IConfigurationManager *self,SystemSettings *output);
static esp_err_t DraftWrite(IConfigurationManager *self,const char *name,const ConfigurationFieldUpdate *fields,size_t count,bool persist);
static IConfigurationManager facade={.GetSnapshot=DraftRead,.UpdateFields=DraftWrite};
IConfigurationManager *MenuEditSession_Config(IConfigurationManager *owner) {
    portENTER_CRITICAL(&editLock);
    bool use=session.active && owner==session.owner;
    portEXIT_CRITICAL(&editLock);
    return use ? &facade : owner;
}
static esp_err_t DraftRead(IConfigurationManager *self,SystemSettings *output) {
    if(self!=&facade || !output)return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&editLock);
    bool active=session.active;
    if(active)*output=session.draft;
    portEXIT_CRITICAL(&editLock);
    return active ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static esp_err_t DraftWrite(IConfigurationManager *self,const char *name,const ConfigurationFieldUpdate *fields,size_t count,bool persist) {
    (void)persist;
    if(self!=&facade || !name || !fields || !count || count>MENU_EDIT_FIELD_CAPACITY)return ESP_ERR_INVALID_ARG;
    for(size_t i=0;i<count;++i)for(size_t j=0;j<i;++j) {
        if(fields[i].offset>SIZE_MAX-fields[i].size || fields[j].offset>SIZE_MAX-fields[j].size ||
           (fields[i].offset<fields[j].offset+fields[j].size && fields[j].offset<fields[i].offset+fields[i].size))return ESP_ERR_INVALID_ARG;
    }
    unsigned block=0;
    while(block<sizeof(blockNames)/sizeof(blockNames[0]) && strcmp(name,blockNames[block]))++block;
    portENTER_CRITICAL(&editLock);
    size_t size;uint8_t *data=Block(&session.draft,block,&size);
    EditedField staged[MENU_EDIT_FIELD_CAPACITY];
    unsigned stagedCount=session.count;
    memcpy(staged,session.fields,sizeof(staged));
    esp_err_t result=ESP_OK;
    if(!session.active || session.committing || !data || (stagedCount && session.block!=block))result=ESP_ERR_INVALID_STATE;
    for(size_t i=0;i<count && result==ESP_OK;++i) {
        if(!fields[i].data || !fields[i].size || fields[i].offset>=size || fields[i].size>size-fields[i].offset) {result=ESP_ERR_INVALID_ARG;break;}
        unsigned slot=0;
        for(;slot<stagedCount;++slot) {
            const EditedField *previous=&staged[slot];
            if(previous->offset==fields[i].offset && previous->size==fields[i].size)break;
            if(previous->offset<fields[i].offset+fields[i].size && fields[i].offset<previous->offset+previous->size) {result=ESP_ERR_INVALID_ARG;break;}
        }
        if(result!=ESP_OK)break;
        if(slot==stagedCount) {
            if(stagedCount==MENU_EDIT_FIELD_CAPACITY) {result=ESP_ERR_NO_MEM;break;}
            staged[stagedCount++]=(EditedField){fields[i].offset,fields[i].size};
        }
    }
    /* Validate the entire batch before copying any caller bytes. */
    if(result==ESP_OK) {
        for(size_t i=0;i<count;++i)memcpy(data+fields[i].offset,fields[i].data,fields[i].size);
        session.block=block;session.count=stagedCount;
        memcpy(session.fields,staged,sizeof(staged));lastError=ESP_OK;
    }
    bool commit=result==ESP_OK && session.fanRange;
    portEXIT_CRITICAL(&editLock);
    return commit ? MenuEditSession_Commit() : result;
}

bool MenuEditSession_Overlay(SystemSettings *snapshot) {
    if(!snapshot)return false;
    portENTER_CRITICAL(&editLock);
    bool active=session.active;
    if(active)*snapshot=session.draft;
    portEXIT_CRITICAL(&editLock);
    return active;
}
UIState MenuEditSession_State(void) {
    portENTER_CRITICAL(&editLock);
    UIState state=session.active ? session.state : UI_STATE_COUNT;
    portEXIT_CRITICAL(&editLock);
    return state;
}
bool MenuEditSession_AutoCommit(void) {
    portENTER_CRITICAL(&editLock);bool result=session.active && session.fanRange;portEXIT_CRITICAL(&editLock);return result;
}
UIState MenuEditSession_FanRange(void) {
    portENTER_CRITICAL(&editLock);
    UIState next=UI_STATE_COUNT;
    if(session.active && session.state==UI_STATE_FAN_MODE && !session.draft.control.fan_manual_mode) {
        next=session.draft.control.fan_strategy ? UI_STATE_FAN_DELTA_OFF : UI_STATE_SET_LOW_TEMP;
        session.state=next;session.fanRange=true;
    }
    portEXIT_CRITICAL(&editLock);
    return next;
}
esp_err_t MenuEditSession_Commit(void) {
    portENTER_CRITICAL(&editLock);
    if(session.committing) {portEXIT_CRITICAL(&editLock);return ESP_ERR_NOT_FINISHED;}
    if(!session.active || !session.owner || !session.owner->UpdateFieldsIfUnchanged) {
        portEXIT_CRITICAL(&editLock);return ESP_ERR_INVALID_STATE;
    }
    session.committing=true;
    commitSnapshot=session;
    portEXIT_CRITICAL(&editLock);
    const EditSession *copy=&commitSnapshot;
    if(!copy->count) {
        portENTER_CRITICAL(&editLock);session.active=false;session.committing=false;portEXIT_CRITICAL(&editLock);
        return ESP_OK;
    }
    size_t size;const uint8_t *initial=Block(&commitSnapshot.initial,copy->block,&size),*draft=Block(&commitSnapshot.draft,copy->block,&size);
    ConfigurationFieldUpdate fields[MENU_EDIT_FIELD_CAPACITY];
    const void *expected[MENU_EDIT_FIELD_CAPACITY];
    for(unsigned i=0;i<copy->count;++i) {
        fields[i]=(ConfigurationFieldUpdate){copy->fields[i].offset,draft+copy->fields[i].offset,copy->fields[i].size};
        expected[i]=initial+copy->fields[i].offset;
    }
    esp_err_t result=copy->owner->UpdateFieldsIfUnchanged(copy->owner,blockNames[copy->block],fields,expected,copy->count,true);
    portENTER_CRITICAL(&editLock);
    errorState=copy->state;lastError=result;
    if(result==ESP_OK || session.cancelRequested)session.active=false;
    session.committing=false;
    portEXIT_CRITICAL(&editLock);
    return result;
}

void MenuEditSession_Cancel(void) {
    portENTER_CRITICAL(&editLock);
    if(session.committing)session.cancelRequested=true;
    else session.active=false;
    portEXIT_CRITICAL(&editLock);
}
void MenuEditSession_ReportResult(UIState state,esp_err_t result) {
    portENTER_CRITICAL(&editLock);errorState=state;lastError=result;portEXIT_CRITICAL(&editLock);
}
esp_err_t MenuEditSession_Result(UIState state) {
    portENTER_CRITICAL(&editLock);esp_err_t result=errorState==state ? lastError : ESP_OK;portEXIT_CRITICAL(&editLock);
    return result;
}

bool MenuEditSession_Merge(SystemSettings *snapshot) {
    if(!snapshot)return false;
    portENTER_CRITICAL(&editLock);
    bool active=session.active;
    if(active && session.count) {
        size_t size;
        uint8_t *destination=Block(snapshot,session.block,&size);
        const uint8_t *source=Block(&session.draft,session.block,&size);
        for(unsigned i=0;i<session.count;++i)
            memcpy(destination+session.fields[i].offset,source+session.fields[i].offset,session.fields[i].size);
    }
    portEXIT_CRITICAL(&editLock);
    return active;
}
