/*
 * CH341 Driver Serial IOCTL handling
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

#include "ch341.h"

static NTSTATUS CH341GetBaudRate(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);
static NTSTATUS CH341SetBaudRate(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);
static NTSTATUS CH341GetLineControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);
static NTSTATUS CH341SetLineControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, CH341GetBaudRate)
#pragma alloc_text(PAGE, CH341SetBaudRate)
#pragma alloc_text(PAGE, CH341GetLineControl)
#pragma alloc_text(PAGE, CH341SetLineControl)
#pragma alloc_text(PAGE, CH341DispatchDeviceControl)
#endif /* defined ALLOC_PRAGMA */

NTSTATUS
CH341SetLine(
    _In_ PDEVICE_OBJECT DeviceObject) {
    PDEVICE_EXTENSION DeviceExtension;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p\n",
                        __FUNCTION__, DeviceObject);
    DeviceExtension = DeviceObject->DeviceExtension;
    return CH341UsbSetLine(DeviceObject,
                           DeviceExtension->BaudRate,
                           DeviceExtension->StopBits,
                           DeviceExtension->Parity,
                           DeviceExtension->DataBits);
}

static
NTSTATUS
CH341GetBaudRate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    PSERIAL_BAUD_RATE BaudRate;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*BaudRate)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    BaudRate = Irp->AssociatedIrp.SystemBuffer;
    ExAcquireFastMutex(&DeviceExtension->LineStateMutex);
    BaudRate->BaudRate = DeviceExtension->BaudRate;
    ExReleaseFastMutex(&DeviceExtension->LineStateMutex);
    Irp->IoStatus.Information = sizeof(*BaudRate);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CH341SetBaudRate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    const SERIAL_BAUD_RATE *BaudRate;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*BaudRate)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    BaudRate = Irp->AssociatedIrp.SystemBuffer;
    ExAcquireFastMutex(&DeviceExtension->LineStateMutex);
    DeviceExtension->BaudRate = BaudRate->BaudRate;
    ExReleaseFastMutex(&DeviceExtension->LineStateMutex);
    return CH341SetLine(DeviceObject);
}

static
NTSTATUS
CH341GetLineControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    PSERIAL_LINE_CONTROL LineControl;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*LineControl)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    LineControl = Irp->AssociatedIrp.SystemBuffer;
    ExAcquireFastMutex(&DeviceExtension->LineStateMutex);
    LineControl->StopBits = DeviceExtension->StopBits;
    LineControl->Parity = DeviceExtension->Parity;
    LineControl->WordLength = DeviceExtension->DataBits;
    ExReleaseFastMutex(&DeviceExtension->LineStateMutex);
    Irp->IoStatus.Information = sizeof(*LineControl);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CH341SetLineControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    const SERIAL_LINE_CONTROL *LineControl;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*LineControl)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    LineControl = Irp->AssociatedIrp.SystemBuffer;
    ExAcquireFastMutex(&DeviceExtension->LineStateMutex);
    DeviceExtension->StopBits = LineControl->StopBits;
    DeviceExtension->Parity = LineControl->Parity;
    DeviceExtension->DataBits = LineControl->WordLength;
    ExReleaseFastMutex(&DeviceExtension->LineStateMutex);
    return CH341SetLine(DeviceObject);
}

static
NTSTATUS
CH341GetChars(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    PSERIAL_CHARS Chars;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Chars)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Chars = Irp->AssociatedIrp.SystemBuffer;
    ExAcquireFastMutex(&DeviceExtension->LineStateMutex);
    RtlCopyMemory(Chars,
                  &DeviceExtension->Chars,
                  sizeof(Chars));
    ExReleaseFastMutex(&DeviceExtension->LineStateMutex);
    Irp->IoStatus.Information = sizeof(*Chars);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CH341GetHandFlow(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    PSERIAL_HANDFLOW HandFlow;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*HandFlow)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    HandFlow = Irp->AssociatedIrp.SystemBuffer;
    ExAcquireFastMutex(&DeviceExtension->LineStateMutex);
    RtlCopyMemory(HandFlow,
                  &DeviceExtension->HandFlow,
                  sizeof(HandFlow));
    ExReleaseFastMutex(&DeviceExtension->LineStateMutex);
    Irp->IoStatus.Information = sizeof(*HandFlow);
    return STATUS_SUCCESS;
}

