/*
 * CH341 USB-Serial Driver Declarations
 * Copyright (C) 2012-2019  Thomas Faber
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <ntddser.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>

/* Pool tags */
#define CH341_TAG      '32LP'
#define CH341_URB_TAG  'U2LP'

/* USB requests */
#define CH341_VENDOR_READ_REQUEST  0x95
#define CH341_VENDOR_WRITE_REQUEST 0x9A
#define CH341_SET_LINE_REQUEST     0xA1
#define CH341_SET_CONTROL_REQUEST  0x10

/* Misc defines */
#if defined(_MSC_VER) && !defined(inline)
#define inline __inline
#endif

#ifndef __WARNING_USING_VARIABLE_FROM_FAILED_FUNCTION_CALL
#define __WARNING_USING_VARIABLE_FROM_FAILED_FUNCTION_CALL 6102
#endif

/* Types */
typedef enum _DEVICE_PNP_STATE {
    NotStarted,
    Started,
    StopPending,
    Stopped,
    RemovePending,
    SurpriseRemovePending,
    Deleted
} DEVICE_PNP_STATE, *PDEVICE_PNP_STATE;

typedef struct _QUEUE {
    IO_CSQ Csq;
    LIST_ENTRY QueueHead;
    KSPIN_LOCK QueueSpinLock;
} QUEUE, *PQUEUE;

typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT LowerDevice;
    DEVICE_PNP_STATE PnpState;
    DEVICE_PNP_STATE PreviousPnpState;
    UNICODE_STRING DeviceName;
    UNICODE_STRING InterfaceLinkName;
    UNICODE_STRING ComPortName;
    USBD_PIPE_HANDLE BulkInPipe;
    USBD_PIPE_HANDLE BulkOutPipe;
    USBD_PIPE_HANDLE InterruptInPipe;
    FAST_MUTEX LineStateMutex;
    ULONG BaudRate;
    UCHAR StopBits;
    UCHAR Parity;
    UCHAR DataBits;
    SERIAL_CHARS Chars;
    SERIAL_HANDFLOW HandFlow;
    USHORT DtrRts;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/* Debugging functions */
static
inline
VOID
CH341Debug(
    _In_ PCSTR Format,
    ...) {
    va_list Arguments;
    va_start(Arguments, Format);
    (VOID)vDbgPrintExWithPrefix("CH341: ",
                                DPFLTR_IHVDRIVER_ID,
                                DPFLTR_TRACE_LEVEL,
                                Format,
                                Arguments);
    va_end(Arguments);
}

static
inline
VOID
CH341Warn(
    _In_ PCSTR Format,
    ...) {
    va_list Arguments;
    va_start(Arguments, Format);
    (VOID)vDbgPrintExWithPrefix("CH341: ",
                                DPFLTR_IHVDRIVER_ID,
                                DPFLTR_WARNING_LEVEL,
                                Format,
                                Arguments);
    va_end(Arguments);
}

static
inline
VOID
CH341Error(
    _In_ PCSTR Format,
    ...) {
    va_list Arguments;
    va_start(Arguments, Format);
    (VOID)vDbgPrintExWithPrefix("CH341: ",
                                DPFLTR_IHVDRIVER_ID,
                                DPFLTR_ERROR_LEVEL,
                                Format,
                                Arguments);
    va_end(Arguments);
}

/* ioctl.c */
__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)
DRIVER_DISPATCH CH341DispatchDeviceControl;
NTSTATUS CH341SetLine(_In_ PDEVICE_OBJECT DeviceObject);

/* pnp.c */
DRIVER_ADD_DEVICE CH341AddDevice;
__drv_dispatchType(IRP_MJ_PNP)
DRIVER_DISPATCH CH341DispatchPnp;

/* usb.c */
NTSTATUS CH341UsbStart(_In_ PDEVICE_OBJECT DeviceObject);
NTSTATUS CH341UsbStop(_In_ PDEVICE_OBJECT DeviceObject);
NTSTATUS CH341UsbSetLine(_In_ PDEVICE_OBJECT DeviceObject,
                         _In_ ULONG BaudRate,
                         _In_ UCHAR StopBits,
                         _In_ UCHAR Parity,
                         _In_ UCHAR DataBits);
NTSTATUS CH341UsbSetControlLines(_In_ PDEVICE_OBJECT DeviceObject,
                                 _In_ USHORT DtrRts);
NTSTATUS CH341UsbRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS CH341UsbWrite(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
