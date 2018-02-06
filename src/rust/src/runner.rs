use std::{error, fmt};
use std::collections::HashMap;
use std::rc::Rc;
use std::sync::Mutex;
use std::hash::{Hash, Hasher};
use std::borrow::Borrow;

use super::tokenizer::Token;
use super::transformer;
use super::transformer::Expression;
use super::bitio;

type RcLambda<'a> = Rc<Lambda<'a> + 'a>;
type LambdaReturn<'a> = Result<(RcLambda<'a>, bool), Error<'a>>;

trait Lambda<'a>: fmt::Debug {
    fn apply(&self, arg: RcLambda<'a>, cache: &mut RunCache<'a>) -> LambdaReturn<'a>;
}

// #[derive(Debug)]
// struct DummyLambda {}

// impl<'a> Lambda<'a> for DummyLambda {
//     fn apply(&self, _: RcLambda<'a>) -> LambdaReturn<'a> {
//         Err(Error::DummyLambdaCalled)
//     }
// }

#[derive(Debug)]
struct Closure<'a> {
    environment: Rc<BaseEnvironment<'a>>,
    body: &'a Expression,
}

impl<'a> Hash for BaseEnvironment<'a> {
    fn hash<H>(&self, state: &mut H)
    where
        H: Hasher,
    {
        for ref val in self.values.iter() {
            let ptr = Rc::into_raw(Rc::clone(&val));
            ptr.hash(state);
            let _ = unsafe { Rc::from_raw(ptr) };
        }
    }
}

impl<'a> PartialEq for BaseEnvironment<'a> {
    fn eq(&self, other: &Self) -> bool {
        if self.values.len() != other.values.len() {
            return false;
        }
        for (i, val) in self.values.iter().enumerate() {
            if !Rc::ptr_eq(&val, &other.values[i]) {
                return false;
            }
        }
        true
    }
}

impl<'a> Eq for BaseEnvironment<'a> {}

impl<'a> Lambda<'a> for Closure<'a> {
    fn apply(&self, arg: RcLambda<'a>, cache: &mut RunCache<'a>) -> LambdaReturn<'a> {
        let environment = OverlayEnvironment {
            base: Rc::clone(&self.environment),
            arg_value: arg,
        };
        run_impl(self.body, &environment, cache)
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
    base: Rc<BaseEnvironment<'a>>,
    arg_value: Rc<Lambda<'a> + 'a>,
}

impl<'a> Environment<'a> for OverlayEnvironment<'a> {
    fn get(&self, name: usize) -> RcLambda<'a> {
        if name == 0 {
            Rc::clone(&self.arg_value)
        } else {
            self.base.get(name - 1)
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
struct DebugLambda {}

impl DebugLambda {
    fn new() -> DebugLambda {
        DebugLambda {}
    }
}

impl<'a> Lambda<'a> for DebugLambda {
    fn apply(&self, arg: RcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        println!("[debug] arg = {:p}", arg.as_ref());
        Ok((arg, true))
    }
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
    fn apply(&self, arg: RcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
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
    fn apply(&self, arg: RcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        Ok((Rc::from(BitInputLambda1 { arg }), true))
    }
}

#[derive(Debug)]
struct BitInputLambda1<'a> {
    arg: RcLambda<'a>,
}

impl<'a> Lambda<'a> for BitInputLambda1<'a> {
    fn apply(&self, arg: RcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        Ok((
            Rc::from(BitInputLambda2 {
                args: [Rc::clone(&self.arg), Rc::clone(&arg)],
            }),
            true,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda2<'a> {
    args: [RcLambda<'a>; 2],
}

impl<'a> Lambda<'a> for BitInputLambda2<'a> {
    fn apply(&self, arg: RcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        Ok((
            Rc::from(BitInputLambda3 {
                args: [
                    Rc::clone(&self.args[0]),
                    Rc::clone(&self.args[1]),
                    Rc::clone(&arg),
                ],
            }),
            true,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda3<'a> {
    args: [RcLambda<'a>; 3],
}

impl<'a> Lambda<'a> for BitInputLambda3<'a> {
    fn apply(&self, _arg: RcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        let bit = BIT_STDIN.lock().unwrap().read();
        let arg_idx = match bit {
            Some(b @ 0...1) => b as usize,
            None => 2,
            _ => unreachable!(),
        };
        Ok((Rc::clone(&self.args[arg_idx]), false))
    }
}

#[derive(Debug)]
pub enum Error<'a> {
    UndefinedVariable(Vec<&'a Token<'a>>),
    // DummyLambdaCalled,
}

impl<'a> error::Error for Error<'a> {
    fn description(&self) -> &str {
        match *self {
            Error::UndefinedVariable(_) => "undefined free variable",
            // Error::DummyLambdaCalled => "dummy lambda called",
        }
    }
}

impl<'a> fmt::Display for Error<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Error::UndefinedVariable(ref vars) => {
                for var in vars {
                    write!(f, "Undefined variable \"{}\" ({})\n", var.token_raw, var.location)?;
                }
                Ok(())
            }
            // Error::DummyLambdaCalled => write!(f, "Dummy lambda called!"),
        }
    }
}

struct RunCache<'a> {
    closure_cache: HashMap<(Rc<BaseEnvironment<'a>>, *const Expression), Rc<Closure<'a>>>,
    apply_cache: HashMap<(*const (Lambda<'a> + 'a), *const (Lambda<'a> + 'a)), RcLambda<'a>>,
}

impl<'a> RunCache<'a> {
    fn new() -> RunCache<'a> {
        RunCache {
            closure_cache: HashMap::new(),
            apply_cache: HashMap::new(),
        }
    }
}

