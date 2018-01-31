extern crate functional_from_zero;

use std::{env, fs};
use std::io::Read;

use functional_from_zero::{parser, runner, tokenizer, transformer};

fn read_code() -> String {
    let filename = env::args().skip(1).next().expect("No filename given.");

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
            code.push_str(&line);
        }
        code.push_str("\n")
    }
    code
}

fn main() {
    let code = read_code_without_comment();
    let tokens = tokenizer::tokenize(&code);
    // println!("tokens = {:?}", tokens);
    match parser::parse(&tokens) {
        Ok(node) => {
            // println!("node = {:?}", node);
            let (expression, free_vars) = transformer::transform(&node);
            // println!("expression = {:?}", expression);
            // println!("free_vars = {:?}", free_vars);
            if let Err(error) = runner::run(&expression, &free_vars) {
                println!("Runtime error: {}", error)
            }
        }
        Err(error) => println!("Error when parsing expression: {}", error),
    }
}
