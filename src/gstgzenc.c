/*
 * GstGz
 * Copyright, (C) 2017 Alexandre Esse, <alexandre.esse.dev@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstgzenc.h"

#include <zlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gzenc_debug);
#define GST_CAT_DEFAULT gzenc_debug

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));
static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-gzip"));

#define DEFAULT_COMPRESSION_LEVEL Z_DEFAULT_COMPRESSION
#define DEFAULT_MEMORY_LEVEL 8
#define DEFAULT_FORMAT 0
#define DEFAULT_STRATEGY Z_DEFAULT_STRATEGY

enum
{
	PROP_0,
	PROP_COMPRESSION_LEVEL,
	PROP_MEMORY_LEVEL,
	PROP_STRATEGY,
	PROP_FORMAT
};

/* The gzip format was designed to retain the directory information about a single file, such as the name and last modification date. The zlib format on the other hand was designed for in-memory and communication channel applications, and has a much more compact header and trailer and uses a faster integrity check than gzip. */
typedef enum {
	GST_GZENC_GZIP,
	GST_GZENC_ZLIB/*,
	GST_GZENC_DEFLATE*/ //TODO: support RFC 1951
} GstGzencFormat;

/* The strategy parameter is used to tune the compression algorithm. Use the value Z_DEFAULT_STRATEGY for normal data, Z_FILTERED for data produced by a filter (or predictor), Z_HUFFMAN_ONLY to force Huffman encoding only (no string match), or Z_RLE to limit match distances to one (run-length encoding). Filtered data consists mostly of small values with a somewhat random distribution. In this case, the compression algorithm is tuned to compress them better. The effect of Z_FILTERED is to force more Huffman coding and less string matching; it is somewhat intermediate between Z_DEFAULT_STRATEGY and Z_HUFFMAN_ONLY. Z_RLE is designed to be almost as fast as Z_HUFFMAN_ONLY, but give better compression for PNG image data. The strategy parameter only affects the compression ratio but not the correctness of the compressed output even if it is not set appropriately. Z_FIXED prevents the use of dynamic Huffman codes, allowing for a simpler decoder for special applications. */
typedef enum {
	GST_GZENC_DEFAULT_STRATEGY,
	GST_GZENC_FILTERED,
	GST_GZENC_HUFFMAN_ONLY,
	GST_GZENC_RLE,
	GST_GZENC_FIXED
} GstGzencStrategy;

struct _GstGzenc
{
	GstElement parent;

	GstPad *sink;
	GstPad *src;

	/* Properties */
	gint compression_level;
	guint memory_level;
	GstGzencStrategy strategy;
	GstGzencFormat format;

	gboolean ready;
	z_stream stream;
	guint64 offset;
};

struct _GstGzencClass
{
  GstElementClass parent_class;
};

#define GST_TYPE_GZENC_FORMAT (gst_gzenc_format_get_type ())

static GType
gst_gzenc_format_get_type (void)
{
  static GType gzenc_format_type = 0;

	if (!gzenc_format_type)
	{
		static GEnumValue format_types[] = {
			{ GST_GZENC_ZLIB,  "RFC 1950 (zlib compressed format)", "zlib"},
			//{ GST_GZENC_DEFLATE, "RFC 1951 (deflate compressed format)", "deflate"},
			{ GST_GZENC_GZIP, "RFC 1952 (gzip compressed format)", "gzip"},
			{ 0, NULL, NULL },
    	};

	    gzenc_format_type =
	    g_enum_register_static ("GstGzencFormat", format_types);
	}
	return gzenc_format_type;
}

#define GST_TYPE_GZENC_STRATEGY (gst_gzenc_strategy_get_type ())

static GType
gst_gzenc_strategy_get_type (void)
{
  static GType gzenc_strategy = 0;

	if (!gzenc_strategy)
	{
		static GEnumValue strategy_types[] = {
			{ GST_GZENC_DEFAULT_STRATEGY, "Default", "default"},
			{ GST_GZENC_FILTERED,  "Filtered", "filtered"},
			{ GST_GZENC_HUFFMAN_ONLY, "Huffman", "huffman"},
			{ GST_GZENC_RLE, "RLE", "rle"},
			{ GST_GZENC_FIXED, "Fixed", "fixed"},
			{ 0, NULL, NULL },
    	};

	    gzenc_strategy =
	    g_enum_register_static ("GstGzencStrategy", strategy_types);
	}
	return gzenc_strategy;
}

