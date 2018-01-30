use std::io;
use std::io::{Read, Write};

#[derive(Debug)]
pub struct BitOutputStream {
    buffer_byte: u8,
    buffer_idx: i8,
}

impl BitOutputStream {
    pub fn new() -> BitOutputStream {
        BitOutputStream {
            buffer_byte: 0,
            buffer_idx: 7,
        }
    }
    pub fn write(&mut self, bit: u8) {
        assert!(bit <= 1);
        self.buffer_byte |= bit << self.buffer_idx;
        if self.buffer_idx == 0 {
            let mut stdout = io::stdout();
            stdout.write(&[self.buffer_byte]).unwrap();
            stdout.flush().unwrap();
            self.buffer_idx = 7;
            self.buffer_byte = 0;
        } else {
            self.buffer_idx -= 1;
        }
    }
}

#[derive(Debug)]
pub struct BitInputStream {
    buffer_byte: u8,
    buffer_idx: i8,
}

impl BitInputStream {
    pub fn new() -> BitInputStream {
        BitInputStream {
            buffer_byte: 0,
            buffer_idx: 0,
        }
    }
    pub fn read(&mut self) -> u8 {
        if self.buffer_idx == 0 {
            let mut buffer: [u8; 1] = [0u8];
            self.buffer_idx = 8;
            // TODO(Darkpi): Handle EOF.
            io::stdin().read_exact(&mut buffer).unwrap();
            self.buffer_byte = buffer[0];
        }
        self.buffer_idx -= 1;
        (self.buffer_byte >> self.buffer_idx) & 1u8
    }
}