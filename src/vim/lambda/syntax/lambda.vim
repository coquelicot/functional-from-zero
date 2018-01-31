" Vim syntax file
" Language:     Lambda(?)
" Maintainer:   Darkpi <peter50216@gmail.com>
" Last Change:  Jan 31, 2018

if version < 600
  syntax clear
elseif exists("b:current_syntax")
  finish
endif

let b:current_syntax = "lambda"

syn region lambdaCommentLine start="^\s*#" end="$" contains=@Spell
syn match lambdaLambdaStart "\\" nextgroup=lambdaLambdaArg
syn match lambdaLambdaArg "\S\+" contained

hi def link lambdaCommentLine Comment
hi def link lambdaLambdaStart Keyword
hi def link lambdaLambdaArg Identifier