static
PCSTR
SerialGetIoctlName(
    _In_ ULONG IoControlCode) {
    switch (IoControlCode) {
    case IOCTL_SERIAL_SET_BAUD_RATE:
        return "IOCTL_SERIAL_SET_BAUD_RATE";
    case IOCTL_SERIAL_GET_BAUD_RATE:
        return "IOCTL_SERIAL_GET_BAUD_RATE";
    case IOCTL_SERIAL_GET_MODEM_CONTROL:
        return "IOCTL_SERIAL_GET_MODEM_CONTROL";
    case IOCTL_SERIAL_SET_MODEM_CONTROL:
        return "IOCTL_SERIAL_SET_MODEM_CONTROL";
    case IOCTL_SERIAL_SET_FIFO_CONTROL:
        return "IOCTL_SERIAL_SET_FIFO_CONTROL";
    case IOCTL_SERIAL_SET_LINE_CONTROL:
        return "IOCTL_SERIAL_SET_LINE_CONTROL";
    case IOCTL_SERIAL_GET_LINE_CONTROL:
        return "IOCTL_SERIAL_GET_LINE_CONTROL";
    case IOCTL_SERIAL_SET_TIMEOUTS:
        return "IOCTL_SERIAL_SET_TIMEOUTS";
    case IOCTL_SERIAL_GET_TIMEOUTS:
        return "IOCTL_SERIAL_GET_TIMEOUTS";
    case IOCTL_SERIAL_SET_CHARS:
        return "IOCTL_SERIAL_SET_CHARS";
    case IOCTL_SERIAL_GET_CHARS:
        return "IOCTL_SERIAL_GET_CHARS";
    case IOCTL_SERIAL_SET_DTR:
        return "IOCTL_SERIAL_SET_DTR";
    case IOCTL_SERIAL_CLR_DTR:
        return "IOCTL_SERIAL_SET_DTR";
    case IOCTL_SERIAL_RESET_DEVICE:
        return "IOCTL_SERIAL_RESET_DEVICE";
    case IOCTL_SERIAL_SET_RTS:
        return "IOCTL_SERIAL_SET_RTS";
    case IOCTL_SERIAL_CLR_RTS:
        return "IOCTL_SERIAL_CLR_RTS";
    case IOCTL_SERIAL_SET_XOFF:
        return "IOCTL_SERIAL_SET_XOFF";
    case IOCTL_SERIAL_SET_XON:
        return "IOCTL_SERIAL_SET_XON";
    case IOCTL_SERIAL_SET_BREAK_ON:
        return "IOCTL_SERIAL_SET_BREAK_ON";
    case IOCTL_SERIAL_SET_BREAK_OFF:
        return "IOCTL_SERIAL_SET_BREAK_OFF";
    case IOCTL_SERIAL_SET_QUEUE_SIZE:
        return "IOCTL_SERIAL_SET_QUEUE_SIZE";
    case IOCTL_SERIAL_GET_WAIT_MASK:
        return "IOCTL_SERIAL_GET_WAIT_MASK";
    case IOCTL_SERIAL_SET_WAIT_MASK:
        return "IOCTL_SERIAL_SET_WAIT_MASK";
    case IOCTL_SERIAL_WAIT_ON_MASK:
        return "IOCTL_SERIAL_WAIT_ON_MASK";
    case IOCTL_SERIAL_IMMEDIATE_CHAR:
        return "IOCTL_SERIAL_IMMEDIATE_CHAR";
    case IOCTL_SERIAL_PURGE:
        return "IOCTL_SERIAL_PURGE";
    case IOCTL_SERIAL_GET_HANDFLOW:
        return "IOCTL_SERIAL_GET_HANDFLOW";
    case IOCTL_SERIAL_SET_HANDFLOW:
        return "IOCTL_SERIAL_SET_HANDFLOW";
    case IOCTL_SERIAL_GET_MODEMSTATUS:
        return "IOCTL_SERIAL_GET_MODEMSTATUS";
    case IOCTL_SERIAL_GET_DTRRTS:
        return "IOCTL_SERIAL_GET_DTRRTS";
    case IOCTL_SERIAL_GET_COMMSTATUS:
        return "IOCTL_SERIAL_GET_COMMSTATUS";
    case IOCTL_SERIAL_GET_PROPERTIES:
        return "IOCTL_SERIAL_GET_PROPERTIES";
    case IOCTL_SERIAL_XOFF_COUNTER:
        return "IOCTL_SERIAL_XOFF_COUNTER";
    case IOCTL_SERIAL_LSRMST_INSERT:
        return "IOCTL_SERIAL_LSRMST_INSERT";
    case IOCTL_SERIAL_CONFIG_SIZE:
        return "IOCTL_SERIAL_CONFIG_SIZE";
    case IOCTL_SERIAL_GET_STATS:
        return "IOCTL_SERIAL_GET_STATS";
    case IOCTL_SERIAL_CLEAR_STATS:
        return "IOCTL_SERIAL_CLEAR_STATS";
    default:
        return "Unknown ioctl";
    }
}

