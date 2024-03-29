/*************************************************************************/ /*!
@File
@Title          Power Management Functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main APIs for power management functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#ifndef POWER_H
#define POWER_H

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_device.h"
#include "pvrsrv_error.h"
#include "servicesext.h"
#include "opaque_types.h"

/*!
 *****************************************************************************
 *	Power management
 *****************************************************************************/

typedef struct _PVRSRV_POWER_DEV_TAG_ PVRSRV_POWER_DEV;

typedef IMG_BOOL (*PFN_SYS_DEV_IS_DEFAULT_STATE_OFF)(PVRSRV_POWER_DEV *psPowerDevice);


#define PVRSRVPowerLock(_rc) \
	MTPVRSRVPowerLock(_rc, __func__, sizeof(__func__))

#define PVRSRVForcedPowerLock(_rc) \
	MTPVRSRVForcedPowerLock(_rc, __func__, sizeof(__func__))

PVRSRV_ERROR MTPVRSRVPowerLock(PCPVRSRV_DEVICE_NODE psDeviceNode, const char* caller, size_t len);
void MTPVRSRVForcedPowerLock(PPVRSRV_DEVICE_NODE psDeviceNode, const char* caller, size_t len);
void PVRSRVPowerUnlock(PCPVRSRV_DEVICE_NODE psDeviceNode);

IMG_BOOL PVRSRVDeviceIsDefaultStateOFF(PVRSRV_POWER_DEV *psPowerDevice);


PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(PPVRSRV_DEVICE_NODE	psDeviceNode,
										 PVRSRV_DEV_POWER_STATE	eNewPowerState,
										 IMG_BOOL				bForced);

PVRSRV_ERROR PVRSRVSetDeviceSystemPowerState(PPVRSRV_DEVICE_NODE psDeviceNode,
											 PVRSRV_SYS_POWER_STATE ePVRState);

PVRSRV_ERROR PVRSRVSetDeviceDefaultPowerState(PCPVRSRV_DEVICE_NODE psDeviceNode,
					PVRSRV_DEV_POWER_STATE eNewPowerState);

/* Type PFN_DC_REGISTER_POWER */
PVRSRV_ERROR PVRSRVRegisterPowerDevice(PPVRSRV_DEVICE_NODE		psDeviceNode,
									   PFN_PRE_POWER				pfnDevicePrePower,
									   PFN_POST_POWER				pfnDevicePostPower,
									   PFN_SYS_DEV_PRE_POWER		pfnSystemPrePower,
									   PFN_SYS_DEV_POST_POWER		pfnSystemPostPower,
									   PFN_PRE_CLOCKSPEED_CHANGE	pfnPreClockSpeedChange,
									   PFN_POST_CLOCKSPEED_CHANGE	pfnPostClockSpeedChange,
									   PFN_FORCED_IDLE_REQUEST		pfnForcedIdleRequest,
									   PFN_FORCED_IDLE_CANCEL_REQUEST	pfnForcedIdleCancelRequest,
									   PFN_DUST_COUNT_REQUEST	pfnDustCountRequest,
									   IMG_HANDLE					hDevCookie,
									   PVRSRV_DEV_POWER_STATE		eCurrentPowerState,
									   PVRSRV_DEV_POWER_STATE		eDefaultPowerState);

PVRSRV_ERROR PVRSRVRemovePowerDevice(PPVRSRV_DEVICE_NODE psDeviceNode);

PVRSRV_ERROR PVRSRVGetDevicePowerState(PCPVRSRV_DEVICE_NODE psDeviceNode,
									   PPVRSRV_DEV_POWER_STATE pePowerState);

IMG_BOOL PVRSRVIsDevicePowered(PPVRSRV_DEVICE_NODE psDeviceNode);

PVRSRV_ERROR PVRSRVDevicePreClockSpeedChange(PPVRSRV_DEVICE_NODE psDeviceNode,
											 IMG_BOOL	bIdleDevice,
											 void	*pvInfo);

void PVRSRVDevicePostClockSpeedChange(PPVRSRV_DEVICE_NODE psDeviceNode,
									  IMG_BOOL		bIdleDevice,
									  void		*pvInfo);

PVRSRV_ERROR PVRSRVDeviceIdleRequestKM(PPVRSRV_DEVICE_NODE psDeviceNode,
					PFN_SYS_DEV_IS_DEFAULT_STATE_OFF	pfnCheckIdleReq,
					IMG_BOOL				bDeviceOffPermitted);

PVRSRV_ERROR PVRSRVDeviceIdleCancelRequestKM(PPVRSRV_DEVICE_NODE psDeviceNode);

PVRSRV_ERROR PVRSRVDeviceDustCountChange(PPVRSRV_DEVICE_NODE psDeviceNode,
						IMG_UINT32	ui32DustCount);


#endif /* POWER_H */

/******************************************************************************
 End of file (power.h)
******************************************************************************/
