/*
 * Copyright (C) 2011 The GNOME Foundation
 *
 * AUTHORS:
 *      Vivien Malerba <malerba@gnome-db.org>
 *
 * This Program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
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
#include "filter-editor.h"
#include <libgda-ui/gdaui-combo.h>
#include <libgda-ui/gdaui-data-selector.h>

struct _FilterEditorPrivate {
	BrowserConnection *bcnc;
	GtkWidget *base_dn;
	GtkWidget *filter;
	GtkWidget *attributes;
	GtkWidget *scope;

	GdaLdapSearchScope default_scope;
};

static void filter_editor_class_init (FilterEditorClass *klass);
static void filter_editor_init       (FilterEditor *feditor, FilterEditorClass *klass);
static void filter_editor_dispose    (GObject *object);

static GObjectClass *parent_class = NULL;

/* signals */
enum {
        ACTIVATE,
	LAST_SIGNAL
};

gint filter_editor_signals [LAST_SIGNAL] = { 0 };

/*
 * FilterEditor class implementation
 */

static void
filter_editor_class_init (FilterEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	filter_editor_signals [ACTIVATE] =
                g_signal_new ("activate",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (FilterEditorClass, activate),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
	klass->activate = NULL;
	object_class->dispose = filter_editor_dispose;
}

static void
filter_editor_init (FilterEditor *feditor, G_GNUC_UNUSED FilterEditorClass *klass)
{
	feditor->priv = g_new0 (FilterEditorPrivate, 1);
	feditor->priv->bcnc = NULL;
	feditor->priv->default_scope = GDA_LDAP_SEARCH_SUBTREE;
}

static void
filter_editor_dispose (GObject *object)
{
	FilterEditor *feditor = (FilterEditor *) object;

	/* free memory */
	if (feditor->priv) {
		if (feditor->priv->bcnc)
			g_object_unref (feditor->priv->bcnc);
		g_free (feditor->priv);
		feditor->priv = NULL;
	}

	parent_class->dispose (object);
}

GType
filter_editor_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo info = {
			sizeof (FilterEditorClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) filter_editor_class_init,
			NULL,
			NULL,
			sizeof (FilterEditor),
			0,
			(GInstanceInitFunc) filter_editor_init,
			0
		};

		type = g_type_register_static (GTK_TYPE_VBOX, "FilterEditor", &info, 0);
	}
	return type;
}

static void
activated_cb (GtkEntry *entry, FilterEditor *feditor)
{
	g_signal_emit (feditor, filter_editor_signals [ACTIVATE], 0);
}

/**
 * filter_editor_new:
 *
 * Returns: a new #GtkWidget
 */
