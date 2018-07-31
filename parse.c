/*
 This file is part of qbce-prepro.

 Copyright 2018 
 Florian Lonsing, Vienna University of Technology, Austria.

 qbce-prepro is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or (at
 your option) any later version.

 qbce-prepro is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with qbce-prepro.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <ctype.h>
#include "parse.h"
#include "error.h"

/* Print array 'lits' of literals of length 'num'. If 'print_info' is
non-zero, then print info about the scope of each literal in the array. */
static void
print_lits (QBCEPrepro * qr, FILE * out, LitID * lits, unsigned int num,
            const int print_info)
{
  Var *vars = qr->pcnf.vars;
  LitID *p, *e;
  for (p = lits, e = p + num; p < e; p++)
    {
      LitID lit = *p;
      Var *var = LIT2VARPTR (vars, lit);
      if (print_info)
        fprintf (out, "%c(%d)%d ",
                 SCOPE_FORALL (var->scope) ? 'A' : 'E',
                 var->scope->nesting, *p);
      else
        fprintf (out, "%d ", *p);
    }
  fprintf (out, "0\n");
}

/* -------------------- START: QDIMACS PARSING -------------------- */

/* Allocate table of variable IDs having fixed size. If the preamble of the
   QDIMACS file specifies a maximum variable ID which is smaller than the ID
   of a variable encountered in the formula, then the program aborts. */
static void
set_up_var_table (QBCEPrepro * qr, int num)
{
  assert (num >= 0);
  assert (!qr->pcnf.size_vars);
  qr->pcnf.size_vars = num + 1;
  assert (!qr->pcnf.vars);
  qr->pcnf.vars =
    (Var *) mm_malloc (qr->mm, qr->pcnf.size_vars * sizeof (Var));
}

/* Allocate a new scope object and append it to the list of scopes. */
static void
open_new_scope (QBCEPrepro * qr, QuantifierType scope_type)
{
  Scope *scope = mm_malloc (qr->mm, sizeof (Scope));
  scope->type = scope_type;
  scope->nesting = qr->pcnf.scopes.last ?
    qr->pcnf.scopes.last->nesting + 1 : 0;
  assert (!qr->opened_scope);
  /* Keep pointer to opened scope to add parsed variable IDs afterwards. */
  qr->opened_scope = scope;
  /* Append scope to list of scopes. */
  LINK_LAST (qr->pcnf.scopes, scope, link);
}

/* Abort if clause contains complementary literals (i.e. clause is
   tautological) or multiple literals of the same variable. (Alternatively,
   tautological clauses or multiple literals could be discarded. However, for
   simplicity, the program will abort.) */
static void
check_and_add_clause (QBCEPrepro * qr, Clause * clause)
{
  /* Add parsed literals to allocated clause object 'clause'. */
  LitID *p, *e, *clause_lits_p = clause->lits;
  for (p = qr->parsed_literals.start, e = qr->parsed_literals.top; p < e; p++)
    {
      LitID lit = *p;
      VarID varid = LIT2VARID (lit);
      ABORT_APP (varid >= qr->pcnf.size_vars,
                       "variable ID in clause exceeds max. ID given in preamble!");
      Var *var = LIT2VARPTR (qr->pcnf.vars, lit);
      ABORT_APP (!var->scope,
                       "variable has not been declared in a scope!");

      /* Check for complementary and multiple occurrences of literals. */
      if (VAR_POS_MARKED (var))
        {
          ABORT_APP (LIT_POS (lit),
                           "literal has multiple positive occurrences!");
          ABORT_APP (LIT_NEG (lit),
                           "literal has complementary occurrences!");
        }
      else if (VAR_NEG_MARKED (var))
        {
          ABORT_APP (LIT_NEG (lit),
                           "literal has multiple negative occurrences!");
          ABORT_APP (LIT_POS (lit),
                           "literal has complementary occurrences!");
        }
      else
        {
          assert (!VAR_MARKED (var));
          if (LIT_NEG (lit))
            VAR_NEG_MARK (var);
          else
            VAR_POS_MARK (var);
        }

      assert (clause_lits_p < clause->lits + clause->num_lits);
      /* Add literal to clause object. */
      *clause_lits_p++ = *p;
      /* Push clause object on stack of occurrences. */
      if (LIT_NEG (lit))
        PUSH_STACK (qr->mm, var->neg_occ_clauses, clause);
      else
        PUSH_STACK (qr->mm, var->pos_occ_clauses, clause);
    }

  /* NOTE: literals in clauses are neither sorted nor universal-reduced,
     i.e. they appear in the clause as they are given in the QDIMACS file. */

  /* Unmark variables. */
  for (p = qr->parsed_literals.start, e = qr->parsed_literals.top; p < e; p++)
    VAR_UNMARK (LIT2VARPTR (qr->pcnf.vars, *p));

  /* Append clause to list of clauses. */
  LINK_LAST (qr->pcnf.clauses, clause, link);
}

