use std::{error, fmt, iter};

use super::tokenizer::{Token, TokenType};

#[derive(Debug)]
pub enum Node<'a> {
    Identifier(&'a Token<'a>),
    Lambda(&'a Token<'a>, Box<Node<'a>>),
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
pub struct Error<'a> {
    error_type: ErrorType,
    token: &'a Token<'a>,
}

impl<'a> Error<'a> {
    fn new(error_type: ErrorType, token: &'a Token<'a>) -> Error<'a> {
        Error { error_type, token }
    }
}

impl<'a> error::Error for Error<'a> {
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

impl<'a> fmt::Display for Error<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "At {}: ", self.token)?;
        match self.error_type {
            ErrorType::EOFReached => write!(f, "Unexpected EOF found when parsing expression."),
            ErrorType::ExtraRightParen => write!(f, "Extra ) in expression."),
            ErrorType::MissingRightParen => write!(f, "Missing ) in expression."),
            ErrorType::MissingLambdaArg => write!(f, "Missing argument for lambda in expression."),
        }
    }
}

fn parse_single<'a, I>(tokens: &mut iter::Peekable<I>) -> Result<Node<'a>, Error<'a>>
where
    I: Iterator<Item = &'a Token<'a>>,
{
    let token = tokens.next().expect(EOF_TOKEN_MISSING);
    match token.token_type {
        TokenType::Identifier(_) => Ok(Node::Identifier(token)),
        TokenType::LeftParen => {
            let expression = parse_multi(tokens)?;
            let token = tokens.next().expect(EOF_TOKEN_MISSING);
            match token.token_type {
                TokenType::RightParen => Ok(expression),
                _ => Err(Error::new(ErrorType::MissingRightParen, token)),
            }
        }
        TokenType::RightParen => Err(Error::new(ErrorType::ExtraRightParen, token)),
        TokenType::LambdaStart => {
            let token = tokens.next().expect(EOF_TOKEN_MISSING);
            match token.token_type {
                TokenType::Identifier(_) => {
                    let expression = parse_multi(tokens)?;
                    Ok(Node::Lambda(token, Box::new(expression)))
                }
                _ => Err(Error::new(ErrorType::MissingLambdaArg, token)),
            }
        }
        TokenType::EndOfFile => Err(Error::new(ErrorType::EOFReached, token)),
        TokenType::WhiteSpace => unreachable!(),
    }
}

fn parse_multi<'a, I>(tokens: &mut iter::Peekable<I>) -> Result<Node<'a>, Error<'a>>
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

pub fn parse<'a>(tokens: &'a [Token]) -> Result<Node<'a>, Error<'a>> {
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
        _ => Err(Error::new(ErrorType::ExtraRightParen, extra_token)),
    }
}
