/* 
 * Copyright (C) 2009 Vivien Malerba
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <glib/gi18n-lib.h>
#include <string.h>
#include <libgda/binreloc/gda-binreloc.h>
#include "auth-dialog.h"
#include "browser-spinner.h"
#include "support.h"
#include <libgda/thread-wrapper/gda-thread-wrapper.h>

/* 
 * Main static functions 
 */
static void auth_dialog_class_init (AuthDialogClass * class);
static void auth_dialog_init (AuthDialog *dialog);
static void auth_dialog_dispose (GObject *object);

/* get a pointer to the parents to be able to call their destructor */
static GObjectClass  *parent_class = NULL;

typedef struct {
	AuthDialogConnection ext;
	GdaDsnInfo  cncinfo;
	GtkWidget  *auth_widget;
	GString    *auth_string;

	GdaThreadWrapper *wrapper;
	guint jobid;
} AuthData;

static void
auth_data_free (AuthData *ad)
{
	g_free (ad->cncinfo.name);
	g_free (ad->cncinfo.description);
	g_free (ad->cncinfo.provider);
	g_free (ad->cncinfo.cnc_string);
	g_free (ad->cncinfo.auth_string);
	g_free (ad->ext.cnc_string);
	if (ad->auth_string)
		g_string_free (ad->auth_string, TRUE);

	g_object_unref (ad->wrapper);
	if (ad->ext.cnc_open_error)
		g_error_free (ad->ext.cnc_open_error);
	if (ad->ext.cnc)
		g_object_unref (ad->ext.cnc);
	g_free (ad);
}

struct _AuthDialogPrivate
{
	GSList    *auth_list; /* list of AuthData pointers */
	GtkWidget *spinner;
	guint      source_id; /* timer to check if connections have been opened */
	GMainLoop *loop; /* waiting loop */
};

/* module error */
GQuark auth_dialog_error_quark (void)
{
        static GQuark quark;
        if (!quark)
                quark = g_quark_from_static_string ("auth_dialog_error");
        return quark;
}

GType
auth_dialog_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GStaticMutex registering = G_STATIC_MUTEX_INIT;
		static const GTypeInfo info = {
			sizeof (AuthDialogClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) auth_dialog_class_init,
			NULL,
			NULL,
			sizeof (AuthDialog),
			0,
			(GInstanceInitFunc) auth_dialog_init
		};
		
		g_static_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (GTK_TYPE_DIALOG, "AuthDialog", &info, 0);
		g_static_mutex_unlock (&registering);
	}

	return type;
}


static void
auth_dialog_class_init (AuthDialogClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	/* virtual functions */
	object_class->dispose = auth_dialog_dispose;
}

/*
static void
auth_contents_changed_cb (GdauiAuth *auth, gboolean is_valid, AuthDialog *dialog)
{
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT, is_valid);
	if (is_valid)
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
}
*/

static void
auth_dialog_init (AuthDialog *dialog)
{
	GtkWidget *label, *hbox, *wid;
	char *markup, *str;
	GtkWidget *dcontents;

	dialog->priv = g_new0 (AuthDialogPrivate, 1);

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CONNECT,
				GTK_RESPONSE_ACCEPT,
				GTK_STOCK_CANCEL,
				GTK_RESPONSE_REJECT, NULL);

#if GTK_CHECK_VERSION(2,18,0)
	dcontents = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
#else
	dcontents = GTK_DIALOG (dialog)->vbox;
#endif
	gtk_box_set_spacing (GTK_BOX (dcontents), 5);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT, TRUE);

	str = gda_gbr_get_file_path (GDA_DATA_DIR, LIBGDA_ABI_NAME, "pixmaps", "gda-browser-auth.png", NULL);
	gtk_window_set_icon_from_file (GTK_WINDOW (dialog), str, NULL);
	g_free (str);

	/* label and spinner */
	hbox = gtk_hbox_new (FALSE, 0); 
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 10);
	gtk_box_pack_start (GTK_BOX (dcontents), hbox, FALSE, FALSE, 0);
	
	str = gda_gbr_get_file_path (GDA_DATA_DIR, LIBGDA_ABI_NAME, "pixmaps", "gda-browser-auth-big.png", NULL);
	wid = gtk_image_new_from_file (str);
	g_free (str);
	gtk_box_pack_start (GTK_BOX (hbox), wid, FALSE, FALSE, 0);

	label = gtk_label_new ("");
	markup = g_markup_printf_escaped ("<big><b>%s\n</b></big>\n",
					  _("Connection opening"));
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);
	gtk_misc_set_alignment (GTK_MISC (label), 0., -1);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 12);
	gtk_widget_show_all (hbox);
	
	dialog->priv->spinner = browser_spinner_new ();
	gtk_container_add (GTK_CONTAINER (hbox), dialog->priv->spinner);

}

