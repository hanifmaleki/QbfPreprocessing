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

void printVariablesOfClause(const Clause *clause);

VarID *getIntersectionOf(ClausePtrStack *stack, VarID varID, int *size);


static int isVariableBlockingInClause(QBCEPrepro *qr, Clause *pClause, int i, int isPositive);

static int isVariableInCommon(QBCEPrepro *qr, Clause *pClause, Clause *clause, int id);

int findAndMarkBlockedClausesForMarkedVariables(QBCEPrepro *qr);

int considerAndMark(QBCEPrepro * qr, VarID id, int isPosetive);



/* Print error message. */
static void
print_abort_err(char *msg, ...) {
    va_list list;
    assert (msg != NULL);
    fprintf(stderr, "qbce-prepro: ");
    va_start (list, msg);
    vfprintf(stderr, msg, list);
    va_end (list);
    fflush(stderr);
    abort();
}

/* Print array 'lits' of literals of length 'num'. If 'print_info' is
non-zero, then print info about the scope of each literal in the array. */
static void
print_lits(QBCEPrepro *qr, FILE *out, LitID *lits, unsigned int num,
           const int print_info) {
    Var *vars = qr->pcnf.vars;
    LitID *p, *e;
    for (p = lits, e = p + num; p < e; p++) {
        LitID lit = *p;
        Var *var = LIT2VARPTR (vars, lit);
        if (print_info)
            fprintf(out, "%c(%d)%d ",
                    SCOPE_FORALL (var->scope) ? 'A' : 'E',
                    var->scope->nesting, *p);
        else
            fprintf(out, "%d ", *p);
    }
    fprintf(out, "0\n");
}

/* -------- END: Application defintions and functions -------- */

/* -------------------- START: COMMAND LINE PARSING -------------------- */
static void
set_default_options(QBCEPrepro *qr) {
    qr->options.in_filename = 0;
    qr->options.in = stdin;
    qr->options.print_usage = 0;
}

static int
isnumstr(char *str) {
    /* Empty string is not considered as number-string. */
    if (!*str)
        return 0;
    char *p;
    for (p = str; *p; p++) {
        if (!isdigit (*p))
            return 0;
    }
    return 1;
}

/* Parse command line arguments to set options accordingly. Run the program
   with '-h' or '--help' to print usage information. */
static void
parse_cmd_line_options(QBCEPrepro *qr, int argc, char **argv) {
    char *result;
    int opt_cnt;
    for (opt_cnt = 1; opt_cnt < argc; opt_cnt++) {
        char *opt_str = argv[opt_cnt];

        if (!strcmp(opt_str, "-h") || !strcmp(opt_str, "--help")) {
            qr->options.print_usage = 1;
        } else if (!strcmp(opt_str, "--simplify")) {
            qr->options.simplify = 1;
        } else if (!strncmp(opt_str, "--print-formula", strlen("--print-formula"))) {
            qr->options.print_formula = 1;
        } else if (!strcmp(opt_str, "-v")) {
            qr->options.verbosity++;
        } else if (isnumstr(opt_str)) {
            qr->options.max_time = atoi(opt_str);
            if (qr->options.max_time == 0) {
                result = "Expecting non-zero value for max-time";
                print_abort_err("%s!\n\n", result);
            }
        } else if (!qr->options.in_filename) {
            qr->options.in_filename = opt_str;
            /* Check input file. */
            DIR *dir;
            if ((dir = opendir(qr->options.in_filename)) != NULL) {
                closedir(dir);
                print_abort_err("input file '%s' is a directory!\n\n",
                                qr->options.in_filename);
            }
            FILE *input_file = fopen(qr->options.in_filename, "r");
            if (!input_file) {
                print_abort_err("could not open input file '%s'!\n\n",
                                qr->options.in_filename);
            } else
                qr->options.in = input_file;
        } else {
            print_abort_err("unknown option '%s'!\n\n", opt_str);
        }
    }
}

/* -------------------- END: COMMAND LINE PARSING -------------------- */

/* -------------------- START: HELPER FUNCTIONS -------------------- */

/* Set signal handler. */
static void
sig_handler(int sig) {
    fprintf(stderr, "\n\n SIG RECEIVED\n\n");
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Set signal handler. */
static void
sigalrm_handler(int sig) {
    fprintf(stderr, "\n\n SIGALRM RECEIVED\n\n");
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Set signal handler. */
static void
set_signal_handlers(void) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGALRM, sigalrm_handler);
    signal(SIGXCPU, sigalrm_handler);
}