#if GST_CHECK_VERSION(1,0,0)
#define gst_gzenc_parent_class parent_class
G_DEFINE_TYPE (GstGzenc, gst_gzenc, GST_TYPE_ELEMENT);
#else
GST_BOILERPLATE (GstGzenc, gst_gzenc, GstElement, GST_TYPE_ELEMENT);
#endif
static void
gst_gzenc_compress_end (GstGzenc * enc)
{
	g_return_if_fail (GST_IS_GZENC (enc));

	if (enc->ready)
	{
		GST_DEBUG_OBJECT (enc, "Finalize gzenc compressing feature");
	    (void)deflateEnd (&enc->stream);
	    memset (&enc->stream, 0, sizeof (enc->stream));
	    enc->ready = FALSE;
  }
}

static void
gst_gzenc_compress_init (GstGzenc * enc)
{
	GST_DEBUG_OBJECT (enc, "Initialize gzenc compressing feature");
	g_return_if_fail (GST_IS_GZENC (enc));
	gst_gzenc_compress_end (enc);

    /* Initialise zlib default values for deflateInit() */
	enc->stream.zalloc = Z_NULL;
	enc->stream.zfree = Z_NULL;
	enc->stream.opaque = Z_NULL;
    enc->stream.avail_in = 0;
    enc->stream.next_in = Z_NULL;

	enc->offset = 0;

	int windowBits, strategy;
	int ret = Z_OK;

	switch (enc->strategy)
	{
		case GST_GZENC_DEFAULT_STRATEGY:
			strategy = Z_DEFAULT_STRATEGY;
			break;
		case GST_GZENC_FILTERED:
			strategy = Z_FILTERED;
			break;
		case GST_GZENC_HUFFMAN_ONLY:
			strategy = Z_HUFFMAN_ONLY;
			break;
		case GST_GZENC_RLE:
			strategy = Z_RLE;
			break;
		case GST_GZENC_FIXED:
			strategy = Z_FIXED;
			break;
		default:
			strategy = Z_DEFAULT_STRATEGY;
			break;
	}

	switch (enc->format)
	{
		case GST_GZENC_GZIP:
			windowBits = MAX_WBITS|16;
			ret = deflateInit2 (&enc->stream, enc->compression_level, 
				Z_DEFLATED, windowBits, enc->memory_level, strategy );
			GST_DEBUG_OBJECT (enc, "Initialize gzenc for gzip");
			break;
		case GST_GZENC_ZLIB:
			ret = deflateInit (&enc->stream, strategy);
			GST_DEBUG_OBJECT (enc, "Initialize gzenc for zlib");
			break;
/*		case GST_GZENC_DEFLATE:
			windowBits = -MAX_WBITS;
			break;*/
		default:
			GST_DEBUG_OBJECT (enc, "Unknown format");
	}

	switch (ret)
	{
	    case Z_OK:
			GST_DEBUG_OBJECT (enc, "deflateInit() return Z_OK");
			enc->ready = TRUE;
			return;
		/* Handle initialisation errors */
		case Z_MEM_ERROR:
			GST_DEBUG_OBJECT (enc, "deflateInit() return Z_MEM_ERROR");
			break;
		case Z_VERSION_ERROR:
			GST_DEBUG_OBJECT (enc, "deflateInit() return Z_VERSION_ERROR");
			break;
		case Z_STREAM_ERROR:
			GST_DEBUG_OBJECT (enc, "deflateInit() return Z_STREAM_ERROR");
			break;
	    default:
			GST_DEBUG_OBJECT (enc, "deflateInit() return unknown error");
			break;
    }
	enc->ready = FALSE;
	GST_ELEMENT_ERROR (enc, CORE, FAILED, (NULL),
		("Failed to start compression."));
	return;
}