static void
auth_dialog_dispose (GObject *object)
{
	AuthDialog *dialog;
	dialog = AUTH_DIALOG (object);
	if (dialog->priv) {
		if (dialog->priv->auth_list) {
			g_slist_foreach (dialog->priv->auth_list, (GFunc) auth_data_free, NULL);
			g_slist_free (dialog->priv->auth_list);
		}
		if (dialog->priv->source_id)
			g_source_remove (dialog->priv->source_id);
		if (dialog->priv->loop)
			g_main_loop_quit (dialog->priv->loop);
		g_free (dialog->priv);
		dialog->priv = NULL;
	}

	/* parent class */
        parent_class->dispose (object);
}

/**
 * auth_dialog_new
 *
 * Creates a new dialog dialog
 *
 * Returns: a new #AuthDialog object
 */
AuthDialog *
auth_dialog_new (GtkWindow *parent)
{
	return (AuthDialog*) g_object_new (AUTH_TYPE_DIALOG, "title", _("Authentication"),
					   "transient-for", parent,
					   "resizable", FALSE,
					   "border-width", 10, 
					   "has-separator", FALSE, NULL);
}

/*
 * executed in a sub thread
 */
static GdaConnection *
sub_thread_open_cnc (AuthData *ad, GError **error)
{
#ifndef DUMMY
	GdaConnection *cnc;
	GdaDsnInfo *info = &(ad->cncinfo);
	if (info->name)
		cnc = gda_connection_open_from_dsn (info->name, ad->auth_string ? ad->auth_string->str : NULL,
						    GDA_CONNECTION_OPTIONS_THREAD_SAFE |
						    GDA_CONNECTION_OPTIONS_AUTO_META_DATA,
						    error);
	else
		cnc = gda_connection_open_from_string (info->provider, info->cnc_string,
						       ad->auth_string ? ad->auth_string->str : NULL,
						       GDA_CONNECTION_OPTIONS_THREAD_SAFE |
						       GDA_CONNECTION_OPTIONS_AUTO_META_DATA,
						       error);
	return cnc;
#else
	sleep (5);
	g_set_error (error, 0, 0, "Oooo");
	return NULL;
#endif
}

static gboolean
check_for_cnc (AuthDialog *dialog)
{
	GSList *list;
	gboolean finished = TRUE;
	for (list = dialog->priv->auth_list; list; list = list->next) {
		AuthData *ad = (AuthData*) list->data;

		if (ad->jobid) {
			GError *lerror = NULL;
			ad->ext.cnc = gda_thread_wrapper_fetch_result (ad->wrapper, FALSE, ad->jobid, &lerror);
			if (ad->ext.cnc || (!ad->ext.cnc && lerror)) {
				/* waiting is finished! */
				if (ad->ext.cnc)
					g_object_set (ad->ext.cnc, "monitor-wrapped-in-mainloop", TRUE, NULL);
				if (lerror)
					ad->ext.cnc_open_error = lerror;
				ad->jobid = 0;
			}
			else
				finished = FALSE;
		}
	}

	if (finished) {
		dialog->priv->source_id = 0;
		if (dialog->priv->loop)
			g_main_loop_quit (dialog->priv->loop);
	}
	return !finished;
}

/**
 * auth_dialog_add_cnc_string
 */
