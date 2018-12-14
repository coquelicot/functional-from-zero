extern crate functional_from_zero;

use std::{env, fs};
use std::io::Read;

use functional_from_zero::interpreter;

fn read_code() -> String {
    let filename = env::args().nth(1).expect("No filename given.");

    let mut code = String::new();
    fs::File::open(filename)
        .expect("Can't open file")
        .read_to_string(&mut code)
        .expect("Can't read file.");
    code
}

fn read_code_without_comment() -> String {
    let mut code = String::new();
    for line in read_code().lines() {
        if !line.trim().starts_with('#') {
            code.push_str(line);
        }
        code.push_str("\n")
    }
    code
}

fn main() {
    let code = read_code_without_comment();
    interpreter::interpret(&code);
}
