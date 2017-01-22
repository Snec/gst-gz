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
#include "gstgzdec.h"

#include <gst/base/gsttypefindhelper.h>
#include <zlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gzdec_debug);
#define GST_CAT_DEFAULT gzdec_debug

static GstStaticPadTemplate sink_template = 
	GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, 
	GST_STATIC_CAPS ("application/x-gzip"));
static GstStaticPadTemplate src_template = 
	GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS, 
	GST_STATIC_CAPS_ANY);

#define DEFAULT_FIRST_BUFFER_SIZE 1024
#define DEFAULT_BUFFER_SIZE 1024

enum
{
    PROP_0,
    PROP_FIRST_BUFFER_SIZE,
    PROP_BUFFER_SIZE
};

struct _GstGzdec
{
    GstElement parent;

    GstPad *sink;
    GstPad *src;

    /* Properties */
    guint first_buffer_size;
    guint buffer_size;

    gboolean ready;
    z_stream stream;
    guint64 offset;
};

struct _GstGzdecClass
{
    GstElementClass parent_class;
};

#if GST_VERSION_MAJOR
#define gst_gzdec_parent_class parent_class
G_DEFINE_TYPE (GstGzdec, gst_gzdec, GST_TYPE_ELEMENT);
#else
GST_BOILERPLATE (GstGzdec, gst_gzdec, GstElement, GST_TYPE_ELEMENT);
#endif

static void
gst_gzdec_decompress_end (GstGzdec * dec)
{
	g_return_if_fail (GST_IS_GZDEC (dec));

    if (dec->ready)
	{
		GST_DEBUG_OBJECT (dec, "Finalize gzdec decompressing feature");
        (void)inflateEnd(&dec->stream);
        memset (&dec->stream, 0, sizeof (dec->stream));
        dec->ready = FALSE;
    }
}

static void
gst_gzdec_decompress_init (GstGzdec * dec)
{
    GST_DEBUG_OBJECT (dec, "Initialize gzdec decompressing feature");
    g_return_if_fail (GST_IS_GZDEC (dec));
    gst_gzdec_decompress_end (dec);

    /* Initialise zlib default values for inflateInit() */
    dec->stream.zalloc = Z_NULL;
    dec->stream.zfree = Z_NULL;
    dec->stream.opaque = Z_NULL;
    dec->stream.avail_in = 0;
    dec->stream.next_in = Z_NULL;

	dec->offset = 0;

    switch (inflateInit2 (&dec->stream, MAX_WBITS|32))
	{
        case Z_OK:
			GST_DEBUG_OBJECT (dec, "inflateInit2() return Z_OK");
            dec->ready = TRUE;
            return;
		/* Handle initialisation errors */
		case Z_MEM_ERROR:
			GST_DEBUG_OBJECT (dec, "inflateInit2() return Z_MEM_ERROR");
			break;
		case Z_VERSION_ERROR:
			GST_DEBUG_OBJECT (dec, "inflateInit2() return Z_VERSION_ERROR");
			break;
		case Z_STREAM_ERROR:
			GST_DEBUG_OBJECT (dec, "inflateInit2() return Z_STREAM_ERROR");
			break;
        default:
			GST_DEBUG_OBJECT (dec, "inflateInit2() return unknown error");
			break;
    }
	dec->ready = FALSE;
	GST_ELEMENT_ERROR (dec, CORE, FAILED, (NULL), 
		("Failed to start decompression."));
    return;
}

static GstFlowReturn
#if GST_VERSION_MAJOR
gst_gzdec_chain (GstPad * pad, GstObject * parent, GstBuffer * in)
#else
gst_gzdec_chain (GstPad * pad, GstBuffer * in)
#endif
{
    GstFlowReturn flow = GST_FLOW_OK;
    GstBuffer *out;
    GstGzdec *dec;
    int ret = Z_OK;
#if GST_VERSION_MAJOR
    GstMapInfo inmap = GST_MAP_INFO_INIT, outmap;

    dec = GST_GZDEC (parent);
#else
	dec = GST_GZDEC (GST_PAD_PARENT (pad));
#endif
    if (!dec->ready)
    {
		/* Don't go further if not ready */
        GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL), 
			("Decompressor not ready."));
#if GST_VERSION_MAJOR
        flow = GST_FLOW_FLUSHING;
#else
		flow = GST_FLOW_WRONG_STATE;
