
/*--------------------------------------------------------------------*/
/*--- Sets of words, with unique set identifiers.                  ---*/
/*---                                                 tc_wordset.h ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Thrcheck, a Valgrind tool for detecting errors
   in threaded programs.

   Copyright (C) 2007-2007 OpenWorks LLP
       info@open-works.co.uk

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.

   Neither the names of the U.S. Department of Energy nor the
   University of California nor the names of its contributors may be
   used to endorse or promote products derived from this software
   without prior written permission.
*/

#ifndef __TC_WORDSET_H
#define __TC_WORDSET_H

//------------------------------------------------------------------//
//---                          WordSet                           ---//
//---                      Public Interface                      ---//
//------------------------------------------------------------------//

typedef  struct _WordSetU  WordSetU;  /* opaque */

typedef  UInt              WordSet;   /* opaque, small int index */

/* Allocate and initialise a WordSetU */
WordSetU* TC_(newWordSetU) ( void* (*alloc_nofail)( SizeT ),
                             void  (*dealloc)(void*) );

/* Free up the WordSetU. */
void TC_(deleteWordSetU) ( WordSetU* );

/* Get the number of elements in this WordSetU. */
Int TC_(cardinalityWSU) ( WordSetU* );

/* Show performance stats for this WordSetU. */
void TC_(ppWSUstats) ( WordSetU* wsu, HChar* name );


/* Element-level operations on WordSets.  Note that the WordSet
   numbers given out are 0, 1, 2, 3, etc, and as it happens 0 always
   represents the empty set. */

WordSet TC_(emptyWS)        ( WordSetU* );
WordSet TC_(addToWS)        ( WordSetU*, WordSet, Word );
WordSet TC_(delFromWS)      ( WordSetU*, WordSet, Word );
WordSet TC_(unionWS)        ( WordSetU*, WordSet, WordSet );
WordSet TC_(intersectWS)    ( WordSetU*, WordSet, WordSet );
WordSet TC_(minusWS)        ( WordSetU*, WordSet, WordSet );
Bool    TC_(isEmptyWS)      ( WordSetU*, WordSet );
Bool    TC_(isSingletonWS)  ( WordSetU*, WordSet, Word );
Word    TC_(anyElementOfWS) ( WordSetU*, WordSet );
Int     TC_(cardinalityWS)  ( WordSetU*, WordSet );
Bool    TC_(elemWS)         ( WordSetU*, WordSet, Word );
WordSet TC_(doubletonWS)    ( WordSetU*, Word, Word );
WordSet TC_(singletonWS)    ( WordSetU*, Word );
WordSet TC_(isSubsetOf)     ( WordSetU*, WordSet, WordSet );

Bool    TC_(plausibleWS)    ( WordSetU*, WordSet );
Bool    TC_(saneWS_SLOW)    ( WordSetU*, WordSet );

void    TC_(ppWS)           ( WordSetU*, WordSet );
void    TC_(getPayloadWS)   ( /*OUT*/Word** words, /*OUT*/Word* nWords, 
                             WordSetU*, WordSet );


//------------------------------------------------------------------//
//---                        end WordSet                         ---//
//---                      Public Interface                      ---//
//------------------------------------------------------------------//

#endif /* ! __TC_WORDSET_H */

/*--------------------------------------------------------------------*/
/*--- end                                             tc_wordset.h ---*/
/*--------------------------------------------------------------------*/
