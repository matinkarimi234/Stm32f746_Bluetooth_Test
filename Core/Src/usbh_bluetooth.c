#include "usbh_bluetooth.h"
#include "usbh_pipes.h"
#include "usbh_ioreq.h"

volatile uint8_t BT_InterfaceFound = 0;

volatile BT_EndpointInfo_t BT_Endpoints = {0};

static uint8_t BT_EventRxBuffer[BT_EVENT_BUFFER_SIZE];

volatile uint8_t BT_EventRxState = BT_EVENT_RX_SUBMIT;

volatile uint32_t BT_EventSubmitCount = 0;
volatile uint32_t BT_EventPacketCount = 0;
volatile uint32_t BT_EventNAKCount = 0;
volatile uint32_t BT_EventErrorCount = 0;

volatile uint16_t BT_LastEventLength = 0;
volatile uint8_t BT_LastEventData[BT_EVENT_BUFFER_SIZE];

static uint32_t BT_EventLastTick = 0;

static uint8_t BT_HCI_ResetCmd[3] =
{
    0x03U,  // Opcode LSB
    0x0CU,  // Opcode MSB
    0x00U   // Parameter length
};

volatile uint8_t BT_HCI_ResetState = BT_HCI_RESET_IDLE;

volatile uint8_t BT_HCI_ResetUSBComplete = 0U;
volatile uint8_t BT_HCI_ResetEventSeen = 0U;
volatile uint8_t BT_HCI_ResetStatus = 0xFFU;

static uint32_t BT_HCI_ResetStartTick = 0U;


volatile BT_VersionInfo_t BT_VersionInfo = {0};

volatile uint8_t BT_VersionState = BT_VERSION_WAIT_RESET;
volatile uint8_t BT_VersionUSBComplete = 0U;
volatile uint8_t BT_VersionEventSeen = 0U;
volatile uint8_t BT_VersionStatus = 0xFFU;

static uint32_t BT_VersionStartTick = 0U;

static uint8_t BT_ReadVersionCmd[3] =
{
    0x01U,  // Opcode LSB
    0x10U,  // Opcode MSB
    0x00U   // No parameters
};


volatile uint8_t BT_AddrState = BT_ADDR_WAIT_VERSION;
volatile uint8_t BT_AddrUSBComplete = 0U;
volatile uint8_t BT_AddrEventSeen = 0U;
volatile uint8_t BT_AddrStatus = 0xFFU;

volatile uint8_t BT_BD_ADDR[6] = {0};

static uint32_t BT_AddrStartTick = 0U;

static uint8_t BT_ReadAddressCmd[3] =
{
    0x09U,  // Opcode LSB
    0x10U,  // Opcode MSB
    0x00U   // Parameter length
};


static void BT_ClearEndpoints(void)
{
    BT_Endpoints.EventInAddr = 0;
    BT_Endpoints.EventInMPS = 0;
    BT_Endpoints.EventInterval = 0;

    BT_Endpoints.ACLInAddr = 0;
    BT_Endpoints.ACLInMPS = 0;

    BT_Endpoints.ACLOutAddr = 0;
    BT_Endpoints.ACLOutMPS = 0;

    BT_Endpoints.EndpointsReady = 0;
	
		BT_Endpoints.EventInPipe = 0xFFU;
		BT_Endpoints.ACLInPipe  = 0xFFU;
		BT_Endpoints.ACLOutPipe = 0xFFU;

		BT_Endpoints.PipesReady = 0U;
}


static void BT_ResetEventRx(void)
{
    uint16_t i;

    BT_EventRxState = BT_EVENT_RX_SUBMIT;

    BT_EventSubmitCount = 0;
    BT_EventPacketCount = 0;
    BT_EventNAKCount = 0;
    BT_EventErrorCount = 0;

    BT_LastEventLength = 0;
    BT_EventLastTick = 0;

    for (i = 0; i < BT_EVENT_BUFFER_SIZE; i++)
    {
        BT_EventRxBuffer[i] = 0;
        BT_LastEventData[i] = 0;
    }
}

static void BT_ResetVersionState(void)
{
    BT_VersionState = BT_VERSION_WAIT_RESET;
    BT_VersionUSBComplete = 0U;
    BT_VersionEventSeen = 0U;
    BT_VersionStatus = 0xFFU;
    BT_VersionStartTick = 0U;

    BT_VersionInfo.HCI_Version = 0U;
    BT_VersionInfo.HCI_Revision = 0U;
    BT_VersionInfo.LMP_Version = 0U;
    BT_VersionInfo.Manufacturer = 0U;
    BT_VersionInfo.LMP_Subversion = 0U;
}


