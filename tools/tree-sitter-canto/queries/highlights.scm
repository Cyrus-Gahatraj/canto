; Keywords
"let" @keyword
"return" @keyword
"if" @keyword
"or" @keyword
"else" @keyword
"loop" @keyword
"and" @keyword
"write" @keyword

; Punctuation & Delimiters
"{" @punctuation.bracket
"}" @punctuation.bracket
"(" @punctuation.bracket
")" @punctuation.bracket
"," @punctuation.delimiter
";" @punctuation.delimiter

; Operators
"*" @operator
"/" @operator
"%" @operator
"+" @operator
"-" @operator
"!" @operator
"<" @operator
">" @operator
"<=" @operator
">=" @operator
"=" @operator
"|" @operator

; Literals & Identifiers
(identifier) @variable
(number) @number
(string) @string
(comment) @comment

; Functions & Variable Names (using fields)
(function_definition name: (identifier) @function)
(varible_definition name: (identifier) @variable)