static gboolean
#if GST_CHECK_VERSION(1,0,0)
gst_gzenc_event (GstPad * pad, GstObject * parent, GstEvent * e)
#else
gst_gzenc_event (GstPad * pad, GstEvent * e)
#endif
{
	GstGzenc *enc;
	gboolean ret;

#if GST_CHECK_VERSION(1,0,0)
	enc = GST_GZENC (parent);
#else
	enc = GST_GZENC (gst_pad_get_parent (pad));
#endif
	switch (GST_EVENT_TYPE (e))
	{
		case GST_EVENT_EOS:
		{
			GstFlowReturn flow = GST_FLOW_OK;
			int r = Z_OK;

			do
			{
				GstBuffer *out;
#if GST_CHECK_VERSION(1,0,0)
				GstMapInfo outmap;
				guint n;

				out = gst_buffer_new_and_alloc (enc->memory_level);

				gst_buffer_map (out, &outmap, GST_MAP_WRITE);
				enc->stream.next_out = (void *) outmap.data;
				enc->stream.avail_out = outmap.size;
#else
				flow = gst_pad_alloc_buffer (enc->src, enc->offset, enc->memory_level,
				    GST_PAD_CAPS (enc->src), &out);

				if (flow != GST_FLOW_OK) {
				  GST_DEBUG_OBJECT (enc, "pad alloc on EOS failed: %s",
				      gst_flow_get_name (flow));
				  break;
				}
				enc->stream.next_out = (void *) GST_BUFFER_DATA (out);
				enc->stream.avail_out = GST_BUFFER_SIZE (out);
#endif
        		r = deflate (&enc->stream, Z_FINISH);
#if GST_CHECK_VERSION(1,0,0)
        		gst_buffer_unmap (out, &outmap);
#endif
				if ((r != Z_OK) && (r != Z_STREAM_END))
				{
				  GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
				      ("Failed to finish to compress (error code %i).", r));
				  gst_buffer_unref (out);
				  break;
				}
#if GST_CHECK_VERSION(1,0,0)
				n = gst_buffer_get_size (out);
				if (enc->stream.avail_out >= n)
#else
				if (enc->stream.avail_out >= GST_BUFFER_SIZE (out))
#endif
				{
					gst_buffer_unref (out);
					break;
				}
#if GST_CHECK_VERSION(1,0,0)
				gst_buffer_resize (out, 0, n - enc->stream.avail_out);
				n = gst_buffer_get_size (out);
				GST_BUFFER_OFFSET (out) = enc->stream.total_out - n;
#else
				GST_BUFFER_SIZE (out) -= enc->stream.avail_out;
				GST_BUFFER_OFFSET (out) =
				    enc->stream.total_out - GST_BUFFER_SIZE (out);
#endif
				flow = gst_pad_push (enc->src, out);

				if (flow != GST_FLOW_OK)
				{
					GST_DEBUG_OBJECT (enc, "push on EOS failed: %s",
						gst_flow_get_name (flow));
					break;
				}
			} while (r != Z_STREAM_END);
#if GST_CHECK_VERSION(1,0,0)
			ret = gst_pad_event_default (pad, parent, e);
#else
			ret = gst_pad_event_default (pad, e);
#endif
			if (r != Z_STREAM_END || flow != GST_FLOW_OK)
			{
				ret = FALSE;
			}

			gst_gzenc_compress_init (enc);
			break;
		}
		default:
#if GST_CHECK_VERSION(1,0,0)
			ret = gst_pad_event_default (pad, parent, e);
#else
			ret = gst_pad_event_default (pad, e);
#endif
		break;
	}
#if !GST_CHECK_VERSION(1,0,0)
	gst_object_unref (enc);
#endif
	return ret;
}

static GstFlowReturn
#if GST_CHECK_VERSION(1,0,0)
gst_gzenc_chain (GstPad * pad, GstObject * parent, GstBuffer * in)
#else
gst_gzenc_chain (GstPad * pad, GstBuffer * in)
#endif
{
	GstFlowReturn flow = GST_FLOW_OK;
	GstBuffer *out;
	GstGzenc *enc;
	guint n;
	int ret;
#if GST_CHECK_VERSION(1,0,0)
	GstMapInfo map = GST_MAP_INFO_INIT, outmap;

	enc = GST_GZENC (parent);
#else
	enc = GST_GZENC (GST_PAD_PARENT (pad));
#endif
	if (!enc->ready)
 	{
	    GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, (NULL), ("Compressor not ready."));
		//gst_gzenc_compress_init (enc);
		//gst_buffer_unref (out);
#if GST_CHECK_VERSION(1,0,0)
		flow = GST_FLOW_FLUSHING;
#else
		flow = GST_FLOW_WRONG_STATE;
#endif
	    goto done;
	}

