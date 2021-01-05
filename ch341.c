/*
 * CH341 USB-Serial Driver
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

#define INITGUID
#include "ch341.h"

DRIVER_INITIALIZE DriverEntry;
static DRIVER_UNLOAD CH341Unload;
__drv_dispatchType(IRP_MJ_POWER)
static DRIVER_DISPATCH CH341DispatchPower;

__drv_dispatchType(IRP_MJ_SYSTEM_CONTROL)
static DRIVER_DISPATCH CH341DispatchSystemControl;
__drv_dispatchType(IRP_MJ_CREATE)
static DRIVER_DISPATCH CH341DispatchCreate;
__drv_dispatchType(IRP_MJ_CLOSE)
static DRIVER_DISPATCH CH341DispatchClose;
__drv_dispatchType(IRP_MJ_READ)
static DRIVER_DISPATCH CH341DispatchRead;
__drv_dispatchType(IRP_MJ_WRITE)
static DRIVER_DISPATCH CH341DispatchWrite;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, CH341Unload)
#pragma alloc_text(PAGE, CH341DispatchPower)
#pragma alloc_text(PAGE, CH341DispatchSystemControl)
#pragma alloc_text(PAGE, CH341DispatchCreate)
#pragma alloc_text(PAGE, CH341DispatchClose)
#pragma alloc_text(PAGE, CH341DispatchRead)
#pragma alloc_text(PAGE, CH341DispatchWrite)
#endif /* defined ALLOC_PRAGMA */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath) {
    PAGED_CODE();
    CH341Debug(         "%s. DriverObject=%p, RegistryPath='%wZ'\n",
                        __FUNCTION__, DriverObject,    RegistryPath);
    DriverObject->DriverUnload = CH341Unload;
    DriverObject->DriverExtension->AddDevice = CH341AddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP] = CH341DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = CH341DispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = CH341DispatchSystemControl;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = CH341DispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = CH341DispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = CH341DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = CH341DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = CH341DispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = CH341DispatchWrite;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
CH341Unload(
    _In_ PDRIVER_OBJECT DriverObject) {
    PAGED_CODE();
    CH341Debug(         "%s. DriverObject=%p\n",
                        __FUNCTION__, DriverObject);
}

static
NTSTATUS
NTAPI
CH341DispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    PAGED_CODE();
    CH341Debug(          "%s. DeviceObject=%p, Irp=%p\n",
                         __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_POWER);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (DeviceExtension->PnpState == Deleted) {
        CH341Warn(         "%s. Device already deleted\n",
                           __FUNCTION__);
        PoStartNextPowerIrp(Irp);
        Status = STATUS_NO_SUCH_DEVICE;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
CH341DispatchSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_SYSTEM_CONTROL);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (DeviceExtension->PnpState == Deleted) {
        CH341Warn(         "%s. Device already deleted\n",
                           __FUNCTION__);
        Status = STATUS_NO_SUCH_DEVICE;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
CH341DispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_CREATE);
    Status = STATUS_SUCCESS;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
CH341DispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_CLOSE);
    Status = STATUS_SUCCESS;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
CH341DispatchRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IoStack;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_READ);
    if (!IoStack->Parameters.Read.Length) {
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    Status = CH341UsbRead(DeviceObject, Irp);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbRead failed with %08lx\n",
                            __FUNCTION__, Status);
    }
    return Status;
}

static
NTSTATUS
NTAPI
CH341DispatchWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IoStack;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_WRITE);
    if (!IoStack->Parameters.Write.Length) {
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    Status = CH341UsbWrite(DeviceObject, Irp);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbWrite failed with %08lx\n",
                            __FUNCTION__, Status);
    }
    return Status;
}
