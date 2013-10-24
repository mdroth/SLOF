\ *****************************************************************************
\ * Copyright (c) 2004, 2008 IBM Corporation
\ * All rights reserved.
\ * This program and the accompanying materials
\ * are made available under the terms of the BSD License
\ * which accompanies this distribution, and is available at
\ * http://www.opensource.org/licenses/bsd-license.php
\ *
\ * Contributors:
\ *     IBM Corporation - initial implementation
\ ****************************************************************************/

#include <claim.fs>
\ Memory "heap" (de-)allocation.

\ Keep a linked list of free blocks per power-of-two size.
\ Never coalesce entries when freed; split blocks when needed while allocating.

\ 3f CONSTANT (max-heads#)
heap-end heap-start - log2 1+ CONSTANT (max-heads#)

CREATE heads (max-heads#) cells allot
heads (max-heads#) cells erase


: size>head  ( size -- headptr )  log2 3 max cells heads + ;


\ Allocate a memory block
: alloc-mem  ( len -- a-addr )
   ." alloc-mem IN " dup .  ." heap " heap-start . ." .." heap-end . ."  maxh#=" (max-heads#) . cr
   d# __LINE__ .d ." : " .s cr
   dup 0= IF
        ." alloc-mem RET1 " dup . cr
        EXIT
   THEN
   d# __LINE__ .d ." : " .s cr
   1 over log2 3 max                   ( len 1 log_len )
   dup (max-heads#) >= IF
        cr ." Out of internal memory." cr
        3drop 0
        ." alloc-mem RET2 " dup . cr
        EXIT
   THEN

   d# __LINE__ .d ." : " .s cr
   lshift >r                           ( len  R: 1<<log_len )
   size>head
   d# __LINE__ .d ." : " .s cr
   dup
   @
   d# __LINE__ .d ." : " .s cr
   IF
      dup @
      dup >r @
      swap !
      r> r>
      drop
   d# __LINE__ .d ." : " .s cr
      ." alloc-mem RET3 " dup . cr
      EXIT
   THEN                                ( headptr  R: 1<<log_len)

   r@
   d# __LINE__ .d ." : " .s cr
   2*
   ." (rec) "
   d# __LINE__ .d ." : " .s cr
   recurse
   dup                   ( headptr a-addr2 a-addr2  R: 1<<log_len)
   d# __LINE__ .d ." : " .s cr
   dup 0= IF
        r>
        2drop
        2drop
        0
   d# __LINE__ .d ." : " .s cr
        ." alloc-mem RET4 " dup . cr
        EXIT
   THEN
   r>
   d# __LINE__ .d ." : " .s cr
   +
   >r
   0
   d# __LINE__ .d ." : " .s cr
   over !
   swap !
   r>
   d# __LINE__ .d ." : " .s cr
   ." alloc-mem RET5 " dup . cr
;


\ Free a memory block

: free-mem  ( a-addr len -- )
   dup 0= IF 2drop EXIT THEN size>head 2dup @ swap ! !
;


: #links  ( a -- n )
   @ 0 BEGIN over WHILE 1+ swap @ swap REPEAT nip
;


: .free  ( -- )
   0 (max-heads#) 0 DO
      heads i cells + #links dup IF
         cr dup . ." * " 1 i lshift dup . ." = " * dup .
      THEN
      +
   LOOP
   cr ." Total " .
;


\ Start with just one free block.
heap-start heap-end heap-start - free-mem


\ : free-mem  ( a-addr len -- ) 2drop ;

\ Uncomment the following line for debugging:
\ #include <alloc-mem-debug.fs>

