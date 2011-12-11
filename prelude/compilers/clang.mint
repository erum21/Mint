# -----------------------------------------------------------------------------
# Definitions for the clang C/C++/Objective-C compiler
# -----------------------------------------------------------------------------

from compiler import compiler

clang = compiler {
  # Inputs
  param flags        : list[string]
  param include_dirs : list[string]
  param library_dirs : list[string]
  param sources      : list[string]
  param outputs      : list[string]
  param warnings_as_errors : bool
  param all_warnings : bool

  # Outputs
  compile => [ 'clang' ] ++
    (all_warnings and [ '-Wall' ]) ++
    (warnings_as_errors and [ '-Werror' ]) ++
    flags ++
    include_dirs.map(x => ['-I', x]).chain() ++
    ['-o', outputs[0]] ++
    sources
}