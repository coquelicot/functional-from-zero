use std::collections::HashMap;

use super::parser::Node;

#[derive(Debug)]
pub struct IdentifierExpression {
    pub name: usize,
}

#[derive(Debug)]
pub struct LambdaExpression {
    pub env_map: Vec<isize>,
    pub body: Box<Expression>,
}

#[derive(Debug)]
pub struct ApplyExpression {
    pub lambda: Box<Expression>,
    pub parameter: Box<Expression>,
}

#[derive(Debug)]
pub enum Expression {
    Identifier(IdentifierExpression),
    Lambda(LambdaExpression),
    Apply(ApplyExpression),
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

fn transform_impl<'a>(root: &Node<'a>, free_vars: &mut VariableNameMap<'a>) -> Expression {
    match *root {
        Node::Identifier(name) => {
            let name = free_vars.get_or_insert(name);
            Expression::Identifier(IdentifierExpression { name })
        }
        Node::Lambda(arg_name, ref body) => {
            let mut body_free_vars = VariableNameMap::new();
            let arg = body_free_vars.get_or_insert(arg_name);
            assert!(arg == 0);
            let body = Box::from(transform_impl(body, &mut body_free_vars));
            let mut env_map = vec![-1; body_free_vars.map.len() - 1];
            for (name, idx) in body_free_vars.map {
                assert!(name != arg_name || idx == 0);
                if idx > 0 {
                    env_map[idx - 1] = free_vars.get_or_insert(name) as isize;
                }
            }
            Expression::Lambda(LambdaExpression { body, env_map })
        }
        Node::Apply(ref lambda, ref parameter) => {
            let lambda = Box::from(transform_impl(lambda, free_vars));
            let parameter = Box::from(transform_impl(parameter, free_vars));
            Expression::Apply(ApplyExpression { lambda, parameter })
        }
    }
}

pub fn transform<'a>(root: &'a Node) -> (Expression, Vec<&'a str>) {
    let mut free_vars = VariableNameMap::new();
    let expression = transform_impl(&root, &mut free_vars);
    let mut free_vars: Vec<(&&str, &usize)> = free_vars.map.iter().collect();
    free_vars.sort_by_key(|&(_, v)| v);
    (expression, free_vars.iter().map(|&(&k, _)| k).collect())
}
