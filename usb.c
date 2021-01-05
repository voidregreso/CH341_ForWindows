/*
 * CH341 Driver USB protocol routines
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

static NTSTATUS CH341UsbSubmitUrb(_In_ PDEVICE_OBJECT DeviceObject, _In_ PURB Urb);
static NTSTATUS CH341UsbGetDescriptor(_In_ PDEVICE_OBJECT DeviceObject,
                                      _In_ UCHAR DescriptorType,
                                      _Out_ PVOID *Buffer,
                                      _Inout_ PULONG BufferLength);
static NTSTATUS CH341UsbVendorRead(_In_ PDEVICE_OBJECT DeviceObject,
                                   _Out_ UCHAR *Buffer,
                                   _In_ USHORT Value,
                                   _In_ USHORT Index);
static NTSTATUS CH341UsbVendorWrite(_In_ PDEVICE_OBJECT DeviceObject,
                                    _In_ USHORT Value,
                                    _In_ USHORT Index);
static NTSTATUS CH341UsbConfigureDevice(_In_ PDEVICE_OBJECT DeviceObject,
                                        _In_ PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor,
                                        _In_ PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor);
static NTSTATUS CH341UsbUnconfigureDevice(_In_ PDEVICE_OBJECT DeviceObject);
_Function_class_(IO_COMPLETION_ROUTINE)
static NTSTATUS NTAPI CH341UsbReadCompletion(_In_ PDEVICE_OBJECT DeviceObject,
        _In_ PIRP Irp,
        _In_reads_(sizeof(URB)) PVOID Context);
_Function_class_(IO_COMPLETION_ROUTINE)
static NTSTATUS NTAPI CH341UsbWriteCompletion(_In_ PDEVICE_OBJECT DeviceObject,
        _In_ PIRP Irp,
        _In_reads_(sizeof(URB)) PVOID Context);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, CH341UsbSubmitUrb)
#pragma alloc_text(PAGE, CH341UsbGetDescriptor)
#pragma alloc_text(PAGE, CH341UsbVendorRead)
#pragma alloc_text(PAGE, CH341UsbVendorWrite)
#pragma alloc_text(PAGE, CH341UsbConfigureDevice)
#pragma alloc_text(PAGE, CH341UsbUnconfigureDevice)
#pragma alloc_text(PAGE, CH341UsbStart)
#pragma alloc_text(PAGE, CH341UsbStop)
#pragma alloc_text(PAGE, CH341UsbSetLine)
#pragma alloc_text(PAGE, CH341UsbRead)
#pragma alloc_text(PAGE, CH341UsbWrite)
#endif /* defined ALLOC_PRAGMA */

