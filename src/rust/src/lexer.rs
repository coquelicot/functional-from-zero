use std::error;
use std::fmt;

use super::tokenizer::Token;
use super::tokenizer::TokenType;

#[derive(Debug)]
pub enum Expression<'a> {
    Identifier(&'a str),
    Lambda(&'a str, Box<Expression<'a>>),
    Apply(Box<Expression<'a>>, Box<Expression<'a>>),
}

#[derive(Debug)]
pub enum Error {
    EOFReached,
    ExtraRightParen,
    MissingRightParen,
    MissingLambdaArg,
}

impl error::Error for Error {
    fn description(&self) -> &str {
        match *self {
            Error::EOFReached => "unexpected EOF reached.",
            Error::ExtraRightParen => "extra ).",
            Error::MissingRightParen => "missing ).",
            Error::MissingLambdaArg => "missing argument for lambda.",
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Error::EOFReached => write!(f, "Unexpected EOF found when parsing expression."),
            Error::ExtraRightParen => write!(f, "Extra ) in expression."),
            Error::MissingRightParen => write!(f, "Missing ) in expression."),
            Error::MissingLambdaArg => write!(f, "Missing argument for lambda in expression."),
        }
    }
}

fn parse_single<'a>(tokens: &'a [Token]) -> Result<(Box<Expression<'a>>, &'a [Token<'a>]), Error> {
    if let Some((token, tokens)) = tokens.split_first() {
        match token.token_type {
            TokenType::Identifier(name) => Ok((Box::new(Expression::Identifier(name)), tokens)),
            TokenType::LeftParen => {
                let (expression, tokens) = parse_multi(tokens)?;
                if let Some((right_paren, tokens)) = tokens.split_first() {
                    if let TokenType::RightParen = right_paren.token_type {
                        return Ok((expression, tokens));
                    }
                }
                Err(Error::MissingRightParen)
            }
            TokenType::RightParen => Err(Error::ExtraRightParen),
            TokenType::LambdaStart => {
                if let Some((arg, tokens)) = tokens.split_first() {
                    if let TokenType::Identifier(name) = arg.token_type {
                        let (expression, tokens) = parse_multi(tokens)?;
                        return Ok((Box::new(Expression::Lambda(name, expression)), tokens));
                    }
                }
                Err(Error::MissingLambdaArg)
            }
        }
    } else {
        Err(Error::EOFReached)
    }
}

fn parse_multi<'a>(tokens: &'a [Token]) -> Result<(Box<Expression<'a>>, &'a [Token<'a>]), Error> {
    let (mut expression, mut tokens) = parse_single(tokens)?;
    loop {
        if let Some(next_token) = tokens.first() {
            if let TokenType::RightParen = next_token.token_type {
                break;
            } else {
                let (parameter, remain_tokens) = parse_single(tokens)?;
                expression = Box::new(Expression::Apply(expression, parameter));
                tokens = remain_tokens;
            }
        } else {
            break;
        }
    }
    Ok((expression, tokens))
}

pub fn parse<'a>(tokens: &'a [Token]) -> Result<Box<Expression<'a>>, Error> {
    let (expression, extra) = parse_multi(tokens)?;
    if !extra.is_empty() {
        Err(Error::ExtraRightParen)
    } else {
        Ok(expression)
    }
}