#endif
    }
	else
	{
#if GST_VERSION_MAJOR
		/* Mapping the input buffer */
		gst_buffer_map (in, &inmap, GST_MAP_READ);
    	dec->stream.next_in = (z_const Bytef *) inmap.data;
    	dec->stream.avail_in = inmap.size;
    	GST_DEBUG_OBJECT (dec, "Input buffer size : dec->stream.avail_in = %d", dec->stream.avail_in);
#else
		dec->stream.next_in = (void *) GST_BUFFER_DATA (in);
		dec->stream.avail_in = GST_BUFFER_SIZE (in);
#endif
    	do
		{
        	guint have;
#if GST_VERSION_MAJOR
		    /* Create and map the output buffer */
		    out = gst_buffer_new_and_alloc (dec->offset ? dec->buffer_size : dec->first_buffer_size);
			gst_buffer_map (out, &outmap, GST_MAP_WRITE);
			dec->stream.next_out = (Bytef *) outmap.data;
		    dec->stream.avail_out = outmap.size;

		    /* Try to decode */
			gst_buffer_unmap (out, &outmap);
#else
    /* Create the output buffer */
    flow = gst_pad_alloc_buffer (dec->src, dec->offset,
        dec->offset ? dec->buffer_size : dec->first_buffer_size,
        GST_PAD_CAPS (dec->src), &out);

    if (flow != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (dec, "pad alloc failed: %s", gst_flow_get_name (flow));
      gst_gzdec_decompress_init (dec);
      break;
    }

    /* Decode */
    dec->stream.next_out = (void *) GST_BUFFER_DATA (out);
    dec->stream.avail_out = GST_BUFFER_SIZE (out);
#endif
		    ret = inflate (&dec->stream, Z_NO_FLUSH);
			switch (ret)
			{
        		case Z_OK:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_OK [%s]",dec->stream.msg);
            		break;
				case Z_STREAM_END:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_STREAM_END [%s]",dec->stream.msg);
					break;
				case Z_NEED_DICT:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_NEED_DICT [%s]",dec->stream.msg);
					break;
				/* Errors */
				case Z_ERRNO:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_ERRNO [%s]",dec->stream.msg);
					break;
				case Z_STREAM_ERROR:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_STREAM_ERROR [%s]",dec->stream.msg);
					break;
				case Z_DATA_ERROR:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_DATA_ERROR [%s]",dec->stream.msg);
					break;
				case Z_MEM_ERROR:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_MEM_ERROR [%s]",dec->stream.msg);
					break;
				case Z_BUF_ERROR:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_BUF_ERROR [%s]",dec->stream.msg);
					break;
				case Z_VERSION_ERROR:
					GST_DEBUG_OBJECT (dec, "inflate() return Z_VERSION_ERROR [%s]",dec->stream.msg);
				default:
					GST_DEBUG_OBJECT (dec, "inflate() return unknow code (%d) [%s]", ret, dec->stream.msg);
					break;
			}

		    if (ret == Z_STREAM_ERROR)
		    {
				GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
				        ("Failed to decompress data (error code %i).", ret));
				gst_gzdec_decompress_init (dec);
				gst_buffer_unref (out);
				flow = GST_FLOW_ERROR;
				break;
		    }
#if GST_VERSION_MAJOR
		    if (dec->stream.avail_out >= gst_buffer_get_size (out))
#else
			if (dec->stream.avail_out >= GST_BUFFER_SIZE (out))
#endif
			{
		        gst_buffer_unref (out);
		        break;
		    }
#if GST_VERSION_MAJOR
			/* Resize the output buffer */
		    gst_buffer_resize (out, 0, gst_buffer_get_size (out) - dec->stream.avail_out);
		    GST_BUFFER_OFFSET (out) = dec->stream.total_out - gst_buffer_get_size (out);
#else
			GST_BUFFER_SIZE (out) -= dec->stream.avail_out;
			GST_BUFFER_OFFSET (out) = dec->stream.total_out - GST_BUFFER_SIZE (out);
#endif
		    /* Configure source pad (if necessary) */
		    if (!dec->offset) {
		        GstCaps *caps = NULL;

		        caps = gst_type_find_helper_for_buffer (GST_OBJECT (dec), out, NULL);
		        if (caps) {
#if !GST_VERSION_MAJOR
					gst_buffer_set_caps (out, caps);
#endif
		            gst_pad_set_caps (dec->src, caps);
		            gst_pad_use_fixed_caps (dec->src);
		            gst_caps_unref (caps);
		        } else {
		            GST_FIXME_OBJECT (dec, "shouldn't we queue output buffers until we have a type?");
		        }
		    }

		    /* Push data */
		    GST_DEBUG_OBJECT (dec, "Push data on src pad");
#if GST_VERSION_MAJOR
		    have = gst_buffer_get_size (out);
#else
			have = GST_BUFFER_SIZE (out);
#endif
		    flow = gst_pad_push (dec->src, out);
		    if (flow != GST_FLOW_OK)
		    {
		        break;
		    } 
		    dec->offset += have;
		} while (ret != Z_STREAM_END);
	}

