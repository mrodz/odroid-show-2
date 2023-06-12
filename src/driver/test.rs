#![cfg(test)]

use std::time::Duration;

use super::usb::{debug_devices, OdroidShow2};

const VID: u16 = 0x10c4;
const PID: u16 = 0xea60;
const TIMEOUT: Duration = Duration::from_millis(500);

#[test]
#[ignore = "VID/PID will be different per machine. Tweak this test to suit your needs."]
fn send_acknowledge_byte() {
	let odroid: OdroidShow2<'_> = OdroidShow2::find_device(VID, PID).unwrap();

	let mut packet = odroid.into_packet("Lorem Ipsum").unwrap();

	packet.start_transaction(TIMEOUT).unwrap();
}

// #[test]
// #[ignore = "VID/PID will be different per machine. Tweak this test to suit your needs."]
// fn can_process_request_to_send_data() {
// 	let odroid: OdroidShow2<'_> = OdroidShow2::find_device(VID, PID).unwrap();

// 	let mut packet = odroid.into_packet("Lorem Ipsum");

// 	let handshake_bytes_sent = packet.start_transaction(TIMEOUT).unwrap();
// 	dbg!(handshake_bytes_sent);

// 	packet.check_is_ready_to_recieve(TIMEOUT).unwrap();
// }

#[test]
#[ignore = "VID/PID will be different per machine. Tweak this test to suit your needs."]
fn can_connect_to_arbitrary_vid_pid() {
	let odroid: OdroidShow2<'_> = OdroidShow2::find_device(VID, PID).unwrap();
	dbg!(odroid);
}

#[test]
fn can_list_devices() {
	let devices = debug_devices().unwrap();
	dbg!(devices);
}