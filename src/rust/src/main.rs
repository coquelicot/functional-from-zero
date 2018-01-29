extern crate functional_from_zero;

use std::io;

use functional_from_zero::{tokenizer, parser, transformer, interpreter};
use functional_from_zero::bitio;

fn read_code_without_comment() -> String {
    let stdin = io::stdin();
    let mut line = String::new();
    let mut code = String::new();

    while stdin.read_line(&mut line).expect("Invalid UTF-8 in input.") > 0 {
        if !line.starts_with('#') {
            code.push_str(&line);
        }
        line.clear();
    }
    code
}

fn main() {
    let code = read_code_without_comment();
    let tokens = tokenizer::tokenize(&code);
    println!("tokens = {:?}", tokens);
    match parser::parse(&tokens) {
        Ok(node) => {
            println!("node = {:?}", node);
            let (expression, free_vars) = transformer::transform(node);
            println!("expression = {:?}", expression);
            println!("free_vars = {:?}", free_vars);
        },
        Err(error) => println!("Error when parsing expression: {}", error),
    }
}
