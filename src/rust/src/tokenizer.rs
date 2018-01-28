use regex::Regex;

#[derive(Debug)]
pub enum TokenType<'a> {
    LeftParen,
    RightParen,
    LambdaStart,
    Identifier(&'a str),
}

#[derive(Debug)]
pub struct Token<'a> {
    pub token_type: TokenType<'a>,
    // TODO(Darkpi): Add line number / column number.
}

pub fn tokenize(code: &str) -> Vec<Token> {
    lazy_static! {
        static ref TOKEN_RE: Regex = Regex::new(r"[()\\]|[[:^space:]&&[^()\\]]+").unwrap();
    }
    TOKEN_RE
        .captures_iter(code)
        .map(|cap| {
            let token = cap.get(0).unwrap().as_str();
            let token_type = match token {
                "(" => TokenType::LeftParen,
                ")" => TokenType::RightParen,
                r"\" => TokenType::LambdaStart,
                name => TokenType::Identifier(name),
            };
            Token { token_type }
        })
        .collect()
}
