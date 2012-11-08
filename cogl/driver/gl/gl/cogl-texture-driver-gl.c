/*
 * Cogl
 *
 * An object oriented GL/GLES Abstraction/Utility Layer
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *
 * Authors:
 *  Matthew Allum  <mallum@openedhand.com>
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cogl-private.h"
#include "cogl-util.h"
#include "cogl-bitmap.h"
#include "cogl-bitmap-private.h"
#include "cogl-texture-private.h"
#include "cogl-pipeline.h"
#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "cogl-primitives.h"
#include "cogl-pipeline-opengl-private.h"
#include "cogl-util-gl-private.h"
#include "cogl-error-private.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef GL_TEXTURE_SWIZZLE_RGBA
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#endif

static GLuint
_cogl_texture_driver_gen (CoglContext *ctx,
                          GLenum gl_target,
                          CoglPixelFormat internal_format)
{
  GLuint tex;

  GE (ctx, glGenTextures (1, &tex));

  _cogl_bind_gl_texture_transient (gl_target, tex, FALSE);

  switch (gl_target)
    {
    case GL_TEXTURE_2D:
    case GL_TEXTURE_3D:
      /* GL_TEXTURE_MAG_FILTER defaults to GL_LINEAR, no need to set it */
      GE( ctx, glTexParameteri (gl_target,
                                GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR) );
      break;

    case GL_TEXTURE_RECTANGLE_ARB:
      /* Texture rectangles already default to GL_LINEAR so nothing
         needs to be done */
      break;

    default:
      g_assert_not_reached();
    }

  /* If the driver doesn't support alpha textures directly then we'll
   * fake them by setting the swizzle parameters */
  if (internal_format == COGL_PIXEL_FORMAT_A_8 &&
      (ctx->private_feature_flags & (COGL_PRIVATE_FEATURE_ALPHA_TEXTURES |
                                     COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE)) ==
      COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE)
    {
      static const GLint red_swizzle[] = { GL_ZERO, GL_ZERO, GL_ZERO, GL_RED };

      GE( ctx, glTexParameteriv (gl_target,
                                 GL_TEXTURE_SWIZZLE_RGBA,
                                 red_swizzle) );
    }

  return tex;
}

/* OpenGL - unlike GLES - can upload a sub region of pixel data from a larger
 * source buffer */
static void
prep_gl_for_pixels_upload_full (CoglContext *ctx,
                                int pixels_rowstride,
                                int image_height,
                                int pixels_src_x,
                                int pixels_src_y,
                                int pixels_bpp)
{
  GE( ctx, glPixelStorei (GL_UNPACK_ROW_LENGTH,
                          pixels_rowstride / pixels_bpp) );

  GE( ctx, glPixelStorei (GL_UNPACK_SKIP_PIXELS, pixels_src_x) );
  GE( ctx, glPixelStorei (GL_UNPACK_SKIP_ROWS, pixels_src_y) );

  if (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_3D))
    GE( ctx, glPixelStorei (GL_UNPACK_IMAGE_HEIGHT, image_height) );

  _cogl_texture_prep_gl_alignment_for_pixels_upload (pixels_rowstride);
}

static void
_cogl_texture_driver_prep_gl_for_pixels_upload (CoglContext *ctx,
                                                int pixels_rowstride,
                                                int pixels_bpp)
{
  prep_gl_for_pixels_upload_full (ctx, pixels_rowstride, 0, 0, 0, pixels_bpp);
}

/* OpenGL - unlike GLES - can download pixel data into a sub region of
 * a larger destination buffer */
static void
prep_gl_for_pixels_download_full (CoglContext *ctx,
                                  int image_width,
                                  int pixels_rowstride,
                                  int image_height,
                                  int pixels_src_x,
                                  int pixels_src_y,
                                  int pixels_bpp)
{
  GE( ctx, glPixelStorei (GL_PACK_ROW_LENGTH, pixels_rowstride / pixels_bpp) );

  GE( ctx, glPixelStorei (GL_PACK_SKIP_PIXELS, pixels_src_x) );
  GE( ctx, glPixelStorei (GL_PACK_SKIP_ROWS, pixels_src_y) );

  if (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_3D))
    GE( ctx, glPixelStorei (GL_PACK_IMAGE_HEIGHT, image_height) );

  _cogl_texture_prep_gl_alignment_for_pixels_download (pixels_bpp,
                                                       image_width,
                                                       pixels_rowstride);
}

static void
_cogl_texture_driver_prep_gl_for_pixels_download (CoglContext *ctx,
                                                      int image_width,
                                                  int pixels_rowstride,
                                                  int pixels_bpp)
{
  prep_gl_for_pixels_download_full (ctx,
                                    pixels_rowstride,
                                    image_width,
                                    0 /* image height */,
                                    0, 0, /* pixels_src_x/y */
                                    pixels_bpp);
}

