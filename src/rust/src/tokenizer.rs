use std::fmt;

use regex::Regex;

#[derive(Debug)]
pub enum TokenType<'a> {
    LeftParen,
    RightParen,
    LambdaStart,
    Identifier(&'a str),
    WhiteSpace,
    EndOfFile,
}

#[derive(Debug, Clone)]
pub struct Location {
    pub line_no: usize,
    pub column_no: usize,
}

impl fmt::Display for Location {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "line {}, column {}", self.line_no, self.column_no)
    }
}

#[derive(Debug)]
pub struct Token<'a> {
    pub token_type: TokenType<'a>,
    pub location: Location,
    pub token_raw: &'a str,
}

impl<'a> fmt::Display for Token<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.location)?;
        if !self.token_raw.is_empty() {
            write!(f, " (near token '{}')", self.token_raw)?;
        }
        Ok(())
    }
}

fn is_whitespace(s: &str) -> bool {
    s.chars().all(|c| c.is_whitespace())
}

pub fn tokenize(code: &str) -> Vec<Token<'_>> {
    lazy_static! {
        static ref TOKEN_RE: Regex = Regex::new(
            r"[()\\]|\n|[[:space:]&&[^\n]]+|[[:^space:]&&[^()\\]]+").unwrap();
    }
    TOKEN_RE
        .captures_iter(code)
        .map(|cap| cap.get(0).unwrap().as_str())
        .chain([""].iter().cloned())
        .scan(
            Location {
                line_no: 1,
                column_no: 1,
            },
            |location, token| {
                let ret = (location.clone(), token);
                match token {
                    "\n" => {
                        location.line_no += 1;
                        location.column_no = 1;
                    }
                    var => location.column_no += var.len(),
                }
                Some(ret)
            },
        )
        .map(|(location, token_raw)| {
            let token_type = match token_raw {
                eof if eof.is_empty() => TokenType::EndOfFile,
                whitespace if is_whitespace(whitespace) => TokenType::WhiteSpace,
                "(" => TokenType::LeftParen,
                ")" => TokenType::RightParen,
                r"\" => TokenType::LambdaStart,
                name => TokenType::Identifier(name),
            };
            Token {
                token_raw,
                token_type,
                location,
            }
        })
        .collect()
}
