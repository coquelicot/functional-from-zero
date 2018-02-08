use std::{error, fmt};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::sync::Mutex;
use std::hash::{Hash, Hasher};

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

type ArcLambda<'a> = Arc<Lambda<'a>>;
type LambdaReturn<'a> = Result<(ArcLambda<'a>, bool), Error<'a>>;

trait LambdaValue<'a>: fmt::Debug + Send + Sync {
    fn apply(&self, arg: ArcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a>;
    fn apply_parallel(&self, arg: ArcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a> {
        self.apply(arg, cache)
    }
    fn is_always_pure(&self) -> bool {
        false
    }
}

#[derive(Debug)]
struct Lambda<'a> {
    value: Box<LambdaValue<'a> + 'a>,
    id: usize,
    always_pure: bool,
}

impl<'a> Lambda<'a> {
    fn new(value: Box<LambdaValue<'a> + 'a>, cache: &RunCache) -> Lambda<'a> {
        let always_pure = value.is_always_pure();
        Lambda {
            value,
            id: cache.new_id(),
            always_pure,
        }
    }
    fn apply<J: Joiner>(&self, arg: ArcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a> {
        if J::is_parallel() {
            self.value.apply_parallel(arg, cache)
        } else {
            self.value.apply(arg, cache)
        }
    }
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

impl<'a> LambdaValue<'a> for Closure<'a> {
    fn apply(&self, arg: ArcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a> {
        let environment = OverlayEnvironment {
            base: Arc::clone(&self.environment),
            arg_value: arg,
        };
        run_impl::<Sequential>(self.body, &environment, cache)
    }
    fn apply_parallel(&self, arg: ArcLambda<'a>, cache: &RunCache<'a>) -> LambdaReturn<'a> {
        let environment = OverlayEnvironment {
            base: Arc::clone(&self.environment),
            arg_value: arg,
        };
        run_impl::<Parallel>(self.body, &environment, cache)
    }
    fn is_always_pure(&self) -> bool {
        self.environment.is_always_pure()
    }
}

trait Environment<'a>: fmt::Debug + Send + Sync {
    fn get(&self, name: usize) -> ArcLambda<'a>;
    fn is_always_pure(&self) -> bool;
    fn is_small(&self) -> bool;
}

#[derive(Debug)]
struct BaseEnvironment<'a> {
    values: Vec<ArcLambda<'a>>,
    always_pure: bool,
}

impl<'a> BaseEnvironment<'a> {
    fn new() -> BaseEnvironment<'a> {
        BaseEnvironment {
            values: vec![],
            always_pure: true,
        }
    }
    fn push(&mut self, lambda: ArcLambda<'a>) {
        self.always_pure &= lambda.always_pure;
        self.values.push(lambda);
    }
}

