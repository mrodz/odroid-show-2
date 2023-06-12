use std::{fmt::Debug, marker::PhantomData, time::{Duration, Instant}};

use anyhow::{bail, Context, Result};
use rusb::{
    ConfigDescriptor, DeviceDescriptor, DeviceHandle, Direction,
    GlobalContext, Interface, TransferType,
};

pub struct InterfaceWrapper<'a> {
    channel: u8,
    lifetime: PhantomData<*mut Interface<'a>>,
}

impl<'a> InterfaceWrapper<'a> {
    pub fn new(channel: u8) -> Self {
        Self {
            channel,
            lifetime: PhantomData,
        }
    }

    /// returns (Read, Write)
    fn get_io_endpoints(
        &self,
        config_descriptor: &ConfigDescriptor,
    ) -> Option<(Endpoint, Endpoint)> {
        let interface = config_descriptor.interfaces().nth(self.channel as usize)?;

        let (mut read, mut write) = (None, None);

        let mut c = 0;

        for interface_descriptor in interface.descriptors() {
            for endpoint_descriptor in interface_descriptor.endpoint_descriptors() {
                let e = Endpoint {
                    config: config_descriptor.number(),
                    iface: interface_descriptor.interface_number(),
                    setting: interface_descriptor.setting_number(),
                    address: endpoint_descriptor.address(),
                };

                match (
                    endpoint_descriptor.transfer_type(),
                    endpoint_descriptor.direction(),
                ) {
                    // (_, Direction::In) => {
                    (TransferType::Bulk, Direction::In) => {
                        read = Some(e);
                        c += 1;
                    }
                    // (_, Direction::Out) => {
                    (TransferType::Bulk, Direction::Out) => {
                        write = Some(e);
                        c += 1;
                    }
                    _ => continue,
                }
            }
        }

        dbg!(c);
        if let (Some(read), Some(write)) = (read, write) {
            Some((read, write))
        } else {
            None
        }
    }
}

pub struct OdroidShow2<'a> {
    descriptor: DeviceDescriptor,
    config_descriptor: ConfigDescriptor,
    handle: DeviceHandle<GlobalContext>,
    interface_channel: InterfaceWrapper<'a>,
    io_out: Endpoint,
    io_in: Endpoint,
}

type Device = rusb::Device<GlobalContext>;

#[allow(unused)]
#[derive(Debug, PartialEq)]
enum OdroidShow2PacketState {
    Unsent,
    RequestSent,
    ReadyToSend,
    DoNotSend,
    Sent,
}

const ACKNOWLEDGE_BYTE: u8 = 0x06;

/// https://www.beyondlogic.org/usbnutshell/usb1.shtml
/// 
/// 
/// ![Show2 Protocol](https://dn.odroid.com/ODROID-SHOW/show_protocol.png)
pub(in crate::driver) struct OdroidShow2Packet<'device, 'data> {
    device: &'device OdroidShow2<'device>,
    length: u8,
    payload: &'data str,
    state: OdroidShow2PacketState,
}

/// Internal endpoint representations
#[derive(Debug, PartialEq, Clone)]
struct Endpoint {
    config: u8,
    iface: u8,
    setting: u8,
    address: u8,
}

impl<'device, 'data> OdroidShow2Packet<'device, 'data> {
    fn new(payload: &'data str, device: &'device OdroidShow2<'device>) -> Result<Self> {
        Ok(Self {
            device,
            length: payload.len().try_into().context("payload is too big! max size: 255")?,
            payload,
            state: OdroidShow2PacketState::Unsent,
        })
    }

    pub(in crate::driver) fn start_transaction(&mut self, timeout: Duration) -> Result<usize> {
        assert_eq!(self.state, OdroidShow2PacketState::Unsent);

        let handle = &self.device.handle;

        let write_endpoint = &self.device.io_out;
        let read_endpoint = &self.device.io_in;

        let bytes_sent = handle
            .write_bulk(write_endpoint.address, &[ACKNOWLEDGE_BYTE], timeout)
            .context("Could not write to USB")?;
        
        assert_eq!(bytes_sent, 1);

        let bytes_sent = handle
            .write_bulk(write_endpoint.address, &[self.length], timeout)
            .context("Could not write to USB")?;

        assert_eq!(bytes_sent, 1);

        let mut buffer = [0; 1];

        let bytes_read = handle.read_bulk(read_endpoint.address, &mut buffer, Duration::from_millis(0))?;

        if bytes_read != 1 || buffer[0] != ACKNOWLEDGE_BYTE {
            bail!("Protocol Error: The Odroid Show did not acknowledge the write request")
        }

        // loop {
        //     let bytes_read = handle.read_bulk(read_endpoint.address, &mut buffer, Duration::from_millis(10));
        //     match bytes_read {
        //         Ok(bytes_read) => {
                    
        //             break;
        //         }
        //         Err(rusb::Error::Timeout) if start.elapsed() < timeout => continue,
        //         error => error.context("waiting for signal...")?,
        //     };
        // }

        Ok(bytes_sent)
    }