static CoglBool
_cogl_texture_driver_upload_subregion_to_gl (CoglContext *ctx,
                                             GLenum gl_target,
                                             GLuint gl_handle,
                                             CoglBool is_foreign,
                                             int src_x,
                                             int src_y,
                                             int dst_x,
                                             int dst_y,
                                             int width,
                                             int height,
                                             CoglBitmap  *source_bmp,
				             GLuint source_gl_format,
				             GLuint source_gl_type,
                                             CoglError **error)
{
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp = _cogl_pixel_format_get_bytes_per_pixel (source_format);
  GLenum gl_error;
  CoglBool status = TRUE;
  CoglError *internal_error = NULL;

  data = _cogl_bitmap_gl_bind (source_bmp, COGL_BUFFER_ACCESS_READ, 0, &internal_error);

  /* NB: _cogl_bitmap_gl_bind() may return NULL when successfull so we
   * have to explicitly check the cogl error pointer to catch
   * problems... */
  if (internal_error)
    {
      _cogl_propogate_error (error, internal_error);
      return FALSE;
    }

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx,
                                  cogl_bitmap_get_rowstride (source_bmp),
                                  0,
                                  src_x,
                                  src_y,
                                  bpp);

  _cogl_bind_gl_texture_transient (gl_target, gl_handle, is_foreign);

  /* Clear any GL errors */
  while ((gl_error = ctx->glGetError ()) != GL_NO_ERROR)
    ;

  ctx->glTexSubImage2D (gl_target, 0,
                        dst_x, dst_y,
                        width, height,
                        source_gl_format,
                        source_gl_type,
                        data);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (source_bmp);

  return status;
}

static CoglBool
_cogl_texture_driver_upload_to_gl (CoglContext *ctx,
                                   GLenum gl_target,
                                   GLuint gl_handle,
                                   CoglBool is_foreign,
                                   CoglBitmap *source_bmp,
                                   GLint internal_gl_format,
                                   GLuint source_gl_format,
                                   GLuint source_gl_type,
                                   CoglError **error)
{
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp = _cogl_pixel_format_get_bytes_per_pixel (source_format);
  GLenum gl_error;
  CoglBool status = TRUE;

  data = _cogl_bitmap_gl_bind (source_bmp, COGL_BUFFER_ACCESS_READ, 0, error);
  if (!data)
    return FALSE;

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx,
                                  cogl_bitmap_get_rowstride (source_bmp),
                                  0, 0, 0, bpp);

  _cogl_bind_gl_texture_transient (gl_target, gl_handle, is_foreign);

  /* Clear any GL errors */
  while ((gl_error = ctx->glGetError ()) != GL_NO_ERROR)
    ;

  ctx->glTexImage2D (gl_target, 0,
                     internal_gl_format,
                     cogl_bitmap_get_width (source_bmp),
                     cogl_bitmap_get_height (source_bmp),
                     0,
                     source_gl_format,
                     source_gl_type,
                     data);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (source_bmp);

  return status;
}

static CoglBool
_cogl_texture_driver_upload_to_gl_3d (CoglContext *ctx,
                                      GLenum gl_target,
                                      GLuint gl_handle,
                                      CoglBool is_foreign,
                                      GLint height,
                                      GLint depth,
                                      CoglBitmap *source_bmp,
                                      GLint internal_gl_format,
                                      GLuint source_gl_format,
                                      GLuint source_gl_type,
                                      CoglError **error)
{
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp = _cogl_pixel_format_get_bytes_per_pixel (source_format);
  GLenum gl_error;
  CoglBool status = TRUE;

  data = _cogl_bitmap_gl_bind (source_bmp, COGL_BUFFER_ACCESS_READ, 0, error);
  if (!data)
    return FALSE;

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx,
                                  cogl_bitmap_get_rowstride (source_bmp),
                                  (cogl_bitmap_get_height (source_bmp) /
                                   depth),
                                  0, 0, bpp);

  _cogl_bind_gl_texture_transient (gl_target, gl_handle, is_foreign);

  /* Clear any GL errors */
  while ((gl_error = ctx->glGetError ()) != GL_NO_ERROR)
    ;

  ctx->glTexImage3D (gl_target,
                     0, /* level */
                     internal_gl_format,
                     cogl_bitmap_get_width (source_bmp),
                     height,
                     depth,
                     0,
                     source_gl_format,
                     source_gl_type,
                     data);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (source_bmp);

  return status;
}

static CoglBool
_cogl_texture_driver_gl_get_tex_image (CoglContext *ctx,
                                       GLenum gl_target,
                                       GLenum dest_gl_format,
                                       GLenum dest_gl_type,
                                       uint8_t *dest)
{
  GE (ctx, glGetTexImage (gl_target,
                          0, /* level */
                          dest_gl_format,
                          dest_gl_type,
                          (GLvoid *)dest));
  return TRUE;
}

