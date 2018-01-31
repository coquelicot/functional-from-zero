use std::fmt;

use regex::Regex;

#[derive(Debug)]
pub enum TokenType<'a> {
    LeftParen,
    RightParen,
    LambdaStart,
    Identifier(&'a str),
}

#[derive(Debug, Clone, Copy)]
pub struct Location {
    pub line_no: usize,
    pub column_no: usize,
}

impl fmt::Display for Location {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "line {}, column {}", self.line_no, self.column_no)
    }
}

#[derive(Debug)]
pub struct Token<'a> {
    pub token_type: TokenType<'a>,
    pub location: Location,
}

pub fn tokenize(code: &str) -> Vec<Token> {
    lazy_static! {
        static ref TOKEN_RE: Regex = Regex::new(r"[()\\]|\n|[[:space:]&&[^\n]]+|[[:^space:]&&[^()\\]]+").unwrap();
    }
    TOKEN_RE
        .captures_iter(code)
        .map(|cap| cap.get(0).unwrap().as_str())
        .scan(
            Location {
                line_no: 1,
                column_no: 1,
            },
            |location, token| {
                let ret = (*location, token);
                match token {
                    "\n" => {
                        location.line_no += 1;
                        location.column_no = 1;
                    }
                    _ => location.column_no += token.len(),
                }
                Some(ret)
            },
        )
        .filter_map(|(location, token)| {
            if token.chars().next().unwrap().is_whitespace() {
                return None;
            }
            let token_type = match token {
                "(" => TokenType::LeftParen,
                ")" => TokenType::RightParen,
                r"\" => TokenType::LambdaStart,
                name => TokenType::Identifier(name),
            };
            Some(Token {
                token_type,
                location,
            })
        })
        .collect()
}
