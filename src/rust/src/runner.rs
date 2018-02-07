use std::{error, fmt};
use std::sync::Arc;
use std::sync::Mutex;
use std::hash::{Hash, Hasher};
use std::borrow::Borrow;

use chashmap::CHashMap;
use rayon;

use super::tokenizer::Token;
use super::transformer;
use super::transformer::{Expression, ExpressionValue};
use super::bitio;

// From https://github.com/rayon-rs/rayon/blob/d9b637e51b9dcc9045a28b40647661092a7f17b5/rayon-demo/src/quicksort/mod.rs#L30
pub trait Joiner {
    fn is_parallel() -> bool;
    fn join<A, RA, B, RB>(oper_a: A, oper_b: B) -> (RA, RB)
    where
        A: FnOnce() -> RA + Send,
        B: FnOnce() -> RB + Send,
        RA: Send,
        RB: Send;
}

struct Parallel;
impl Joiner for Parallel {
    #[inline]
    fn is_parallel() -> bool {
        true
    }
    #[inline]
    fn join<A, RA, B, RB>(oper_a: A, oper_b: B) -> (RA, RB)
    where
        A: FnOnce() -> RA + Send,
        B: FnOnce() -> RB + Send,
        RA: Send,
        RB: Send,
    {
        rayon::join(oper_a, oper_b)
    }
}

struct Sequential;
impl Joiner for Sequential {
    #[inline]
    fn is_parallel() -> bool {
        false
    }
    #[inline]
    fn join<A, RA, B, RB>(oper_a: A, oper_b: B) -> (RA, RB)
    where
        A: FnOnce() -> RA + Send,
        B: FnOnce() -> RB + Send,
        RA: Send,
        RB: Send,
    {
        let a = oper_a();
        let b = oper_b();
        (a, b)
    }
}

type ArcLambda<'a> = Arc<Lambda<'a> + 'a>;
type LambdaReturn<'a> = Result<(ArcLambda<'a>, bool), Error<'a>>;

trait Lambda<'a>: fmt::Debug + Send + Sync {
    fn apply(&self, arg: ArcLambda<'a>, cache: &mut RunCache<'a>) -> LambdaReturn<'a>;
}

// #[derive(Debug)]
// struct DummyLambda {}

// impl<'a> Lambda<'a> for DummyLambda {
//     fn apply(&self, _: ArcLambda<'a>) -> LambdaReturn<'a> {
//         Err(Error::DummyLambdaCalled)
//     }
// }

#[derive(Debug)]
struct Closure<'a> {
    environment: Arc<BaseEnvironment<'a>>,
    body: &'a Expression,
}

impl<'a> Lambda<'a> for Closure<'a> {
    fn apply(&self, arg: ArcLambda<'a>, cache: &mut RunCache<'a>) -> LambdaReturn<'a> {
        let environment = OverlayEnvironment {
            base: Arc::clone(&self.environment),
            arg_value: arg,
        };
        run_impl::<Sequential>(self.body, &environment, cache)
    }
}

trait Environment<'a>: fmt::Debug + Send + Sync {
    fn get(&self, name: usize) -> ArcLambda<'a>;
}

#[derive(Debug)]
struct BaseEnvironment<'a> {
    values: Vec<ArcLambda<'a>>,
}

impl<'a> BaseEnvironment<'a> {
    fn new() -> BaseEnvironment<'a> {
        BaseEnvironment { values: vec![] }
    }
    fn push(&mut self, lambda: ArcLambda<'a>) {
        self.values.push(lambda);
    }
}

impl<'a> Environment<'a> for BaseEnvironment<'a> {
    fn get(&self, name: usize) -> ArcLambda<'a> {
        Arc::clone(&self.values[name])
    }
}

impl<'a> Hash for BaseEnvironment<'a> {
    fn hash<H>(&self, state: &mut H)
    where
        H: Hasher,
    {
        for ref val in self.values.iter() {
            let ptr = Arc::into_raw(Arc::clone(&val));
            ptr.hash(state);
            let _ = unsafe { Arc::from_raw(ptr) };
        }
    }
}

impl<'a> PartialEq for BaseEnvironment<'a> {
    fn eq(&self, other: &Self) -> bool {
        if self.values.len() != other.values.len() {
            return false;
        }
        for (i, val) in self.values.iter().enumerate() {
            if !Arc::ptr_eq(&val, &other.values[i]) {
                return false;
            }
        }
        true
    }
}

impl<'a> Eq for BaseEnvironment<'a> {}

#[derive(Debug)]
struct OverlayEnvironment<'a> {
    base: Arc<BaseEnvironment<'a>>,
    arg_value: ArcLambda<'a>,
}