gboolean
auth_dialog_add_cnc_string (AuthDialog *dialog, const gchar *cnc_string, GError **error)
{
	g_return_val_if_fail (AUTH_IS_DIALOG (dialog), FALSE);
	g_return_val_if_fail (cnc_string, FALSE);

	gchar *real_cnc_string;
	GdaDsnInfo *info;
        gchar *user, *pass, *real_cnc, *real_provider, *real_auth_string = NULL;

	/* if cnc string is a regular file, then use it with SQLite */
        if (g_file_test (cnc_string, G_FILE_TEST_IS_REGULAR)) {
                gchar *path, *file, *e1, *e2;
                const gchar *pname = "SQLite";

                path = g_path_get_dirname (cnc_string);
                file = g_path_get_basename (cnc_string);
                if (g_str_has_suffix (file, ".mdb")) {
                        pname = "MSAccess";
                        file [strlen (file) - 4] = 0;
                }
                else if (g_str_has_suffix (file, ".db"))
                        file [strlen (file) - 3] = 0;
                e1 = gda_rfc1738_encode (path);
                e2 = gda_rfc1738_encode (file);
                g_free (path);
                g_free (file);
                real_cnc_string = g_strdup_printf ("%s://DB_DIR=%s;LOAD_GDA_FUNCTIONS=TRUE;DB_NAME=%s", pname, e1, e2);
                g_free (e1);
                g_free (e2);
                gda_connection_string_split (real_cnc_string, &real_cnc, &real_provider, &user, &pass);
        }
        else {
                gda_connection_string_split (cnc_string, &real_cnc, &real_provider, &user, &pass);
                real_cnc_string = g_strdup (cnc_string);
        }
        if (!real_cnc) {
                g_free (user);
                g_free (pass);
                g_free (real_provider);
                g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_DSN_NOT_FOUND_ERROR,
                             _("Malformed connection string '%s'"), cnc_string);
                g_free (real_cnc_string);
                return FALSE;
        }

	AuthData *ad;
	ad = g_new0 (AuthData, 1);
	ad->wrapper = gda_thread_wrapper_new ();
	ad->ext.cnc_string = g_strdup (cnc_string);
	ad->auth_string = NULL;
	info = gda_config_get_dsn_info (real_cnc);
        if (info && !real_provider) {
		ad->cncinfo.name = g_strdup (info->name);
		ad->cncinfo.provider = g_strdup (info->provider);
		if (info->description)
			ad->cncinfo.description = g_strdup (info->description);
		if (info->cnc_string)
			ad->cncinfo.cnc_string = g_strdup (info->cnc_string);
		if (info->auth_string)
			ad->cncinfo.auth_string = g_strdup (info->auth_string);
	}
	else {
		ad->cncinfo.name = NULL;
		ad->cncinfo.provider = real_provider;
		real_provider = NULL;
		ad->cncinfo.cnc_string = real_cnc;
		real_cnc = NULL;
		ad->cncinfo.auth_string = real_auth_string;
		real_auth_string = NULL;
	}

	if (! ad->cncinfo.provider) {
		g_free (user);
                g_free (pass);
                g_free (real_provider);
                g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_DSN_NOT_FOUND_ERROR,
                             _("Malformed connection string '%s'"), cnc_string);
                g_free (real_cnc_string);
		auth_data_free (ad);
		return FALSE;
	}

	if (user || pass) {
		gchar *s1;
		s1 = gda_rfc1738_encode (user);
		if (pass) {
			gchar *s2;
			s2 = gda_rfc1738_encode (pass);
			real_auth_string = g_strdup_printf ("USERNAME=%s;PASSWORD=%s", s1, s2);
			g_free (s2);
		}
		else
			real_auth_string = g_strdup_printf ("USERNAME=%s", s1);
		g_free (s1);
	}
	if (real_auth_string) {
		if (ad->cncinfo.auth_string)
			g_free (ad->cncinfo.auth_string);
		ad->cncinfo.auth_string = real_auth_string;
		real_auth_string = NULL;
	}
	

	dialog->priv->auth_list = g_slist_append (dialog->priv->auth_list, ad);

	/* build widget */
	gboolean auth_needed = FALSE;
	GdaProviderInfo *pinfo;
	pinfo = gda_config_get_provider_info (ad->cncinfo.provider);
	if (pinfo && pinfo->auth_params && pinfo->auth_params->holders)
		auth_needed = TRUE;
	if (auth_needed) {
		GdaSet *set;

                set = gda_set_copy (pinfo->auth_params);
                ad->auth_widget = gdaui_basic_form_new (set);
                /*g_signal_connect (G_OBJECT (ad->auth_widget), "holder-changed",
		  G_CALLBACK (auth_form_changed), dialog);*/
                g_object_unref (set);

		/* add widget */
		GtkWidget *hbox, *label;
		gchar *str, *tmp, *ptr;
		GtkWidget *dcontents;

#if GTK_CHECK_VERSION(2,18,0)
		dcontents = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
#else
		dcontents = GTK_DIALOG (dialog)->vbox;
#endif

		label = gtk_label_new ("");
		tmp = g_strdup (ad->ext.cnc_string);
		for (ptr = tmp; *ptr; ptr++) {
			if (*ptr == ':') {
				/* remove everything up to the '@' */
				gchar *ptr2;
				for (ptr2 = ptr+1; *ptr2; ptr2++) {
					if (*ptr2 == '@') {
						memmove (ptr, ptr2, strlen (ptr2) + 1);
						break;
					}
				}
				break;
			}
		}
		str = g_strdup_printf ("<b>%s: %s</b>\n%s", _("For connection"), tmp,
				       _("enter authentication information"));
		g_free (tmp);
		gtk_label_set_markup (GTK_LABEL (label), str);
		gtk_misc_set_alignment (GTK_MISC (label), 0., -1);
		g_free (str);
		gtk_box_pack_start (GTK_BOX (dcontents), label, FALSE, FALSE, 0);
		gtk_widget_show (label);

		hbox = gtk_hbox_new (FALSE, 0); /* HIG */
		gtk_box_pack_start (GTK_BOX (dcontents), hbox, TRUE, TRUE, 0);
		label = gtk_label_new ("      ");
		gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (hbox), ad->auth_widget, TRUE, TRUE, 0);
		gtk_widget_show_all (hbox);

		/* set values */
		if (ad->cncinfo.auth_string) {
			/* split array in a list of named parameters, and for each parameter value, 
			 * set the correcponding parameter in @dset */
			GdaSet *dset;
			
			dset = gdaui_basic_form_get_data_set (GDAUI_BASIC_FORM (ad->auth_widget));
			gchar **array = NULL;
			array = g_strsplit (ad->cncinfo.auth_string, ";", 0);
			if (array) {
				gint index = 0;
				gchar *tok;
				gchar *value;
				gchar *name;
				
				for (index = 0; array[index]; index++) {
					name = strtok_r (array [index], "=", &tok);
					if (name)
						value = strtok_r (NULL, "=", &tok);
					else
						value = NULL;
					if (name && value) {
						GdaHolder *param;
						gda_rfc1738_decode (name);
						gda_rfc1738_decode (value);
						
						param = gda_set_get_holder (dset, name);
						if (param)
							g_assert (gda_holder_set_value_str (param, NULL, value, NULL));
					}
				}
				
				g_strfreev (array);
			}
		}
	}
	else {
		/* open connection right away */
		ad->jobid = gda_thread_wrapper_execute (ad->wrapper,
							(GdaThreadWrapperFunc) sub_thread_open_cnc,
							(gpointer) ad,
							(GDestroyNotify) NULL,
							&(ad->ext.cnc_open_error));
		if (dialog->priv->source_id == 0) {
			dialog->priv->source_id = g_timeout_add (200, (GSourceFunc) check_for_cnc, dialog);
		}
	}
	
	g_free (real_cnc_string);
        g_free (real_cnc);
        g_free (user);
        g_free (pass);
        g_free (real_provider);
        g_free (real_auth_string);

	return TRUE;
}

