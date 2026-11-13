#ifndef RACOOL_FAN_SOURCE_ATTRIBUTES_H
#define RACOOL_FAN_SOURCE_ATTRIBUTES_H
#include <stdint.h>
/* EP1 / Thermostat UI Configuration. 0x8020..0x8040 are existing contracts. */
#define FAN_ATTR_MODE 0x8050u
#define FAN_ATTR_OBJECT_SOURCE 0x8051u
#define FAN_ATTR_AMBIENT_SOURCE 0x8052u
#define FAN_ATTR_DELTA_ON 0x8053u
#define FAN_ATTR_DELTA_OFF 0x8054u
#define FAN_ATTR_EXTERNAL_FIRST 0x8055u
#define FAN_ATTR_EFFECTIVE_OBJECT 0x8058u
#define FAN_ATTR_EFFECTIVE_AMBIENT 0x8059u
#define FAN_ATTR_DIFFERENCE 0x805au
#define FAN_ATTR_EFFECTIVE_OBJECT_SOURCE 0x805bu
#define FAN_ATTR_EFFECTIVE_AMBIENT_SOURCE 0x805cu
#define FAN_ATTR_OBJECT_STATUS 0x805du
#define FAN_ATTR_AMBIENT_STATUS 0x805eu
#define FAN_ATTR_SLOT_STATE_FIRST 0x805fu
#define EVENT_TEMPERATURE_SOURCES_UPDATED "TEMPERATURE_SOURCES_UPDATED"
#define FAN_SOURCE_DIAGNOSTIC_REFRESH_MS 10000u
typedef struct {
    int16_t temperatures[6]; /* EXT1..3, effective object/ambient, difference; INT16_MIN=invalid. */
    uint8_t sources; /* object low nibble, ambient high nibble */
    uint8_t status; /* object low nibble, ambient high nibble */
    uint8_t slot_states; /* two bits per external slot */
    uint8_t reserved;
} FanSourceDiagnostics;
#endif
