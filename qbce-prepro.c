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

#include <stdio.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <dirent.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include "qbce-prepro.h"
#include "stack.h"
#include "mem.h"
#include "parse.h"
#include "error.h"

/* -------------------- START: Helper macros -------------------- */

#define USAGE \
"usage: ./qbce-prepro [options] input-formula [timeout]\n"\
"\n"\
"  - 'input-formula' is a file in QDIMACS format (default: stdin)\n"\
"  - '[timeout]' is an optional timeout in seconds\n"\
"  - '[options]' is any combination of the following:\n\n"\
"    -h, --help         print this usage information and exit\n"\
"    --simplify         detect and mark blocked clauses\n"\
"    --print-formula    print parsed (and simplified) formula to stdout\n"\
"                         Note: if option '--simplify' is NOT given\n"\
"                         then the original formula is printed as is\n"\
"    -v                 increase verbosity level incrementally (default: 0)\n"\
"\n"

/* -------------------- END: Helper macros -------------------- */

/* -------- START: Application defintions and functions -------- */

/* Print error message. */
static void
print_abort_err (char *msg, ...)
{
  va_list list;
  assert (msg != NULL);
  fprintf (stderr, "qbce-prepro: ");
  va_start (list, msg);
  vfprintf (stderr, msg, list);
  va_end (list);
  fflush (stderr);
  abort ();
}

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

/* -------- END: Application defintions and functions -------- */

/* -------------------- START: COMMAND LINE PARSING -------------------- */
static void
set_default_options (QBCEPrepro * qr)
{
  qr->options.in_filename = 0;
  qr->options.in = stdin;
  qr->options.print_usage = 0;
}

static int
isnumstr (char *str)
{
  /* Empty string is not considered as number-string. */
  if (!*str)
    return 0;
  char *p;
  for (p = str; *p; p++)
    {
      if (!isdigit (*p))
        return 0;
    }
  return 1;
}

/* Parse command line arguments to set options accordingly. Run the program
   with '-h' or '--help' to print usage information. */
static void
parse_cmd_line_options (QBCEPrepro * qr, int argc, char **argv)
{
  char *result;
  int opt_cnt;
  for (opt_cnt = 1; opt_cnt < argc; opt_cnt++)
    {
      char *opt_str = argv[opt_cnt];

      if (!strcmp (opt_str, "-h") || !strcmp (opt_str, "--help"))
        {
          qr->options.print_usage = 1;
        }
      else if (!strcmp (opt_str, "--simplify"))
        {
          qr->options.simplify = 1;
        }
      else
        if (!strncmp (opt_str, "--print-formula", strlen ("--print-formula")))
        {
          qr->options.print_formula = 1;
        }
      else if (!strcmp (opt_str, "-v"))
        {
          qr->options.verbosity++;
        }
      else if (isnumstr (opt_str))
        {
          qr->options.max_time = atoi (opt_str);
          if (qr->options.max_time == 0)
            {
              result = "Expecting non-zero value for max-time";
              print_abort_err ("%s!\n\n", result);
            }
        }
      else if (!qr->options.in_filename)
        {
          qr->options.in_filename = opt_str;
          /* Check input file. */
          DIR *dir;
          if ((dir = opendir (qr->options.in_filename)) != NULL)
            {
              closedir (dir);
              print_abort_err ("input file '%s' is a directory!\n\n",
                               qr->options.in_filename);
            }
          FILE *input_file = fopen (qr->options.in_filename, "r");
          if (!input_file)
            {
              print_abort_err ("could not open input file '%s'!\n\n",
                               qr->options.in_filename);
            }
          else
            qr->options.in = input_file;
        }
      else
        {
          print_abort_err ("unknown option '%s'!\n\n", opt_str);
        }
    }
}

/* -------------------- END: COMMAND LINE PARSING -------------------- */

/* -------------------- START: HELPER FUNCTIONS -------------------- */

/* Set signal handler. */
static void
sig_handler (int sig)
{
  fprintf (stderr, "\n\n SIG RECEIVED\n\n");
  signal (sig, SIG_DFL);
  raise (sig);
}

/* Set signal handler. */
static void
sigalrm_handler (int sig)
{
  fprintf (stderr, "\n\n SIGALRM RECEIVED\n\n");
  signal (sig, SIG_DFL);
  raise (sig);
}

/* Set signal handler. */
static void
set_signal_handlers (void)
{
  signal (SIGINT, sig_handler);
  signal (SIGTERM, sig_handler);
  signal (SIGALRM, sigalrm_handler);
  signal (SIGXCPU, sigalrm_handler);
}

