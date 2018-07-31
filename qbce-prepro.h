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

#ifndef QBCE_PREPRO_H_INCLUDED
#define QBCE_PREPRO_H_INCLUDED

#include "pcnf.h"

/* QBCEPrepro object. This is used by the main application. */
struct QBCEPrepro
{
  /* PCNF object representing the parsed formula. */
  PCNF pcnf;
  /* Simple memory manager. */
  MemMan *mm;
  /* Declared number of clauses in QDIMACS file. */
  unsigned int declared_num_clauses;
  /* Number of blocked clauses. */
  unsigned int cnt_blocked_clauses;
  /* Stack of literals or variable IDs read during parsing. */
  LitIDStack parsed_literals;
  /* Pointer to most recently opened scope during parsing. */
  Scope *opened_scope;
  /* Every clause gets a unique ID (for debugging purposes). */
  ClauseID cur_clause_id;
  /* Start time of program. */
  double start_time;

  /* Options to be set via command line. */
  struct
  {
    char *in_filename;
    FILE *in;
    unsigned int max_time;
    unsigned int verbosity;
    unsigned int print_usage;
    unsigned int simplify;
    unsigned int print_formula;
  } options;
};

typedef struct QBCEPrepro QBCEPrepro;

#endif