/* Check and add a parsed clause to the PCNF data structures. */
static void
import_parsed_clause (QBCEPrepro * qr)
{
  assert (!qr->opened_scope);
  /* Allocate new clause object capable of storing 'num_lits' literals. The
     literals on the stack 'parsed_literals' will be copied to the new clause
     object. */
  int num_lits = COUNT_STACK (qr->parsed_literals);
  Clause *clause = mm_malloc (qr->mm, sizeof (Clause) +
                              num_lits * sizeof (LitID));
  clause->id = ++qr->cur_clause_id;
  ABORT_APP (clause->id > qr->declared_num_clauses,
                   "actual number of clauses exceeds declared number of clauses!");
  clause->num_lits = num_lits;

  /* Add the parsed clause to the formula and to the stacks of variable
     occurrences, provided that it does not contain complementary or multiple
     literals of the same variable. */
  check_and_add_clause (qr, clause);

  if (qr->options.verbosity >= 2)
    {
      fprintf (stderr, "Imported clause: ");
      print_lits (qr, stderr, clause->lits, clause->num_lits, 1);
    }
}

/* Add parsed scope to data structures. */
static void
import_parsed_scope_variables (QBCEPrepro * qr)
{
  assert (qr->opened_scope);
  assert (EMPTY_STACK (qr->opened_scope->vars));
  LitID *p, *e;
  for (p = qr->parsed_literals.start, e = qr->parsed_literals.top; p < e; p++)
    {
      LitID varid = *p;
      ABORT_APP (varid <= 0,
                       "variable ID in scope must be positive!\n");
      ABORT_APP ((VarID) varid >= qr->pcnf.size_vars,
                       "variable ID in scope exceeds max. ID given in preamble!");

      /* Add variable ID to list of IDs in the scope. */
      PUSH_STACK (qr->mm, qr->opened_scope->vars, varid);
      Var *var = VARID2VARPTR (qr->pcnf.vars, varid);
      /* Set variable ID and pointer to scope of variable. */
      ABORT_APP (var->id, "variable already quantified!\n");
      var->id = varid;
      assert (!var->scope);
      var->scope = qr->opened_scope;
    }
  /* The current scope has been added to the scope list already. */
  assert (qr->opened_scope == qr->pcnf.scopes.first ||
          (qr->opened_scope->link.prev && !qr->opened_scope->link.next));
  assert (qr->opened_scope != qr->pcnf.scopes.first ||
          (!qr->opened_scope->link.prev && !qr->opened_scope->link.next));
}

/* Collect parsed literals of a scope or a clause on auxiliary stack to be
   imported and added to data structures later. */
static void
collect_parsed_literal (QBCEPrepro * qr, int num)
{
  if (num == 0)
    {
      if (qr->opened_scope)
        {
          import_parsed_scope_variables (qr);
          qr->opened_scope = 0;
        }
      else
        import_parsed_clause (qr);
      RESET_STACK (qr->parsed_literals);
    }
  else
    PUSH_STACK (qr->mm, qr->parsed_literals, num);
}

