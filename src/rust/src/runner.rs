use std::{error, fmt};
use std::rc::Rc;
use std::cell::RefCell;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;
use std::hash::{Hash, Hasher};
use std::collections::HashMap;

use super::tokenizer::Token;
use super::transformer;
use super::transformer::{Expression, ExpressionValue};
use super::bitio;

type RcLambda<'a> = Rc<Lambda<'a>>;
type LambdaReturn<'a> = RcLambda<'a>;

trait LambdaValue<'a>: fmt::Debug {
    fn apply(&self, arg: RcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a>;
}

#[derive(Debug)]
pub struct Lambda<'a> {
    value: Box<LambdaValue<'a> + 'a>,
    id: usize,
    apply_cache: RefCell<HashMap<usize, RcLambda<'a>>>,
}

impl<'a> Lambda<'a> {
    fn new(value: Box<LambdaValue<'a> + 'a>, cache: &RunCache) -> Lambda<'a> {
        Lambda {
            value,
            id: cache.new_id(),
            apply_cache: Default::default(),
        }
    }
    fn apply(&self, arg: RcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a> {
        self.value.apply(arg, cache)
    }
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

impl<'a> LambdaValue<'a> for Closure<'a> {
    fn apply(&self, arg: RcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a> {
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
pub struct BaseEnvironment<'a> {
    values: Vec<RcLambda<'a>>,
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
    fn get(&self, name: usize) -> RcLambda<'a> {
        Rc::clone(&self.values[name])
    }
}

impl<'a> Hash for BaseEnvironment<'a> {
    fn hash<H>(&self, state: &mut H)
    where
        H: Hasher,
    {
        for val in self.values.iter() {
            let ptr = Rc::into_raw(Rc::clone(val));
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

#[derive(Debug)]
struct OverlayEnvironment<'a> {
    base: Rc<BaseEnvironment<'a>>,
    arg_value: RcLambda<'a>,
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

impl<'a> LambdaValue<'a> for DebugLambda {
    fn apply(&self, arg: RcLambda<'a>, _: &RunCache) -> LambdaReturn<'a> {
        println!("[debug] arg = {:p}", arg.as_ref());
        arg
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

impl<'a> LambdaValue<'a> for BitOutputLambda {
    fn apply(&self, arg: RcLambda<'a>, _: &RunCache) -> LambdaReturn<'a> {
        BIT_STDOUT.lock().unwrap().write(self.bit);
        arg
    }
}

#[derive(Debug)]
struct BitInputLambda {}

impl BitInputLambda {
    fn new() -> BitInputLambda {
        BitInputLambda {}
    }
}

impl<'a> LambdaValue<'a> for BitInputLambda {
    fn apply(&self, arg: RcLambda<'a>, cache: &RunCache) -> LambdaReturn<'a> {
        Rc::from(Lambda::new(Box::from(BitInputLambda1 { arg }), cache))
    }
}

#[derive(Debug)]
struct BitInputLambda1<'a> {
    arg: RcLambda<'a>,
}

impl<'a> LambdaValue<'a> for BitInputLambda1<'a> {
    fn apply(&self, arg: RcLambda<'a>, cache: &RunCache) -> LambdaReturn<'a> {
        Rc::from(Lambda::new(
            Box::from(BitInputLambda2 {
                args: [Rc::clone(&self.arg), Rc::clone(&arg)],
            }),
            cache,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda2<'a> {
    args: [RcLambda<'a>; 2],
}

impl<'a> LambdaValue<'a> for BitInputLambda2<'a> {
    fn apply(&self, arg: RcLambda<'a>, cache: &RunCache) -> LambdaReturn<'a> {
        Rc::from(Lambda::new(
            Box::from(BitInputLambda3 {
                args: [
                    Rc::clone(&self.args[0]),
                    Rc::clone(&self.args[1]),
                    Rc::clone(&arg),
                ],
            }),
            cache,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda3<'a> {
    args: [RcLambda<'a>; 3],
}

impl<'a> LambdaValue<'a> for BitInputLambda3<'a> {
    fn apply(&self, _arg: RcLambda<'a>, _: &RunCache) -> LambdaReturn<'a> {
        let bit = BIT_STDIN.lock().unwrap().read();
        let arg_idx = match bit {
            Some(b @ 0...1) => b as usize,
            None => 2,
            _ => unreachable!(),
        };
        Rc::clone(&self.args[arg_idx])
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
    closure_cache: RefCell<HashMap<(Rc<BaseEnvironment<'a>>, usize), RcLambda<'a>>>,
    id_counter: AtomicUsize,
}

impl<'a> RunCache<'a> {
    fn new() -> RunCache<'a> {
        RunCache {
            closure_cache: Default::default(),
            id_counter: AtomicUsize::new(0),
        }
    }

    fn new_id(&self) -> usize {
        self.id_counter.fetch_add(1, Ordering::Relaxed)
    }
}

fn run_impl<'a>(
    expression: &'a Expression,
    environment: &Environment<'a>,
    cache: &RunCache<'a>,
) -> LambdaReturn<'a> {
    match expression.value {
        ExpressionValue::Identifier(transformer::IdentifierExpression { name }) => {
            environment.get(name)
        }
        ExpressionValue::Apply(transformer::ApplyExpression {
            ref lambda,
            ref parameter,
        }) => {
            let lambda = run_impl(lambda, environment, cache);
            let parameter = run_impl(parameter, environment, cache);
            let key = parameter.id;
            if let Some(result) = lambda.apply_cache.borrow().get(&key) {
                return result.clone();
            }
            let result = lambda.apply(parameter, cache);
            lambda.apply_cache.borrow_mut().insert(key, result.clone());
            result
        }
        ExpressionValue::Lambda(transformer::LambdaExpression {
            ref env_map,
            ref body,
        }) => {
            let mut new_environment = BaseEnvironment::new();
            for &parent_idx in env_map.iter() {
                new_environment.push(environment.get(parent_idx))
            }
            let new_environment = Rc::from(new_environment);
            let key = (new_environment.clone(), body.id);
            cache
                .closure_cache
                .borrow_mut()
                .entry(key)
                .or_insert_with(|| {
                    Rc::from(Lambda::new(
                        Box::from(Closure {
                            environment: new_environment,
                            body,
                        }),
                        cache,
                    ))
                })
                .clone()
        }
    }
}

pub fn run<'a>(expression: &'a Expression, free_vars: Vec<&'a Token<'a>>) -> Result<(), Error<'a>> {
    let mut undefined_vars: Vec<&Token> = vec![];
    let mut environment = BaseEnvironment::new();
    let mut cache = RunCache::new();
    for name in free_vars.iter() {
        match name.token_raw {
            "__builtin_p0" => environment.push(Rc::from(Lambda::new(
                Box::from(BitOutputLambda::new(0)),
                &mut cache,
            ))),
            "__builtin_p1" => environment.push(Rc::from(Lambda::new(
                Box::from(BitOutputLambda::new(1)),
                &mut cache,
            ))),
            "__builtin_g" => environment.push(Rc::from(Lambda::new(
                Box::from(BitInputLambda::new()),
                &mut cache,
            ))),
            "__builtin_debug" => environment.push(Rc::from(Lambda::new(
                Box::from(DebugLambda::new()),
                &mut cache,
            ))),
            _ => undefined_vars.push(name),
        }
    }
    if !undefined_vars.is_empty() {
        return Err(Error::UndefinedVariable(undefined_vars));
    }
    run_impl(expression, &environment, &mut cache);
    Ok(())
}