static void
print_usage ()
{
  fprintf (stdout, USAGE);
}

/* Free allocated memory. */
static void
cleanup (QBCEPrepro * qr)
{
  if (qr->options.in_filename)
    fclose (qr->options.in);

  DELETE_STACK (qr->mm, qr->parsed_literals);

  Var *vp, *ve;
  for (vp = qr->pcnf.vars, ve = vp + qr->pcnf.size_vars; vp < ve; vp++)
    {
      DELETE_STACK (qr->mm, vp->pos_occ_clauses);
      DELETE_STACK (qr->mm, vp->neg_occ_clauses);
    }
  mm_free (qr->mm, qr->pcnf.vars, qr->pcnf.size_vars * sizeof (Var));

  Scope *s, *sn;
  for (s = qr->pcnf.scopes.first; s; s = sn)
    {
      sn = s->link.next;
      DELETE_STACK (qr->mm, s->vars);
      mm_free (qr->mm, s, sizeof (Scope));
    }

  Clause *c, *cn;
  for (c = qr->pcnf.clauses.first; c; c = cn)
    {
      cn = c->link.next;
      mm_free (qr->mm, c, sizeof (Clause) + c->num_lits * sizeof (LitID));
    }
}

/* Print (simplified) formula to file 'out'. If '--simplify'
   is specified then blocked clauses will not be printed. */
static void
print_formula (QBCEPrepro * qr, FILE * out)
{
  assert (qr->pcnf.clauses.cnt >= qr->cnt_blocked_clauses);
  /* Print preamble. */
  assert (qr->pcnf.size_vars > 0);
  fprintf (out, "p cnf %d %d\n", (qr->pcnf.size_vars - 1),
           (qr->pcnf.clauses.cnt - qr->cnt_blocked_clauses));

  /* Print prefix. */
  Scope *s;
  for (s = qr->pcnf.scopes.first; s; s = s->link.next)
    {
      fprintf (out, "%c ", SCOPE_FORALL (s) ? 'a' : 'e');
      print_lits (qr, out, (LitID *) s->vars.start,
                  COUNT_STACK (s->vars), 0);
    }

  /* Print clauses. */
  Clause *c;
  for (c = qr->pcnf.clauses.first; c; c = c->link.next)
    if (!c->blocked)
      print_lits (qr, out, c->lits, c->num_lits, 0);
}

/* Get process time. Can be used for performance statistics. */
static double
time_stamp ()
{
  double result = 0;
  struct rusage usage;

  if (!getrusage (RUSAGE_SELF, &usage))
    {
      result += usage.ru_utime.tv_sec + 1e-6 * usage.ru_utime.tv_usec;
      result += usage.ru_stime.tv_sec + 1e-6 * usage.ru_stime.tv_usec;
    }

  return result;
}

/* -------------------- END: HELPER FUNCTIONS -------------------- */

/* -------------------- START: QBCE -------------------- */

/* Find and mark all blocked clauses. For blocked clauses 'c', the
   flag 'c->blocked' should be set to '1' (true). */
static void
find_and_mark_blocked_clauses (QBCEPrepro * qr)
{
  ABORT_APP (1, "TO BE IMPLEMENTED!");
}

/* -------------------- END: QBCE -------------------- */

/* --------------- START: DEMO CODE (DATA STRUCTURES) --------------- */

static void
demo_print_variables_occurrences (QBCEPrepro * qr, Var *var, 
                                  ClausePtrStack *clause_stack)
{
  fprintf (stderr, "Printing occurrences of variable %u\n", var->id);

  /* Get pointer to stacks of (pointers) to clauses where variable 'var'
     appears positively and negatively, respectively. */
  ClausePtrStack *pos_occs =  &(var->pos_occ_clauses);
  ClausePtrStack *neg_occs =  &(var->neg_occ_clauses);

  assert (EMPTY_STACK (*clause_stack));

  /* Collect clauses where 'var' appears positively on stack 'clause_stack',
     using flag 'mark' of clause objects to indicate if a clause object has
     been pushed on the stack already (in this example, the use of 'mark' is
     not necessary since variable can have at most one literal in a clause,
     however, we show it for demonstration). */
  Clause **cp, **ce;
  for (cp = pos_occs->start, ce = pos_occs->top; cp < ce; cp++)
    {
      Clause *c = *cp;
      if (!c->mark)
        {
          c->mark = 1;
          PUSH_STACK (qr->mm, *clause_stack, c);
        }
    }

  /* Same as above but for clause where 'var' appears negatively. */
  for (cp = neg_occs->start, ce = neg_occs->top; cp < ce; cp++)
    {
      Clause *c = *cp;
      if (!c->mark)
        {
          c->mark = 1;
          PUSH_STACK (qr->mm, *clause_stack, c);
        }
    }

  /* Iterate over all clauses collected on 'clause_stack', reset mark and
     print clause. */
  for (cp = clause_stack->start, ce = clause_stack->top; cp < ce; cp++)
    {
      Clause *c = *cp;
      assert (c->mark);
      c->mark = 0;
      /* Print clause. */
      fprintf (stderr, " Occ: ");
      print_lits (qr, stderr, c->lits, c->num_lits, 0);

      /* Additionally, print all literals in the clause that are smaller than
         'var' with respect to the prefix ordering. */
      fprintf (stderr, "  Literals smaller than %u in prefix ordering: ", var->id);
      LitID *lp, *le;
      for (lp = c->lits, le = lp + c->num_lits; lp < le; lp++)
        {
          LitID lit = *lp; 
          Var *other = LIT2VARPTR (qr->pcnf.vars, lit);
          if (other->scope->nesting < var->scope->nesting)
            fprintf (stderr, "%d ", lit);
        }
      fprintf (stderr, "0\n");
    }

  RESET_STACK (*clause_stack);
  assert (EMPTY_STACK (*clause_stack));
}

