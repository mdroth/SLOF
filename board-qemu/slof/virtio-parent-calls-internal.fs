\ ****************************************************************************/
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

\ get-node CONSTANT my-phandle

\ s" dma-function.fs" included

0 VALUE dma-debug?

: marker-virtio-parent-calls
   1 drop
;

\ DMA memory allocation functions
: dma-alloc ( size -- virt )
   \ cr cr ." VIRTIO-DMA-ALLOC MARKER A0, path, stack+phandle: " pwd ." , " .s cr
   dma-debug? IF cr ." VIRTIO-DMA-ALLOC MARKER A0" cr THEN
   s" dma-alloc" $call-parent
;

: dma-alloc2 ( size -- virt )
   \ cr cr ." DMA-ALLOC2 MARKER A0, path, stack+phandle: " pwd ." , " .s cr
   s" dma-alloc" $call-parent
;

: dma-free ( virt size -- )
   \ cr cr ." DMA-FREE MARKER A0, path, stack+phandle: " pwd ." , " .s cr
   s" dma-free" $call-parent
;

: dma-map-in ( virt size cacheable? -- devaddr )
   \ cr cr ." DMA-MAP-IN MARKER A0, path, stack+phandle: " pwd ." , " .s cr
   s" dma-map-in" $call-parent
;

: dma-map-out ( virt devaddr size -- )
   \ cr cr ." DMA-MAP-OUT MARKER A0, path, stack+phandle: " pwd ." , " .s cr
   s" dma-map-out" $call-parent
;
