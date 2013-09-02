/*
 * Cogl
 *
 * An object oriented GL/GLES Abstraction/Utility Layer
 *
 * Copyright (C) 2010 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_INDICES_H__
#define __COGL_INDICES_H__

/* We forward declare the CoglIndices type here to avoid some circular
 * dependency issues with the following headers.
 */
typedef struct _CoglIndices CoglIndices;

#include <cogl/cogl-index-buffer.h>

#ifdef COGL_HAS_GTYPE_SUPPORT
#include <glib-object.h>
#endif

COGL_BEGIN_DECLS

/**
 * SECTION:cogl-indices
 * @short_description: Describe vertex indices stored in a #CoglIndexBuffer.
 *
 * Indices allow you to avoid duplicating vertices in your vertex data
 * by virtualizing your data and instead providing a sequence of index
 * values that tell the GPU which data should be used for each vertex.
 *
 * If the GPU is given a sequence of indices it doesn't simply walk
 * through each vertex of your data in order it will instead walk
 * through the indices which can provide random access to the
 * underlying data.
 *
 * Since it's very common to have duplicate vertices when describing a
 * shape as a list of triangles it can often be a significant space
 * saving to describe geometry using indices. Reducing the size of
 * your models can make it cheaper to map them into the GPU by
 * reducing the demand on memory bandwidth and may help to make better
 * use of your GPUs internal vertex caching.
 *
 * For example, to describe a quadrilateral as 2 triangles for the GPU
 * you could either provide data with 6 vertices or instead with
 * indices you can provide vertex data for just 4 vertices and an
 * index buffer that specfies the 6 vertices by indexing the shared
 * vertices multiple times.
 *
 * |[
 *   CoglVertex2f quad_vertices[] = {
 *     {x0, y0}, //0 = top left
 *     {x1, y1}, //1 = bottom left
 *     {x2, y2}, //2 = bottom right
 *     {x3, y3}, //3 = top right
 *   };
 *   //tell the gpu how to interpret the quad as 2 triangles...
 *   unsigned char indices[] = {0, 1, 2, 0, 2, 3};
 * ]|
 *
 * Even in the above illustration we see a saving of 10bytes for one
 * quad compared to having data for 6 vertices and no indices but if
 * you need to draw 100s or 1000s of quads then its really quite
 * significant.
 *
 * Something else to consider is that often indices can be defined
 * once and remain static while the vertex data may change for
 * animations perhaps. That means you may be able to ignore the
 * negligable cost of mapping your indices into the GPU if they don't
 * ever change.
 *
 * The above illustration is actually a good example of static indices
 * because it's really common that developers have quad mesh data that
 * they need to display and we know exactly what that indices array
 * needs to look like depending on the number of quads that need to be
 * drawn. It doesn't matter how the quads might be animated and
 * changed the indices will remain the same. Cogl even has a utility
 * (cogl_get_rectangle_indices()) to get access to re-useable indices
 * for drawing quads as above.
 */

#ifdef COGL_HAS_GTYPE_SUPPORT
/**
 * cogl_indices_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
GType cogl_indices_get_gtype (void);
#endif

CoglIndices *
cogl_indices_new (CoglContext *context,
                  CoglIndicesType type,
                  const void *indices_data,
                  int n_indices);

CoglIndices *
cogl_indices_new_for_buffer (CoglIndicesType type,
                             CoglIndexBuffer *buffer,
                             size_t offset);

CoglIndexBuffer *
cogl_indices_get_buffer (CoglIndices *indices);

CoglIndicesType
cogl_indices_get_type (CoglIndices *indices);

size_t
cogl_indices_get_offset (CoglIndices *indices);

void
cogl_indices_set_offset (CoglIndices *indices,
                         size_t offset);

CoglIndices *
cogl_get_rectangle_indices (CoglContext *context, int n_rectangles);

/**
 * cogl_is_indices:
 * @object: A #CoglObject pointer
 *
 * Gets whether the given object references a #CoglIndices.
 *
 * Return value: %TRUE if the object references a #CoglIndices
 *   and %FALSE otherwise.
 * Since: 1.10
 * Stability: unstable
 */
CoglBool
cogl_is_indices (void *object);

COGL_END_DECLS

#endif /* __COGL_INDICES_H__ */