static void
print_usage() {
    fprintf(stdout, USAGE);
}

/* Free allocated memory. */
static void
cleanup(QBCEPrepro *qr) {
    if (qr->options.in_filename)
        fclose(qr->options.in);

    DELETE_STACK (qr->mm, qr->parsed_literals);

    Var *vp, *ve;
    for (vp = qr->pcnf.vars, ve = vp + qr->pcnf.size_vars; vp < ve; vp++) {
        DELETE_STACK (qr->mm, vp->pos_occ_clauses);
        DELETE_STACK (qr->mm, vp->neg_occ_clauses);
    }
    mm_free(qr->mm, qr->pcnf.vars, qr->pcnf.size_vars * sizeof(Var));

    Scope *s, *sn;
    for (s = qr->pcnf.scopes.first; s; s = sn) {
        sn = s->link.next;
        DELETE_STACK (qr->mm, s->vars);
        mm_free(qr->mm, s, sizeof(Scope));
    }

    Clause *c, *cn;
    for (c = qr->pcnf.clauses.first; c; c = cn) {
        cn = c->link.next;
        mm_free(qr->mm, c, sizeof(Clause) + c->num_lits * sizeof(LitID));
    }
}

/* Print (simplified) formula to file 'out'. If '--simplify'
   is specified then blocked clauses will not be printed. */
static void
print_formula(QBCEPrepro *qr, FILE *out) {
    assert (qr->pcnf.clauses.cnt >= qr->cnt_blocked_clauses);
    /* Print preamble. */
    assert (qr->pcnf.size_vars > 0);
    fprintf(out, "p cnf %d %d\n", (qr->pcnf.size_vars - 1),
            (qr->pcnf.clauses.cnt - qr->cnt_blocked_clauses));

    /* Print prefix. */
    Scope *s;
    for (s = qr->pcnf.scopes.first; s; s = s->link.next) {
        fprintf(out, "%c ", SCOPE_FORALL (s) ? 'a' : 'e');
        print_lits(qr, out, (LitID *) s->vars.start,
                   COUNT_STACK (s->vars), 0);
    }

    /* Print clauses. */
    Clause *c;
    for (c = qr->pcnf.clauses.first; c; c = c->link.next)
        if (!c->blocked)
            print_lits(qr, out, c->lits, c->num_lits, 0);
}

