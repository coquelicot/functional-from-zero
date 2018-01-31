use std::{error, fmt};

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
            location: token.location,
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

fn parse_single<'a>(tokens: &'a [Token]) -> Result<(Node<'a>, &'a [Token<'a>]), Error> {
    if let Some((token, tokens)) = tokens.split_first() {
        match token.token_type {
            TokenType::Identifier(name) => Ok((Node::Identifier(name), tokens)),
            TokenType::LeftParen => {
                let (expression, tokens) = parse_multi(tokens)?;
                if let Some((right_paren, tokens)) = tokens.split_first() {
                    if let TokenType::RightParen = right_paren.token_type {
                        return Ok((expression, tokens));
                    }
                }
                Err(Error::new(ErrorType::MissingRightParen, &token))
            }
            TokenType::RightParen => Err(Error::new(ErrorType::ExtraRightParen, &token)),
            TokenType::LambdaStart => {
                if let Some((arg, tokens)) = tokens.split_first() {
                    if let TokenType::Identifier(name) = arg.token_type {
                        let (expression, tokens) = parse_multi(tokens)?;
                        return Ok((Node::Lambda(name, Box::new(expression)), tokens));
                    }
                }
                Err(Error::new(ErrorType::MissingLambdaArg, &token))
            }
        }
    } else {
        // TODO(Darkpi): Add EOF token.
        Err(Error {
            error_type: ErrorType::EOFReached,
            location: Location {
                line_no: 0,
                column_no: 0,
            },
        })
    }
}

fn parse_multi<'a>(tokens: &'a [Token]) -> Result<(Node<'a>, &'a [Token<'a>]), Error> {
    let (mut expression, mut tokens) = parse_single(tokens)?;
    loop {
        if let Some(next_token) = tokens.first() {
            if let TokenType::RightParen = next_token.token_type {
                break;
            } else {
                let (parameter, remain_tokens) = parse_single(tokens)?;
                expression = Node::Apply(Box::new(expression), Box::new(parameter));
                tokens = remain_tokens;
            }
        } else {
            break;
        }
    }
    Ok((expression, tokens))
}

pub fn parse<'a>(tokens: &'a [Token]) -> Result<Node<'a>, Error> {
    let (expression, extra) = parse_multi(tokens)?;
    if let Some(token) = extra.first() {
        Err(Error::new(ErrorType::ExtraRightParen, &token))
    } else {
        Ok(expression)
    }
}
