#ifndef TEST_USBH_CORE_H
#define TEST_USBH_CORE_H
#include <stdint.h>
#include <stddef.h>
#define __weak
typedef enum { USBH_OK, USBH_BUSY, USBH_FAIL } USBH_StatusTypeDef;
typedef enum { USBH_URB_IDLE, USBH_URB_DONE, USBH_URB_NOTREADY, USBH_URB_NYET, USBH_URB_ERROR, USBH_URB_STALL } USBH_URBStateTypeDef;
#define CMD_SEND 0
#define CTRL_DATA_OUT_WAIT 1
typedef struct {
 uint32_t Timer;
 uint8_t RequestState;
 struct { uint8_t state, pipe_out;
  struct { struct { uint8_t bmRequestType, bRequest; struct { uint16_t w; } wValue, wIndex, wLength; } b; } setup;
 } Control;
} USBH_HandleTypeDef;
typedef struct { uint8_t unused; } USBH_ClassTypeDef;
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *, uint8_t);
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *, uint8_t);
#endif