static
NTSTATUS
CH341UsbSubmitUrb(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PURB Urb) {
    NTSTATUS Status;
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Urb=%p\n",
                        __FUNCTION__, DeviceObject,    Urb);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        DeviceExtension->LowerDevice,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp) {
        CH341Error(         "%s. Allocating IRP for submitting URB failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    IoStack = IoGetNextIrpStackLocation(Irp);
    NT_ASSERT(IoStack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL);
    IoStack->Parameters.Others.Argument1 = Urb;
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        NT_ASSERT(Status == STATUS_SUCCESS);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
CH341UsbGetDescriptor(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR DescriptorType,
    _Out_ PVOID *Buffer,
    _Inout_ PULONG BufferLength) {
    NTSTATUS Status;
    PURB Urb;
    PAGED_CODE();
    NT_ASSERT(Buffer);
    NT_ASSERT(BufferLength);
    NT_ASSERT(*BufferLength > 0);
    CH341Debug(         "%s. DeviceObject=%p, DescriptorType=%u, Buffer=%p, BufferLength=%p\n",
                        __FUNCTION__, DeviceObject,    DescriptorType,    Buffer,    BufferLength);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    *Buffer = ExAllocatePoolWithTag(NonPagedPool, *BufferLength, CH341_TAG);
    if (!*Buffer) {
        CH341Error(         "%s. Allocating URB transfer buffer of size %lu failed\n",
                            __FUNCTION__, *BufferLength);
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UsbBuildGetDescriptorRequest(Urb,
                                 sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 DescriptorType,
                                 0,
                                 0,
                                 *Buffer,
                                 NULL,
                                 *BufferLength,
                                 NULL);
    Status = CH341UsbSubmitUrb(DeviceObject, Urb);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSubmitUrb failed with %08lx, %08lx\n",
                            __FUNCTION__, Status, Urb->UrbHeader.Status);
        ExFreePoolWithTag(*Buffer, CH341_TAG);
        *Buffer = NULL;
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        *BufferLength = 0;
        return Status;
    }
    if (!USBD_SUCCESS(Urb->UrbHeader.Status)) {
        CH341Error(         "%s. Urb failed with %08lx\n",
                            __FUNCTION__, Urb->UrbHeader.Status);
        Status = Urb->UrbHeader.Status;
        ExFreePoolWithTag(*Buffer, CH341_TAG);
        *Buffer = NULL;
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        *BufferLength = 0;
        return Status;
    }
    *BufferLength = Urb->UrbControlDescriptorRequest.TransferBufferLength;
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return Status;
}

static
NTSTATUS
CH341UsbVendorRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ UCHAR *Buffer,
    _In_ USHORT Value,
    _In_ USHORT Index) {
    NTSTATUS Status;
    PURB Urb;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Buffer=%p, Value=0x%x, Index=0x%x\n",
                        __FUNCTION__, DeviceObject,    Buffer,    Value,      Index);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UsbBuildVendorRequest(Urb,
                          URB_FUNCTION_VENDOR_DEVICE,
                          sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                          USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                          0,
                          CH341_VENDOR_READ_REQUEST,
                          Value,
                          Index,
                          Buffer,
                          NULL,
                          1,
                          NULL);
    Status = CH341UsbSubmitUrb(DeviceObject, Urb);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSubmitUrb failed with %08lx, %08lx\n",
                            __FUNCTION__, Status, Urb->UrbHeader.Status);
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    if (!USBD_SUCCESS(Urb->UrbHeader.Status)) {
        CH341Error(         "%s. URB failed with %08lx\n",
                            __FUNCTION__, Urb->UrbHeader.Status);
        Status = Urb->UrbHeader.Status;
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    CH341Debug(         "%s. Vendor Read 0x%x/0x%x returned length %lu: 0x%x\n",
                        __FUNCTION__, Value,
                        Index,
                        Urb->UrbControlVendorClassRequest.TransferBufferLength,
                        Buffer[0]);
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return Status;
}

static
NTSTATUS
CH341UsbVendorWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ USHORT Value,
    _In_ USHORT Index) {
    NTSTATUS Status;
    PURB Urb;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Value=0x%x, Index=0x%x\n",
                        __FUNCTION__, DeviceObject,    Value,      Index);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UsbBuildVendorRequest(Urb,
                          URB_FUNCTION_VENDOR_DEVICE,
                          sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                          USBD_TRANSFER_DIRECTION_OUT,
                          0,
                          CH341_VENDOR_WRITE_REQUEST,
                          Value,
                          Index,
                          NULL,
                          NULL,
                          0,
                          NULL);
    Status = CH341UsbSubmitUrb(DeviceObject, Urb);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSubmitUrb failed with %08lx, %08lx\n",
                            __FUNCTION__, Status, Urb->UrbHeader.Status);
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    if (!USBD_SUCCESS(Urb->UrbHeader.Status)) {
        CH341Error(         "%s. URB failed with %08lx\n",
                            __FUNCTION__, Urb->UrbHeader.Status);
        Status = Urb->UrbHeader.Status;
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return Status;
}