/* Get process time. Can be used for performance statistics. */
static double
time_stamp() {
    double result = 0;
    struct rusage usage;

    if (!getrusage(RUSAGE_SELF, &usage)) {
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
find_and_mark_blocked_clauses(QBCEPrepro *qr) {
    Var *vars = qr->pcnf.vars;
    VarID sizeVar = qr->pcnf.size_vars;
    /*
     * For each variable of PCNF
     */
    for(int i=0; i<sizeVar;i++){
        /*
         * The blocking literals are being checked only for variables having existential scopes
         */
        if ((vars[i].scope!=0)&&(SCOPE_EXISTS(vars[i].scope))) {
            /*
             * To check if the positive literal is blocking one
             */
            vars[i].mark0 = 1;
            //VAR_POS_MARK(vars[i]);

            /*
             * To check if the negative literal is blocking one
             */
            vars[i].mark1 = 1;
            //VAR_NEG_MARK(vars[i]);
        }
    }
    
    /*
     * The iterator variable, specifying the number of iterations
     */
    int iterator = 1;
    int blockedCount;

    /*
     * Repeat, until there is no blocking literal in the formula
     */
    do {
        blockedCount = findAndMarkBlockedClausesForMarkedVariables(qr);
        /*
         * Uncoment to print number of blocked clauses in each iteration
         */
        //printf("There exist %d blocked clauses in iteration %d\n", blockedCount, iterator);
        iterator++;
    } while (blockedCount > 0);

}


/*
 * This method scans through marked variables and for each one, call a method specifying
 * whether it is a blocking literal in any clause.
 * The method returns the number of blocked clauses.
 */
int findAndMarkBlockedClausesForMarkedVariables(QBCEPrepro *qr) {
    /*
     * Getting variable count and the pointer to the first item in the list
     */
    VarID varsCount = qr->pcnf.size_vars;
    Var *var = qr->pcnf.vars;

    /*
     * This variable maintain of blocked clause
     */
    int numberOfBlockedClauses = 0;

    /*
     * For each variable
     */
    for(int j=0; j< varsCount; j++){
        /*
         * This variable maintain number of blocking clauses considering each literal
         */
        int mark;

        /*
         * If variable var[j].mark1 is true, the negative literal is considered
         */
        if(var[j].mark1) {
        //if(VAR_NEG_MARK(var[j]) {
            mark = considerAndMark(qr, var[j].id, 0);
            numberOfBlockedClauses += mark;
        }

        /*
         * If variable var[j].mark0 is true, the positive literal is considered
         */
        if(var[j].mark0) {
        //if(VAR_POS_MARKED(var[j])){
            mark = considerAndMark(qr, var[j].id, 1);
            numberOfBlockedClauses += mark;
        }
    }

    return numberOfBlockedClauses;
}


/*
 * The method find all blocked clause, with respect to the variable @id
 * Parameter @idPositive specify the sign of the literal
 * It returns the number of blocked clauses
 * It also mark the variables which should be considered in the next iterations
 */
int considerAndMark(QBCEPrepro * qr, VarID id, int isPosetive) {
    /*
     * Getting the array of variables
     */
    Var *vars = qr->pcnf.vars;

    /*
     * For positive literal, it is intended to search in the stack of negative non-blocked clauses
     * and vice versa.
     */
    ClausePtrStack stack ;
    if(isPosetive)
        stack = vars[id].neg_occ_clauses;
    else
        stack = vars[id].pos_occ_clauses;

    /*
     * The total number of blocking clauses
     */
    int numberOfBlockedClauses = 0;
    /*
     * Getting the count of the stack, the top pointer
     */
    int count = COUNT_STACK((stack));
    Clause **pClause = stack.start;

    /*
     * Iterating on the clauses with the opposite sign of the input literal
     */
    for (int i = 0; i < count; i++) {
        /*
         * Getting the clause
         */
        Clause *clause = pClause[i];

        /*
         * Check the clause to be not blocked
         */
        if (clause->blocked)
            continue;

        /*
         * Call @isVariableBlockingInClause method to check whether the specified literal
         * in the specified clause is blocking
         */
        if(isVariableBlockingInClause(qr, clause, id, isPosetive)){
            /*
             * Increment the number of blocked clauses
             */
            numberOfBlockedClauses += 1;

            /*
             * Blocking the clause
             */
            clause->blocked=1;

            /*
             * Updating the total number of blocked clauses in the pcnf
             */
            qr->cnt_blocked_clauses++;

            /*
             * All of the variables which are in the clause should be considered with
             * opposite sign in the next iteration, in order to investigate whether a
             * new blocking clause exist after blocking the current clause
             */
            unsigned int varCount = clause->num_lits;
            LitID * litID = clause->lits;

            /*
             * Iterating over all literals of the blocking clause
             */
            for(int j=0; j<varCount;j++){
                LitID var = litID[j];
                if ((vars[abs(var)].scope!=0)&&(SCOPE_EXISTS(vars[abs(var)].scope))) {

                    /*
                     * For positive literals mark0 is set to 1
                     * For negative literals mark1 is set to 1
                     */
                    if (var > 0)
                        qr->pcnf.vars[abs(var)].mark0 = 1;
                        //VAR_POS_MARK(vars[abs(var)]);
                    else
                        qr->pcnf.vars[abs(var)].mark1 = 1;
                        //VAR_NEG_MARK(vars[abs(var)]);
                }
            }
        }
    }

    /*
     * To unmark the investigated literal and exclude it from next iterations
     * (Unless it is marked by the next variables)
     */
    if(isPosetive)
        vars[id].mark0=0;
    else
        vars[id].mark1=0;

    return numberOfBlockedClauses;

}



/*
 * This method checks whether a specific variable @id with sign @isPositive in clause @pClause is a bloking literal.
 */
static int isVariableBlockingInClause(QBCEPrepro *qr, Clause *pClause, int varId, int isPositive) {
    /*
     * Fetch appropriate stack based on the sign of the literal
     */
    ClausePtrStack stack ;
    if(isPositive)
        stack = qr->pcnf.vars[varId].pos_occ_clauses;
    else
        stack = qr->pcnf.vars[varId].neg_occ_clauses;

    /*
     * Iterating over the clauses of the stack
     */
    size_t count = COUNT_STACK(stack);
    Clause **neg = stack.start;
    for (int i = 0; i < count; i++) {
        Clause *clause = neg[i];
        /*
         * Ignoring blocked clauses
         */
        if (clause->blocked)
            continue;

        /*
         * The method @isVariableInCommon check whther tow clauses has common variable
         * considering nesting level restriction.
         */
        if (!isVariableInCommon(qr, pClause, clause, varId)) {
            /*
             * As soon as finding a clause with no variable in common, return negative answer
             */
            return 0;
        }
    }
    /*
     * If all clauses has common variable with opposite sign with respect to the level restriction,
     * it return @True
     */
    return 1;
}


/*
 * This method check whether two clause has common variables @x such that
 * - The sign of occurrences are opposite in two clauses
 * - The level of @x is not greater than the level of variable @id
 */
static int isVariableInCommon(QBCEPrepro *qr, Clause *pClause, Clause *clause, int id) {
    /*
     * getting the size and starting pointer of two clauses
     */
    unsigned int size1 = pClause->num_lits;
    int *vars1 = pClause->lits;
    unsigned int size2 = clause->num_lits;
    //qdpll_get_nesting_of_var
    /*
     * Fetching the level of variable @id
     */
    unsigned int nesting1 = qr->pcnf.vars[id].scope->nesting;
    LitID *vars2 = clause->lits;

    /*
     * Iterating over variables of first clause
     */
    for (int i = 0; i < size1; i++) {
        int var1 = vars1[i];

        /*
         * Fetching the level of the considering variable
         */
        unsigned int nesting2 = qr->pcnf.vars[abs(var1)].scope->nesting;
        /*
         * Ignoring the occurrence of same variable
         */
        if(abs(var1)==id)
            continue;

        /*
         * Applying level restriction condition
         */
        if(nesting2>nesting1)
            continue;

        /*
         * Iterating over the variables of the second clause
         */
        for (int j = 0; j < size2; j++) {

            int var2 = vars2[j];

            /*
             * Checking the same variable and opposite sign
             * Returning true answer as soon as finding one
             */
            if (var2 == -1 * var1)
                return 1;
        }
    }

    /*
     * In case of not finding such variable, the negative result is returned
     */
    return 0;
}


/*
 * A helper (currently not used) method for printing the content of a clause
 */
void printVariablesOfClause(const Clause *clause) {
    unsigned int varsCount = clause->num_lits;
    printf("%d:\t", varsCount);
    LitID *id = clause->lits;
    for (int i = 0; i < varsCount; i++)
        printf("%d\t", id[i]);
    printf("\n");
}

/* -------------------- END: QBCE -------------------- */

/* --------------- START: DEMO CODE (DATA STRUCTURES) --------------- */

static void
demo_print_variables_occurrences(QBCEPrepro *qr, Var *var,
                                 ClausePtrStack *clause_stack) {
    fprintf(stderr, "Printing occurrences of variable %u\n", var->id);

    /* Get pointer to stacks of (pointers) to clauses where variable 'var'
       appears positively and negatively, respectively. */
    ClausePtrStack *pos_occs = &(var->pos_occ_clauses);
    ClausePtrStack *neg_occs = &(var->neg_occ_clauses);

    assert (EMPTY_STACK(*clause_stack));

    /* Collect clauses where 'var' appears positively on stack 'clause_stack',
       using flag 'mark' of clause objects to indicate if a clause object has
       been pushed on the stack already (in this example, the use of 'mark' is
       not necessary since variable can have at most one literal in a clause,
       however, we show it for demonstration). */
    Clause **cp, **ce;
    for (cp = pos_occs->start, ce = pos_occs->top; cp < ce; cp++) {
        Clause *c = *cp;
        if (!c->mark) {
            c->mark = 1;
            PUSH_STACK (qr->mm, *clause_stack, c);
        }
    }

    /* Same as above but for clause where 'var' appears negatively. */
    for (cp = neg_occs->start, ce = neg_occs->top; cp < ce; cp++) {
        Clause *c = *cp;
        if (!c->mark) {
            c->mark = 1;
            PUSH_STACK (qr->mm, *clause_stack, c);
        }
    }

    /* Iterate over all clauses collected on 'clause_stack', reset mark and
       print clause. */
    for (cp = clause_stack->start, ce = clause_stack->top; cp < ce; cp++) {
        Clause *c = *cp;
        assert (c->mark);
        c->mark = 0;
        /* Print clause. */
        fprintf(stderr, " Occ: ");
        print_lits(qr, stderr, c->lits, c->num_lits, 0);

        /* Additionally, print all literals in the clause that are smaller than
           'var' with respect to the prefix ordering. */
        fprintf(stderr, "  Literals smaller than %u in prefix ordering: ", var->id);
        LitID *lp, *le;
        for (lp = c->lits, le = lp + c->num_lits; lp < le; lp++) {
            LitID lit = *lp;
            Var *other = LIT2VARPTR (qr->pcnf.vars, lit);
            if (other->scope->nesting < var->scope->nesting)
                fprintf(stderr, "%d ", lit);
        }
        fprintf(stderr, "0\n");
    }

    RESET_STACK (*clause_stack);
    assert (EMPTY_STACK(*clause_stack));
}

/* This function is intended to demonstrate the data structures. It prints all
   clauses where a particular variable appears in. */
static void
demo(QBCEPrepro *qr) {
    /* Declare and initialize stack 'clause_stack' of pointers to clauses. */
    ClausePtrStack clause_stack;
    INIT_STACK (clause_stack);

    /* Iterate over table of variable objects. */
    Var *vp, *ve;
    for (vp = qr->pcnf.vars, ve = vp + qr->pcnf.size_vars; vp < ve; vp++) {
        /* Consider only variable objects which correspond to variables that
           actually appear in the formula. This is done by checking whether the
           variable ID is nonzero. */
        if (vp->id)
            demo_print_variables_occurrences(qr, vp, &clause_stack);
    }

    /* Release all memory of stack 'clause_stack'. */
    DELETE_STACK (qr->mm, clause_stack);
}

/* --------------- end: DEMO CODE (DATA STRUCTURES) --------------- */


int
main(int argc, char **argv) {
    double start_time = time_stamp();
    int result = 0;
    /* Initialize QBCEPrepro object. */
    QBCEPrepro qr;
    memset(&qr, 0, sizeof(QBCEPrepro));
    qr.start_time = start_time;
    /* Initialize simple memory manager. */
    MemMan *mm = mm_create();
    qr.mm = mm;
    set_default_options(&qr);

    parse_cmd_line_options(&qr, argc, argv);

    set_signal_handlers();

    if (qr.options.print_usage) {
        print_usage();
        cleanup(&qr);
        return result;
    }

    if (qr.options.max_time) {
        fprintf(stderr, "Setting run time limit of %d seconds\n",
                qr.options.max_time);
        alarm(qr.options.max_time);
    }

    /* Parse QDIMACS formula and simplify, if appropriate command line options
       are given. */
    parse(&qr, qr.options.in);
    ABORT_APP (qr.declared_num_clauses > qr.cur_clause_id,
               "declared number of clauses exceeds actual number of clauses!");

    // Function 'demo' illustrates the use of data structures
    //  demo(&qr);

    find_and_mark_blocked_clauses(&qr);

    /* Print formula to stdout. */
    if (qr.options.print_formula)
        print_formula(&qr, stdout);

    if (qr.options.verbosity >= 1) {
        /* Print statistics. */
        fprintf(stderr, "\nDONE, printing statistics:\n");
        if (!qr.options.max_time)
            fprintf(stderr, "  time limit: not set\n");
        else
            fprintf(stderr, "  time limit: %d\n", qr.options.max_time);
        fprintf(stderr, "  simplification enabled: %s\n",
                qr.options.simplify ? "yes" : "no");
        fprintf(stderr, "  printing formula: %s\n",
                qr.options.print_formula ? "yes" : "no");
        fprintf(stderr, "  QBCE: %d blocked clauses of total %d clauses ( %f %% of initial CNF)\n",
                qr.cnt_blocked_clauses, qr.declared_num_clauses, qr.declared_num_clauses ?
                                                                 ((qr.cnt_blocked_clauses /
                                                                   (float) qr.declared_num_clauses) * 100) : 0);
        fprintf(stderr, "  run time: %f\n", time_stamp() - qr.start_time);
    }

    /* Clean up, free memory and exit. */
    cleanup(&qr);
    mm_delete(qr.mm);

    return result;
}
