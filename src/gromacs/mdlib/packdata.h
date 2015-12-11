/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014,2015, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */

/* A simple serialization library that supports the offload kernel, where
 * the Intel Xeon Phi is used as a coprocessor.
 */

#include "config.h"

#define PACK_BUFFER_ALIGN 64

typedef struct packet_buffer_struct
{
    void   *p;
    size_t  s;
} packet_buffer;

// Packet-level operations
gmx_offload void packdata(char *packet, packet_buffer *buffers, int num_buffers);

gmx_offload void unpackdata(char *packet, void **buffers,         int num_buffers);

gmx_offload size_t compute_required_size(packet_buffer *buffers,  int num_buffers);

gmx_offload packet_buffer get_buffer(char *packet, int buffer_num);

// Buffer-level operations
gmx_offload
typedef struct packet_iter_struct
{
    char *packet;
    char *ptr;
} packet_iter;

gmx_offload void create_packet_iter(char *packet, packet_iter *iter);

template<typename T> gmx_offload T *value(packet_iter *iter)
{
    return (T *)(iter->packet + (*(size_t *)(iter->ptr + sizeof(size_t))));
}

gmx_offload size_t size(packet_iter *iter);

/* Return pointer to current buffer and advance to next buffer */
template<typename T> gmx_offload T *next(packet_iter *iter)
{
    T *oldval = value<T>(iter);
    iter->ptr += 2*sizeof(size_t);
    return oldval;
}

/* Same as "next" except allocates a new buffer and copies the current buffer's contents to it
 * instead of returning a pointer to the buffer inside the packet. Allocated buffer size is
 * (size * multiplier).
 */
template<typename T>
gmx_offload T *anext(packet_iter *iter, int multiplier)
{
    T *buffer;
    size_t len = size(iter);
    snew_aligned(buffer, len*multiplier, 64);
    memcpy(buffer, next<T>(iter), len);
    return buffer;
}

/* Copy contents of current buffer to an existing buffer and advance to next buffer.
 * Client must make sure destination buffer is large enough.
 */
template<typename T>
gmx_offload void cnext(packet_iter *iter, T *buffer)
{
    size_t bsize = size(iter);
    memcpy(buffer, next<T>(iter), bsize);
}