NTSTATUS
NTAPI
CH341DispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    ULONG IoControlCode;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_DEVICE_CONTROL ||
              IoStack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (DeviceExtension->PnpState == Deleted) {
        CH341Warn(         "%s. Device already deleted\n",
                           __FUNCTION__);
        Status = STATUS_NO_SUCH_DEVICE;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;
    switch (IoControlCode) {
    case IOCTL_SERIAL_GET_BAUD_RATE:
        Status = CH341GetBaudRate(DeviceObject, Irp);
        break;
    case IOCTL_SERIAL_SET_BAUD_RATE:
        Status = CH341SetBaudRate(DeviceObject, Irp);
        break;
    case IOCTL_SERIAL_GET_LINE_CONTROL:
        Status = CH341GetLineControl(DeviceObject, Irp);
        break;
    case IOCTL_SERIAL_SET_LINE_CONTROL:
        Status = CH341SetLineControl(DeviceObject, Irp);
        break;
    case IOCTL_SERIAL_GET_CHARS:
        Status = CH341GetChars(DeviceObject, Irp);
        break;
    case IOCTL_SERIAL_SET_CHARS:
        if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(SERIAL_CHARS)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            RtlCopyMemory(&DeviceExtension->Chars,
                          Irp->AssociatedIrp.SystemBuffer,
                          sizeof(SERIAL_CHARS));
        }
        Status = STATUS_SUCCESS;
        break;
    case IOCTL_SERIAL_GET_HANDFLOW:
        Status = CH341GetHandFlow(DeviceObject, Irp);
        break;
    case IOCTL_SERIAL_SET_HANDFLOW:
        if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(SERIAL_HANDFLOW)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            RtlCopyMemory(&DeviceExtension->HandFlow,
                          Irp->AssociatedIrp.SystemBuffer,
                          sizeof(SERIAL_HANDFLOW));
        }
        Status = STATUS_SUCCESS;
        break;
    case IOCTL_SERIAL_CLR_DTR:
        DeviceExtension->DtrRts &= ~SERIAL_DTR_STATE;
        Status = CH341UsbSetControlLines(DeviceObject, DeviceExtension->DtrRts);
        break;
    case IOCTL_SERIAL_SET_DTR:
        DeviceExtension->DtrRts |= SERIAL_DTR_STATE;
        Status = CH341UsbSetControlLines(DeviceObject, DeviceExtension->DtrRts);
        break;
    case IOCTL_SERIAL_CLR_RTS:
        DeviceExtension->DtrRts &= ~SERIAL_RTS_STATE;
        Status = CH341UsbSetControlLines(DeviceObject, DeviceExtension->DtrRts);
        break;
    case IOCTL_SERIAL_SET_RTS:
        DeviceExtension->DtrRts |= SERIAL_RTS_STATE;
        Status = CH341UsbSetControlLines(DeviceObject, DeviceExtension->DtrRts);
        break;
    case IOCTL_SERIAL_GET_DTRRTS:
        if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = DeviceExtension->DtrRts;
        }
    case IOCTL_SERIAL_CLEAR_STATS:
    default:
        CH341Debug(         "%s. DeviceControl %x, code %s (%08lx)\n",
                            __FUNCTION__, IoStack->MajorFunction, SerialGetIoctlName(IoControlCode), IoControlCode);
        Status = STATUS_NOT_SUPPORTED;
    }
    ASSERT(Status != STATUS_PENDING);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
    //IoSkipCurrentIrpStackLocation(Irp);
    //return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}
