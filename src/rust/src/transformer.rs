use std::collections::HashMap;
use std::rc::Rc;
use std::sync::atomic::{AtomicUsize, Ordering};

use super::tokenizer::Token;
use super::parser::Node;

#[derive(Debug)]
pub struct IdentifierExpression {
    pub name: usize,
}

#[derive(Debug)]
pub struct LambdaExpression {
    pub env_map: Vec<usize>,
    pub body: Rc<Expression>,
}

#[derive(Debug)]
pub struct ApplyExpression {
    pub lambda: Rc<Expression>,
    pub parameter: Rc<Expression>,
}

#[derive(Debug)]
pub enum ExpressionValue {
    Identifier(IdentifierExpression),
    Lambda(LambdaExpression),
    Apply(ApplyExpression),
}

#[derive(Debug)]
pub struct Expression {
    pub value: ExpressionValue,
    pub id: usize,
}

#[derive(Debug)]
pub struct ExpressionCache {
    id_counter: AtomicUsize,
    identifier_cache: HashMap<usize, Rc<Expression>>,
    lambda_cache: HashMap<(Vec<usize>, usize), Rc<Expression>>,
    apply_cache: HashMap<(usize, usize), Rc<Expression>>,
}

impl ExpressionCache {
    fn new() -> ExpressionCache {
        ExpressionCache {
            id_counter: AtomicUsize::new(0),
            identifier_cache: HashMap::new(),
            lambda_cache: HashMap::new(),
            apply_cache: HashMap::new(),
        }
    }
    fn new_id(&mut self) -> usize {
        self.id_counter.fetch_add(1, Ordering::Relaxed)
    }
    fn new_identifier(&mut self, name: usize) -> Rc<Expression> {
        let id = self.new_id();
        self.identifier_cache
            .entry(name)
            .or_insert_with(|| {
                Rc::from(Expression {
                    id,
                    value: ExpressionValue::Identifier(IdentifierExpression { name }),
                })
            })
            .clone()
    }
    fn new_lambda(&mut self, env_map: Vec<usize>, body: Rc<Expression>) -> Rc<Expression> {
        let id = self.new_id();
        self.lambda_cache
            .entry((env_map.clone(), body.id))
            .or_insert_with(|| {
                Rc::from(Expression {
                    id,
                    value: ExpressionValue::Lambda(LambdaExpression { env_map, body }),
                })
            })
            .clone()
    }
    fn new_apply(
        &mut self,
        lambda: Rc<Expression>,
        parameter: Rc<Expression>,
    ) -> Rc<Expression> {
        let id = self.new_id();
        self.apply_cache
            .entry((lambda.id, parameter.id))
            .or_insert_with(|| {
                Rc::from(Expression {
                    id,
                    value: ExpressionValue::Apply(ApplyExpression { lambda, parameter }),
                })
            })
            .clone()
    }
}

// value is (reindexed name, first seen token).
type Variable<'a> = (usize, &'a Token<'a>);

#[derive(Debug)]
struct VariableNameMap<'a> {
    map: HashMap<&'a str, Variable<'a>>,
}

impl<'a> VariableNameMap<'a> {
    fn new() -> VariableNameMap<'a> {
        VariableNameMap {
            map: HashMap::new(),
        }
    }
    fn get_or_insert(&mut self, token: &'a Token<'a>) -> usize {
        let len = self.map.len();
        self.map.entry(token.token_raw).or_insert((len, token)).0
    }
}

fn transform_impl<'a>(
    root: &Node<'a>,
    free_vars: &mut VariableNameMap<'a>,
    expression_cache: &mut ExpressionCache,
) -> Rc<Expression> {
    match *root {
        Node::Identifier(token) => {
            let name = free_vars.get_or_insert(token);
            expression_cache.new_identifier(name)
        }
        Node::Lambda(arg_token, ref body) => {
            let mut body_free_vars = VariableNameMap::new();
            let arg = body_free_vars.get_or_insert(arg_token);
            assert!(arg == 0);
            let body = transform_impl(body, &mut body_free_vars, expression_cache);
            let mut env_map = vec![0; body_free_vars.map.len() - 1];
            for (name, (idx, token)) in body_free_vars.map {
                assert!(name != arg_token.token_raw || idx == 0);
                if idx > 0 {
                    env_map[idx - 1] = free_vars.get_or_insert(token);
                }
            }
            expression_cache.new_lambda(env_map, body)
        }
        Node::Apply(ref lambda, ref parameter) => {
            let lambda = transform_impl(lambda, free_vars, expression_cache);
            let parameter = transform_impl(parameter, free_vars, expression_cache);
            expression_cache.new_apply(lambda, parameter)
        }
    }
}

pub fn transform<'a>(root: &'a Node) -> (Rc<Expression>, Vec<&'a Token<'a>>) {
    let mut free_vars = VariableNameMap::new();
    let mut expression_cache = ExpressionCache::new();
    let expression = transform_impl(&root, &mut free_vars, &mut expression_cache);
    let mut free_vars: Vec<(usize, &Token)> = free_vars.map.into_iter().map(|(_, v)| v).collect();
    free_vars.sort_by_key(|&(name, _)| name);
    (
        expression,
        free_vars.iter().map(|&(_, token)| token).collect(),
    )
}
