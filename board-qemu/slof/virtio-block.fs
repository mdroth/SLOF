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

\ ." Populating " pwd cr

s" block" device-type

cr ." virtio-block LOADED" cr
FALSE VALUE initialized?
FALSE VALUE vpc-loaded?

\ Required interface for deblocker

200 VALUE block-size
8000 CONSTANT max-transfer 

INSTANCE VARIABLE deblocker

virtio-setup-vd VALUE virtiodev

\ Quiesce the virtqueue of this device so that no more background
\ transactions can be pending.
: shutdown  ( -- )
   ." virtio-blk shutdown" cr
    initialized? IF
        my-phandle node>path open-dev ?dup IF
            virtiodev virtio-blk-shutdown
            close-dev
        THEN
        FALSE to initialized?
    THEN
;

\ Basic device initialization - which has only to be done once
: init  ( -- )
   ." virtio-blk init" cr
   virtiodev virtio-blk-init to block-size
   TRUE to initialized?
   ['] shutdown add-quiesce-xt
;

\ Read multiple blocks - called by deblocker package
: read-blocks  ( addr block# #blocks -- #read )
   virtiodev virtio-blk-read
;

: write-blocks  ( addr block# #blocks -- #written )
    \ Do not allow writes to the partition table (GPT is in first 34 sectors)
    over 22 < IF
        ." virtio-blk ERROR: Write access to partition table is not allowed." cr
        3drop 0 EXIT
    THEN
    virtiodev virtio-blk-write
;

\ Standard node "open" function
: open  ( -- okay? )
   ." virtio-blk open marker 0" cr
   open 0= IF false EXIT THEN
   dup initialized? 0= AND IF
      ." virtio-blk open marker 0b" cr
      init
   ELSE
      ." virtio-blk open marker 0c (forcing re-init)" cr
      init
   THEN
   ." virtio-blk open marker 1" cr
   0 0 s" deblocker" $open-package dup deblocker ! dup IF
      ." virtio-blk open marker 2" cr
      s" disk-label" find-package IF
         my-args rot interpose
         ." virtio-blk open marker 3" cr
      THEN
   THEN
   ." virtio-blk open marker 4" cr
   0<>
;

\ Standard node "close" function
: close  ( -- )
   ." virtio-blk close marker 0" cr
   deblocker @ close-package
   ." virtio-blk close marker 1" cr
   close
   ." virtio-blk close marker 2" cr
;

\ Standard node "seek" function
: seek  ( pos.lo pos.hi -- status )
   s" seek" deblocker @ $call-method
;

\ Standard node "read" function
: read  ( addr len -- actual )
   s" read" deblocker @ $call-method
;

: write ( addr len -- actual )
    s" write" deblocker @ $call-method
;

\ Set disk alias if none is set yet
: (set-alias)
   s" disk" get-next-alias ?dup IF
      get-node node>path set-alias
   THEN
;
(set-alias)
