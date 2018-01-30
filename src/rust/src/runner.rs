use std::{error, fmt};
use std::rc::Rc;
use std::sync::Mutex;

use super::transformer;
use super::transformer::Expression;
use super::bitio;

type RcLambda<'a> = Rc<Lambda<'a> + 'a>;
type LambdaReturn<'a> = Result<(RcLambda<'a>, bool), Error>;

trait Lambda<'a>: fmt::Debug {
    fn apply(&self, arg: RcLambda<'a>) -> LambdaReturn<'a>;
}

#[derive(Debug)]
struct DummyLambda {}

impl<'a> Lambda<'a> for DummyLambda {
    fn apply(&self, _: RcLambda<'a>) -> LambdaReturn<'a> {
        Err(Error::DummyLambdaCalled)
    }
}

#[derive(Debug)]
struct Closure<'a> {
    arg: usize,
    environment: Rc<Environment<'a> + 'a>,
    body: &'a Expression,
}

impl<'a> Lambda<'a> for Closure<'a> {
    fn apply(&self, arg: RcLambda<'a>) -> LambdaReturn<'a> {
        let environment = OverlayEnvironment {
            base: Rc::clone(&self.environment),
            overlay_idx: self.arg,
            overlay_value: arg,
        };
        run_impl(&self.body, &environment)
    }
}

trait Environment<'a>: fmt::Debug {
    fn get(&self, name: usize) -> RcLambda<'a>;
}

#[derive(Debug)]
struct BaseEnvironment<'a> {
    values: Vec<Rc<Lambda<'a> + 'a>>,
}

impl<'a> BaseEnvironment<'a> {
    fn new() -> BaseEnvironment<'a> {
        BaseEnvironment { values: vec![] }
    }
    fn push(&mut self, lambda: RcLambda<'a>) {
        self.values.push(lambda);
    }
}

impl<'a> Environment<'a> for BaseEnvironment<'a> {
    fn get(&self, name: usize) -> Rc<Lambda<'a> + 'a> {
        Rc::clone(&self.values[name])
    }
}

#[derive(Debug)]
struct OverlayEnvironment<'a> {
    base: Rc<Environment<'a> + 'a>,
    overlay_idx: usize,
    overlay_value: Rc<Lambda<'a> + 'a>,
}

impl<'a> Environment<'a> for OverlayEnvironment<'a> {
    fn get(&self, name: usize) -> RcLambda<'a> {
        if name == self.overlay_idx {
            Rc::clone(&self.overlay_value)
        } else {
            self.base.get(name)
        }
    }
}

lazy_static! {
    static ref BIT_STDOUT: Mutex<bitio::BitOutputStream> = (
        Mutex::new(bitio::BitOutputStream::new()));
    static ref BIT_STDIN: Mutex<bitio::BitInputStream> = (
        Mutex::new(bitio::BitInputStream::new()));
}

#[derive(Debug)]
struct BitOutputLambda {
    bit: u8,
}

impl BitOutputLambda {
    fn new(bit: u8) -> BitOutputLambda {
        BitOutputLambda { bit }
    }
}

impl<'a> Lambda<'a> for BitOutputLambda {
    fn apply(&self, arg: RcLambda<'a>) -> LambdaReturn<'a> {
        BIT_STDOUT.lock().unwrap().write(self.bit);
        Ok((arg, false))
    }
}

#[derive(Debug)]
struct BitInputLambda {}

impl BitInputLambda {
    fn new() -> BitInputLambda {
        BitInputLambda {}
    }
}

impl<'a> Lambda<'a> for BitInputLambda {
    fn apply(&self, arg: RcLambda<'a>) -> LambdaReturn<'a> {
        let bit = BIT_STDIN.lock().unwrap().read();
        Ok((
            Rc::from(BitInputLambda2 {
                bit,
                first_arg: arg,
            }),
            false,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda2<'a> {
    bit: u8,
    first_arg: RcLambda<'a>,
}

impl<'a> Lambda<'a> for BitInputLambda2<'a> {
    fn apply(&self, arg: RcLambda<'a>) -> LambdaReturn<'a> {
        if self.bit == 0 {
            Ok((Rc::clone(&self.first_arg), false))
        } else {
            Ok((arg, false))
        }
    }
}

#[derive(Debug)]
pub enum Error {
    UndefinedVariable(Vec<String>),
    DummyLambdaCalled,
}

impl error::Error for Error {
    fn description(&self) -> &str {
        match *self {
            Error::UndefinedVariable(_) => "undefined free variable",
            Error::DummyLambdaCalled => "dummy lambda called",
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Error::UndefinedVariable(ref vars) => {
                write!(f, "Undefined free variables [{}]", vars.join(", "))
            }
            Error::DummyLambdaCalled => write!(f, "Dummy lambda called!"),
        }
    }
}

fn run_impl<'a>(expression: &'a Expression, environment: &Environment<'a>) -> LambdaReturn<'a> {
    match *expression {
        Expression::Identifier(transformer::IdentifierExpression { name }) => {
            Ok((environment.get(name), true))
        }
        Expression::Apply(transformer::ApplyExpression {
            ref lambda,
            ref parameter,
        }) => {
            let (lambda, lambda_pure) = run_impl(lambda, environment)?;
            let (parameter, parameter_pure) = run_impl(parameter, environment)?;
            let (result, apply_pure) = lambda.apply(parameter)?;
            Ok((result, lambda_pure && parameter_pure && apply_pure))
        }
        Expression::Lambda(transformer::LambdaExpression {
            arg,
            ref env_map,
            ref body,
        }) => {
            let mut new_environment = BaseEnvironment::new();
            for &parent_idx in env_map.iter() {
                if parent_idx != -1 {
                    new_environment.push(environment.get(parent_idx as usize))
                } else {
                    // TODO(Darkpi): Use the same dummy lambda everywhere.
                    new_environment.push(Rc::from(DummyLambda {}))
                }
            }
            Ok((
                Rc::from(Closure {
                    arg,
                    environment: Rc::from(new_environment),
                    body: body.as_ref(),
                }),
                true,
            ))
        }
    }
}

pub fn run(expression: &Expression, free_vars: &Vec<&str>) -> Result<(), Error> {
    let mut undefined_vars: Vec<String> = vec![];
    let mut environment = BaseEnvironment::new();
    for name in free_vars.iter() {
        match *name {
            "__builtin_p0" => environment.push(Rc::from(BitOutputLambda::new(0))),
            "__builtin_p1" => environment.push(Rc::from(BitOutputLambda::new(1))),
            "__builtin_g" => environment.push(Rc::from(BitInputLambda::new())),
            var => undefined_vars.push(var.to_string()),
        }
    }
    if !undefined_vars.is_empty() {
        return Err(Error::UndefinedVariable(undefined_vars));
    }
    run_impl(expression, &environment)?;
    Ok(())
}