impl<'a> Environment<'a> for OverlayEnvironment<'a> {
    fn get(&self, name: usize) -> ArcLambda<'a> {
        if name == 0 {
            Arc::clone(&self.arg_value)
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
    fn apply(&self, arg: ArcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
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
    fn apply(&self, arg: ArcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
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
    fn apply(&self, arg: ArcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        Ok((Arc::from(BitInputLambda1 { arg }), true))
    }
}

#[derive(Debug)]
struct BitInputLambda1<'a> {
    arg: ArcLambda<'a>,
}

impl<'a> Lambda<'a> for BitInputLambda1<'a> {
    fn apply(&self, arg: ArcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        Ok((
            Arc::from(BitInputLambda2 {
                args: [Arc::clone(&self.arg), Arc::clone(&arg)],
            }),
            true,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda2<'a> {
    args: [ArcLambda<'a>; 2],
}

impl<'a> Lambda<'a> for BitInputLambda2<'a> {
    fn apply(&self, arg: ArcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        Ok((
            Arc::from(BitInputLambda3 {
                args: [
                    Arc::clone(&self.args[0]),
                    Arc::clone(&self.args[1]),
                    Arc::clone(&arg),
                ],
            }),
            true,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda3<'a> {
    args: [ArcLambda<'a>; 3],
}

impl<'a> Lambda<'a> for BitInputLambda3<'a> {
    fn apply(&self, _arg: ArcLambda<'a>, _: &mut RunCache) -> LambdaReturn<'a> {
        let bit = BIT_STDIN.lock().unwrap().read();
        let arg_idx = match bit {
            Some(b @ 0...1) => b as usize,
            None => 2,
            _ => unreachable!(),
        };
        Ok((Arc::clone(&self.args[arg_idx]), false))
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
    closure_cache: CHashMap<(Arc<BaseEnvironment<'a>>, *const Expression), Arc<Closure<'a>>>,
    apply_cache: CHashMap<(*const (Lambda<'a> + 'a), *const (Lambda<'a> + 'a)), ArcLambda<'a>>,
}

impl<'a> RunCache<'a> {
    fn new() -> RunCache<'a> {
        RunCache {
            closure_cache: CHashMap::new(),
            apply_cache: CHashMap::new(),
        }
    }
}

fn run_impl<'a, J: Joiner>(
    expression: &'a Expression,
    environment: &Environment<'a>,
    cache: &mut RunCache<'a>,
) -> LambdaReturn<'a> {
    match expression.value {
        ExpressionValue::Identifier(transformer::IdentifierExpression { name }) => {
            Ok((environment.get(name), true))
        }
        ExpressionValue::Apply(transformer::ApplyExpression {
            ref lambda,
            ref parameter,
        }) => {
            let (lambda, lambda_pure) = run_impl::<Sequential>(lambda, environment, cache)?;
            let (parameter, parameter_pure) =
                run_impl::<Sequential>(parameter, environment, cache)?;
            // let (lambda_res, parameter_res) = J::join(
            //     || run_impl(lambda, environment, cache),
            //     || run_impl(parameter, environment, cache),
            // );
            // let (lambda, lambda_pure) = lambda_res?;
            // let (parameter, parameter_pure) = parameter_res?;
            let lambda_ptr = Arc::into_raw(lambda.clone());
            let parameter_ptr = Arc::into_raw(parameter.clone());
            if let Some(result) = cache.apply_cache.get(&(lambda_ptr, parameter_ptr)) {
                return Ok((result.clone(), lambda_pure && parameter_pure));
            }
            let (result, apply_pure) = lambda.apply(parameter, cache)?;
            if apply_pure {
                cache
                    .apply_cache
                    .insert((lambda_ptr, parameter_ptr), result.clone());
            }
            let _ = unsafe { Arc::from_raw(parameter_ptr) };
            let _ = unsafe { Arc::from_raw(lambda_ptr) };
            Ok((result, lambda_pure && parameter_pure && apply_pure))
        }
        ExpressionValue::Lambda(transformer::LambdaExpression {
            ref env_map,
            ref body,
        }) => {
            let mut new_environment = BaseEnvironment::new();
            for &parent_idx in env_map.iter() {
                new_environment.push(environment.get(parent_idx as usize))
            }
            let new_environment = Arc::from(new_environment);
            let key = (new_environment.clone(), body.borrow() as *const Expression);
            if let Some(result) = cache.closure_cache.get(&key) {
                return Ok((result.clone(), true));
            }
            let result = Arc::from(Closure {
                environment: new_environment,
                body: body,
            });
            cache.closure_cache.insert(key, result.clone());
            Ok((result, true))
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
            "__builtin_p0" => environment.push(Arc::from(BitOutputLambda::new(0))),
            "__builtin_p1" => environment.push(Arc::from(BitOutputLambda::new(1))),
            "__builtin_g" => environment.push(Arc::from(BitInputLambda::new())),
            "__builtin_debug" => environment.push(Arc::from(DebugLambda::new())),
            _ => undefined_vars.push(name),
        }
    }
    if !undefined_vars.is_empty() {
        return Err(Error::UndefinedVariable(undefined_vars));
    }
    let mut cache = RunCache::new();
    run_impl::<Sequential>(expression, &environment, &mut cache)?;
    Ok(())
}