static void BT_ReleasePipes(USBH_HandleTypeDef *phost, uint8_t close_pipes)
{
    if (BT_Endpoints.EventInPipe != 0xFFU)
    {
        if (close_pipes != 0U)
            USBH_ClosePipe(phost, BT_Endpoints.EventInPipe);

        USBH_FreePipe(phost, BT_Endpoints.EventInPipe);
        BT_Endpoints.EventInPipe = 0xFFU;
    }

    if (BT_Endpoints.ACLInPipe != 0xFFU)
    {
        if (close_pipes != 0U)
            USBH_ClosePipe(phost, BT_Endpoints.ACLInPipe);

        USBH_FreePipe(phost, BT_Endpoints.ACLInPipe);
        BT_Endpoints.ACLInPipe = 0xFFU;
    }

    if (BT_Endpoints.ACLOutPipe != 0xFFU)
    {
        if (close_pipes != 0U)
            USBH_ClosePipe(phost, BT_Endpoints.ACLOutPipe);

        USBH_FreePipe(phost, BT_Endpoints.ACLOutPipe);
        BT_Endpoints.ACLOutPipe = 0xFFU;
    }

    BT_Endpoints.PipesReady = 0U;
}

static void BT_ResetAddressState(void)
{
    uint8_t i;

    BT_AddrState = BT_ADDR_WAIT_VERSION;
    BT_AddrUSBComplete = 0U;
    BT_AddrEventSeen = 0U;
    BT_AddrStatus = 0xFFU;
    BT_AddrStartTick = 0U;

    for (i = 0; i < 6U; i++)
    {
        BT_BD_ADDR[i] = 0U;
    }
}


