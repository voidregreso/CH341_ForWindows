/*
 * CH341 Driver PnP routines
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

static NTSTATUS CH341InitializeDevice(_In_ PDEVICE_OBJECT DeviceObject,
                                      _In_ PDEVICE_OBJECT PhysicalDeviceObject);
static NTSTATUS CH341DestroyDevice(_In_ PDEVICE_OBJECT DeviceObject);
static NTSTATUS CH341StartDevice(_In_ PDEVICE_OBJECT DeviceObject);
static NTSTATUS CH341StopDevice(_In_ PDEVICE_OBJECT DeviceObject);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, CH341InitializeDevice)
#pragma alloc_text(PAGE, CH341DestroyDevice)
#pragma alloc_text(PAGE, CH341StartDevice)
#pragma alloc_text(PAGE, CH341StopDevice)
#pragma alloc_text(PAGE, CH341AddDevice)
#pragma alloc_text(PAGE, CH341DispatchPnp)
#endif /* defined ALLOC_PRAGMA */

static
NTSTATUS
CH341InitializeDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject) {
    NTSTATUS Status;
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    HANDLE KeyHandle;
    UNICODE_STRING ValueName;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInformation;
    ULONG ValueInformationLength;
    ULONG SkipExternalNaming;
    USHORT ComPortNameLength;
    PWCHAR ComPortNameBuffer = NULL;
    const UNICODE_STRING DosDevices = RTL_CONSTANT_STRING(L"\\DosDevices\\");
    PCONFIGURATION_INFORMATION ConfigInfo;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, PhysicalDeviceObject=%p\n",
                        __FUNCTION__, DeviceObject,    PhysicalDeviceObject);
    ExInitializeFastMutex(&DeviceExtension->LineStateMutex);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                       &GUID_DEVINTERFACE_COMPORT,
                                       NULL,
                                       &DeviceExtension->InterfaceLinkName);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. IoRegisterDeviceInterface failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    CH341Debug(         "%s. Device Interface is '%wZ'\n",
                        __FUNCTION__, &DeviceExtension->InterfaceLinkName);
    Status = IoOpenDeviceRegistryKey(PhysicalDeviceObject,
                                     PLUGPLAY_REGKEY_DEVICE,
                                     KEY_QUERY_VALUE,
                                     &KeyHandle);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. IoOpenDeviceRegistryKey failed with %08lx\n",
                            __FUNCTION__, Status);
        RtlFreeUnicodeString(&DeviceExtension->InterfaceLinkName);
        return Status;
    }
    ValueInformationLength = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data[sizeof(ULONG)]);
    ValueInformation = ExAllocatePoolWithTag(PagedPool, ValueInformationLength, CH341_TAG);
    if (!ValueInformation) {
        CH341Error(         "%s. Allocating registry value information failed\n",
                            __FUNCTION__);
        RtlFreeUnicodeString(&DeviceExtension->InterfaceLinkName);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlInitUnicodeString(&ValueName, L"SkipExternalNaming");
    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInformation,
                             ValueInformationLength,
                             &ValueInformationLength);
    if (NT_SUCCESS(Status) &&
            ValueInformationLength == (ULONG)FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION,
                    Data[sizeof(ULONG)]) &&
            ValueInformation->Type == REG_DWORD &&
            ValueInformation->DataLength == sizeof(ULONG)) {
        SkipExternalNaming = *(const ULONG *)ValueInformation->Data;
    } else {
        SkipExternalNaming = 0;
    }
    ExFreePoolWithTag(ValueInformation, CH341_TAG);
    if (!SkipExternalNaming) {
        RtlInitUnicodeString(&ValueName, L"PortName");
        Status = ZwQueryValueKey(KeyHandle,
                                 &ValueName,
                                 KeyValuePartialInformation,
                                 NULL,
                                 0,
                                 &ValueInformationLength);
        if (Status == STATUS_BUFFER_TOO_SMALL) {
#pragma prefast(suppress: __WARNING_USING_VARIABLE_FROM_FAILED_FUNCTION_CALL, "ZwQueryValueKey sets ResultLength if buffer too small")
            NT_ASSERT(ValueInformationLength != 0);
            ValueInformation = ExAllocatePoolWithTag(PagedPool,
                               ValueInformationLength,
                               CH341_TAG);
            if (!ValueInformation) {
                CH341Error(         "%s. Allocating registry value information failed\n",
                                    __FUNCTION__);
                RtlFreeUnicodeString(&DeviceExtension->InterfaceLinkName);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            Status = ZwQueryValueKey(KeyHandle,
                                     &ValueName,
                                     KeyValuePartialInformation,
                                     ValueInformation,
                                     ValueInformationLength,
                                     &ValueInformationLength);
            if (!NT_SUCCESS(Status)) {
                CH341Error(         "%s. ZwQueryValueKey failed with %08lx\n",
                                    __FUNCTION__, Status);
                ExFreePoolWithTag(ValueInformation, CH341_TAG);
                RtlFreeUnicodeString(&DeviceExtension->InterfaceLinkName);
                return Status;
            }
            if (ValueInformation->Type != REG_SZ ||
                    ValueInformation->Data[ValueInformation->DataLength - 1] != 0 ||
                    ValueInformation->Data[ValueInformation->DataLength - 2] != 0 ||
                    ValueInformation->DataLength >= MAXUSHORT) {
                CH341Error("%s. PortName registry key is invalid\n",
                           __FUNCTION__);
                ExFreePoolWithTag(ValueInformation, CH341_TAG);
                RtlFreeUnicodeString(&DeviceExtension->InterfaceLinkName);
                return STATUS_INVALID_PARAMETER;
            }
            ComPortNameLength = DosDevices.Length + (USHORT)ValueInformation->DataLength;
            ComPortNameBuffer = ExAllocatePoolWithTag(PagedPool,
                                ComPortNameLength,
                                CH341_TAG);
            if (!ComPortNameBuffer) {
                CH341Error(         "%s. Allocating COM port name failed\n",
                                    __FUNCTION__);
                ExFreePoolWithTag(ValueInformation, CH341_TAG);
                RtlFreeUnicodeString(&DeviceExtension->InterfaceLinkName);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            RtlInitEmptyUnicodeString(&DeviceExtension->ComPortName,
                                      ComPortNameBuffer,
                                      ComPortNameLength);
            RtlCopyUnicodeString(&DeviceExtension->ComPortName, &DosDevices);
            (VOID)RtlAppendUnicodeToString(&DeviceExtension->ComPortName,
                                           (PCWSTR)ValueInformation->Data);
            ExFreePoolWithTag(ValueInformation, CH341_TAG);
        } else {
            CH341Debug(         "%s. ZwQueryValueKey failed with %08lx\n",
                                __FUNCTION__, Status);
            Status = STATUS_SUCCESS;
        }
    }
    NT_ASSERT(DeviceExtension->ComPortName.Buffer == ComPortNameBuffer);
    CH341Debug(         "%s. COM Port name is is '%wZ'\n",
                        __FUNCTION__, &DeviceExtension->ComPortName);
    ConfigInfo = IoGetConfigurationInformation();
    ConfigInfo->SerialCount++;
    CH341Debug(         "%s. New serial port count: %lu\n",
                        __FUNCTION__, ConfigInfo->SerialCount);
    return Status;
}

static
NTSTATUS
CH341DestroyDevice(
    _In_ PDEVICE_OBJECT DeviceObject) {
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PCONFIGURATION_INFORMATION ConfigInfo;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p\n",
                        __FUNCTION__, DeviceObject);
    ConfigInfo = IoGetConfigurationInformation();
    ConfigInfo->SerialCount--;
    CH341Debug(         "%s. New serial port count: %lu\n",
                        __FUNCTION__, ConfigInfo->SerialCount);
    if (DeviceExtension->ComPortName.Buffer)
        ExFreePoolWithTag(DeviceExtension->ComPortName.Buffer, CH341_TAG);
    RtlFreeUnicodeString(&DeviceExtension->InterfaceLinkName);
    ExFreePoolWithTag(DeviceExtension->DeviceName.Buffer, CH341_TAG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CH341StartDevice(
    _In_ PDEVICE_OBJECT DeviceObject) {
    NTSTATUS Status;
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p\n",
                        __FUNCTION__, DeviceObject);
    Status = CH341UsbStart(DeviceObject);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbStart failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    DeviceExtension->BaudRate = 115200;
    DeviceExtension->StopBits = 0;
    DeviceExtension->Parity = 0;
    DeviceExtension->DataBits = 0;
    DeviceExtension->Chars.XonChar = 0x11;
    DeviceExtension->Chars.XoffChar = 0x13;
    DeviceExtension->HandFlow.ControlHandShake = SERIAL_DTR_CONTROL;
    DeviceExtension->HandFlow.FlowReplace = SERIAL_RTS_CONTROL;
    DeviceExtension->HandFlow.XonLimit = 2048;
    DeviceExtension->HandFlow.XoffLimit = 512;
    Status = CH341SetLine(DeviceObject);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSetLine failed with %08lx\n",
                            __FUNCTION__, Status);
    }
    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceLinkName,
                                       TRUE);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. IoSetDeviceInterfaceState failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    if (DeviceExtension->ComPortName.Buffer) {
        Status = IoCreateSymbolicLink(&DeviceExtension->ComPortName,
                                      &DeviceExtension->DeviceName);
        if (!NT_SUCCESS(Status)) {
            CH341Error(         "%s. IoCreateSymbolicLink failed with %08lx\n",
                                __FUNCTION__, Status);
            (VOID)IoSetDeviceInterfaceState(&DeviceExtension->InterfaceLinkName,
                                            FALSE);
            return Status;
        }
        /* FIXME */
#if 0
        Status = RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP,
                                       SERIAL_DEVICE_MAP,
                                       DeviceExtension->DeviceName.Buffer,
                                       DeviceExtension->ComPortName.Buffer + xxx,
                                       DeviceExtension->ComPortName.Length - xxx * sizeof(WCHAR));