static
NTSTATUS
CH341UsbConfigureDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor,
    _In_ PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor) {
    NTSTATUS Status;
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PURB Urb;
    USBD_INTERFACE_LIST_ENTRY InterfaceList[2];
    ULONG i;
    PUSBD_PIPE_INFORMATION PipeInfo;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, ConfigDescriptor=%p, InterfaceDescriptor=%p\n",
                        __FUNCTION__, DeviceObject,    ConfigDescriptor,    InterfaceDescriptor);
    RtlZeroMemory(InterfaceList, sizeof(InterfaceList));
    InterfaceList[0].InterfaceDescriptor = InterfaceDescriptor;
    Urb = USBD_CreateConfigurationRequestEx(ConfigDescriptor,
                                            InterfaceList);
    if (!Urb) {
        CH341Error(         "%s. USBD_CreateConfigurationRequestEx failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = CH341UsbSubmitUrb(DeviceObject, Urb);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSubmitUrb failed with %08lx, %08lx\n",
                            __FUNCTION__, Status, Urb->UrbHeader.Status);
        ExFreePoolWithTag(Urb, 0);
        return Status;
    }
    CH341Debug(         "%s. NumberOfPipes=%u\n",
                        __FUNCTION__, Urb->UrbSelectConfiguration.Interface.NumberOfPipes);
    for (i = 0; i < Urb->UrbSelectConfiguration.Interface.NumberOfPipes; i++) {
        PipeInfo = &Urb->UrbSelectConfiguration.Interface.Pipes[i];
        if (PipeInfo->PipeType == UsbdPipeTypeBulk &&
                USB_ENDPOINT_DIRECTION_IN(PipeInfo->EndpointAddress) &&
                !DeviceExtension->BulkInPipe) {
            DeviceExtension->BulkInPipe = PipeInfo->PipeHandle;
        }
        if (PipeInfo->PipeType == UsbdPipeTypeBulk &&
                USB_ENDPOINT_DIRECTION_OUT(PipeInfo->EndpointAddress) &&
                !DeviceExtension->BulkOutPipe) {
            DeviceExtension->BulkOutPipe = PipeInfo->PipeHandle;
        }
        if (PipeInfo->PipeType == UsbdPipeTypeInterrupt &&
                USB_ENDPOINT_DIRECTION_IN(PipeInfo->EndpointAddress) &&
                !DeviceExtension->InterruptInPipe) {
            DeviceExtension->InterruptInPipe = PipeInfo->PipeHandle;
        }
    }
    if (!DeviceExtension->BulkInPipe ||
            !DeviceExtension->BulkOutPipe ||
            !DeviceExtension->InterruptInPipe) {
        CH341Error(         "%s. Invalid endpoint combination\n",
                            __FUNCTION__, Status);
        NT_ASSERT(DeviceExtension->BulkInPipe &&
                  DeviceExtension->BulkOutPipe &&
                  DeviceExtension-> InterruptInPipe);
        ExFreePoolWithTag(Urb, 0);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    ExFreePoolWithTag(Urb, 0);
    return Status;
}

static
NTSTATUS
CH341UsbUnconfigureDevice(
    _In_ PDEVICE_OBJECT DeviceObject) {
    NTSTATUS Status;
    PURB Urb;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p\n",
                        __FUNCTION__, DeviceObject);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_SELECT_CONFIGURATION),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UsbBuildSelectConfigurationRequest(Urb,
                                       sizeof(struct _URB_SELECT_CONFIGURATION),
                                       NULL);
    Status = CH341UsbSubmitUrb(DeviceObject, Urb);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSubmitUrb failed with %08lx, %08lx\n",
                            __FUNCTION__, Status, Urb->UrbHeader.Status);
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return Status;
}

