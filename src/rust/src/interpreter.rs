use std::cell::RefCell;

use super::transformer::{Expression, VariableNameMap};
use super::bitio;

trait Lambda {
    fn exec(&self, arg: Box<Lambda>) -> (Box<Lambda>, bool);
}

struct Closure {}

struct Environment {}

struct BitOutputLambda {
    bit: u8,
    out: RefCell<bitio::BitOutputStream>,
}

impl Lambda for BitOutputLambda {
    fn exec(&self, arg: Box<Lambda>) -> (Box<Lambda>, bool) {
        self.out.borrow_mut().write(self.bit);
        (arg, false)
    }
}

pub fn run(expression: Box<Expression>, free_vars: VariableNameMap) {}
