use std::collections::HashMap;

use super::parser::Node;

#[derive(Debug)]
pub struct IdentifierExpression {
    name: usize,
}

#[derive(Debug)]
pub struct LambdaExpression {
    arg: usize,
    env_map: Vec<isize>,
    body: Box<Expression>,
}

#[derive(Debug)]
pub struct ApplyExpression {
    lambda: Box<Expression>,
    parameter: Box<Expression>,
}

#[derive(Debug)]
pub enum Expression {
    Identifier(IdentifierExpression),
    Lambda(LambdaExpression),
    Apply(ApplyExpression),
}

#[derive(Debug)]
pub struct VariableNameMap<'a> {
    map: HashMap<&'a str, usize>,
}

impl<'a> VariableNameMap<'a> {
    fn new() -> VariableNameMap<'a> {
        VariableNameMap {
            map: HashMap::new(),
        }
    }
    fn get(&mut self, name: &'a str) -> usize {
        let len = self.map.len();
        *self.map.entry(name).or_insert(len)
    }
}

fn transform_impl<'a>(
    root: &Box<Node<'a>>,
    free_vars: &mut VariableNameMap<'a>,
) -> Box<Expression> {
    match **root {
        Node::Identifier(name) => {
            let name = free_vars.get(name);
            Box::from(Expression::Identifier(IdentifierExpression { name }))
        }
        Node::Lambda(arg_name, ref body) => {
            let mut body_free_vars = VariableNameMap::new();
            let body = transform_impl(body, &mut body_free_vars);
            let arg = body_free_vars.get(arg_name);
            let mut env_map = vec![-1; body_free_vars.map.len()];
            for (name, idx) in body_free_vars.map {
                if name != arg_name {
                    env_map[idx] = free_vars.get(name) as isize;
                }
            }
            Box::from(Expression::Lambda(LambdaExpression { arg, body, env_map }))
        }
        Node::Apply(ref lambda, ref parameter) => {
            let lambda = transform_impl(lambda, free_vars);
            let parameter = transform_impl(parameter, free_vars);
            Box::from(Expression::Apply(ApplyExpression { lambda, parameter }))
        }
    }
}

pub fn transform(root: Box<Node>) -> (Box<Expression>, VariableNameMap) {
    let mut free_vars = VariableNameMap::new();
    let expression = transform_impl(&root, &mut free_vars);
    (expression, free_vars)
}