#if GST_CHECK_VERSION(1,0,0)
	gst_buffer_map (in, &map, GST_MAP_READ);
	enc->stream.next_in = (void *) map.data;
	enc->stream.avail_in = map.size;
#else
	enc->stream.next_in = (void *) GST_BUFFER_DATA (in);
	enc->stream.avail_in = GST_BUFFER_SIZE (in);
#endif

  while (enc->stream.avail_in) {
#if GST_CHECK_VERSION(1,0,0)
    out = gst_buffer_new_and_alloc (enc->memory_level);
    gst_buffer_map (out, &outmap, GST_MAP_WRITE);
    enc->stream.next_out = (void *) outmap.data;
    enc->stream.avail_out = outmap.size;
#else
	flow = gst_pad_alloc_buffer (enc->src, enc->offset, enc->memory_level, 
		GST_PAD_CAPS (pad), &out);
    if (flow != GST_FLOW_OK)
	{
		gst_gzenc_compress_init (enc);
		break;
    }
	enc->stream.next_out = (void *) GST_BUFFER_DATA (out);
    enc->stream.avail_out = GST_BUFFER_SIZE (out);
#endif
    ret = deflate (&enc->stream, Z_NO_FLUSH);
#if GST_CHECK_VERSION(1,0,0)
    gst_buffer_unmap (out, &outmap);
#endif
    if (ret == Z_STREAM_ERROR)
	{
		GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
			("Failed to compress data (error code %i)", ret));
		gst_gzenc_compress_init (enc);
		gst_buffer_unref (out);
		flow = GST_FLOW_ERROR;
		goto done;
	}
#if GST_CHECK_VERSION(1,0,0)
    n = gst_buffer_get_size (out);
#else
	n = GST_BUFFER_SIZE (out);
#endif
    if (enc->stream.avail_out >= n) {
      gst_buffer_unref (out);
      break;
    }
#if GST_CHECK_VERSION(1,0,0)
    gst_buffer_resize (out, 0, n - enc->stream.avail_out);
    n = gst_buffer_get_size (out);
    GST_BUFFER_OFFSET (out) = enc->stream.total_out - n;
#else
    GST_BUFFER_SIZE (out) -= enc->stream.avail_out;
    GST_BUFFER_OFFSET (out) = enc->stream.total_out - GST_BUFFER_SIZE (out);
    n = GST_BUFFER_SIZE (out);
#endif
    flow = gst_pad_push (enc->src, out);

    if (flow != GST_FLOW_OK)
      break;

    enc->offset += n;
  }

done:
#if GST_CHECK_VERSION(1,0,0)
  gst_buffer_unmap (in, &map);
#endif
  gst_buffer_unref (in);
  return flow;

}

static void
#if GST_CHECK_VERSION(1,0,0)
gst_gzenc_init (GstGzenc * enc)
#else
gst_gzenc_init (GstGzenc * enc, GstGzencClass * klass)
#endif
{
	enc->sink = gst_pad_new_from_static_template (&sink_template, "sink");
	gst_pad_set_chain_function (enc->sink, GST_DEBUG_FUNCPTR (gst_gzenc_chain));
	gst_pad_set_event_function (enc->sink, GST_DEBUG_FUNCPTR (gst_gzenc_event));
	gst_element_add_pad (GST_ELEMENT (enc), enc->sink);

	enc->src = gst_pad_new_from_static_template (&src_template, "src");
	gst_pad_set_caps (enc->src, gst_static_pad_template_get_caps (&src_template));
	gst_pad_use_fixed_caps (enc->src);
	gst_element_add_pad (GST_ELEMENT (enc), enc->src);

	enc->compression_level = DEFAULT_COMPRESSION_LEVEL;
	enc->memory_level = DEFAULT_MEMORY_LEVEL;
	enc->format = DEFAULT_FORMAT;
	enc->strategy = DEFAULT_STRATEGY;
	gst_gzenc_compress_init (enc);
}