NTSTATUS
CH341UsbStart(
    _In_ PDEVICE_OBJECT DeviceObject) {
    NTSTATUS Status;
    PVOID Descriptor;
    ULONG DescriptorLength;
    PUSB_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    UCHAR Buffer[1];
    PDEVICE_EXTENSION DeviceExtension;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p\n",
                        __FUNCTION__, DeviceObject);
    DeviceExtension = DeviceObject->DeviceExtension;
    DescriptorLength = sizeof(USB_DEVICE_DESCRIPTOR);
    Status = CH341UsbGetDescriptor(DeviceObject,
                                   USB_DEVICE_DESCRIPTOR_TYPE,
                                   &Descriptor,
                                   &DescriptorLength);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbGetDescriptor failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    NT_ASSERT(DescriptorLength == sizeof(USB_DEVICE_DESCRIPTOR));
    DeviceDescriptor = Descriptor;
    CH341Debug(         "%s. Device descriptor: "
                        "bLength=%u, "
                        "bDescriptorType=%u, "
                        "bcdUSB=0x%x, "
                        "bDeviceClass=0x%x, "
                        "bDeviceSubClass=0x%x, "
                        "bDeviceProtocol=0x%x, "
                        "bMaxPacketSize0=%u, "
                        "idVendor=0x%x, "
                        "idProduct=0x%x, "
                        "bcdDevice=0x%x, "
                        "iManufacturer=%u, "
                        "iProduct=%u, "
                        "iSerialNumber=%u, "
                        "bNumConfigurations=%u\n",
                        __FUNCTION__, DeviceDescriptor->bLength,
                        DeviceDescriptor->bDescriptorType,
                        DeviceDescriptor->bcdUSB,
                        DeviceDescriptor->bDeviceClass,
                        DeviceDescriptor->bDeviceSubClass,
                        DeviceDescriptor->bDeviceProtocol,
                        DeviceDescriptor->bMaxPacketSize0,
                        DeviceDescriptor->idVendor,
                        DeviceDescriptor->idProduct,
                        DeviceDescriptor->bcdDevice,
                        DeviceDescriptor->iManufacturer,
                        DeviceDescriptor->iProduct,
                        DeviceDescriptor->iSerialNumber,
                        DeviceDescriptor->bNumConfigurations);
    /* We only support CH341 HX right now */
    NT_ASSERT(DeviceDescriptor->bDeviceClass != USB_DEVICE_CLASS_COMMUNICATIONS);
    NT_ASSERT(DeviceDescriptor->bMaxPacketSize0 == 64);
    ExFreePoolWithTag(Descriptor, CH341_TAG);
    DescriptorLength = sizeof(USB_CONFIGURATION_DESCRIPTOR);
    Status = CH341UsbGetDescriptor(DeviceObject,
                                   USB_CONFIGURATION_DESCRIPTOR_TYPE,
                                   &Descriptor,
                                   &DescriptorLength);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbGetDescriptor failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    NT_ASSERT(DescriptorLength == sizeof(USB_CONFIGURATION_DESCRIPTOR));
    ConfigDescriptor = Descriptor;
    NT_ASSERT(ConfigDescriptor->wTotalLength != 0);
    DescriptorLength = ConfigDescriptor->wTotalLength;
    ExFreePoolWithTag(Descriptor, CH341_TAG);
    Status = CH341UsbGetDescriptor(DeviceObject,
                                   USB_CONFIGURATION_DESCRIPTOR_TYPE,
                                   &Descriptor,
                                   &DescriptorLength);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbGetDescriptor failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    ConfigDescriptor = Descriptor;
    NT_ASSERT(DescriptorLength == ConfigDescriptor->wTotalLength);
    CH341Debug(         "%s. Config descriptor: "
                        "bLength=%u, "
                        "bDescriptorType=%u, "
                        "wTotalLength=%u, "
                        "bNumInterfaces=%u, "
                        "bConfigurationValue=%u, "
                        "iConfiguration=%u, "
                        "bmAttributes=0x%x, "
                        "MaxPower=%u\n",
                        __FUNCTION__, ConfigDescriptor->bLength,
                        ConfigDescriptor->bDescriptorType,
                        ConfigDescriptor->wTotalLength,
                        ConfigDescriptor->bNumInterfaces,
                        ConfigDescriptor->bConfigurationValue,
                        ConfigDescriptor->iConfiguration,
                        ConfigDescriptor->bmAttributes,
                        ConfigDescriptor->MaxPower);
    if (ConfigDescriptor->bNumInterfaces != 1) {
        CH341Error(         "%s. Configuration contains %u interfaces, expected one\n",
                            __FUNCTION__, ConfigDescriptor->bNumInterfaces);
        ExFreePoolWithTag(Descriptor, CH341_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(ConfigDescriptor,
                          ConfigDescriptor,
                          -1,
                          -1,
                          -1,
                          -1,
                          -1);
    if (!InterfaceDescriptor) {
        CH341Error(         "%s. USBD_ParseConfigurationDescriptorEx failed\n",
                            __FUNCTION__);
        ExFreePoolWithTag(Descriptor, CH341_TAG);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    CH341Debug(         "%s. Interface descriptor: "
                        "bLength=%u, "
                        "bDescriptorType=%u, "
                        "bInterfaceNumber=%u, "
                        "bAlternateSetting=%u, "
                        "bNumEndpoints=%u, "
                        "bInterfaceClass=0x%x, "
                        "bInterfaceSubClass=0x%x, "
                        "bInterfaceProtocol=0x%x, "
                        "iInterface=%u\n",
                        __FUNCTION__, InterfaceDescriptor->bLength,
                        InterfaceDescriptor->bDescriptorType,
                        InterfaceDescriptor->bInterfaceNumber,
                        InterfaceDescriptor->bAlternateSetting,
                        InterfaceDescriptor->bNumEndpoints,
                        InterfaceDescriptor->bInterfaceClass,
                        InterfaceDescriptor->bInterfaceSubClass,
                        InterfaceDescriptor->bInterfaceProtocol,
                        InterfaceDescriptor->iInterface);
    Status = CH341UsbConfigureDevice(DeviceObject, ConfigDescriptor, InterfaceDescriptor);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbConfigureDevice failed with %08lx\n",
                            __FUNCTION__, Status);
        ExFreePoolWithTag(Descriptor, CH341_TAG);
        return Status;
    }
    ExFreePoolWithTag(Descriptor, CH341_TAG);
    /* TODO: Buffer should probably be nonpaged */
    Status = CH341UsbVendorRead(DeviceObject, Buffer, 0x8484, 0); // expect: 2
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorRead[1] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorWrite(DeviceObject, 0x0404, 0);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorWrite[2] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorRead(DeviceObject, Buffer, 0x8484, 0); // expect: 2
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorRead[3] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorRead(DeviceObject, Buffer, 0x8383, 0); // expect: 0
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorRead[4] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorRead(DeviceObject, Buffer, 0x8484, 0); // expect: 2
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorRead[5] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorWrite(DeviceObject, 0x0404, 0);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorWrite[6] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorRead(DeviceObject, Buffer, 0x8484, 0); // expect: 2
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorRead[7] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorRead(DeviceObject, Buffer, 0x8383, 0); // expect: 0
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorRead[8] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorWrite(DeviceObject, 0, 1);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorWrite[9] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorWrite(DeviceObject, 1, 0);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorWrite[10] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    Status = CH341UsbVendorWrite(DeviceObject, 2, 0x44); // non-HX has 0x24 here instead of 0x44
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbVendorWrite[11] failed with %08lx\n",
                            __FUNCTION__, Status);
        return Status;
    }
    return Status;
}

