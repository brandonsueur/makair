// MakAir
//
// Copyright: 2020, Makers For Life
// License: Public Domain License

use std::sync::mpsc::{Receiver, Sender, TryRecvError};

use telemetry::serial::core::{Error, ErrorKind};
use telemetry::structures::TelemetryMessage;
use telemetry::{self, TelemetryChannelType};

pub struct SerialPollerBuilder;
pub struct SerialPoller;

#[derive(Debug)]
pub enum PollEvent {
    Ready(TelemetryMessage),
    Pending,
}

#[allow(clippy::new_ret_no_self)]
impl SerialPollerBuilder {
    pub fn new() -> SerialPoller {
        SerialPoller {}
    }
}

impl SerialPoller {
    pub fn poll(
        &mut self,
        rx: &Receiver<TelemetryChannelType>,
        warp10tx: &Option<Sender<TelemetryMessage>>,
    ) -> Result<PollEvent, Error> {
        match rx.try_recv() {
            Ok(message) => match message {
                Ok(message) => {
                    if let Some(tx) = warp10tx {
                        tx.send(message.clone()).unwrap_or(());
                    }
                    Ok(PollEvent::Ready(message))
                }
                Err(serial_error) => Err(serial_error),
            },
            Err(TryRecvError::Empty) => Ok(PollEvent::Pending),
            Err(TryRecvError::Disconnected) => {
                Err(Error::new(ErrorKind::NoDevice, "device is disconnected"))
            }
        }
    }
}
