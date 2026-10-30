#ifndef PIR_H
#define PIR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t PIR_Sensor_Read(void);
uint8_t PIR_Sensor_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* PIR_H */
