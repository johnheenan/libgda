/* common-pict.c
 * Copyright (C) 2006 - 2007 Vivien Malerba <malerba@gnome-db.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "common-pict.h"
#include <string.h>
#include <libgda/gda-quark-list.h>
#include <libgda/gda-blob-op.h>

/*
 * Fills in @bindata->data and @bindata->data_length with the contents of @value.
 *
 * Returns: TRUE if the data has been loaded correctly
 */
gboolean
common_pict_load_data (PictOptions *options, const GValue *value, PictBinData *bindata, 
		       const gchar **stock, GError **error)
{
	gboolean allok = TRUE;

	if (value) {
		if (gda_value_is_null ((GValue *) value)) {
			*stock = GTK_STOCK_MISSING_IMAGE;
			g_set_error (error, 0, 0, _("No data"));
			allok = FALSE;
		}
		else {
			if (G_VALUE_TYPE ((GValue *) value) == GDA_TYPE_BLOB) {
				GdaBlob *blob;
				GdaBinary *bin;

				blob = (GdaBlob *) gda_value_get_blob ((GValue *) value);
				g_assert (blob);
				bin = (GdaBinary *) blob;
				if (blob->op &&
				    (bin->binary_length != gda_blob_op_get_length (blob->op)))
					gda_blob_op_read_all (blob->op, blob);
				if (bin->binary_length > 0) {
					bindata->data = g_new (guchar, bin->binary_length);
					bindata->data_length = bin->binary_length;
					memcpy (bindata->data, bin->data, bin->binary_length);
				}
			}
			else if (G_VALUE_TYPE ((GValue *) value) == GDA_TYPE_BINARY) {
				GdaBinary *bin;

				bin = (GdaBinary *) gda_value_get_binary ((GValue *) value);
				if (bin && bin->binary_length > 0) {
					bindata->data = g_new (guchar, bin->binary_length);
					bindata->data_length = bin->binary_length;
					memcpy (bindata->data, bin->data, bin->binary_length);
				}
				else {
					*stock = GTK_STOCK_DIALOG_ERROR;
					g_set_error (error, 0, 0,
						     _("No data"));
					allok = FALSE;
				}
			}
			else if (G_VALUE_TYPE ((GValue *) value) == G_TYPE_STRING) {
				const gchar *str;

				str = g_value_get_string (value);
				if (str) {
					switch (options->encoding) {
					case ENCODING_NONE:
						bindata->data = (guchar *) g_strdup (str);
						bindata->data_length = strlen ((gchar *) bindata->data);
						break;
					case ENCODING_BASE64: {
#if (GLIB_MINOR_VERSION >= 12)
						gsize out_len;
						bindata->data = g_base64_decode (str, &out_len);
						if (out_len > 0)
							bindata->data_length = out_len;
						else {
							g_free (bindata->data);
							bindata->data = NULL;
							bindata->data_length = 0;
						}
#else
						g_warning ("Base64 enoding/decoding is not supported in the "
							   "GLib version %d.%d.%d",
							   glib_major_version, glib_minor_version, glib_micro_version);
#endif

						break;
					}
					}
				}
				else {
					*stock = GTK_STOCK_MISSING_IMAGE;
					g_set_error (error, 0, 0, _("Empty data"));
					allok = FALSE;
				}
			}
			else {
				*stock = GTK_STOCK_DIALOG_ERROR;
				g_set_error (error, 0, 0, _("Unhandled type of data"));
				allok = FALSE;
			}
		}
	}
	else {
		*stock = GTK_STOCK_MISSING_IMAGE;
		g_set_error (error, 0, 0, _("Empty data"));
		allok = FALSE;
	}

	return allok;
}

static void
compute_reduced_size (gint width, gint height, PictAllocation *allocation, 
		     gint *out_width, gint *out_height)
{
	gint reqw, reqh;

	reqw = allocation->width;
	reqh = allocation->height;

	if ((reqw < width) || (reqh < height)) {
		gint w, h;
	
		if ((double) height * (double) reqw > (double) width * (double) reqh) {
			w = 0.5 + (double) width * (double) reqh / (double) height;
			h =  reqh;
		} else {
			h = 0.5 + (double) height * (double) reqw / (double) width;
			w =  reqw;
		}
		*out_width = w;
		*out_height = h;
	}
	else {
		*out_width = width;
		*out_height = height;
	}
}