/* This function is intended to demonstrate the data structures. It prints all
   clauses where a particular variable appears in. */
static void
demo (QBCEPrepro * qr)
{
  /* Declare and initialize stack 'clause_stack' of pointers to clauses. */
  ClausePtrStack clause_stack;
  INIT_STACK (clause_stack);

  /* Iterate over table of variable objects. */
  Var *vp, *ve;
  for (vp = qr->pcnf.vars, ve = vp + qr->pcnf.size_vars; vp < ve; vp++)
    {
      /* Consider only variable objects which correspond to variables that
         actually appear in the formula. This is done by checking whether the
         variable ID is nonzero. */
      if (vp->id)
        demo_print_variables_occurrences (qr, vp, &clause_stack);
    }

  /* Release all memory of stack 'clause_stack'. */
  DELETE_STACK (qr->mm, clause_stack);
}

/* --------------- end: DEMO CODE (DATA STRUCTURES) --------------- */


int
main (int argc, char **argv)
{
  double start_time = time_stamp ();
  int result = 0;
  /* Initialize QBCEPrepro object. */
  QBCEPrepro qr;
  memset (&qr, 0, sizeof (QBCEPrepro));
  qr.start_time = start_time;
  /* Initialize simple memory manager. */
  MemMan *mm = mm_create ();
  qr.mm = mm;
  set_default_options (&qr);

  parse_cmd_line_options (&qr, argc, argv);

  set_signal_handlers ();

  if (qr.options.print_usage)
    {
      print_usage ();
      cleanup (&qr);
      return result;
    }

  if (qr.options.max_time)
    {
      fprintf (stderr, "Setting run time limit of %d seconds\n",
               qr.options.max_time);
      alarm (qr.options.max_time);
    }

  /* Parse QDIMACS formula and simplify, if appropriate command line options
     are given. */
  parse (&qr, qr.options.in);
  ABORT_APP (qr.declared_num_clauses > qr.cur_clause_id,
                   "declared number of clauses exceeds actual number of clauses!");
  
  // Function 'demo' illustrates the use of data structures
  //  demo(&qr);

  find_and_mark_blocked_clauses (&qr);

  /* Print formula to stdout. */
  if (qr.options.print_formula)
    print_formula (&qr, stdout);

  if (qr.options.verbosity >= 1)
    {
      /* Print statistics. */
      fprintf (stderr, "\nDONE, printing statistics:\n");
      if (!qr.options.max_time)
        fprintf (stderr, "  time limit: not set\n");
      else
        fprintf (stderr, "  time limit: %d\n", qr.options.max_time);
      fprintf (stderr, "  simplification enabled: %s\n",
               qr.options.simplify ? "yes" : "no");
      fprintf (stderr, "  printing formula: %s\n",
               qr.options.print_formula ? "yes" : "no");
      fprintf (stderr, "  QBCE: %d blocked clauses of total %d clauses ( %f %% of initial CNF)\n",
               qr.cnt_blocked_clauses, qr.declared_num_clauses, qr.declared_num_clauses ? 
               ((qr.cnt_blocked_clauses / (float)qr.declared_num_clauses) * 100) : 0);
      fprintf (stderr, "  run time: %f\n", time_stamp () - qr.start_time);
    }

  /* Clean up, free memory and exit. */
  cleanup (&qr);
  mm_delete (qr.mm);

  return result;
}