#if GST_VERSION_MAJOR
        gst_buffer_unmap (in, &inmap);
#endif
        gst_buffer_unref (in);
        return flow; 
}

static void
#if GST_VERSION_MAJOR
gst_gzdec_init (GstGzdec * dec)
#else
gst_gzdec_init (GstGzdec * dec, GstGzdecClass * klass)
#endif
{
    GST_DEBUG_OBJECT (dec, "Initialize gzdec");
    dec->first_buffer_size = DEFAULT_FIRST_BUFFER_SIZE;
    dec->buffer_size = DEFAULT_BUFFER_SIZE;

    dec->sink = gst_pad_new_from_static_template (&sink_template, "sink");
    gst_pad_set_chain_function (dec->sink, GST_DEBUG_FUNCPTR (gst_gzdec_chain));
    gst_element_add_pad (GST_ELEMENT (dec), dec->sink);

    dec->src = gst_pad_new_from_static_template (&src_template, "src");
    gst_element_add_pad (GST_ELEMENT (dec), dec->src);
    gst_pad_use_fixed_caps (dec->src);

    gst_gzdec_decompress_init (dec);
}

static void
gst_gzdec_finalize (GObject * object)
{

    GstGzdec *dec = GST_GZDEC (object);
    GST_DEBUG_OBJECT (dec, "Finalize gzdec");
    gst_gzdec_decompress_end (dec);

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gzdec_get_property (GObject * object, guint prop_id,
        GValue * value, GParamSpec * pspec)
{
    GstGzdec *dec = GST_GZDEC (object);

    switch (prop_id) {
        case PROP_BUFFER_SIZE:
            g_value_set_uint (value, dec->buffer_size);
			GST_DEBUG_OBJECT (dec, "Buffer size is : %d",dec->buffer_size);
            break;
        case PROP_FIRST_BUFFER_SIZE:
            g_value_set_uint (value, dec->first_buffer_size);
			GST_DEBUG_OBJECT (dec, "Buffer size is : %d",dec->buffer_size);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gst_gzdec_set_property (GObject * object, guint prop_id,
        const GValue * value, GParamSpec * pspec)
{
    GstGzdec *dec = GST_GZDEC (object);
    
    switch (prop_id) {
        case PROP_BUFFER_SIZE:
            dec->buffer_size = g_value_get_uint (value);
			GST_DEBUG_OBJECT (dec, "Buffer size set to : %d",dec->buffer_size);
            break;
        case PROP_FIRST_BUFFER_SIZE:
            dec->first_buffer_size = g_value_get_uint (value);
			GST_DEBUG_OBJECT (dec, "First buffer size set to : %d",dec->first_buffer_size);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GstStateChangeReturn
gst_gzdec_change_state (GstElement * element, GstStateChange transition)
{
    GstGzdec *dec = GST_GZDEC (element);
    GstStateChangeReturn ret;
    GST_DEBUG_OBJECT (dec, "Changing gzdec state");
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (ret != GST_STATE_CHANGE_SUCCESS)
        return ret;

    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            gst_gzdec_decompress_init (dec);
            break;
        default:
            break;
    }
    return ret;
}

static void
gst_gzdec_class_init (GstGzdecClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

    gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_gzdec_change_state);

    gobject_class->finalize = gst_gzdec_finalize;
    gobject_class->get_property = gst_gzdec_get_property;
    gobject_class->set_property = gst_gzdec_set_property;

    g_object_class_install_property (G_OBJECT_CLASS (klass),
            PROP_FIRST_BUFFER_SIZE, g_param_spec_uint ("first-buffer-size",
                "Size of first buffer", "Size of first buffer (used to determine the "
                "mime type of the uncompressed data)", 1, G_MAXUINT,
                DEFAULT_FIRST_BUFFER_SIZE,
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
            g_param_spec_uint ("buffer-size", "Buffer size", "Buffer size",
                1, G_MAXUINT, DEFAULT_BUFFER_SIZE,
                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#if GST_VERSION_MAJOR
    gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&sink_template));
    gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&src_template));
    gst_element_class_set_static_metadata (gstelement_class, "GZ decoder",
            "Codec/Decoder", "Decodes compressed streams",
            "Alexandre Esse <alexandre.esse@gmail.com>");
#endif
    GST_DEBUG_CATEGORY_INIT (gzdec_debug, "gzdec", 0, "GZ decompressor");
}

#if !GST_VERSION_MAJOR
static void
gst_gzdec_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (ec, &sink_template);
  gst_element_class_add_static_pad_template (ec, &src_template);
  gst_element_class_set_details_simple (ec, "GZ decoder",
		"Codec/Decoder", "Decodes compressed streams",
		"Alexandre Esse <alexandre.esse@gmail.com>");
}
#endif
