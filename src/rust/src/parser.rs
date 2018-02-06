use std::{error, fmt, iter};

use super::tokenizer::{Location, Token, TokenType};

#[derive(Debug)]
pub enum Node<'a> {
    Identifier(&'a str),
    Lambda(&'a str, Box<Node<'a>>),
    Apply(Box<Node<'a>>, Box<Node<'a>>),
}

#[derive(Debug)]
pub enum ErrorType {
    EOFReached,
    ExtraRightParen,
    MissingRightParen,
    MissingLambdaArg,
}

#[derive(Debug)]
pub struct Error {
    error_type: ErrorType,
    location: Location,
}

impl Error {
    fn new(error_type: ErrorType, token: &Token) -> Error {
        Error {
            error_type,
            location: token.location.clone(),
        }
    }
}

impl error::Error for Error {
    fn description(&self) -> &str {
        match self.error_type {
            ErrorType::EOFReached => "unexpected EOF reached",
            ErrorType::ExtraRightParen => "extra )",
            ErrorType::MissingRightParen => "missing )",
            ErrorType::MissingLambdaArg => "missing argument for lambda",
        }
    }
}

const EOF_TOKEN_MISSING: &str = "EOF Token missing QQ";

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "At {}: ", self.location)?;
        match self.error_type {
            ErrorType::EOFReached => write!(f, "Unexpected EOF found when parsing expression."),
            ErrorType::ExtraRightParen => write!(f, "Extra ) in expression."),
            ErrorType::MissingRightParen => write!(f, "Missing ) in expression."),
            ErrorType::MissingLambdaArg => write!(f, "Missing argument for lambda in expression."),
        }
    }
}

fn parse_single<'a, I>(tokens: &mut iter::Peekable<I>) -> Result<Node<'a>, Error>
where
    I: Iterator<Item = &'a Token<'a>>,
{
    let token = tokens.next().expect(EOF_TOKEN_MISSING);
    match token.token_type {
        TokenType::Identifier(name) => Ok(Node::Identifier(name)),
        TokenType::LeftParen => {
            let expression = parse_multi(tokens)?;
            if let Some(&Token {
                token_type: TokenType::RightParen,
                ..
            }) = tokens.next()
            {
                return Ok(expression);
            }
            Err(Error::new(ErrorType::MissingRightParen, &token))
        }
        TokenType::RightParen => Err(Error::new(ErrorType::ExtraRightParen, &token)),
        TokenType::LambdaStart => {
            if let Some(&Token {
                token_type: TokenType::Identifier(name),
                ..
            }) = tokens.next()
            {
                let expression = parse_multi(tokens)?;
                return Ok(Node::Lambda(name, Box::new(expression)));
            }
            Err(Error::new(ErrorType::MissingLambdaArg, &token))
        }
        TokenType::EndOfFile => Err(Error::new(ErrorType::EOFReached, &token)),
        TokenType::WhiteSpace => unreachable!(),
    }
}

fn parse_multi<'a, I>(tokens: &mut iter::Peekable<I>) -> Result<Node<'a>, Error>
where
    I: Iterator<Item = &'a Token<'a>>,
{
    let mut expression = parse_single(tokens)?;
    loop {
        {
            let next_token = tokens.peek().expect(EOF_TOKEN_MISSING);
            match next_token.token_type {
                TokenType::RightParen | TokenType::EndOfFile => break,
                _ => (),
            }
        }
        let parameter = parse_single(tokens)?;
        expression = Node::Apply(Box::new(expression), Box::new(parameter));
    }
    Ok(expression)
}

pub fn parse<'a>(tokens: &'a [Token]) -> Result<Node<'a>, Error> {
    let mut token_iter = tokens
        .iter()
        .filter(|x| match x.token_type {
            TokenType::WhiteSpace => false,
            _ => true,
        })
        .peekable();
    let expression = parse_multi(&mut token_iter)?;
    let extra_token = token_iter.next().expect(EOF_TOKEN_MISSING);
    match extra_token.token_type {
        TokenType::EndOfFile => Ok(expression),
        _ => Err(Error::new(ErrorType::ExtraRightParen, &extra_token)),
    }
}