fn run_impl<'a>(
    expression: &'a Expression,
    environment: &Environment<'a>,
    cache: &mut RunCache<'a>,
) -> LambdaReturn<'a> {
    match *expression {
        Expression::Identifier(transformer::IdentifierExpression { name }) => {
            Ok((environment.get(name), true))
        }
        Expression::Apply(transformer::ApplyExpression {
            ref lambda,
            ref parameter,
        }) => {
            let (lambda, lambda_pure) = run_impl(lambda, environment, cache)?;
            let (parameter, parameter_pure) = run_impl(parameter, environment, cache)?;
            let lambda_ptr = Rc::into_raw(lambda.clone());
            let parameter_ptr = Rc::into_raw(parameter.clone());
            if let Some(result) = cache.apply_cache.get(&(lambda_ptr, parameter_ptr)) {
                return Ok((result.clone(), lambda_pure && parameter_pure));
            }
            let (result, apply_pure) = lambda.apply(parameter, cache)?;
            if apply_pure {
                cache
                    .apply_cache
                    .insert((lambda_ptr, parameter_ptr), result.clone());
            }
            let _ = unsafe { Rc::from_raw(parameter_ptr) };
            let _ = unsafe { Rc::from_raw(lambda_ptr) };
            Ok((result, lambda_pure && parameter_pure && apply_pure))
        }
        Expression::Lambda(transformer::LambdaExpression {
            ref env_map,
            ref body,
        }) => {
            let mut new_environment = BaseEnvironment::new();
            for &parent_idx in env_map.iter() {
                new_environment.push(environment.get(parent_idx as usize))
            }
            let new_environment = Rc::from(new_environment);
            let closure = cache
                .closure_cache
                .entry((new_environment.clone(), body.borrow() as *const Expression))
                .or_insert_with(|| {
                    Rc::from(Closure {
                        environment: new_environment,
                        body: body,
                    })
                });
            Ok((closure.clone(), true))
        }
    }
}

pub fn run<'a>(
    expression: &'a Expression,
    free_vars: &'a Vec<&'a Token<'a>>,
) -> Result<(), Error<'a>> {
    let mut undefined_vars: Vec<&Token> = vec![];
    let mut environment = BaseEnvironment::new();
    for name in free_vars.iter() {
        match name.token_raw {
            "__builtin_p0" => environment.push(Rc::from(BitOutputLambda::new(0))),
            "__builtin_p1" => environment.push(Rc::from(BitOutputLambda::new(1))),
            "__builtin_g" => environment.push(Rc::from(BitInputLambda::new())),
            "__builtin_debug" => environment.push(Rc::from(DebugLambda::new())),
            _ => undefined_vars.push(name),
        }
    }
    if !undefined_vars.is_empty() {
        return Err(Error::UndefinedVariable(undefined_vars));
    }
    let mut cache = RunCache::new();
    run_impl(expression, &environment, &mut cache)?;
    Ok(())
}