NTSTATUS
CH341UsbStop(
    _In_ PDEVICE_OBJECT DeviceObject) {
    NTSTATUS Status;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p\n",
                        __FUNCTION__, DeviceObject);
    Status = CH341UsbUnconfigureDevice(DeviceObject);
    return Status;
}

NTSTATUS
CH341UsbSetLine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG BaudRate,
    _In_ UCHAR StopBits,
    _In_ UCHAR Parity,
    _In_ UCHAR DataBits) {
    NTSTATUS Status;
    struct _LINE {
        ULONG BaudRate;
        UCHAR StopBits;
        UCHAR Parity;
        UCHAR DataBits;
    } Line;
    PURB Urb;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, BaudRate=%lu, StopBits=%u, Parity=%u, "
                        "DataBits=%u\n",
                        __FUNCTION__, DeviceObject,    BaudRate,     StopBits,    Parity,
                        DataBits);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UsbBuildVendorRequest(Urb,
                          URB_FUNCTION_CLASS_DEVICE,
                          sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                          USBD_TRANSFER_DIRECTION_OUT,
                          0,
                          CH341_SET_LINE_REQUEST,
                          0,
                          0,
                          &Line,
                          NULL,
                          sizeof(Line),
                          NULL);
    /* TODO: this should probably be nonpaged */
    Line.BaudRate = BaudRate;
    Line.StopBits = StopBits;
    Line.Parity = Parity;
    Line.DataBits = DataBits;
    Status = CH341UsbSubmitUrb(DeviceObject, Urb);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSubmitUrb failed with %08lx, %08lx\n",
                            __FUNCTION__, Status, Urb->UrbHeader.Status);
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    if (!USBD_SUCCESS(Urb->UrbHeader.Status)) {
        CH341Error(         "%s. URB failed with %08lx\n",
                            __FUNCTION__, Urb->UrbHeader.Status);
        Status = Urb->UrbHeader.Status;
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return Status;
}

NTSTATUS
CH341UsbSetControlLines(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ USHORT DtrRts) {
    NTSTATUS Status;
    PURB Urb;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, DtrRts=%u\n",
                        __FUNCTION__, DeviceObject,    DtrRts);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    NT_ASSERT((DtrRts & ~(SERIAL_DTR_STATE | SERIAL_RTS_STATE)) == 0);
    UsbBuildVendorRequest(Urb,
                          URB_FUNCTION_CLASS_DEVICE,
                          sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
                          USBD_TRANSFER_DIRECTION_OUT,
                          0,
                          CH341_SET_CONTROL_REQUEST,
                          DtrRts,
                          0,
                          NULL,
                          NULL,
                          0,
                          NULL);
    Status = CH341UsbSubmitUrb(DeviceObject, Urb);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. CH341UsbSubmitUrb failed with %08lx, %08lx\n",
                            __FUNCTION__, Status, Urb->UrbHeader.Status);
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    if (!USBD_SUCCESS(Urb->UrbHeader.Status)) {
        CH341Error(         "%s. URB failed with %08lx\n",
                            __FUNCTION__, Urb->UrbHeader.Status);
        Status = Urb->UrbHeader.Status;
        ExFreePoolWithTag(Urb, CH341_URB_TAG);
        return Status;
    }
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return Status;
}