static void 
loader_size_prepared_cb (GdkPixbufLoader *loader, gint width, gint height, PictAllocation *allocation)
{
	gint w, h;

	compute_reduced_size (width, height, allocation, &w, &h);
	if ((w != width) || (h != height))
		gdk_pixbuf_loader_set_size (loader, w, h);

	/*
	gint reqw, reqh;

	reqw = allocation->width;
	reqh = allocation->height;

	if ((reqw < width) || (reqh < height)) {
		gint w, h;
	
		if ((double) height * (double) reqw > (double) width * (double) reqh) {
			w = 0.5 + (double) width * (double) reqh / (double) height;
			h =  reqh;
		} else {
			h = 0.5 + (double) height * (double) reqw / (double) width;
			w =  reqw;
		}
		
		gdk_pixbuf_loader_set_size (loader, w, h);
	}
	*/
}

/*
 * Creates a GdkPixbuf from @bindata and @options; returns NULL if an error occured.
 *
 * if @allocation is %NULL, then the GdaPixbuf will have the real size of the image.
 */
GdkPixbuf * 
common_pict_make_pixbuf (PictOptions *options, PictBinData *bindata, PictAllocation *allocation, 
			 const gchar **stock, GError **error)
{
	GdkPixbuf *retpixbuf = NULL;

	if (bindata->data) {
		if (options->serialize) {
			GdkPixdata pixdata;
			GError *loc_error = NULL;
			
			if (!gdk_pixdata_deserialize (&pixdata, bindata->data_length, 
						      bindata->data, &loc_error)) {
				g_free (bindata->data);
				bindata->data = NULL;
				bindata->data_length = 0;

				*stock = GTK_STOCK_DIALOG_ERROR;
				g_set_error (error, 0, 0,
					     _("Error while deserializing data:\n%s"),
					     loc_error && loc_error->message ? loc_error->message : _("No detail"));

				g_error_free (loc_error);
			}
			else {
				retpixbuf = gdk_pixbuf_from_pixdata (&pixdata, FALSE, &loc_error);
				if (!retpixbuf) {
					*stock = GTK_STOCK_DIALOG_ERROR;
					g_set_error (error, 0, 0,
						     _("Error while interpreting data as an image:\n%s"),
						     loc_error && loc_error->message ? loc_error->message : _("No detail"));
					g_error_free (loc_error);
				}
				else {
					/* scale resulting pixbuf */
					GdkPixbuf *tmp;
					gint width, height, w, h;
					width = gdk_pixbuf_get_width (retpixbuf);
					height = gdk_pixbuf_get_height (retpixbuf);
					compute_reduced_size (width, height, allocation, &w, &h);
					if ((w != width) || (h != height)) {
						tmp = gdk_pixbuf_scale_simple (retpixbuf, w, h, 
									       GDK_INTERP_BILINEAR);
						if (tmp) {
							g_object_unref (retpixbuf);
							retpixbuf = tmp;
						}
					}
				}
			}
		}
		else {
			GdkPixbufLoader *loader;
			GError *loc_error = NULL;
			
			loader = gdk_pixbuf_loader_new ();
			if (allocation)
				g_signal_connect (G_OBJECT (loader), "size-prepared",
						  G_CALLBACK (loader_size_prepared_cb), allocation);
			if (gdk_pixbuf_loader_write (loader, bindata->data, bindata->data_length, &loc_error) &&
			    gdk_pixbuf_loader_close (loader, &loc_error)) {
				retpixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
				if (!retpixbuf) {
					if (loc_error)
						g_propagate_error (error, loc_error);
					*stock = GTK_STOCK_MISSING_IMAGE;
				}
				else
					g_object_ref (retpixbuf);
			}
			else {
				gchar *notice_msg;
				notice_msg = g_strdup_printf (_("Error while interpreting data as an image:\n%s"),
							      loc_error && loc_error->message ? loc_error->message : _("No detail"));
				g_error_free (loc_error);
				*stock = GTK_STOCK_DIALOG_WARNING;
#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 18
				g_set_error (error, 0, 0, "%s", notice_msg);
#else
				g_set_error_literal (error, 0, 0, notice_msg);
#endif
				g_free (notice_msg);
			}
			
			g_object_unref (loader);
		}
	}

	return retpixbuf;
}

/*
 * Creates a new popup menu, attaches it to @ettach_to. The actions are in reference to @bindata,
 * and the @callback callback is called when the data in @bindata has been modified
 */
typedef struct {
	PictBinData  *bindata;
	PictOptions  *options;
	PictCallback  callback;
	gpointer      data;
} PictMenuData;
static void file_load_cb (GtkWidget *button, PictMenuData *menudata);
static void file_save_cb (GtkWidget *button, PictMenuData *menudata);

