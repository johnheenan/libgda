/*
 * Copyright (C) 2009 The GNOME Foundation.
 *
 * AUTHORS:
 *      Vivien Malerba <malerba@gnome-db.org>
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
#include <libgda/libgda.h>
#include <sql-parser/gda-sql-parser.h>
#include "mgr-favorites.h"
#include "support.h"

struct _MgrFavoritesPriv {
	BrowserConnection    *bcnc;
	BrowserFavoritesType  fav_type;
	gint                  order_key;
};

static void mgr_favorites_class_init (MgrFavoritesClass *klass);
static void mgr_favorites_init       (MgrFavorites *tmgr1, MgrFavoritesClass *klass);
static void mgr_favorites_dispose    (GObject *object);
static void mgr_favorites_set_property (GObject *object,
					guint param_id,
					const GValue *value,
					GParamSpec *pspec);
static void mgr_favorites_get_property (GObject *object,
					guint param_id,
					GValue *value,
					GParamSpec *pspec);

/* virtual methods */
static GSList *mgr_favorites_update_children (GdaTreeManager *manager, GdaTreeNode *node, 
					    const GSList *children_nodes, gboolean *out_error, GError **error);

static GObjectClass *parent_class = NULL;

/* properties */
enum {
        PROP_0,
	PROP_BROWSER_CNC
};

/*
 * MgrFavorites class implementation
 * @klass:
 */
