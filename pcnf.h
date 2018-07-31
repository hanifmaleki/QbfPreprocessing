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

#ifndef PCNF_H_INCLUDED
#define PCNF_H_INCLUDED

#include "stack.h"

/* -------- START: PCNF data structures -------- */

/* Generic macros to link and unlink elements from a doubly linked list.  */
#define LINK_LAST(anchor,element,link)		\
  do {						\
    assert (!(element)->link.prev);		\
    assert (!(element)->link.next);		\
    if ((anchor).last)				\
      {						\
        assert (!(anchor).last->link.next);	\
        assert ((anchor).first);                \
        assert (!(anchor).first->link.prev);	\
        (anchor).last->link.next = (element);	\
      }						\
    else                                        \
      {						\
        assert (!(anchor).first);		\
        (anchor).first = (element);		\
      }						\
    (element)->link.prev = (anchor).last;	\
    (anchor).last = (element);			\
    (anchor).cnt++;				\
  } while (0)

#define LINK_FIRST(anchor,element,link)		\
  do {						\
    assert (!(element)->link.prev);		\
    assert (!(element)->link.next);		\
    (element)->link.next = (anchor).first;	\
    if ((anchor).first)				\
      {						\
        assert ((anchor).last);			\
        (anchor).first->link.prev = (element);	\
      }						\
    else					\
      {						\
        assert (!(anchor).last);		\
        (anchor).last = (element);		\
      }						\
    (anchor).first = (element);			\
    (anchor).cnt++;				\
  } while (0)

#define UNLINK(anchor,element,link)				\
  do {								\
    assert ((anchor).cnt);					\
    if ((element)->link.prev)					\
      {								\
        assert ((anchor).first);                                \
        assert ((anchor).last);					\
        assert ((element)->link.prev->link.next == (element));	\
        (element)->link.prev->link.next = (element)->link.next; \
      }								\
    else                                                        \
      {								\
        assert ((anchor).first == (element));			\
        (anchor).first = (element)->link.next;			\
      }								\
    if ((element)->link.next)					\
      {								\
        assert ((anchor).first);                                \
        assert ((anchor).last);					\
        assert ((element)->link.next->link.prev == (element));	\
        (element)->link.next->link.prev = (element)->link.prev; \
      }								\
    else                                                        \
      {								\
        assert ((anchor).last == (element));			\
        (anchor).last = (element)->link.prev;			\
      }								\
    (element)->link.prev = (element)->link.next = 0;		\
    (anchor).cnt--;						\
  } while (0)

typedef struct PCNF PCNF;
typedef struct Scope Scope;
typedef struct Var Var;
typedef struct Clause Clause;

typedef int LitID;
typedef unsigned int VarID;
typedef unsigned int ClauseID;
typedef unsigned int Nesting;

enum QuantifierType
{
  QTYPE_EXISTS = -1,
  QTYPE_UNDEF = 0,
  QTYPE_FORALL = 1
};

typedef enum QuantifierType QuantifierType;

#define DECLARE_DLIST(name, type)					\
  struct name ## List {type * first; type * last; unsigned int cnt;};	\
  typedef struct name ## List name ## List				\

#define DECLARE_DLINK(name, type)			  \
  struct name ## Link {type * prev; type * next;};        \
  typedef struct name ## Link name ## Link                \

/* Declare lists and stacks of Scopes, Clauses, etc. */
DECLARE_DLINK (Scope, Scope);
DECLARE_DLIST (Scope, Scope);
DECLARE_DLINK (Clause, Clause);
DECLARE_DLIST (Clause, Clause);

DECLARE_STACK (VarID, VarID);
DECLARE_STACK (LitID, LitID);
DECLARE_STACK (ClausePtr, Clause *);
DECLARE_STACK (VarPtr, Var *);

/* PCNF object, defined by list of scopes (quantifier prefix), array of
   variable objects (variable is indexed by its QDIMACS ID), and doubly linked
   list of clauses. */
struct PCNF
{
  ScopeList scopes;
  /* Size of var-table. */
  VarID size_vars;
  /* Table of variable objects indexed by unsigned integer ID. */
  Var *vars;
  ClauseList clauses;
};

/* Scope object (quantifier block in the quantifier prefix). */
struct Scope
{
  QuantifierType type;
  /* Scopes have nesting level, starting at 0, increases by one from left to
     right. */
  unsigned int nesting;
  /* IDs of variables in a scope are kept on a stack. */
  VarIDStack vars;
  /* Scopes appear in a doubly linked list. */
  ScopeLink link;
};

/* Variable object. */
struct Var
{
  /* ID of a variable is used as index to access the array of variable objects. */
  VarID id;
  /* Two multi-purpose marks. */
  unsigned int mark0:1;
  unsigned int mark1:1;

  /* Stacks with pointers to clauses containing positive and negative literals
     of the variable. */
  ClausePtrStack neg_occ_clauses;
  ClausePtrStack pos_occ_clauses;

  /* Pointer to scope of variable. */
  Scope *scope;
};

/* Clause object. */
struct Clause
{
  /* Clauses get a unique ID. This is mainly for debugging. */
  ClauseID id;
  /* Number of literals in a clause, i.e. its size. */
  unsigned int num_lits;

  /* Mark indicating that clause is blocked and hence redundant. */
  unsigned int blocked:1;
  /* Multi-purpose mark. */
  unsigned int mark:1;

  /* All  clauses are kept in a doubly linked list. */
  ClauseLink link;

  /* Flexible literal array. DO NOT ADD MEMBERS AFTER 'lits[]'! (violation of
     C99 standard). */
  LitID lits[];
};

/* Some helper macros. */

/* Check if literal is positive or negative. */
#define LIT_NEG(lit) ((lit) < 0)
#define LIT_POS(lit) (!LIT_NEG((lit)))

/* Convert literal to variable ID or to pointer to variable object. */
#define LIT2VARID(lit) ((lit) < 0 ? -(lit) : (lit))
#define LIT2VARPTR(vars, lit) ((vars) + LIT2VARID(lit))
#define LIT2VAR(vars, lit) ((vars)[LIT2VARID(lit)])

/* Convert variable ID to pointer to variable object. */
#define VARID2VARPTR(vars, id) ((vars) + (id))

/* Check if scope is existential or universal. */
#define SCOPE_EXISTS(s) ((s)->type == QTYPE_EXISTS)
#define SCOPE_FORALL(s) ((s)->type == QTYPE_FORALL)

/* Mark/unmark variable by (re-)setting its positive or negative mark. */
#define VAR_POS_MARK(v) ((v)->mark0 = 1)
#define VAR_NEG_MARK(v) ((v)->mark1 = 1)
#define VAR_UNMARK(v) ((v)->mark0 = (v)->mark1 = 0)
#define VAR_POS_MARKED(v) ((v)->mark0)
#define VAR_NEG_MARKED(v) ((v)->mark1)
#define VAR_MARKED(v) ((v)->mark0 || (v)->mark1)

/* -------- END: PCNF data structures -------- */

#endif
