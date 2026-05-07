#![no_std]

use bitfield::bitfield;
use embedded_hal::blocking::i2c::{Write, WriteRead};

/// Chip-specific constants (addresses, IDs)
mod chip {
    pub const I2C: u8 = 0x1B << 1;
    pub const ID: u8 = 0x2E;
    pub const ID_ADDR: u8 = 0;
    pub const STATUS_ADDR: u8 = 2;
    pub const KEY_STATUS_ADDR: u8 = 3;
    pub const CALIBRATE_ADDR: u8 = 56;
}

/// Errors that can occur when communicating with the AT42QT1070
#[derive(Clone, Copy, Debug)]
pub enum Error<I2cError> {
    /// An I2C bus error occurred
    I2cError(I2cError),
    /// The chip ID read from the device did not match the expected value (0x2E)
    IdMismatch(u8),
}

impl<E> From<E> for Error<E> {
    fn from(error: E) -> Self {
        Error::I2cError(error)
    }
}

bitfield! {
    /// Detection status register (address 2)
    pub struct Status(u8);
    impl Debug;
    /// Set while calibration is in progress
    pub calibrate, _: 7;
    /// Set if key signal overflows
    pub overflow, _: 6;
    /// Set if any key is in detect (touched)
    pub touch, _: 0;
}

bitfield! {
    /// Key status register (address 3) — one bit per key
    pub struct KeyStatus(u8);
    impl Debug;
    pub key6, _: 6;
    pub key5, _: 5;
    pub key4, _: 4;
    pub key3, _: 3;
    pub key2, _: 2;
    pub key1, _: 1;
    pub key0, _: 0;
}

/// Platform-agnostic driver for the AT42QT1070 capacitive touch sensor.
///
/// Communicates over I2C using the `embedded-hal` traits so it can be used
/// with any microcontroller that has a compatible HAL implementation.
pub struct Driver<I2C> {
    i2c: I2C,
}

impl<I2C, I2cError> Driver<I2C>
where
    I2C: WriteRead<Error = I2cError> + Write<Error = I2cError>,
{
    /// Create a new driver instance.
    ///
    /// Reads the chip ID register and returns `Error::IdMismatch` if the
    /// value does not equal the expected `0x2E`.
    pub fn new(i2c: I2C) -> Result<Driver<I2C>, Error<I2cError>> {
        let mut driver = Driver { i2c };

        let id = driver.get_id()?;
        if id != chip::ID {
            return Err(Error::IdMismatch(id));
        }

        Ok(driver)
    }

    /// Read the chip ID register (address 0). Expected value: `0x2E`.
    fn get_id(&mut self) -> Result<u8, Error<I2cError>> {
        let mut buffer = [0u8; 1];
        self.i2c
            .write_read(chip::I2C, &[chip::ID_ADDR], &mut buffer)?;
        Ok(buffer[0])
    }

    /// Read the detection status register (address 2).
    pub fn get_status(&mut self) -> Result<Status, Error<I2cError>> {
        let mut buffer = [0u8; 1];
        self.i2c
            .write_read(chip::I2C, &[chip::STATUS_ADDR], &mut buffer)?;
        Ok(Status(buffer[0]))
    }

    /// Read the key status register (address 3).
    pub fn get_key_status(&mut self) -> Result<KeyStatus, Error<I2cError>> {
        let mut buffer = [0u8; 1];
        self.i2c
            .write_read(chip::I2C, &[chip::KEY_STATUS_ADDR], &mut buffer)?;
        Ok(KeyStatus(buffer[0]))
    }

    /// Trigger a full recalibration of all keys.
    ///
    /// Writes `0xFF` to the calibrate register and then polls the status
    /// register until the calibrate bit is cleared.
    pub fn calibrate(&mut self) -> Result<(), Error<I2cError>> {
        self.i2c
            .write(chip::I2C, &[chip::CALIBRATE_ADDR, 0xFF])?;

        loop {
            let status = self.get_status()?;
            if !status.calibrate() {
                break;
            }
        }

        Ok(())
    }

    /// Consume the driver and return the underlying I2C peripheral.
    pub fn release(self) -> I2C {
        self.i2c
    }
}
