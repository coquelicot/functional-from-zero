use std::collections::HashMap;
use std::rc::Rc;

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
pub enum Expression {
    Identifier(IdentifierExpression),
    Lambda(LambdaExpression),
    Apply(ApplyExpression),
}

#[derive(Debug)]
pub struct ExpressionCache {
    identifier_cache: HashMap<usize, Rc<Expression>>,
    lambda_cache: HashMap<(Vec<usize>, *const Expression), Rc<Expression>>,
    apply_cache: HashMap<(*const Expression, *const Expression), Rc<Expression>>,
}

impl ExpressionCache {
    fn new() -> ExpressionCache {
        ExpressionCache {
            identifier_cache: HashMap::new(),
            lambda_cache: HashMap::new(),
            apply_cache: HashMap::new(),
        }
    }
    fn new_identifier(&mut self, name: usize) -> Rc<Expression> {
        self.identifier_cache
            .entry(name)
            .or_insert_with(|| Rc::from(Expression::Identifier(IdentifierExpression { name })))
            .clone()
    }
    fn new_lambda(&mut self, env_map: Vec<usize>, body: Rc<Expression>) -> Rc<Expression> {
        self.lambda_cache
            .entry((env_map.clone(), body.as_ref() as *const Expression))
            .or_insert_with(|| Rc::from(Expression::Lambda(LambdaExpression { env_map, body })))
            .clone()
    }
    fn new_apply(&mut self, lambda: Rc<Expression>, parameter: Rc<Expression>) -> Rc<Expression> {
        self.apply_cache
            .entry((
                lambda.as_ref() as *const Expression,
                parameter.as_ref() as *const Expression,
            ))
            .or_insert_with(|| Rc::from(Expression::Apply(ApplyExpression { lambda, parameter })))
            .clone()
    }
}

#[derive(Debug)]
struct VariableNameMap<'a> {
    map: HashMap<&'a str, usize>,
}

impl<'a> VariableNameMap<'a> {
    fn new() -> VariableNameMap<'a> {
        VariableNameMap {
            map: HashMap::new(),
        }
    }
    fn get_or_insert(&mut self, name: &'a str) -> usize {
        let len = self.map.len();
        *self.map.entry(name).or_insert(len)
    }
}

fn transform_impl<'a>(
    root: &Node<'a>,
    free_vars: &mut VariableNameMap<'a>,
    expression_cache: &mut ExpressionCache,
) -> Rc<Expression> {
    match *root {
        Node::Identifier(name) => {
            let name = free_vars.get_or_insert(name);
            expression_cache.new_identifier(name)
        }
        Node::Lambda(arg_name, ref body) => {
            let mut body_free_vars = VariableNameMap::new();
            let arg = body_free_vars.get_or_insert(arg_name);
            assert!(arg == 0);
            let body = transform_impl(body, &mut body_free_vars, expression_cache);
            let mut env_map = vec![0; body_free_vars.map.len() - 1];
            for (name, idx) in body_free_vars.map {
                assert!(name != arg_name || idx == 0);
                if idx > 0 {
                    env_map[idx - 1] = free_vars.get_or_insert(name);
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

pub fn transform<'a>(root: &'a Node) -> (Rc<Expression>, Vec<&'a str>) {
    let mut free_vars = VariableNameMap::new();
    let mut expression_cache = ExpressionCache::new();
    let expression = transform_impl(&root, &mut free_vars, &mut expression_cache);
    let mut free_vars: Vec<(&&str, &usize)> = free_vars.map.iter().collect();
    free_vars.sort_by_key(|&(_, v)| v);
    (expression, free_vars.iter().map(|&(&k, _)| k).collect())
}