_Function_class_(IO_COMPLETION_ROUTINE)
static
NTSTATUS
NTAPI
CH341UsbReadCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_(sizeof(URB)) PVOID Context) {
    PURB Urb = Context;
    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p, Context=%p\n",
                        __FUNCTION__, DeviceObject,    Irp,    Context);
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        if (USBD_SUCCESS(Urb->UrbHeader.Status))
            Irp->IoStatus.Information = Urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
        else
            CH341Warn(         "%s. URB failed with %08lx\n",
                               __FUNCTION__, Urb->UrbHeader.Status);
    } else {
        CH341Warn(         "%s. IRP failed with %08lx\n",
                           __FUNCTION__, Irp->IoStatus.Status);
    }
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
CH341UsbRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp) {
    NTSTATUS Status;
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PURB Urb;
    PIO_STACK_LOCATION IoStack;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    UsbBuildInterruptOrBulkTransferRequest(Urb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           DeviceExtension->BulkInPipe,
                                           Irp->AssociatedIrp.SystemBuffer,
                                           NULL,
                                           IoStack->Parameters.Read.Length,
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                           NULL);
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = Urb;
    Status = IoSetCompletionRoutineEx(DeviceObject,
                                      Irp,
                                      CH341UsbReadCompletion,
                                      Urb,
                                      TRUE,
                                      TRUE,
                                      TRUE);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. IoSetCompletionRoutineEx failed with %08lx\n",
                            __FUNCTION__, Status);
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoMarkIrpPending(Irp);
    (VOID)IoCallDriver(DeviceExtension->LowerDevice, Irp);
    return STATUS_PENDING;
}


_Function_class_(IO_COMPLETION_ROUTINE)
static
NTSTATUS
NTAPI
CH341UsbWriteCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_(sizeof(URB)) PVOID Context) {
    PURB Urb = Context;
    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p, Context=%p\n",
                        __FUNCTION__, DeviceObject,    Irp,    Context);
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        if (USBD_SUCCESS(Urb->UrbHeader.Status))
            Irp->IoStatus.Information = Urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
        else
            CH341Warn(         "%s. URB failed with %08lx\n",
                               __FUNCTION__, Urb->UrbHeader.Status);
    } else {
        CH341Warn(         "%s. IRP failed with %08lx\n",
                           __FUNCTION__, Irp->IoStatus.Status);
    }
    ExFreePoolWithTag(Urb, CH341_URB_TAG);
    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
CH341UsbWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp) {
    NTSTATUS Status;
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PURB Urb;
    PIO_STACK_LOCATION IoStack;
    PAGED_CODE();
    CH341Debug(         "%s. DeviceObject=%p, Irp=%p\n",
                        __FUNCTION__, DeviceObject,    Irp);
    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                CH341_URB_TAG);
    if (!Urb) {
        CH341Error(         "%s. Allocating URB failed\n",
                            __FUNCTION__);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    UsbBuildInterruptOrBulkTransferRequest(Urb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           DeviceExtension->BulkOutPipe,
                                           Irp->AssociatedIrp.SystemBuffer,
                                           NULL,
                                           IoStack->Parameters.Write.Length,
                                           USBD_TRANSFER_DIRECTION_OUT,
                                           NULL);
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = Urb;
    Status = IoSetCompletionRoutineEx(DeviceObject,
                                      Irp,
                                      CH341UsbWriteCompletion,
                                      Urb,
                                      TRUE,
                                      TRUE,
                                      TRUE);
    if (!NT_SUCCESS(Status)) {
        CH341Error(         "%s. IoSetCompletionRoutineEx failed with %08lx\n",
                            __FUNCTION__, Status);
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoMarkIrpPending(Irp);
    (VOID)IoCallDriver(DeviceExtension->LowerDevice, Irp);
    return STATUS_PENDING;
}