#endif
    }
    return Status;
}

static
NTSTATUS
CH341StopDevice(
    _In_ PDEVICE_OBJECT DeviceObject) {
    NTSTATUS Status;
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p\n",
                        __FUNCTION__, DeviceObject);
    if (DeviceExtension->ComPortName.Buffer)
        (VOID)IoDeleteSymbolicLink(&DeviceExtension->ComPortName);
    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceLinkName,
                                       FALSE);
    return Status;
}

NTSTATUS
NTAPI
CH341AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject) {
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_EXTENSION DeviceExtension;
    UNICODE_STRING DeviceName;
    static INT DeviceNumber = 0;
    PAGED_CODE();
    CH341Debug(         "%s. DriverObject=%p, PhysicalDeviceObject=%p\n",
                        __FUNCTION__, DriverObject,    PhysicalDeviceObject);
    DeviceName.MaximumLength = sizeof(L"\\Device\\CH341Serial999");
    DeviceName.Length = 0;
    DeviceName.Buffer = ExAllocatePoolWithTag(PagedPool,
                        DeviceName.MaximumLength,
                        CH341_TAG);
    if (!DeviceName.Buffer) {
        CH341Error(         "%s. Allocating device name buffer failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = RtlUnicodeStringPrintf(&DeviceName,
                                    L"\\Device\\CH341Serial%d",
                                    DeviceNumber++);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. RtlUnicodeStringPrintf failed with %08lx\n",
                            __FUNCTION__, Status);
        ExFreePoolWithTag(DeviceName.Buffer, CH341_TAG);
        return Status;
    }
    CH341Debug(         "%s. Device Name is '%wZ'\n",
                        __FUNCTION__, &DeviceName);
    Status = IoCreateDevice(DriverObject,
                            sizeof(DEVICE_EXTENSION),
                            &DeviceName,
                            FILE_DEVICE_SERIAL_PORT,
                            FILE_DEVICE_SECURE_OPEN,
                            TRUE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. IoCreateDevice failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->DeviceName = DeviceName;
    NT_ASSERT(DeviceExtension->LowerDevice == NULL);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject,
             PhysicalDeviceObject,
             &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. IoAttachDeviceToDeviceStackSafe failed with %08lx\n",
                            __FUNCTION__, Status);
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }
    NT_ASSERT(DeviceExtension->LowerDevice);
    NT_ASSERT(DeviceExtension->LowerDevice->Flags & DO_POWER_PAGABLE);
    if (DeviceExtension->LowerDevice->Flags & DO_POWER_PAGABLE)
        DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags |= DO_BUFFERED_IO;
    Status = CH341InitializeDevice(DeviceObject, PhysicalDeviceObject);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341InitializeDevice failed with %08lx\n",
                            __FUNCTION__, Status);
        IoDetachDevice(DeviceExtension->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static const PCSTR PnpMinorFunctionNames[] = {
    "IRP_MN_START_DEVICE",
    "IRP_MN_QUERY_REMOVE_DEVICE",
    "IRP_MN_REMOVE_DEVICE",
    "IRP_MN_CANCEL_REMOVE_DEVICE",
    "IRP_MN_STOP_DEVICE",
    "IRP_MN_QUERY_STOP_DEVICE",
    "IRP_MN_CANCEL_STOP_DEVICE",
    "IRP_MN_QUERY_DEVICE_RELATIONS",
    "IRP_MN_QUERY_INTERFACE",
    "IRP_MN_QUERY_CAPABILITIES",
    "IRP_MN_QUERY_RESOURCES",
    "IRP_MN_QUERY_RESOURCE_REQUIREMENTS",
    "IRP_MN_QUERY_DEVICE_TEXT",
    "IRP_MN_FILTER_RESOURCE_REQUIREMENTS",
    "0x0E",
    "IRP_MN_READ_CONFIG",
    "IRP_MN_WRITE_CONFIG",
    "IRP_MN_EJECT",
    "IRP_MN_SET_LOCK",
    "IRP_MN_QUERY_ID",
    "IRP_MN_QUERY_PNP_DEVICE_STATE",
    "IRP_MN_QUERY_BUS_INFORMATION",
    "IRP_MN_DEVICE_USAGE_NOTIFICATION",
    "IRP_MN_SURPRISE_REMOVAL",
    "IRP_MN_QUERY_LEGACY_BUS_INFORMATION",
    "IRP_MN_DEVICE_ENUMERATED"
};

static
PCSTR
CH341GetPnpMinorFunctionName(
    _In_ UCHAR MinorFunction) {
    if (MinorFunction < RTL_NUMBER_OF(PnpMinorFunctionNames))
        return PnpMinorFunctionNames[MinorFunction];
    return "Unknown";
}

NTSTATUS
NTAPI
CH341DispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp) {
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PDEVICE_EXTENSION DeviceExtension;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_PNP);
    DeviceExtension = DeviceObject->DeviceExtension;
    if (DeviceExtension->PnpState == Deleted) {
        CH341Warn(         "%s. Device already deleted\n",
                           __FUNCTION__);
        Status = STATUS_NO_SUCH_DEVICE;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    CH341Debug(         "%s. MinorFunction=%s (0x%x)\n",
                        __FUNCTION__, CH341GetPnpMinorFunctionName(IoStack->MinorFunction),
                        IoStack->MinorFunction);
    switch (IoStack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        if (IoForwardIrpSynchronously(DeviceExtension->LowerDevice, Irp))
            Status = Irp->IoStatus.Status;
        else {
            CH341Error(         "%s. IoForwardIrpSynchronously failed\n",
                                __FUNCTION__);
            Status = STATUS_UNSUCCESSFUL;
        }
        if (!NT_SUCCESS(Status)) {
            CH341Warn(         "%s. IRP_MN_START_DEVICE failed with %08lx\n",
                               __FUNCTION__, Status);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        Status = CH341StartDevice(DeviceObject);
        if (!NT_SUCCESS(Status)) {
            CH341Error(         "%s. CH341StartDevice failed with %08lx\n",
                                __FUNCTION__, Status);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        DeviceExtension->PnpState = Started;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    case IRP_MN_QUERY_STOP_DEVICE:
        DeviceExtension->PreviousPnpState = DeviceExtension->PnpState;
        DeviceExtension->PnpState = StopPending;
        break;
    case IRP_MN_QUERY_REMOVE_DEVICE:
        DeviceExtension->PreviousPnpState = DeviceExtension->PnpState;
        DeviceExtension->PnpState = RemovePending;
        break;
    case IRP_MN_CANCEL_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
        DeviceExtension->PnpState = DeviceExtension->PreviousPnpState;
        break;
    case IRP_MN_STOP_DEVICE:
        DeviceExtension->PnpState = Stopped;
        (VOID)CH341UsbStop(DeviceObject);
        break;
    case IRP_MN_SURPRISE_REMOVAL:
        DeviceExtension->PnpState = SurpriseRemovePending;
        Status = CH341StopDevice(DeviceObject);
        if (!NT_SUCCESS(Status))
            CH341Warn(         "%s. CH341StopDevice failed with %08lx\n",
                               __FUNCTION__, Status);
        break;
    case IRP_MN_REMOVE_DEVICE:
        DeviceExtension->PreviousPnpState = DeviceExtension->PnpState;
        DeviceExtension->PnpState = Deleted;
        if (DeviceExtension->PreviousPnpState != SurpriseRemovePending) {
            Status = CH341StopDevice(DeviceObject);
            if (!NT_SUCCESS(Status))
                CH341Warn(         "%s. CH341StopDevice failed with %08lx\n",
                                   __FUNCTION__, Status);
        }
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(Irp);
        Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
        IoDetachDevice(DeviceExtension->LowerDevice);
        (VOID)CH341DestroyDevice(DeviceObject);
        IoDeleteDevice(DeviceObject);
        return Status;
    default:
        /* Unsupported request - leave Irp->IoStack.Status untouched */
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(DeviceExtension->LowerDevice, Irp);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}