static CoglBool
_cogl_texture_driver_size_supported_3d (CoglContext *ctx,
                                        GLenum gl_target,
                                        GLenum gl_format,
                                        GLenum gl_type,
                                        int width,
                                        int height,
                                        int depth)
{
  GLenum proxy_target;
  GLint new_width = 0;

  if (gl_target == GL_TEXTURE_3D)
    proxy_target = GL_PROXY_TEXTURE_3D;
  else
    /* Unknown target, assume it's not supported */
    return FALSE;

  /* Proxy texture allows for a quick check for supported size */
  GE( ctx, glTexImage3D (proxy_target, 0, GL_RGBA,
                         width, height, depth, 0 /* border */,
                         gl_format, gl_type, NULL) );

  GE( ctx, glGetTexLevelParameteriv (proxy_target, 0,
                                     GL_TEXTURE_WIDTH, &new_width) );

  return new_width != 0;
}

static CoglBool
_cogl_texture_driver_size_supported (CoglContext *ctx,
                                     GLenum gl_target,
                                     GLenum gl_intformat,
                                     GLenum gl_format,
                                     GLenum gl_type,
                                     int width,
                                     int height)
{
  GLenum proxy_target;
  GLint new_width = 0;

  if (gl_target == GL_TEXTURE_2D)
    proxy_target = GL_PROXY_TEXTURE_2D;
#if HAVE_COGL_GL
  else if (gl_target == GL_TEXTURE_RECTANGLE_ARB)
    proxy_target = GL_PROXY_TEXTURE_RECTANGLE_ARB;
#endif
  else
    /* Unknown target, assume it's not supported */
    return FALSE;

  /* Proxy texture allows for a quick check for supported size */
  GE( ctx, glTexImage2D (proxy_target, 0, gl_intformat,
                         width, height, 0 /* border */,
                         gl_format, gl_type, NULL) );

  GE( ctx, glGetTexLevelParameteriv (proxy_target, 0,
                                     GL_TEXTURE_WIDTH, &new_width) );

  return new_width != 0;
}

static void
_cogl_texture_driver_try_setting_gl_border_color
                                       (CoglContext *ctx,
                                        GLuint gl_target,
                                        const GLfloat *transparent_color)
{
  /* Use a transparent border color so that we can leave the
     color buffer alone when using texture co-ordinates
     outside of the texture */
  GE( ctx, glTexParameterfv (gl_target, GL_TEXTURE_BORDER_COLOR,
                             transparent_color) );
}

static CoglBool
_cogl_texture_driver_allows_foreign_gl_target (CoglContext *ctx,
                                               GLenum gl_target)
{
  /* GL_ARB_texture_rectangle textures are supported if they are
     created from foreign because some chipsets have trouble with
     GL_ARB_texture_non_power_of_two. There is no Cogl call to create
     them directly to emphasize the fact that they don't work fully
     (for example, no mipmapping and complicated shader support) */

  /* Allow 2-dimensional or rectangle textures only */
  if (gl_target != GL_TEXTURE_2D && gl_target != GL_TEXTURE_RECTANGLE_ARB)
    return FALSE;

  return TRUE;
}

static void
_cogl_texture_driver_gl_generate_mipmaps (CoglContext *ctx,
                                          GLenum gl_target)
{
  GE( ctx, glGenerateMipmap (gl_target) );
}

static CoglPixelFormat
_cogl_texture_driver_find_best_gl_get_data_format
                                            (CoglContext *context,
                                             CoglPixelFormat format,
                                             GLenum *closest_gl_format,
                                             GLenum *closest_gl_type)
{
  return context->driver_vtable->pixel_format_to_gl (context,
                                                     format,
                                                     NULL, /* don't need */
                                                     closest_gl_format,
                                                     closest_gl_type);
}

const CoglTextureDriver
_cogl_texture_driver_gl =
  {
    _cogl_texture_driver_gen,
    _cogl_texture_driver_prep_gl_for_pixels_upload,
    _cogl_texture_driver_upload_subregion_to_gl,
    _cogl_texture_driver_upload_to_gl,
    _cogl_texture_driver_upload_to_gl_3d,
    _cogl_texture_driver_prep_gl_for_pixels_download,
    _cogl_texture_driver_gl_get_tex_image,
    _cogl_texture_driver_size_supported,
    _cogl_texture_driver_size_supported_3d,
    _cogl_texture_driver_try_setting_gl_border_color,
    _cogl_texture_driver_allows_foreign_gl_target,
    _cogl_texture_driver_gl_generate_mipmaps,
    _cogl_texture_driver_find_best_gl_get_data_format
  };