void
common_pict_create_menu (PictMenu *pictmenu, GtkWidget *attach_to, PictBinData *bindata, PictOptions *options,
			 PictCallback callback, gpointer data)
{
	GtkWidget *menu, *mitem;
	PictMenuData *menudata;
	
	menudata = g_new (PictMenuData, 1);
	menudata->bindata = bindata;
	menudata->options = options;
	menudata->callback = callback;
	menudata->data = data;
	
	menu = gtk_menu_new ();
	g_object_set_data_full (G_OBJECT (menu), "menudata", menudata, g_free);
	g_signal_connect (menu, "deactivate", 
			  G_CALLBACK (gtk_widget_hide), NULL);
	pictmenu->menu = menu;
		
	mitem = gtk_menu_item_new_with_mnemonic (_("_Load image from file"));
	gtk_widget_show (mitem);
	gtk_container_add (GTK_CONTAINER (menu), mitem);
	g_signal_connect (mitem, "activate",
			  G_CALLBACK (file_load_cb), menudata);
	pictmenu->load_mitem = mitem;
		
	mitem = gtk_menu_item_new_with_mnemonic (_("_Save image"));
	gtk_widget_show (mitem);
	gtk_container_add (GTK_CONTAINER (menu), mitem);
	g_signal_connect (mitem, "activate",
			  G_CALLBACK (file_save_cb), menudata);
	gtk_widget_set_sensitive (mitem, bindata->data ? TRUE : FALSE);
	pictmenu->save_mitem = mitem;

	gtk_menu_attach_to_widget (GTK_MENU (menu), attach_to, NULL);
}

static void
file_load_cb (GtkWidget *button, PictMenuData *menudata)
{
	GtkWidget *dlg;
	GtkFileFilter *filter;

	dlg = gtk_file_chooser_dialog_new (_("Select image to load"), 
					   GTK_WINDOW (gtk_widget_get_toplevel (button)),
					   GTK_FILE_CHOOSER_ACTION_OPEN, 
					   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					   GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					   NULL);
	filter = gtk_file_filter_new ();
	gtk_file_filter_add_pixbuf_formats (filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dlg), filter);
	
	if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		gsize length;
		GError *error = NULL;
		gchar *data;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dlg));

		if (g_file_get_contents (filename, &data, &length, &error)) {
			if (menudata->bindata->data) {
				g_free (menudata->bindata->data);
				menudata->bindata->data = NULL;
				menudata->bindata->data_length = 0;
			}

			if (menudata->options->serialize) {
				GdkPixdata pixdata;
				GdkPixbuf *pixbuf;
				guint stream_length;

				pixbuf = gdk_pixbuf_new_from_file (filename, &error);
				if (pixbuf) {
					gdk_pixdata_from_pixbuf (&pixdata, pixbuf, TRUE);
					menudata->bindata->data = gdk_pixdata_serialize (&pixdata, &stream_length);
					menudata->bindata->data_length = stream_length;

					g_object_unref (pixbuf);
					g_free (data);
				}
				else {
					menudata->bindata->data = (guchar *) data;
					menudata->bindata->data_length = length;
				}
			}
			else {
				menudata->bindata->data = (guchar *) data;
				menudata->bindata->data_length = length;
			}

			/* call the callback */
			if (menudata->callback)
				(menudata->callback) (menudata->data);
		}
		else {
			GtkWidget *msg;

			msg = gtk_message_dialog_new_with_markup (GTK_WINDOW (gtk_widget_get_toplevel (button)), 
								  GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
								  GTK_BUTTONS_CLOSE,
								  _("Could not load the contents of '%s':\n %s"), 
								  filename, 
								  error && error->message ? error->message : _("No detail"));
			if (error)
				g_error_free (error);
			gtk_widget_destroy (dlg);
			dlg = NULL;

			gtk_dialog_run (GTK_DIALOG (msg));
			gtk_widget_destroy (msg);
		}
		g_free (filename);
	}
	
	if (dlg)
		gtk_widget_destroy (dlg);
}

typedef struct {
	GtkComboBox *combo;
	GSList      *formats;
} PictFormat;

static void
add_if_writable (GdkPixbufFormat *data, PictFormat *format)
{
	if (gdk_pixbuf_format_is_writable (data)) {
		gchar *str;

		str= g_strdup_printf ("%s (%s)", gdk_pixbuf_format_get_name (data),
				      gdk_pixbuf_format_get_description (data));
		gtk_combo_box_append_text (format->combo, str);
		g_free (str);
		format->formats = g_slist_append (format->formats, g_strdup (gdk_pixbuf_format_get_name (data)));
	}
}