    pub(in crate::driver) fn check_is_ready_to_recieve(&mut self, timeout: Duration) -> Result<()> {
        assert_eq!(self.state, OdroidShow2PacketState::RequestSent);

        let handle = &self.device.handle;

        let read_endpoint = &self.device.io_in;

        let mut buffer = [0; 1];

        let start = Instant::now();

        loop {
            let Ok(bytes_read) = handle.read_bulk(read_endpoint.address, &mut buffer, Duration::from_millis(10)) else {
                if start.elapsed() >= timeout {
                    bail!("timeout")
                } else {
                    continue;
                }
            };

            if bytes_read != 1 || buffer[0] != ACKNOWLEDGE_BYTE {
                bail!("Protocol Error: The Odroid Show did not acknowledge the write request")
            }
    
            self.state = OdroidShow2PacketState::ReadyToSend;

            break;
        }

        Ok(())
    }
}

impl<'device> OdroidShow2<'device> {
    pub fn find_device(vid: u16, pid: u16) -> Result<Self> {
        Self::find_device_with_interface(vid, pid, None)
    }

    pub fn find_device_with_interface(
        vid: u16,
        pid: u16,
        interface_channel: Option<u8>,
    ) -> Result<Self> {
        let (device, descriptor) = match_device_vid_pid(vid, pid)?;

        let config_descriptor = device
            .active_config_descriptor()
            .context("Could not get config descriptor")?;

        let mut handle = device.open()
            .context("Found the device, but it could not be opened. This could be a driver issue: see <https://github.com/libusb/libusb/wiki/Windows#how-to-use-libusb-on-windows> for more details")?;

        let interface_idx = if let Some(interface) = interface_channel {
            interface
        } else {
            let num_interfaces = config_descriptor.num_interfaces();
            if num_interfaces != 1 {
                bail!("This USB device has {num_interfaces} interfaces to choose from. Specify an interface by using `OdroidShow2::find_device_with_interface`")
            } else {
                0
            }
        };

        let interface_channel = InterfaceWrapper::new(interface_idx);

        handle.claim_interface(dbg!(interface_idx))?;

        let (io_in, io_out) = interface_channel
            .get_io_endpoints(&config_descriptor)
            .context("could not get write endpoint")?;

        dbg!(&io_out, &io_in);

        Ok(Self {
            descriptor,
            config_descriptor: config_descriptor,
            handle,
            interface_channel,
            io_in,
            io_out,
        })
    }

    pub(in crate::driver) fn into_packet<'data: 'device>(
        &'device self,
        data: &'data str,
    ) -> Result<OdroidShow2Packet<'device, 'data>> {
        OdroidShow2Packet::new(data, self)
    }
}

impl Debug for OdroidShow2<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // let product_string = self.handle.read_product_string_ascii(&self.descriptor)
        f.debug_struct("OdroidShow-2")
            .field(
                "protocol",
                &format!("USB {}", self.descriptor.usb_version()),
            )
            .field("vid", &self.descriptor.vendor_id())
            .field("pid", &self.descriptor.product_id())
            .field(
                "connection",
                &self.handle.read_product_string_ascii(&self.descriptor),
            )
            .finish_non_exhaustive()
    }
}

fn match_device_vid_pid(vid: u16, pid: u16) -> Result<(Device, DeviceDescriptor)> {
    for device in rusb::devices()?.iter() {
        let device_desc = device.device_descriptor()?;
        if device_desc.vendor_id() == vid && device_desc.product_id() == pid {
            return Ok((device, device_desc));
        }
    }

    bail!("device not found")
}

#[cfg(test)]
pub(in crate::driver) fn debug_devices() -> Result<Vec<String>> {
    let mut result = vec![];
    for device in rusb::devices().unwrap().iter() {
        let device_desc: DeviceDescriptor = device.device_descriptor().unwrap();
        let config_desc = device
            .active_config_descriptor()
            .context("failed getting config descriptor")?;

        let interfaces: Vec<_> = config_desc.interfaces().collect();
        let interfaces: Vec<_> = interfaces.iter().map(|x| x.number()).collect();

        let Ok(handle) = device.open() else {
            continue;
        };

        let name: String = handle
            .read_product_string_ascii(&device_desc)
            .context("failed reading product name")?;

        result.push(format!(
            "Bus {:03} Device {:03} ID {:04x}:{:04x} Interfaces = {interfaces:?} {name:?}",
            device.bus_number(),
            device.address(),
            device_desc.vendor_id(),
            device_desc.product_id(),
        ));
    }

    Ok(result)
}