#define PARSER_READ_NUM(num, c)                        \
  assert (isdigit (c));                                \
  num = 0;					       \
  do						       \
    {						       \
      num = num * 10 + (c - '0');		       \
    }						       \
  while (isdigit ((c = getc (in))));

#define PARSER_SKIP_SPACE_DO_WHILE(c)		     \
  do						     \
    {                                                \
      c = getc (in);				     \
    }                                                \
  while (isspace (c));

#define PARSER_SKIP_SPACE_WHILE(c)		     \
  while (isspace (c))                                \
    c = getc (in);

/* Non-static top-level function for parsing. */
void parse (QBCEPrepro * qr, FILE * in)
{
  int neg = 0, preamble_found = 0;
  LitID num = 0;
  QuantifierType scope_type = QTYPE_UNDEF;

  assert (in);

  int c;
  while ((c = getc (in)) != EOF)
    {
      PARSER_SKIP_SPACE_WHILE (c);

      while (c == 'c')
        {
          while ((c = getc (in)) != '\n' && c != EOF)
            ;
          c = getc (in);
        }

      PARSER_SKIP_SPACE_WHILE (c);

      if (c == 'p')
        {
          /* parse preamble */
          PARSER_SKIP_SPACE_DO_WHILE (c);
          if (c != 'c')
            goto MALFORMED_PREAMBLE;
          PARSER_SKIP_SPACE_DO_WHILE (c);
          if (c != 'n')
            goto MALFORMED_PREAMBLE;
          PARSER_SKIP_SPACE_DO_WHILE (c);
          if (c != 'f')
            goto MALFORMED_PREAMBLE;
          PARSER_SKIP_SPACE_DO_WHILE (c);
          if (!isdigit (c))
            goto MALFORMED_PREAMBLE;

          /* Read number of variables */
          PARSER_READ_NUM (num, c);

          /* Allocate array of variable IDs of size 'num + 1', since 0 is an
             invalid variable ID. */
          set_up_var_table (qr, num);

          PARSER_SKIP_SPACE_WHILE (c);
          if (!isdigit (c))
            goto MALFORMED_PREAMBLE;

          /* Read number of clauses */
          PARSER_READ_NUM (num, c);

          qr->declared_num_clauses = num;

          if (qr->options.verbosity >= 1)
            fprintf (stderr, "parsed preamble: p cnf %d %d\n",
                     (qr->pcnf.size_vars - 1), qr->declared_num_clauses);

          preamble_found = 1;
          goto PARSE_SCOPE_OR_CLAUSE;

        MALFORMED_PREAMBLE:
          ABORT_APP (1, "malformed preamble!\n");
          return;
        }
      else
        {
          ABORT_APP (1, "expecting preamble!\n");
          return;
        }

    PARSE_SCOPE_OR_CLAUSE:

      PARSER_SKIP_SPACE_WHILE (c);

      if (c == 'a' || c == 'e')
        {
          /* Open a new scope */
          if (c == 'a')
            scope_type = QTYPE_FORALL;
          else
            scope_type = QTYPE_EXISTS;

          ABORT_APP (qr->opened_scope,
                           "must close scope by '0' before opening a new scope!\n");

          open_new_scope (qr, scope_type);

          PARSER_SKIP_SPACE_DO_WHILE (c);
        }

      if (!isdigit (c) && c != '-')
        {
          if (c == EOF)
            return;
          ABORT_APP (1, "expecting digit or '-'!\n");
          return;
        }
      else
        {
          if (c == '-')
            {
              neg = 1;
              if (!isdigit ((c = getc (in))))
                {
                  ABORT_APP (1, "expecting digit!\n");
                  return;
                }
            }

          /* parse a literal or variable ID */
          PARSER_READ_NUM (num, c);
          num = neg ? -num : num;

          collect_parsed_literal (qr, num);

          neg = 0;

          goto PARSE_SCOPE_OR_CLAUSE;
        }
    }

  if (!preamble_found)
    ABORT_APP (1, "preamble missing!\n");
}

/* -------------------- END: QDIMACS PARSING -------------------- */