static void
file_save_cb (GtkWidget *button, PictMenuData *menudata)
{
	GtkWidget *dlg;
	GtkWidget *combo, *expander, *hbox, *label;
	GSList *formats;
	PictFormat pictformat;

	/* determine writable formats */
	expander = gtk_expander_new (_("Image format"));
	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (expander), hbox);

	label = gtk_label_new (_("Format image as:"));
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	combo = gtk_combo_box_new_text ();
	gtk_box_pack_start (GTK_BOX (hbox), combo, TRUE, TRUE, 0);
	gtk_widget_show_all (hbox);

	formats = gdk_pixbuf_get_formats ();
	pictformat.combo = (GtkComboBox*) combo;
	pictformat.formats = NULL;
	g_slist_foreach (formats, (GFunc) add_if_writable, &pictformat);
	g_slist_free (formats);

	gtk_combo_box_prepend_text (GTK_COMBO_BOX (combo), _("Current format"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

	dlg = gtk_file_chooser_dialog_new (_("Select a file to save the image to"), 
					   GTK_WINDOW (gtk_widget_get_toplevel (button)),
					   GTK_FILE_CHOOSER_ACTION_SAVE, 
					   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					   GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					   NULL);

	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dlg), expander);
	if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		gboolean allok = TRUE;
		GError *error = NULL;
		gint format;

		format = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dlg));

		if (format == 0) {
			/* save data AS IS */
			allok = g_file_set_contents (filename, (gchar *) menudata->bindata->data, 
						     menudata->bindata->data_length, &error);
		}
		else {
			/* export data to another format */
			GdkPixbuf *pixbuf;
			gchar *format_str;
			const gchar *stock;

			format_str = g_slist_nth_data (pictformat.formats, format - 1);
			pixbuf = common_pict_make_pixbuf (menudata->options, menudata->bindata, NULL, &stock, &error);
			if (pixbuf) {
				allok = gdk_pixbuf_save (pixbuf, filename, format_str, &error, NULL);
				g_object_unref (pixbuf);
			}
			else 
				allok = FALSE;
		}

		if (!allok) {
			GtkWidget *msg;
				
			msg = gtk_message_dialog_new_with_markup (GTK_WINDOW (gtk_widget_get_toplevel (button)), 
								  GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
								  GTK_BUTTONS_CLOSE,
								  _("Could not save the image to '%s':\n %s"), 
								  filename, 
								  error && error->message ? error->message : _("No detail"));
			if (error)
				g_error_free (error);
			gtk_widget_destroy (dlg);
			dlg = NULL;
			
			gtk_dialog_run (GTK_DIALOG (msg));
			gtk_widget_destroy (msg);
		}
		g_free (filename);
	}
	
	if (dlg)
		gtk_widget_destroy (dlg);

	g_slist_foreach (pictformat.formats, (GFunc) g_free, NULL);
	g_slist_free (pictformat.formats);
}

/* 
 * adjust the sensitiveness of the menu items in the popup menu
 */
void
common_pict_adjust_menu_sensitiveness (PictMenu *pictmenu, gboolean editable, PictBinData *bindata)
{
	if (!pictmenu || !pictmenu->menu)
		return;
	gtk_widget_set_sensitive (pictmenu->load_mitem, editable);
	gtk_widget_set_sensitive (pictmenu->save_mitem, bindata->data ? TRUE : FALSE);
}

/*
 * Inits the hash table in @options
 */