impl<'a> Environment<'a> for BaseEnvironment<'a> {
    fn get(&self, name: usize) -> ArcLambda<'a> {
        Arc::clone(&self.values[name])
    }
    fn is_always_pure(&self) -> bool {
        self.always_pure
    }
    fn is_small(&self) -> bool {
        self.values.len() <= 2
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
    fn is_always_pure(&self) -> bool {
        self.base.is_always_pure() && self.arg_value.always_pure
    }
    fn is_small(&self) -> bool {
        self.base.is_small()
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
    fn apply(&self, arg: ArcLambda<'a>, _: &RunCache) -> LambdaReturn<'a> {
        println!(
            "[debug] arg = {:p}, arg_always_pure = {}",
            arg.as_ref(),
            arg.always_pure
        );
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

impl<'a> LambdaValue<'a> for BitOutputLambda {
    fn apply(&self, arg: ArcLambda<'a>, _: &RunCache) -> LambdaReturn<'a> {
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

impl<'a> LambdaValue<'a> for BitInputLambda {
    fn apply(&self, arg: ArcLambda<'a>, cache: &RunCache) -> LambdaReturn<'a> {
        Ok((
            Arc::from(Lambda::new(Box::from(BitInputLambda1 { arg }), cache)),
            true,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda1<'a> {
    arg: ArcLambda<'a>,
}

impl<'a> LambdaValue<'a> for BitInputLambda1<'a> {
    fn apply(&self, arg: ArcLambda<'a>, cache: &RunCache) -> LambdaReturn<'a> {
        Ok((
            Arc::from(Lambda::new(
                Box::from(BitInputLambda2 {
                    args: [Arc::clone(&self.arg), Arc::clone(&arg)],
                }),
                cache,
            )),
            true,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda2<'a> {
    args: [ArcLambda<'a>; 2],
}

impl<'a> LambdaValue<'a> for BitInputLambda2<'a> {
    fn apply(&self, arg: ArcLambda<'a>, cache: &RunCache) -> LambdaReturn<'a> {
        Ok((
            Arc::from(Lambda::new(
                Box::from(BitInputLambda3 {
                    args: [
                        Arc::clone(&self.args[0]),
                        Arc::clone(&self.args[1]),
                        Arc::clone(&arg),
                    ],
                }),
                cache,
            )),
            true,
        ))
    }
}

#[derive(Debug)]
struct BitInputLambda3<'a> {
    args: [ArcLambda<'a>; 3],
}

impl<'a> LambdaValue<'a> for BitInputLambda3<'a> {
    fn apply(&self, _arg: ArcLambda<'a>, _: &RunCache) -> LambdaReturn<'a> {
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
    closure_cache: CHashMap<(Arc<BaseEnvironment<'a>>, usize), ArcLambda<'a>>,
    apply_cache: CHashMap<(usize, usize), ArcLambda<'a>>,
    id_counter: AtomicUsize,
    c1: AtomicUsize,
    c2: AtomicUsize,
}

impl<'a> RunCache<'a> {
    fn new() -> RunCache<'a> {
        RunCache {
            closure_cache: CHashMap::new(),
            apply_cache: CHashMap::new(),
            id_counter: AtomicUsize::new(0),
            c1: AtomicUsize::new(0),
            c2: AtomicUsize::new(0),
        }
    }

    fn new_id(&self) -> usize {
        self.id_counter.fetch_add(1, Ordering::Relaxed)
    }
}

fn run_impl<'a, J: Joiner>(
    expression: &'a Expression,
    environment: &Environment<'a>,
    cache: &RunCache<'a>,
) -> LambdaReturn<'a> {
    match expression.value {
        ExpressionValue::Identifier(transformer::IdentifierExpression { name }) => {
            Ok((environment.get(name), true))
        }
        ExpressionValue::Apply(transformer::ApplyExpression {
            ref lambda,
            ref parameter,
        }) => {
            let (lambda_res, parameter_res) = if environment.is_small() {
                (
                    run_impl::<J>(lambda, environment, cache),
                    run_impl::<J>(parameter, environment, cache),
                )
            } else {
                J::join(
                    || run_impl::<J>(lambda, environment, cache),
                    || run_impl::<J>(parameter, environment, cache),
                )
            };
            let (lambda, lambda_pure) = lambda_res?;
            let (parameter, parameter_pure) = parameter_res?;
            let key = (lambda.id, parameter.id);
            if let Some(result) = cache.apply_cache.get(&key) {
                return Ok((result.clone(), lambda_pure && parameter_pure));
            }
            let (result, apply_pure) = if !J::is_parallel() && lambda.always_pure && parameter_pure
            {
                lambda.apply::<Parallel>(parameter, cache)?
            } else {
                lambda.apply::<J>(parameter, cache)?
            };
            let result = if apply_pure {
                cache.apply_cache.upsert(
                    key.clone(),
                    || {
                        cache.c1.fetch_add(1, Ordering::Relaxed);
                        result.clone()
                    },
                    |_| {
                        cache.c2.fetch_add(1, Ordering::Relaxed);
                    },
                );
                cache
                    .apply_cache
                    .get(&key)
                    .expect("apply_cache upsert empty...")
                    .clone()
            } else {
                result
            };
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
            let key = (new_environment.clone(), body.id);
            cache.closure_cache.upsert(
                key.clone(),
                || {
                    Arc::from(Lambda::new(
                        Box::from(Closure {
                            environment: new_environment,
                            body: body,
                        }),
                        cache,
                    ))
                },
                |_| {},
            );
            Ok((
                cache
                    .closure_cache
                    .get(&key)
                    .expect("closure_cache upsert empty...")
                    .clone(),
                true,
            ))
        }
    }
}

pub fn run<'a>(
    expression: &'a Expression,
    free_vars: &'a Vec<&'a Token<'a>>,
) -> Result<(), Error<'a>> {
    let mut undefined_vars: Vec<&Token> = vec![];
    let mut environment = BaseEnvironment::new();
    let mut cache = RunCache::new();
    for name in free_vars.iter() {
        match name.token_raw {
            "__builtin_p0" => environment.push(Arc::from(Lambda::new(
                Box::from(BitOutputLambda::new(0)),
                &mut cache,
            ))),
            "__builtin_p1" => environment.push(Arc::from(Lambda::new(
                Box::from(BitOutputLambda::new(1)),
                &mut cache,
            ))),
            "__builtin_g" => environment.push(Arc::from(Lambda::new(
                Box::from(BitInputLambda::new()),
                &mut cache,
            ))),
            "__builtin_debug" => environment.push(Arc::from(Lambda::new(
                Box::from(DebugLambda::new()),
                &mut cache,
            ))),
            _ => undefined_vars.push(name),
        }
    }
    if !undefined_vars.is_empty() {
        return Err(Error::UndefinedVariable(undefined_vars));
    }
    run_impl::<Parallel>(expression, &environment, &mut cache)?;
    println!(
        "cache={}, dup={}",
        cache.c1.load(Ordering::SeqCst),
        cache.c2.load(Ordering::SeqCst)
    );
    Ok(())
}