static USBH_StatusTypeDef BT_Init(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_DeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_Request(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef BT_SOF(USBH_HandleTypeDef *phost);

USBH_ClassTypeDef USBH_Bluetooth_Class =
{
    "BLUETOOTH",
    USB_BT_CLASS,
    BT_Init,
    BT_DeInit,
    BT_Request,
    BT_Process,
    BT_SOF,
    NULL
};

static void BT_ResetHCIState(void)
{
    BT_HCI_ResetState = BT_HCI_RESET_IDLE;

    BT_HCI_ResetUSBComplete = 0U;
    BT_HCI_ResetEventSeen = 0U;
    BT_HCI_ResetStatus = 0xFFU;

    BT_HCI_ResetStartTick = 0U;
}


static USBH_StatusTypeDef BT_Init(USBH_HandleTypeDef *phost)
{
    uint8_t interface;
    uint8_t i;
    uint8_t count;

    USBH_InterfaceDescTypeDef *itf;
    USBH_EpDescTypeDef *ep;

    uint8_t addr;
    uint8_t type;
    uint16_t mps;

    BT_InterfaceFound = 0;
    BT_ClearEndpoints();
		BT_ResetEventRx();

    interface = USBH_FindInterface(
        phost,
        USB_BT_CLASS,
        USB_BT_SUBCLASS,
        USB_BT_PROTOCOL
    );

    if (interface == 0xFFU)
        return USBH_FAIL;

    if (USBH_SelectInterface(phost, interface) != USBH_OK)
        return USBH_FAIL;

    BT_InterfaceFound = 1;

    itf = &phost->device.CfgDesc.Itf_Desc[interface];

    count = itf->bNumEndpoints;

    if (count > USBH_MAX_NUM_ENDPOINTS)
        count = USBH_MAX_NUM_ENDPOINTS;

    for (i = 0; i < count; i++)
    {
        ep = &itf->Ep_Desc[i];

        addr = ep->bEndpointAddress;
        type = ep->bmAttributes & 0x03U;
        mps = ep->wMaxPacketSize & 0x07FFU;

        // Interrupt IN - HCI events
        if ((type == 0x03U) && ((addr & 0x80U) != 0U))
        {
            BT_Endpoints.EventInAddr = addr;
            BT_Endpoints.EventInMPS = mps;
            BT_Endpoints.EventInterval = ep->bInterval;
        }

        // Bulk IN - ACL reception
        else if ((type == 0x02U) && ((addr & 0x80U) != 0U))
        {
            BT_Endpoints.ACLInAddr = addr;
            BT_Endpoints.ACLInMPS = mps;
        }

        // Bulk OUT - ACL transmission
        else if ((type == 0x02U) && ((addr & 0x80U) == 0U))
        {
            BT_Endpoints.ACLOutAddr = addr;
            BT_Endpoints.ACLOutMPS = mps;
        }
    }


    if ((BT_Endpoints.EventInAddr != 0U) &&
        (BT_Endpoints.ACLInAddr != 0U) &&
        (BT_Endpoints.ACLOutAddr != 0U) &&
        (BT_Endpoints.EventInMPS != 0U) &&
        (BT_Endpoints.ACLInMPS != 0U) &&
        (BT_Endpoints.ACLOutMPS != 0U))
    {
        BT_Endpoints.EndpointsReady = 1U;
    }
    else
    {
        BT_InterfaceFound = 0U;
        return USBH_FAIL;
    }

    // Allocate the three host pipes
    BT_Endpoints.EventInPipe =
        USBH_AllocPipe(phost, BT_Endpoints.EventInAddr);

    BT_Endpoints.ACLInPipe =
        USBH_AllocPipe(phost, BT_Endpoints.ACLInAddr);

    BT_Endpoints.ACLOutPipe =
        USBH_AllocPipe(phost, BT_Endpoints.ACLOutAddr);

    // 0xFF means no free pipe was available
    if ((BT_Endpoints.EventInPipe == 0xFFU) ||
        (BT_Endpoints.ACLInPipe == 0xFFU) ||
        (BT_Endpoints.ACLOutPipe == 0xFFU))
    {
        BT_ReleasePipes(phost, 0U);
        BT_InterfaceFound = 0U;
        return USBH_FAIL;
    }

    // HCI events: Interrupt IN
    USBH_OpenPipe(phost,
                  BT_Endpoints.EventInPipe,
                  BT_Endpoints.EventInAddr,
                  phost->device.address,
                  phost->device.speed,
                  USB_EP_TYPE_INTR,
                  BT_Endpoints.EventInMPS);

    // ACL receive: Bulk IN
    USBH_OpenPipe(phost,
                  BT_Endpoints.ACLInPipe,
                  BT_Endpoints.ACLInAddr,
                  phost->device.address,
                  phost->device.speed,
                  USB_EP_TYPE_BULK,
                  BT_Endpoints.ACLInMPS);

    // ACL transmit: Bulk OUT
    USBH_OpenPipe(phost,
                  BT_Endpoints.ACLOutPipe,
                  BT_Endpoints.ACLOutAddr,
                  phost->device.address,
                  phost->device.speed,
                  USB_EP_TYPE_BULK,
                  BT_Endpoints.ACLOutMPS);

    // Start USB data sequencing at DATA0
    USBH_LL_SetToggle(phost, BT_Endpoints.EventInPipe, 0U);
    USBH_LL_SetToggle(phost, BT_Endpoints.ACLInPipe, 0U);
    USBH_LL_SetToggle(phost, BT_Endpoints.ACLOutPipe, 0U);
		
		BT_ResetHCIState();
		BT_ResetVersionState();
		BT_ResetAddressState();
		
    BT_Endpoints.PipesReady = 1U;
		BT_HCI_ResetState = BT_HCI_RESET_SEND;

    return USBH_OK;

}


static USBH_StatusTypeDef BT_DeInit(USBH_HandleTypeDef *phost)
{
    BT_ReleasePipes(phost, BT_Endpoints.PipesReady);

    BT_InterfaceFound = 0U;
    BT_ClearEndpoints();
		BT_ResetEventRx();
		BT_ResetHCIState();
		BT_ResetVersionState();
		BT_ResetAddressState();

    return USBH_OK;
}

static USBH_StatusTypeDef BT_Request(USBH_HandleTypeDef *phost)
{
    (void)phost;
    return USBH_OK;
}


static void BT_ProcessReadAddress(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;

    switch (BT_AddrState)
    {
        case BT_ADDR_WAIT_VERSION:

            if (BT_VersionState == BT_VERSION_OK)
            {
                BT_AddrState = BT_ADDR_SEND;
            }
            else if (BT_VersionState == BT_VERSION_ERROR)
            {
                BT_AddrState = BT_ADDR_ERROR;
            }
            break;

        case BT_ADDR_SEND:

            if (phost->RequestState != CMD_SEND)
                break;

            phost->Control.setup.b.bmRequestType = 0x20U;
            phost->Control.setup.b.bRequest = 0x00U;
            phost->Control.setup.b.wValue.w = 0U;
            phost->Control.setup.b.wIndex.w = 0U;
            phost->Control.setup.b.wLength.w = 3U;

            BT_AddrStartTick = phost->Timer;
            BT_AddrState = BT_ADDR_USB_WAIT;
            break;

        case BT_ADDR_USB_WAIT:

            status = USBH_CtlReq(
                phost,
                BT_ReadAddressCmd,
                sizeof(BT_ReadAddressCmd)
            );

            if (status == USBH_OK)
            {
                BT_AddrUSBComplete = 1U;
                BT_AddrStartTick = phost->Timer;
                BT_AddrState = BT_ADDR_EVENT_WAIT;
            }
            else if (status != USBH_BUSY)
            {
                BT_AddrState = BT_ADDR_ERROR;
            }
            else if ((phost->Timer - BT_AddrStartTick)
                     >= 2000U)
            {
                BT_AddrState = BT_ADDR_ERROR;
            }
            break;

        case BT_ADDR_EVENT_WAIT:

            if (BT_AddrEventSeen != 0U)
            {
                if (BT_AddrStatus == 0U)
                    BT_AddrState = BT_ADDR_OK;
                else
                    BT_AddrState = BT_ADDR_ERROR;
            }
            else if ((phost->Timer - BT_AddrStartTick)
                     >= 2000U)
            {
                BT_AddrState = BT_ADDR_ERROR;
            }
            break;

        case BT_ADDR_OK:
        case BT_ADDR_ERROR:
        default:
            break;
    }
}



static void BT_ProcessHCIReset(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;

    switch (BT_HCI_ResetState)
    {
        case BT_HCI_RESET_IDLE:
            break;

        case BT_HCI_RESET_SEND:

            // Only start when the control pipe is idle.
            if (phost->RequestState != CMD_SEND)
                break;

            // USB class-specific, Host-to-Device request.
            phost->Control.setup.b.bmRequestType = 0x20U;
            phost->Control.setup.b.bRequest = 0x00U;
            phost->Control.setup.b.wValue.w = 0U;
            phost->Control.setup.b.wIndex.w = 0U;
            phost->Control.setup.b.wLength.w = 3U;

            BT_HCI_ResetState = BT_HCI_RESET_USB_WAIT;
            break;

        case BT_HCI_RESET_USB_WAIT:

            // Nonblocking control transfer on EP0.
            // Must be called repeatedly while BUSY.
            status = USBH_CtlReq(
                phost,
                BT_HCI_ResetCmd,
                sizeof(BT_HCI_ResetCmd)
            );

            if (status == USBH_OK)
            {
                BT_HCI_ResetUSBComplete = 1U;

                BT_HCI_ResetStartTick = phost->Timer;
                BT_HCI_ResetState = BT_HCI_RESET_EVENT_WAIT;
            }
            else if (status != USBH_BUSY)
            {
                BT_HCI_ResetState = BT_HCI_RESET_ERROR;
            }

            break;

        case BT_HCI_RESET_EVENT_WAIT:

            if (BT_HCI_ResetEventSeen != 0U)
            {
                if (BT_HCI_ResetStatus == 0U)
                {
                    BT_HCI_ResetState = BT_HCI_RESET_OK;
                }
                else
                {
                    BT_HCI_ResetState = BT_HCI_RESET_ERROR;
                }
            }
            else if ((phost->Timer - BT_HCI_ResetStartTick)
                     >= 2000U)
            {
                // HCI event timeout
                BT_HCI_ResetState = BT_HCI_RESET_ERROR;
            }

            break;

        case BT_HCI_RESET_OK:
            // HCI Reset succeeded.
            break;

        case BT_HCI_RESET_ERROR:
            // Preserve failure state for debugging.
            break;

        default:
            BT_HCI_ResetState = BT_HCI_RESET_ERROR;
            break;
    }
}


static void BT_ProcessReadVersion(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;

    switch (BT_VersionState)
    {
        case BT_VERSION_WAIT_RESET:

            if (BT_HCI_ResetState == BT_HCI_RESET_OK)
            {
                BT_VersionState = BT_VERSION_SEND;
            }
            break;

        case BT_VERSION_SEND:

            if (phost->RequestState != CMD_SEND)
                break;

            phost->Control.setup.b.bmRequestType = 0x20U;
            phost->Control.setup.b.bRequest = 0x00U;
            phost->Control.setup.b.wValue.w = 0U;
            phost->Control.setup.b.wIndex.w = 0U;
            phost->Control.setup.b.wLength.w = 3U;

            BT_VersionStartTick = phost->Timer;
            BT_VersionState = BT_VERSION_USB_WAIT;
            break;

        case BT_VERSION_USB_WAIT:

            status = USBH_CtlReq(
                phost,
                BT_ReadVersionCmd,
                sizeof(BT_ReadVersionCmd)
            );

            if (status == USBH_OK)
            {
                BT_VersionUSBComplete = 1U;
                BT_VersionStartTick = phost->Timer;
                BT_VersionState = BT_VERSION_EVENT_WAIT;
            }
            else if (status != USBH_BUSY)
            {
                BT_VersionState = BT_VERSION_ERROR;
            }
            else if ((phost->Timer - BT_VersionStartTick) >= 2000U)
            {
                BT_VersionState = BT_VERSION_ERROR;
            }
            break;

        case BT_VERSION_EVENT_WAIT:

            if (BT_VersionEventSeen != 0U)
            {
                if (BT_VersionStatus == 0U)
                    BT_VersionState = BT_VERSION_OK;
                else
                    BT_VersionState = BT_VERSION_ERROR;
            }
            else if ((phost->Timer - BT_VersionStartTick) >= 2000U)
            {
                BT_VersionState = BT_VERSION_ERROR;
            }
            break;

        case BT_VERSION_OK:
        case BT_VERSION_ERROR:
        default:
            break;
    }
}



static USBH_StatusTypeDef BT_Process(USBH_HandleTypeDef *phost)
{
    USBH_URBStateTypeDef urb;
    USBH_StatusTypeDef status;

    uint32_t length;
    uint32_t i;
    uint32_t interval;

    if (BT_Endpoints.PipesReady == 0U)
        return USBH_OK;

    interval = BT_Endpoints.EventInterval;

    if (interval == 0U)
        interval = 1U;

    switch (BT_EventRxState)
    {
        // ------------------------------------
        // Submit interrupt IN transfer
        // ------------------------------------
        case BT_EVENT_RX_SUBMIT:

            if ((BT_Endpoints.EventInMPS == 0U) ||
                (BT_Endpoints.EventInMPS > BT_EVENT_BUFFER_SIZE))
            {
                BT_EventErrorCount++;
                BT_EventRxState = BT_EVENT_RX_ERROR;
                break;
            }

            status = USBH_InterruptReceiveData(
                phost,
                BT_EventRxBuffer,
                (uint8_t)BT_Endpoints.EventInMPS,
                BT_Endpoints.EventInPipe
            );

            if (status == USBH_OK)
            {
                BT_EventSubmitCount++;
                BT_EventRxState = BT_EVENT_RX_WAIT;
            }
            else
            {
                BT_EventErrorCount++;
                BT_EventLastTick = phost->Timer;
                BT_EventRxState = BT_EVENT_RX_DELAY;
            }

            break;

        // ------------------------------------
        // Check USB transfer result
        // ------------------------------------
        case BT_EVENT_RX_WAIT:

            urb = USBH_LL_GetURBState(
                phost,
                BT_Endpoints.EventInPipe
            );

            if (urb == USBH_URB_DONE)
            {
                length = USBH_LL_GetLastXferSize(
                    phost,
                    BT_Endpoints.EventInPipe
                );

                if (length > BT_EVENT_BUFFER_SIZE)
                    length = BT_EVENT_BUFFER_SIZE;

                BT_LastEventLength = (uint16_t)length;

                for (i = 0; i < length; i++)
                {
                    BT_LastEventData[i] = BT_EventRxBuffer[i];
                }

                BT_EventPacketCount++;
								
								/* Detect HCI Reset Command Complete event.
								 *
								 * Expected:
								 * 0E 04 01 03 0C 00
								 */

								if ((length >= 6U) &&
										((BT_HCI_ResetState == BT_HCI_RESET_USB_WAIT) ||
										 (BT_HCI_ResetState == BT_HCI_RESET_EVENT_WAIT)) &&
										(BT_EventRxBuffer[0] == 0x0EU) &&
										(BT_EventRxBuffer[1] >= 4U) &&
										(BT_EventRxBuffer[3] == 0x03U) &&
										(BT_EventRxBuffer[4] == 0x0CU))
								{
										BT_HCI_ResetStatus = BT_EventRxBuffer[5];
										BT_HCI_ResetEventSeen = 1U;
								}

                BT_EventLastTick = phost->Timer;
                BT_EventRxState = BT_EVENT_RX_DELAY;
								
								
								/* Read Local Version Information
								 * Command Complete: opcode 0x1001
								 */

								if ((length >= 6U) &&
										((BT_VersionState == BT_VERSION_USB_WAIT) ||
										 (BT_VersionState == BT_VERSION_EVENT_WAIT)) &&
										(BT_EventRxBuffer[0] == 0x0EU) &&
										(BT_EventRxBuffer[1] >= 4U) &&
										(BT_EventRxBuffer[3] == 0x01U) &&
										(BT_EventRxBuffer[4] == 0x10U))
								{
										BT_VersionStatus = BT_EventRxBuffer[5];

										if (BT_VersionStatus == 0U)
										{
												if ((length >= 14U) &&
														(BT_EventRxBuffer[1] == 12U))
												{
														BT_VersionInfo.HCI_Version =
																BT_EventRxBuffer[6];

														BT_VersionInfo.HCI_Revision =
																(uint16_t)BT_EventRxBuffer[7] |
																((uint16_t)BT_EventRxBuffer[8] << 8);

														BT_VersionInfo.LMP_Version =
																BT_EventRxBuffer[9];

														BT_VersionInfo.Manufacturer =
																(uint16_t)BT_EventRxBuffer[10] |
																((uint16_t)BT_EventRxBuffer[11] << 8);

														BT_VersionInfo.LMP_Subversion =
																(uint16_t)BT_EventRxBuffer[12] |
																((uint16_t)BT_EventRxBuffer[13] << 8);
												}
												else
												{
														// Internal marker: malformed/incomplete response
														BT_VersionStatus = 0xFFU;
												}
										}

										BT_VersionEventSeen = 1U;
								}
								
								/* HCI Read BD_ADDR Command Complete
								 *
								 * Expected:
								 * 0E 0A 01 09 10 00 XX XX XX XX XX XX
								 */

								if ((length >= 6U) &&
										((BT_AddrState == BT_ADDR_USB_WAIT) ||
										 (BT_AddrState == BT_ADDR_EVENT_WAIT)) &&
										(BT_EventRxBuffer[0] == 0x0EU) &&
										(BT_EventRxBuffer[3] == 0x09U) &&
										(BT_EventRxBuffer[4] == 0x10U))
								{
										BT_AddrStatus = BT_EventRxBuffer[5];

										if (BT_AddrStatus == 0U)
										{
												if ((length == 12U) &&
														(BT_EventRxBuffer[1] == 0x0AU))
												{
														/* Reverse byte order for conventional display */
														BT_BD_ADDR[0] = BT_EventRxBuffer[11];
														BT_BD_ADDR[1] = BT_EventRxBuffer[10];
														BT_BD_ADDR[2] = BT_EventRxBuffer[9];
														BT_BD_ADDR[3] = BT_EventRxBuffer[8];
														BT_BD_ADDR[4] = BT_EventRxBuffer[7];
														BT_BD_ADDR[5] = BT_EventRxBuffer[6];
												}
												else
												{
														BT_AddrStatus = 0xFFU;
												}
										}

										BT_AddrEventSeen = 1U;
								}


            }
            else if ((urb == USBH_URB_NOTREADY) ||
                     (urb == USBH_URB_NYET))
            {
                // NAK: no event available yet
                BT_EventNAKCount++;

                BT_EventLastTick = phost->Timer;
                BT_EventRxState = BT_EVENT_RX_DELAY;
            }
            else if ((urb == USBH_URB_ERROR) ||
                     (urb == USBH_URB_STALL))
            {
                BT_EventErrorCount++;
                BT_EventRxState = BT_EVENT_RX_ERROR;
            }

            break;

        // ------------------------------------
        // Wait until next polling interval
        // ------------------------------------
        case BT_EVENT_RX_DELAY:

            if ((phost->Timer - BT_EventLastTick) >= interval)
            {
                BT_EventRxState = BT_EVENT_RX_SUBMIT;
            }

            break;

        // ------------------------------------
        // Error state
        // ------------------------------------
        case BT_EVENT_RX_ERROR:

            // Preserve error state for debugging.
            // Recovery will be added later.

            break;

        default:

            BT_EventRxState = BT_EVENT_RX_ERROR;
            break;
    }
		
		BT_ProcessHCIReset(phost);
		BT_ProcessReadVersion(phost);
		BT_ProcessReadAddress(phost);
		
    return USBH_OK;
}


static USBH_StatusTypeDef BT_SOF(USBH_HandleTypeDef *phost)
{
    (void)phost;
    return USBH_OK;
}
