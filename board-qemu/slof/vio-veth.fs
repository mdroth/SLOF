\ *****************************************************************************
\ * Copyright (c) 2011 IBM Corporation
\ * All rights reserved.
\ * This program and the accompanying materials
\ * are made available under the terms of the BSD License
\ * which accompanies this distribution, and is available at
\ * http://www.opensource.org/licenses/bsd-license.php
\ *
\ * Contributors:
\ *     IBM Corporation - initial implementation
\ ****************************************************************************/

." Populating " pwd cr

" network" device-type

INSTANCE VARIABLE obp-tftp-package
0 VALUE veth-priv
0 VALUE open-count

: open  ( -- okay? )
   open-count 0= IF
      my-unit 1 rtas-set-tce-bypass
      my-args s" obp-tftp" $open-package obp-tftp-package !
      s" local-mac-address" get-node get-property not
      s" reg" get-node get-property not 3 pick and IF
         >r nip r>
         libveth-open dup not IF ." libveth-open failed" EXIT THEN
         drop TO veth-priv
      THEN
   THEN
   open-count 1 + to open-count
   true
;

: close  ( -- )
   open-count 0> IF
      open-count 1 - dup to open-count
      0= IF
         s" close" obp-tftp-package @ $call-method
         veth-priv libveth-close
         my-unit 0 rtas-set-tce-bypass
      THEN
   THEN
;

: read ( buf len -- actual )
   veth-priv libveth-read
;

: write ( buf len -- actual )
   veth-priv libveth-write
;

: load  ( addr -- len )
    s" load" obp-tftp-package @ $call-method 
;

: ping  ( -- )
    s" ping" obp-tftp-package @ $call-method
;

: setup-alias
    " net" find-alias 0= IF
        " net" get-node node>path set-alias
    ELSE
        drop
    THEN 
;
setup-alias
