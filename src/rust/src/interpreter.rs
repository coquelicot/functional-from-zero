use super::{parser, runner, tokenizer, transformer};

pub fn interpret(code: &str) {
    let tokens = tokenizer::tokenize(code);
    // println!("tokens = {:?}", tokens);
    match parser::parse(&tokens) {
        Ok(node) => {
            // println!("node = {:?}", node);
            let (expression, free_vars) = transformer::transform(&node);
            // println!("expression = {:?}", expression);
            // println!("free_vars = {:?}", free_vars);
            if let Err(error) = runner::run(&expression, &free_vars) {
                println!("* Runtime error:\n{}", error)
            }
        }
        Err(error) => println!("* Error when parsing expression:\n{}", error),
    }
}