static void
gst_gzenc_finalize (GObject * object)
{
	GstGzenc *enc = GST_GZENC (object);
    GST_DEBUG_OBJECT (enc, "Finalize gzenc");
	gst_gzenc_compress_end (enc);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gzenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
	GstGzenc *enc = GST_GZENC (object);

	switch (prop_id)
	{
		case PROP_COMPRESSION_LEVEL:
			g_value_set_int (value, enc->compression_level);
			GST_DEBUG_OBJECT (enc, "Compression level is : %d", enc->compression_level);
			break;
		case PROP_MEMORY_LEVEL:
			g_value_set_uint (value, enc->memory_level);
			GST_DEBUG_OBJECT (enc, "Memory level is : %d", enc->memory_level);
			break;
		case PROP_STRATEGY:
			g_value_set_enum (value, enc->strategy);
			GST_DEBUG_OBJECT (enc, "Strategy is : %d", enc->strategy);
			break;
		case PROP_FORMAT:
			g_value_set_enum (value, enc->format);
			GST_DEBUG_OBJECT (enc, "Format is : %d", enc->format);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
gst_gzenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
	GstGzenc *enc = GST_GZENC (object);

	switch (prop_id)
	{
		case PROP_COMPRESSION_LEVEL:
			enc->compression_level = g_value_get_int (value);
			GST_DEBUG_OBJECT (enc, "Compression level set to : %d",enc->compression_level);
			gst_gzenc_compress_init (enc);
			break;
		case PROP_MEMORY_LEVEL:
			enc->memory_level = g_value_get_uint (value);
			GST_DEBUG_OBJECT (enc, "Memory level set to : %d",enc->memory_level);
    		break;
		case PROP_STRATEGY:
			enc->strategy = g_value_get_enum (value);
			GST_DEBUG_OBJECT (enc, "Strategy set to : %d",enc->strategy);
			gst_gzenc_compress_init (enc);
    		break;
		case PROP_FORMAT:
			enc->format = g_value_get_enum (value);
			GST_DEBUG_OBJECT (enc, "Format set to : %d",enc->format);
			gst_gzenc_compress_init (enc);
    		break;
    	default:
    		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_gzenc_class_init (GstGzencClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
#if GST_CHECK_VERSION(1,0,0)
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
#endif
  gobject_class->set_property = gst_gzenc_set_property;
  gobject_class->get_property = gst_gzenc_get_property;

  gobject_class->finalize = gst_gzenc_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COMPRESSION_LEVEL,
      g_param_spec_int ("compression-level", "Compression level", "The compression level must be -1, or between 0 and 9: 1 gives best speed, 9 gives best compression, 0 gives no compression at all (the input data is simply copied a block at a time). -1 requests a default compromise between speed and compression (currently equivalent to level 6).",
          -1, 9, DEFAULT_COMPRESSION_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MEMORY_LEVEL,
      g_param_spec_uint ("memory-level", "Memory level", "The memory-level parameter specifies how much memory should be allocated for the internal compression state. memory-level=1 uses minimum memory but is slow and reduces compression ratio; memory-level=9 uses maximum memory for optimal speed. The default value is 8.",
          1, 9, DEFAULT_MEMORY_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_STRATEGY,
    	g_param_spec_enum ("strategy", "Strategy", "The strategy parameter is used to tune the compression algorithm.",
                       GST_TYPE_GZENC_STRATEGY, GST_GZENC_DEFAULT_STRATEGY,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FORMAT,
    	g_param_spec_enum ("format", "Format", "Type of format generated",
                       GST_TYPE_GZENC_FORMAT, GST_GZENC_GZIP,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#if GST_CHECK_VERSION(1,0,0)
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_static_metadata (gstelement_class, "GZ encoder",
      "Codec/Encoder", "Compresses streams",
      "Alexandre Esse <alexandre.esse@gmail.com>");
#endif
  GST_DEBUG_CATEGORY_INIT (gzenc_debug, "gzenc", 0, "GZ compressor");
}

#if !GST_CHECK_VERSION(1,0,0)
static void
gst_gzenc_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (ec, &sink_template);
  gst_element_class_add_static_pad_template (ec, &src_template);
  gst_element_class_set_details_simple (ec, "GZ encoder",
      "Codec/Encoder", "Compresses streams",
      "Alexandre Esse <alexandre.esse@gmail.com>");
}
#endif
