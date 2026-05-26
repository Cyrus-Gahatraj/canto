module.exports = grammar({
  name: "canto",
  extras: $ => [
    /\s/,
    $.comment,
  ],

  conflicts: $ => [
    [$._definition, $.expression],
    [$._statement, $.expression],
    [$._definition, $.binary_expression],
    [$._statement, $.binary_expression],
    [$.varible_definition, $.binary_expression],
    [$.write_statement, $.binary_expression],
    [$.if_statement, $.if_statement],
  ],

  precedences: $ => [
    [
      'if',
      'write',
      'return',
      'unary',
      'binary_log',
      'binary_comp',
      'binary_add',
      'binary_mul',
    ]
  ],

  rules: {
    source_file: $ => repeat(seq($._definition, optional(';'))),

    comment: $ => token(choice(
      seq('~', /[^\n]*/),
      seq('~~', /[^*]*\*+([^~*][^*]*\*+)*/, '~')
    )),

    _definition: $ => choice(
      $.function_definition,
      $.varible_definition,
      $.if_statement,
      $.loop_statement,
      $.write_statement,
      $.expression
    ),

    function_definition: $ => seq(
      'let',
      field('name', $.identifier),
      $.parameter_list,
      $.block
    ),

    varible_definition: $ => seq(
      'let',
      field('name', $.identifier),
      $.expression,
    ),

    parameter_list: $ => seq('(',  ')'),

    _type: $ => choice('Int'),

    block: $ => seq(
      '{',
      repeat(seq($._statement, optional(';'))),
      '}'
    ),

    _statement: $ => choice(
      $.return_statement,
      $.varible_definition,
      $.if_statement,
      $.loop_statement,
      $.write_statement,
      $.expression
    ),

    return_statement: $ => prec('return', seq(
      'return',
      $.expression,
    )),

    if_statement: $ => prec('if', seq(
      'if',
      field('condition', $.expression),
      field('consequence', $.block),
      repeat($.or_clause),
      optional($.else_clause)
    )),

    or_clause: $ => prec('if', seq(
      'or',
      field('condition', $.expression),
      field('consequence', $.block)
    )),

    else_clause: $ => seq(
      'else',
      field('consequence', $.block)
    ),

    loop_statement: $ => seq(
      'loop',
      optional(field('value', $.expression)),
      field('body', $.block)
    ),

    write_statement: $ => prec('write', seq(
      'write',
      commaSeparated($.expression)
    )),

    expression: $ => choice(
      $.identifier,
      $.number,
      $.string,
      $.unary_expression,
      $.binary_expression,
      $.if_statement,
      $.loop_statement
    ),

    unary_expression: $ => choice(
      prec('unary', seq('-', $.expression)),
      prec('unary', seq('!', $.expression)),
    ),

    binary_expression: $ => choice(
      prec.left('binary_mul', seq($.expression, choice('*', '/', '%'), $.expression)),
      prec.left('binary_add', seq($.expression, choice('+', '-'), $.expression)),
      prec.left('binary_comp', seq($.expression, choice('<', '>', '<=', '>=', '='), $.expression)),
      prec.left('binary_log', seq($.expression, choice('and', 'or', '|'), $.expression)),
    ),

    identifier: $ => /[a-z_][a-zA-Z0-9_]*/,
    number: $ => /\d+/,
    string: $ => /"([^"\\]|\\.)*"/
  }
});

function commaSeparated(rule) {
  return seq(rule, repeat(seq(',', rule)));
}