/**
 * auth_dialog_run
 * @dialog: a #GdaAuth object
 * @retry: if set to %TRUE, then this method returns only when either a connection has been opened or the
 *         user gave up
 * @error: a place to store errors, or %NULL
 *
 * Displays the dialog and let the user open some connections. this function returns either only when
 * all the connections have been opened, or when the user cancelled.
 *
 * Return: %TRUE if all the connections have been opened, and %FALSE if the user cancelled.
 */
gboolean
auth_dialog_run (AuthDialog *dialog)
{
	gboolean allopened = FALSE;

	g_return_val_if_fail (AUTH_IS_DIALOG (dialog), FALSE);

	gtk_widget_show (GTK_WIDGET (dialog));
	
	while (1) {
		gint result;
		GSList *list;
		gboolean needs_running = FALSE;

		/* determine if we need to run the dialog */
		for (list = dialog->priv->auth_list; list; list = list->next) {
			AuthData *ad;
			ad = (AuthData *) list->data;
			if (ad->auth_widget) {
				needs_running = TRUE;
				break;
			}
		}
		
		if (needs_running)
			result = gtk_dialog_run (GTK_DIALOG (dialog));
		else
			result = GTK_RESPONSE_ACCEPT;

		gtk_widget_show (dialog->priv->spinner);
		browser_spinner_start (BROWSER_SPINNER (dialog->priv->spinner));

		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT, FALSE);
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_REJECT, FALSE);
		
		if (result == GTK_RESPONSE_ACCEPT) {
			for (list = dialog->priv->auth_list; list; list = list->next) {
				AuthData *ad;
				ad = (AuthData *) list->data;
				if (ad->auth_widget && !ad->jobid) {
					GSList *plist;
					GdaSet *set;
					set = gdaui_basic_form_get_data_set (GDAUI_BASIC_FORM (ad->auth_widget));
					if (ad->auth_string) {
						g_string_free (ad->auth_string, TRUE);
						ad->auth_string = NULL;
					}
					for (plist = set ? set->holders : NULL;
					     plist; plist = plist->next) {
						GdaHolder *holder = GDA_HOLDER (plist->data);
						const GValue *cvalue = NULL;
						if (gda_holder_is_valid (holder))
							cvalue = gda_holder_get_value (holder);
						if (cvalue) {
							gchar *r1, *r2;
							r1 = gda_value_stringify (cvalue);
							r2 = gda_rfc1738_encode (r1);
							g_free (r1);
							if (r2) {
								r1 = gda_rfc1738_encode (gda_holder_get_id (holder));
								if (ad->auth_string)
									g_string_append_c (ad->auth_string, ';');
								else
									ad->auth_string = g_string_new ("");
								g_string_append (ad->auth_string, r1);
								g_string_append_c (ad->auth_string, '=');
								g_string_append (ad->auth_string, r2);
							
								g_free (r1);
								g_free (r2);
							}
						}
					}
					gtk_widget_set_sensitive (ad->auth_widget, FALSE);
					ad->jobid = gda_thread_wrapper_execute (ad->wrapper,
										(GdaThreadWrapperFunc) sub_thread_open_cnc,
										(gpointer) ad,
										(GDestroyNotify) NULL,
										&(ad->ext.cnc_open_error));
					if (dialog->priv->source_id == 0) {
						dialog->priv->source_id = 
							g_timeout_add (200, (GSourceFunc) check_for_cnc, dialog);
					}
				}
			}

			if (dialog->priv->source_id != 0) {
				dialog->priv->loop = g_main_loop_new (NULL, FALSE);
				g_main_loop_run (dialog->priv->loop);
				g_main_loop_unref (dialog->priv->loop);
				dialog->priv->loop = NULL;
			}

			allopened = TRUE;
			for (list = dialog->priv->auth_list; list; list = list->next) {
				AuthData *ad;
				
				ad = (AuthData *) list->data;
				if (ad->auth_widget && !ad->ext.cnc) {
					g_print ("ERROR: %s\n", ad->ext.cnc_open_error && ad->ext.cnc_open_error->message ? 
						 ad->ext.cnc_open_error->message : _("No detail"));
					browser_show_error (GTK_WINDOW (dialog), _("Could not open connection:\n%s"),
							    ad->ext.cnc_open_error && ad->ext.cnc_open_error->message ? 
							    ad->ext.cnc_open_error->message : _("No detail"));
					allopened = FALSE;
					gtk_widget_set_sensitive (ad->auth_widget, TRUE);
				}
			}
			if (allopened)
				goto out;
		}
		else {
			/* cancelled connection opening */
			goto out;
		}
		
		browser_spinner_stop (BROWSER_SPINNER (dialog->priv->spinner));
		gtk_widget_hide (dialog->priv->spinner);
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT, TRUE);
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_REJECT, TRUE);
	}

 out:
	gtk_widget_hide (GTK_WIDGET (dialog));
	return allopened;
}


/**
 * auth_dialog_get_connections
 *
 * Returns: a list of pointers to AuthDialogConnection structures.
 */
const GSList *
auth_dialog_get_connections (AuthDialog *dialog)
{
	g_return_val_if_fail (AUTH_IS_DIALOG (dialog), NULL);
	return dialog->priv->auth_list;
}