void
common_pict_init_cache (PictOptions *options)
{
	g_assert (!options->pixbuf_hash);
	options->pixbuf_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

/* 
 * computes a "hash" of the binary data 
 */
static guint
compute_hash (guchar *data, glong data_length)
{
	guint result = 0;
	guchar *ptr;

	if (!data)
		return 0;
	for (ptr = data; ptr <= data + data_length - 1; ptr++)
		result += *ptr;
	
	return result;
}

/*
 * Adds @pixbuf in the cache
 */
void
common_pict_add_cached_pixbuf (PictOptions *options, const GValue *value, GdkPixbuf *pixbuf)
{
	guint hash;
	g_return_if_fail (pixbuf);

	if (!options->pixbuf_hash || !value)
		return;

	else if (GDA_VALUE_HOLDS_BINARY (value)) {
		const GdaBinary *bin;
		bin = gda_value_get_binary (value);
		hash = compute_hash (bin->data, bin->binary_length);
		g_hash_table_insert (options->pixbuf_hash, GUINT_TO_POINTER (hash), pixbuf);
		g_object_ref (pixbuf);
	}
	else if (GDA_VALUE_HOLDS_BLOB (value)) {
		const GdaBinary *bin;
		const GdaBlob *blob;
		blob = gda_value_get_blob (value);
		bin = (GdaBinary *) blob;
		if (bin) {
			if (!bin->data && blob->op)
				gda_blob_op_read_all (blob->op, (GdaBlob*) blob);
			hash = compute_hash (bin->data, bin->binary_length);
			g_hash_table_insert (options->pixbuf_hash, GUINT_TO_POINTER (hash), pixbuf);
			g_object_ref (pixbuf);
		}
	}
}

/*
 * Tries to find a cached pixbuf
 */
GdkPixbuf *
common_pict_fetch_cached_pixbuf (PictOptions *options, const GValue *value)
{
	GdkPixbuf *pixbuf = NULL;
	guint hash;

	if (!options->pixbuf_hash)
		return NULL;
	if (!value)
		return NULL;
	else if (GDA_VALUE_HOLDS_BINARY (value)) {
		const GdaBinary *bin;
		bin = gda_value_get_binary (value);
		if (bin) {
			hash = compute_hash (bin->data, bin->binary_length);
			pixbuf = g_hash_table_lookup (options->pixbuf_hash, GUINT_TO_POINTER (hash));
		}
	}
	else if (GDA_VALUE_HOLDS_BLOB (value)) {
		const GdaBinary *bin;
		const GdaBlob *blob;
		blob = gda_value_get_blob (value);
		bin = (GdaBinary *) blob;
		if (bin) {
			if (!bin->data && blob->op)
				gda_blob_op_read_all (blob->op, (GdaBlob*) blob);
			hash = compute_hash (bin->data, bin->binary_length);
			pixbuf = g_hash_table_lookup (options->pixbuf_hash, GUINT_TO_POINTER (hash));
		}
	}

	return pixbuf;
}

/*
 * clears all the cached pixbuf objects
 */
void
common_pict_clear_pixbuf_cache (PictOptions *options)
{
	if (!options->pixbuf_hash)
		return;
	g_hash_table_remove_all (options->pixbuf_hash);
}

/*
 * Fills @options with the correct values parsed from @options_str
 */
void
common_pict_parse_options (PictOptions *options, const gchar *options_str)
{
	if (options_str && *options_str) {
		GdaQuarkList *params;
		const gchar *str;

		params = gda_quark_list_new_from_string (options_str);
		str = gda_quark_list_find (params, "ENCODING");
		if (str) {
			if (!strcmp (str, "base64")) 
				options->encoding = ENCODING_BASE64;
		}
		str = gda_quark_list_find (params, "SERIALIZE");
		if (str) {
			if ((*str == 't') || (*str == 'T'))
				options->serialize = TRUE;
		}
		gda_quark_list_free (params);
	}
}

/*
 * Creates a new GValue from the data in @bindata, using @options, of type @gtype
 */
GValue *
common_pict_get_value (PictBinData *bindata, PictOptions *options, GType gtype)
{
	GValue *value = NULL;

	if (bindata->data) {
		if (gtype == GDA_TYPE_BLOB) 
			value = gda_value_new_blob (bindata->data, bindata->data_length);
		else if (gtype == GDA_TYPE_BINARY) 
			value = gda_value_new_binary (bindata->data, bindata->data_length);
		else if (gtype == G_TYPE_STRING) {
			gchar *str = NULL;

			switch (options->encoding) {
			case ENCODING_NONE:
				str = g_strndup ((gchar *) bindata->data, 
						 bindata->data_length);
				break;
			case ENCODING_BASE64: 
#if (GLIB_MINOR_VERSION >= 12)
				str = g_base64_encode (bindata->data, bindata->data_length);
#else
				g_warning ("Base64 enoding/decoding is not supported in the GLib version %d.%d.%d",
					   glib_major_version, glib_minor_version, glib_micro_version);
#endif
				break;
			}
			
			value = gda_value_new (G_TYPE_STRING);
			g_value_take_string (value, str);
		}
		else
			g_assert_not_reached ();
	}

	if (!value) {
		/* in case the gda_data_handler_get_value_from_sql() returned an error because
		   the contents of the GtkEntry cannot be interpreted as a GValue */
		value = gda_value_new_null ();
	}

	return value;
}