static void
mgr_favorites_class_init (MgrFavoritesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	/* virtual methods */
	((GdaTreeManagerClass*) klass)->update_children = mgr_favorites_update_children;

	/* Properties */
        object_class->set_property = mgr_favorites_set_property;
        object_class->get_property = mgr_favorites_get_property;

	g_object_class_install_property (object_class, PROP_BROWSER_CNC,
                                         g_param_spec_object ("browser-connection", NULL, "Connection to use",
                                                              BROWSER_TYPE_CONNECTION,
                                                              G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
	object_class->dispose = mgr_favorites_dispose;
}

static void
mgr_favorites_init (MgrFavorites *mgr, MgrFavoritesClass *klass)
{
	mgr->priv = g_new0 (MgrFavoritesPriv, 1);
}

static void
mgr_favorites_dispose (GObject *object)
{
	MgrFavorites *mgr = (MgrFavorites *) object;

	if (mgr->priv) {
		if (mgr->priv->bcnc)
			g_object_unref (mgr->priv->bcnc);

		g_free (mgr->priv);
		mgr->priv = NULL;
	}

	/* chain to parent class */
	parent_class->dispose (object);
}

/**
 * mgr_favorites_get_type
 *
 * Returns: the GType
 */
GType
mgr_favorites_get_type (void)
{
        static GType type = 0;

        if (G_UNLIKELY (type == 0)) {
                static GStaticMutex registering = G_STATIC_MUTEX_INIT;
                static const GTypeInfo info = {
                        sizeof (MgrFavoritesClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) mgr_favorites_class_init,
                        NULL,
                        NULL,
                        sizeof (MgrFavorites),
                        0,
                        (GInstanceInitFunc) mgr_favorites_init
                };

                g_static_mutex_lock (&registering);
                if (type == 0)
                        type = g_type_register_static (GDA_TYPE_TREE_MANAGER, "MgrFavorites", &info, 0);
                g_static_mutex_unlock (&registering);
        }
        return type;
}

static void
mgr_favorites_set_property (GObject *object,
				   guint param_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
        MgrFavorites *mgr;

        mgr = MGR_FAVORITES (object);
        if (mgr->priv) {
                switch (param_id) {
		case PROP_BROWSER_CNC:
			mgr->priv->bcnc = (BrowserConnection*) g_value_get_object (value);
			if (mgr->priv->bcnc)
				g_object_ref (mgr->priv->bcnc);
			
			break;
                }
        }
}

static void
mgr_favorites_get_property (GObject *object,
				   guint param_id,
				   GValue *value,
				   GParamSpec *pspec)
{
        MgrFavorites *mgr;

        mgr = MGR_FAVORITES (object);
        if (mgr->priv) {
                switch (param_id) {
		case PROP_BROWSER_CNC:
			g_value_set_object (value, mgr->priv->bcnc);
			break;
                }
        }
}

/**
 * mgr_favorites_new
 * @bcnc: a #BrowserConnection object
 * @type: the type of favorites to handle
 * @order_key: ordering key, see browser_favorites_add()
 *
 * Creates a new #GdaTreeManager object which will add one tree node for each favorite of the @type type
 *
 * Returns: a new #GdaTreeManager object
 */
GdaTreeManager*
mgr_favorites_new (BrowserConnection *bcnc, BrowserFavoritesType type, gint order_key)
{
	MgrFavorites *mgr;
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	mgr = (MgrFavorites*) g_object_new (MGR_FAVORITES_TYPE,
					    "browser-connection", bcnc, NULL);
	mgr->priv->fav_type = type;
	mgr->priv->order_key = order_key;
	return (GdaTreeManager*) mgr;
}

static gchar *
create_summary_for_statement (BrowserConnection *bcnc, const gchar *sql)
{
	GdaSqlParser *parser;
	GString *string;
	GdaBatch *batch;

	parser = browser_connection_create_parser (bcnc);
	string = g_string_new ("");
	
	batch = gda_sql_parser_parse_string_as_batch (parser, sql, NULL, NULL);
	if (batch) {
		const GSList *stmt_list;
		stmt_list = gda_batch_get_statements (batch);
		if (!stmt_list || !(stmt_list->data))
			g_string_append (string, _("Empty statement"));
		else if (stmt_list->next)
			g_string_append (string, _("Multiple statements"));
		else {
			switch (gda_statement_get_statement_type (GDA_STATEMENT (stmt_list->data))) {
			case GDA_SQL_STATEMENT_SELECT:
				g_string_append (string, _("SELECT statement"));
				break;
			case GDA_SQL_STATEMENT_INSERT:
				g_string_append (string, _("INSERT statement"));
				break;
			case GDA_SQL_STATEMENT_UPDATE:
				g_string_append (string, _("UPDATE statement"));
				break;
			case GDA_SQL_STATEMENT_DELETE:
				g_string_append (string, _("DELETE statement"));
				break;
			case GDA_SQL_STATEMENT_COMPOUND:
				g_string_append (string, _("COMPOUND SELECT statement"));
				break;
			case GDA_SQL_STATEMENT_BEGIN:
				g_string_append (string, _("BEGIN statement"));
				break;
			case GDA_SQL_STATEMENT_ROLLBACK:
				g_string_append (string, _("ROLLBACK statement"));
				break;
			case GDA_SQL_STATEMENT_COMMIT:
				g_string_append (string, _("COMMIT statement"));
				break;
			case GDA_SQL_STATEMENT_SAVEPOINT:
				g_string_append (string, _("ADD SAVEPOINT statement"));
				break;
			case GDA_SQL_STATEMENT_ROLLBACK_SAVEPOINT:
				g_string_append (string, _("ROLLBACK SAVEPOINT statement"));
				break;
			case GDA_SQL_STATEMENT_DELETE_SAVEPOINT:
				g_string_append (string, _("DELETE SAVEPOINT statement"));
				break;
			case GDA_SQL_STATEMENT_UNKNOWN:
				g_string_append (string, _("Unknown statement"));
				break;
			case GDA_SQL_STATEMENT_NONE:
				g_string_append (string, _("Empty statement"));
				break;
			}
		}

		/* parameters */
		GdaSet *params;
		if (gda_batch_get_parameters (batch, &params, NULL) && params) {
			GSList *list;
			GdaHolder *holder;
			for (list = params->holders; list; list = list->next) {
				holder = GDA_HOLDER (list->data);
				g_string_append_c (string, '\n');
				g_string_append (string, gda_holder_get_id (holder));
				g_string_append (string, "::");
				g_string_append (string, gda_g_type_to_string (gda_holder_get_g_type (holder)));
				if (! gda_holder_get_not_null (holder))
					g_string_append (string, "::null");
			}
			g_object_unref (params);
                }
		
		g_object_unref (batch);
	}
	else {
		gint len;
		gchar *tmp;
		tmp = g_strdup (sql);
		len = strlen (sql);
		if (len > 40)
			tmp [40] = 0;
		g_string_append (string, tmp);
		g_free (tmp);
	}

	g_object_unref (parser);
	return g_string_free (string, FALSE);
}

/*
 * Build a hash where key = contents as a string, and value = GdaTreeNode
 */
static GHashTable *
hash_for_existing_nodes (const GSList *nodes)
{
	GHashTable *hash;
	const GSList *list;

	hash = g_hash_table_new_full (g_int_hash, g_int_equal, g_free, NULL);
	for (list = nodes; list; list = list->next) {
		const GValue *cvalue;
		cvalue = gda_tree_node_get_node_attribute ((GdaTreeNode*) list->data, MGR_FAVORITES_ID_ATT_NAME);
		if (cvalue && (G_VALUE_TYPE (cvalue) == G_TYPE_INT)) {
			gint id;
			id = g_value_get_int (cvalue);
			if (id >= 0) {
				gint *key = g_new0 (gint, 1);
				*key = id;
				g_hash_table_insert (hash, (gpointer) key, list->data);
			}
		}
	}
	return hash;
}

static GSList *
mgr_favorites_update_children (GdaTreeManager *manager, GdaTreeNode *node, const GSList *children_nodes,
			       gboolean *out_error, GError **error)
{
	MgrFavorites *mgr = MGR_FAVORITES (manager);
	GSList *nodes_list = NULL;
	GHashTable *ehash = NULL;
	GSList *fav_list;
	GError *lerror = NULL;
	BrowserConnection *bcnc;

	if (out_error)
		*out_error = FALSE;

	if (children_nodes)
		ehash = hash_for_existing_nodes (children_nodes);

	bcnc = mgr->priv->bcnc;
	fav_list = browser_favorites_list (browser_connection_get_favorites (bcnc),
					   0, mgr->priv->fav_type,
					   mgr->priv->order_key, &lerror);
	if (fav_list) {
		GSList *list;
		gint pos;
		for (list = fav_list, pos = 0; list; list = list->next, pos ++) {
			BrowserFavoritesAttributes *fav = (BrowserFavoritesAttributes *) list->data;
			GdaTreeNode* snode = NULL;
			GValue *av;

			if (ehash)
				snode = g_hash_table_lookup (ehash, &(fav->id));

			
			if (snode)
				/* use the same node */
				g_object_ref (G_OBJECT (snode));

			if (fav->type == BROWSER_FAVORITES_TABLES) {
				if (!snode) {
					GdaQuarkList *ql;
					const gchar *fname = NULL;
					
					ql = gda_quark_list_new_from_string (fav->contents);
					if (!ql || !(fname = gda_quark_list_find (ql, "OBJ_SHORT_NAME"))) {
						g_warning ("Invalid TABLE favorite format: %s", fav->contents);
					}
					else {
						snode = gda_tree_manager_create_node (manager, node, NULL);
						g_value_set_string ((av = gda_value_new (G_TYPE_STRING)), fname);
						gda_tree_node_set_node_attribute (snode, "markup", av, NULL);
						gda_value_free (av);
						
						g_value_set_string ((av = gda_value_new (G_TYPE_STRING)),
								    fav->contents);
						gda_tree_node_set_node_attribute (snode,
										  MGR_FAVORITES_CONTENTS_ATT_NAME,
										  av, NULL);
						gda_value_free (av);
						
						
						g_value_set_int ((av = gda_value_new (G_TYPE_INT)), fav->id);
						gda_tree_node_set_node_attribute (snode,
										  MGR_FAVORITES_ID_ATT_NAME,
										  av, NULL);
						gda_value_free (av);

						g_value_set_uint ((av = gda_value_new (G_TYPE_UINT)), fav->type);
						gda_tree_node_set_node_attribute (snode,
										  MGR_FAVORITES_TYPE_ATT_NAME,
										  av, NULL);
						gda_value_free (av);
						
						/* icon */
						GdkPixbuf *pixbuf;
						pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_TABLE);
						av = gda_value_new (G_TYPE_OBJECT);
						g_value_set_object (av, pixbuf);
						gda_tree_node_set_node_attribute (snode, "icon", av, NULL);
						gda_value_free (av);
					}
					if (ql)
						gda_quark_list_free (ql);
				}
			}
			else if (fav->type == BROWSER_FAVORITES_DIAGRAMS) {
				if (!snode) {
					snode = gda_tree_manager_create_node (manager, node, NULL);
									
					g_value_set_int ((av = gda_value_new (G_TYPE_INT)), fav->id);
					gda_tree_node_set_node_attribute (snode,
									  MGR_FAVORITES_ID_ATT_NAME,
									  av, NULL);
					gda_value_free (av);

					g_value_set_uint ((av = gda_value_new (G_TYPE_UINT)), fav->type);
					gda_tree_node_set_node_attribute (snode,
									  MGR_FAVORITES_TYPE_ATT_NAME,
									  av, NULL);
					gda_value_free (av);
					
					/* icon */
					GdkPixbuf *pixbuf;
					pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_DIAGRAM);
					av = gda_value_new (G_TYPE_OBJECT);
					g_value_set_object (av, pixbuf);
					gda_tree_node_set_node_attribute (snode, "icon", av, NULL);
					gda_value_free (av);
				}

				g_value_set_string ((av = gda_value_new (G_TYPE_STRING)),
						    fav->contents);
				gda_tree_node_set_node_attribute (snode,
								  MGR_FAVORITES_CONTENTS_ATT_NAME,
								  av, NULL);
				gda_value_free (av);

				g_value_set_string ((av = gda_value_new (G_TYPE_STRING)), fav->name);
				gda_tree_node_set_node_attribute (snode, "markup", av, NULL);
				gda_value_free (av);
			}
			else if (fav->type == BROWSER_FAVORITES_QUERIES) {
				if (!snode) {
					snode = gda_tree_manager_create_node (manager, node, NULL);
					
					g_value_set_int ((av = gda_value_new (G_TYPE_INT)), fav->id);
					gda_tree_node_set_node_attribute (snode,
									  MGR_FAVORITES_ID_ATT_NAME,
									  av, NULL);
					gda_value_free (av);

					g_value_set_int ((av = gda_value_new (G_TYPE_INT)), pos);
					gda_tree_node_set_node_attribute (snode,
									  MGR_FAVORITES_POSITION_ATT_NAME,
									  av, NULL);
					gda_value_free (av);

					/* icon */
					GdkPixbuf *pixbuf;
					pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_QUERY);
					av = gda_value_new (G_TYPE_OBJECT);
					g_value_set_object (av, pixbuf);
					gda_tree_node_set_node_attribute (snode, "icon", av, NULL);
					gda_value_free (av);

					/* summary */
					g_value_take_string ((av = gda_value_new (G_TYPE_STRING)),
							     create_summary_for_statement (bcnc, fav->contents));
					gda_tree_node_set_node_attribute (snode, "summary", av, NULL);
					gda_value_free (av);
				}

				g_value_set_string ((av = gda_value_new (G_TYPE_STRING)),
						    fav->contents);
				gda_tree_node_set_node_attribute (snode,
								  MGR_FAVORITES_CONTENTS_ATT_NAME,
								  av, NULL);
				gda_value_free (av);

				g_value_set_string ((av = gda_value_new (G_TYPE_STRING)), fav->name);
				gda_tree_node_set_node_attribute (snode, MGR_FAVORITES_NAME_ATT_NAME,
								  av, NULL);
				gda_value_free (av);
			}
			else if (fav->type == BROWSER_FAVORITES_DATA_MANAGERS) {
				if (!snode) {
					snode = gda_tree_manager_create_node (manager, node, NULL);
					
					g_value_set_int ((av = gda_value_new (G_TYPE_INT)), fav->id);
					gda_tree_node_set_node_attribute (snode,
									  MGR_FAVORITES_ID_ATT_NAME,
									  av, NULL);
					gda_value_free (av);

					g_value_set_int ((av = gda_value_new (G_TYPE_INT)), pos);
					gda_tree_node_set_node_attribute (snode,
									  MGR_FAVORITES_POSITION_ATT_NAME,
									  av, NULL);
					gda_value_free (av);

					g_value_set_uint ((av = gda_value_new (G_TYPE_UINT)), fav->type);
                                                gda_tree_node_set_node_attribute (snode,
                                                                                  MGR_FAVORITES_TYPE_ATT_NAME,
                                                                                  av, NULL);
                                        gda_value_free (av);

					/* icon */
					GdkPixbuf *pixbuf;
					pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_TABLE);
					av = gda_value_new (G_TYPE_OBJECT);
					g_value_set_object (av, pixbuf);
					gda_tree_node_set_node_attribute (snode, "icon", av, NULL);
					gda_value_free (av);
				}

				g_value_set_string ((av = gda_value_new (G_TYPE_STRING)),
						    fav->contents);
				gda_tree_node_set_node_attribute (snode,
								  MGR_FAVORITES_CONTENTS_ATT_NAME,
								  av, NULL);
				gda_value_free (av);

				g_value_set_string ((av = gda_value_new (G_TYPE_STRING)), fav->name);
				gda_tree_node_set_node_attribute (snode, MGR_FAVORITES_NAME_ATT_NAME,
								  av, NULL);
				gda_value_free (av);
			}
			else {
				TO_IMPLEMENT;
			}
			if (snode)
				nodes_list = g_slist_prepend (nodes_list, snode);
		}
		browser_favorites_free_list (fav_list);
	}
	else if (lerror) {
		if (out_error)
			*out_error = TRUE;
		g_propagate_error (error, lerror);

		GdaTreeNode* snode = NULL;
		gchar *str;
		GValue *value;

		str = g_strdup_printf ("<i>%s</i>", _("Getting\nfavorites..."));
		snode = gda_tree_manager_create_node (manager, node, str);
		g_value_take_string ((value = gda_value_new (G_TYPE_STRING)), str);
		gda_tree_node_set_node_attribute (snode, "markup", value, NULL);
		nodes_list = g_slist_prepend (nodes_list, snode);
	}
	else {
		GdaTreeNode* snode = NULL;
		gchar *str;
		GValue *value;

		if (mgr->priv->fav_type & BROWSER_FAVORITES_TABLES) {
			str = g_strdup_printf ("<i>%s</i>", _("No favorite:\ndrag item to\ndefine one"));
			snode = gda_tree_manager_create_node (manager, node, str);
			g_value_take_string ((value = gda_value_new (G_TYPE_STRING)), str);
			gda_tree_node_set_node_attribute (snode, "markup", value, NULL);
			nodes_list = g_slist_prepend (nodes_list, snode);
		}
	}

	if (ehash)
		g_hash_table_destroy (ehash);

	return g_slist_reverse (nodes_list);
}