GtkWidget *
filter_editor_new (BrowserConnection *bcnc)
{
	FilterEditor *feditor;
	GtkWidget *table, *label, *entry;
	GdaDataModel *model;
	GList *values;
	GValue *v1, *v2;
	gfloat ya;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	feditor = FILTER_EDITOR (g_object_new (FILTER_EDITOR_TYPE, NULL));
	feditor->priv->bcnc = g_object_ref ((GObject*) bcnc);

	table = gtk_table_new (4, 2, FALSE);
	gtk_table_set_col_spacing (GTK_TABLE (table), 0, 5);
	gtk_box_pack_start (GTK_BOX (feditor), table, TRUE, TRUE, 0);
	
	label = gtk_label_new (_("Base DN:"));
	gtk_misc_get_alignment (GTK_MISC (label), NULL, &ya);
	gtk_misc_set_alignment (GTK_MISC (label), 0., ya);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
	label = gtk_label_new (_("Filter expression:"));
	gtk_misc_get_alignment (GTK_MISC (label), NULL, &ya);
	gtk_misc_set_alignment (GTK_MISC (label), 0., ya);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, GTK_FILL, GTK_SHRINK, 0, 0);
	label = gtk_label_new (_("Attributes to fetch:"));
	gtk_misc_get_alignment (GTK_MISC (label), NULL, &ya);
	gtk_misc_set_alignment (GTK_MISC (label), 0., ya);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3, GTK_FILL, GTK_SHRINK, 0, 0);
	label = gtk_label_new (_("Search scope:"));
	gtk_misc_get_alignment (GTK_MISC (label), NULL, &ya);
	gtk_misc_set_alignment (GTK_MISC (label), 0., ya);
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4, GTK_FILL, GTK_SHRINK, 0, 0);
	
	entry = gtk_entry_new ();
	gtk_table_attach_defaults (GTK_TABLE (table), entry, 1, 2, 0, 1);
	feditor->priv->base_dn = entry;
	g_signal_connect (entry, "activate",
			  G_CALLBACK (activated_cb), feditor);

	entry = gtk_entry_new ();
	gtk_table_attach_defaults (GTK_TABLE (table), entry, 1, 2, 1, 2);
	feditor->priv->filter = entry;
	g_signal_connect (entry, "activate",
			  G_CALLBACK (activated_cb), feditor);

	entry = gtk_entry_new ();
	gtk_table_attach_defaults (GTK_TABLE (table), entry, 1, 2, 2, 3);
	feditor->priv->attributes = entry;
	g_signal_connect (entry, "activate",
			  G_CALLBACK (activated_cb), feditor);

	model = gda_data_model_array_new_with_g_types (2, G_TYPE_INT, G_TYPE_STRING);
	g_value_set_string ((v1 = gda_value_new (G_TYPE_STRING)), "Base (search the base DN only)");
	values = g_list_prepend (NULL, v1);
	g_value_set_int ((v2 = gda_value_new (G_TYPE_INT)), GDA_LDAP_SEARCH_BASE);
	values = g_list_prepend (values, v2);
	g_assert (gda_data_model_append_values (model, values, NULL) >= 0);
	gda_value_free (v1);
	gda_value_free (v2);

	g_value_set_string ((v1 = gda_value_new (G_TYPE_STRING)), "Onelevel (search immediate children of base DN only)");
	values = g_list_prepend (NULL, v1);
	g_value_set_int ((v2 = gda_value_new (G_TYPE_INT)), GDA_LDAP_SEARCH_ONELEVEL);
	values = g_list_prepend (values, v2);
	g_assert (gda_data_model_append_values (model, values, NULL) >= 0);
	gda_value_free (v1);
	gda_value_free (v2);

	g_value_set_string ((v1 = gda_value_new (G_TYPE_STRING)), "Subtree (search of the base DN and the entire subtree below)");
	values = g_list_prepend (NULL, v1);
	g_value_set_int ((v2 = gda_value_new (G_TYPE_INT)), GDA_LDAP_SEARCH_SUBTREE);
	values = g_list_prepend (values, v2);
	g_assert (gda_data_model_append_values (model, values, NULL) >= 0);
	gda_value_free (v1);
	gda_value_free (v2);

	gint cols[] = {1};
	entry = gdaui_combo_new_with_model (model, 1, cols);
	g_object_unref (model);
	gtk_table_attach_defaults (GTK_TABLE (table), entry, 1, 2, 3, 4);
	feditor->priv->scope = entry;
	filter_editor_clear (feditor);

	gtk_widget_show_all (table);
	return (GtkWidget*) feditor;
}

/**
 * filter_editor_clear:
 */
void
filter_editor_clear (FilterEditor *fedit)
{
	g_return_if_fail (IS_FILTER_EDITOR (fedit));
	filter_editor_set_settings (fedit, NULL, NULL, NULL, fedit->priv->default_scope);
}

/**
 * filter_editor_set_settings:
 */
void
filter_editor_set_settings (FilterEditor *fedit,
			    const gchar *base_dn, const gchar *filter,
			    const gchar *attributes, GdaLdapSearchScope scope)
{
	g_return_if_fail (IS_FILTER_EDITOR (fedit));

	gtk_entry_set_text (GTK_ENTRY (fedit->priv->base_dn), base_dn ? base_dn : "");
	gtk_entry_set_text (GTK_ENTRY (fedit->priv->filter), filter ? filter : "");
	gtk_entry_set_text (GTK_ENTRY (fedit->priv->attributes), attributes ? attributes : "");
	gdaui_data_selector_select_row (GDAUI_DATA_SELECTOR (fedit->priv->scope), scope - 1);
}

/**
 * filter_editor_get_settings:
 */
void
filter_editor_get_settings (FilterEditor *fedit,
			    gchar **out_base_dn, gchar **out_filter,
			    gchar **out_attributes, GdaLdapSearchScope *out_scope)
{
	const gchar *tmp;
	g_return_if_fail (IS_FILTER_EDITOR (fedit));
	if (out_base_dn) {
		tmp = gtk_entry_get_text (GTK_ENTRY (fedit->priv->base_dn));
		*out_base_dn = tmp && *tmp ? g_strdup (tmp) : NULL;
	}
	if (out_filter) {
		tmp = gtk_entry_get_text (GTK_ENTRY (fedit->priv->filter));
		if (tmp && *tmp) {
			/* add surrounding parenthesis if not yet there */
			if (*tmp != '(') {
				gint len;
				len = strlen (tmp);
				if (tmp [len-1] != ')')
					*out_filter = g_strdup_printf ("(%s)", tmp);
				else
					*out_filter = g_strdup (tmp);/* may result in an error when executed */	
			}
			else
				*out_filter = g_strdup (tmp);

		}
		else
			*out_filter = NULL;
	}
	if (out_attributes) {
		tmp = gtk_entry_get_text (GTK_ENTRY (fedit->priv->attributes));
		*out_attributes = tmp && *tmp ? g_strdup (tmp) : NULL;
	}
	if (out_scope)
		*out_scope = gtk_combo_box_get_active (GTK_COMBO_BOX (fedit->priv->scope)) + 1;
}