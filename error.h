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

#ifndef ERROR_H_INCLUDED
#define ERROR_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>

/* Macro to print message and abort. */
#define ABORT_APP(cond,msg)                                       \
  do {                                                                  \
    if (cond)                                                           \
      {                                                                 \
        fprintf (stderr, "[QBCE-PREPRO] %s at line %d: %s\n", __func__,   \
                 __LINE__, msg);                                        \
        fflush (stderr);                                                \
        abort();                                                        \
      }                                                                 \
  } while (0)

#endif
