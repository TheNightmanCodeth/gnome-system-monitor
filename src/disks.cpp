#include <config.h>

#include <libgnomevfs/gnome-vfs.h>
#include <gtk/gtk.h>
#include <glibtop/mountlist.h>
#include <glibtop/fsusage.h>
#include <glib/gi18n.h>

extern "C" {
#include "procman.h"
#include "disks.h"
#include "util.h"
#include "interface.h"
}

enum DiskColumns
{
	/* PixBuf column */
	DISK_ICON,
	/* string columns* */
	DISK_DEVICE,
	DISK_DIR,
	DISK_TYPE,
	DISK_TOTAL,
	DISK_FREE,
	DISK_AVAIL,
	/* USED have to be the last column */
	DISK_USED,
	/* numeric columns */
	DISK_USED_PERCENTAGE,
	DISK_TOTAL_UINT64,
	DISK_FREE_UINT64,
	DISK_AVAIL_UINT64,
	DISK_USED_UINT64,
	DISK_N_COLUMNS
};



static int
sort_bytes(GtkTreeModel *model, GtkTreeIter *iter1, GtkTreeIter *iter2,
	   gpointer data)
{
	int col = GPOINTER_TO_INT(data);
	guint64 bytes1, bytes2;

	gtk_tree_model_get(model, iter1, col, &bytes1, -1);
	gtk_tree_model_get(model, iter2, col, &bytes2, -1);

	return PROCMAN_RCMP(bytes1, bytes2);
}



static void
fsusage_stats(const glibtop_fsusage *buf,
	      guint64 *bused, guint64 *bfree, guint64 *bavail, guint64 *btotal,
	      gint *percentage)
{
	guint64 total = buf->blocks * buf->block_size;

	if (!total) {
		/* not a real device */
		*btotal = *bfree = *bavail = *bused = 0ULL;
		*percentage = 0;
	} else {
		float percent;
		*btotal = total;
		*bfree = buf->bfree * buf->block_size;
		*bavail = buf->bavail * buf->block_size;
		*bused = *btotal - *bfree;
		/* percent = 100.0f * *bused / *btotal; */
		percent = 100.0f * *bused / (*bused + *bavail);
		*percentage = CLAMP((gint)percent, 0, 100);
	}
}



static GdkPixbuf *
get_icon_for_device(const char *mountpoint)
{
	GdkPixbuf *pixbuf;
	GtkIconTheme *icon_theme;
	GnomeVFSVolumeMonitor *monitor;
	GnomeVFSVolume *volume;
	char *icon_name;

	monitor = gnome_vfs_get_volume_monitor();
	volume = gnome_vfs_volume_monitor_get_volume_for_path(monitor, mountpoint);

	if (!volume) {
		g_warning("Cannot get volume for mount point '%s'",
			  mountpoint);
		/* Fallback using / icon */
		volume = gnome_vfs_volume_monitor_get_volume_for_path(monitor, "/");
	}

	icon_name = gnome_vfs_volume_get_icon(volume);
	icon_theme = gtk_icon_theme_get_default();
	pixbuf = gtk_icon_theme_load_icon(icon_theme, icon_name, 24, GTK_ICON_LOOKUP_USE_BUILTIN, NULL);

	gnome_vfs_volume_unref(volume);
	g_free(icon_name);

	return pixbuf;
}


static gboolean
find_disk_in_model(GtkTreeModel *model, const char *mountpoint,
		   GtkTreeIter *result)
{
	GtkTreeIter iter;
	gboolean found = FALSE;

	if (gtk_tree_model_get_iter_first(model, &iter)) {
		do {
			char *dir;

			gtk_tree_model_get(model, &iter,
					   DISK_DIR, &dir,
					   -1);

			if (dir && !strcmp(dir, mountpoint)) {
				*result = iter;
				found = TRUE;
			}

			g_free(dir);

		} while (!found && gtk_tree_model_iter_next(model, &iter));
	}

	return found;
}



static void
remove_old_disks(GtkTreeModel *model, const glibtop_mountentry *entries, guint n)
{
	GtkTreeIter iter;

	if (!gtk_tree_model_get_iter_first(model, &iter))
		return;

	do {
		char *dir;
		guint i;
		gboolean found = FALSE;

		gtk_tree_model_get(model, &iter,
				   DISK_DIR, &dir,
				   -1);

		for (i = 0; i != n; ++i) {
			if (!strcmp(dir, entries[i].mountdir)) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			if (!gtk_list_store_remove(GTK_LIST_STORE(model), &iter))
				break;
		}

		g_free(dir);

	} while (gtk_tree_model_iter_next(model, &iter));
}



static void
add_disk(GtkListStore *list, const glibtop_mountentry *entry)
{
	GdkPixbuf *pixbuf;
	GtkTreeIter iter;
	glibtop_fsusage usage;
	gchar *used_str, *total_str, *free_str, *avail_str;
	guint64 bused, bfree, bavail, btotal;
	gint percentage;

	pixbuf = get_icon_for_device(entry->mountdir);

	glibtop_get_fsusage(&usage, entry->mountdir);
	fsusage_stats(&usage, &bused, &bfree, &bavail, &btotal, &percentage);

	used_str = SI_gnome_vfs_format_file_size_for_display(bused);
	free_str = SI_gnome_vfs_format_file_size_for_display(bfree);
	avail_str = SI_gnome_vfs_format_file_size_for_display(bavail);
	total_str = SI_gnome_vfs_format_file_size_for_display(btotal);

	/* if we can find a row with the same mountpoint, we get it but we
	   still need to update all the fields.
	   This makes selection persistent.
	*/
	if (!find_disk_in_model(GTK_TREE_MODEL(list), entry->mountdir, &iter))
		gtk_list_store_append(list, &iter);

	gtk_list_store_set(list, &iter,
			   DISK_ICON, pixbuf,
			   DISK_DEVICE, entry->devname,
			   DISK_DIR, entry->mountdir,
			   DISK_TYPE, entry->type,
			   DISK_TOTAL, total_str,
			   DISK_FREE, free_str,
			   DISK_AVAIL, avail_str,
			   DISK_USED, used_str,
			   DISK_USED_PERCENTAGE, percentage,
			   DISK_TOTAL_UINT64, btotal,
			   DISK_FREE_UINT64, bfree,
			   DISK_AVAIL_UINT64, bavail,
			   DISK_USED_UINT64, bused,
			   -1);

	if (pixbuf)
		g_object_unref(pixbuf);

	g_free(used_str);
	g_free(free_str);
	g_free(avail_str);
	g_free(total_str);
}



int
cb_update_disks(gpointer data)
{
	ProcData *const procdata = static_cast<ProcData*>(data);

	GtkListStore *list;
	glibtop_mountentry * entries;
	glibtop_mountlist mountlist;
	guint i;

	list = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(procdata->disk_list)));

	entries = glibtop_get_mountlist(&mountlist, procdata->config.show_all_fs);

	remove_old_disks(GTK_TREE_MODEL(list), entries, mountlist.number);

	for (i = 0; i < mountlist.number; i++)
		add_disk(list, &entries[i]);

	g_free(entries);

	return TRUE;
}


static void
cb_disk_columns_changed(GtkTreeView *treeview, gpointer user_data)
{
	ProcData * const procdata = static_cast<ProcData*>(user_data);

	procman_save_tree_state(procdata->client,
				GTK_WIDGET(treeview),
				"/apps/procman/disktreenew");
}


static void open_dir(GtkTreeView       *tree_view,
		     GtkTreePath       *path,
		     GtkTreeViewColumn *column,
		     gpointer	       user_data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	char *dir, *url;

	model = gtk_tree_view_get_model(tree_view);

	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		char *p;
		p = gtk_tree_path_to_string(path);
		g_warning("Cannot get iter for path '%s'\n", p);
		g_free(p);
		return;
	}

	gtk_tree_model_get(model, &iter, DISK_DIR, &dir, -1);

	url = g_strdup_printf("file://%s", dir);

	if (gnome_vfs_url_show(url) != GNOME_VFS_OK)
		g_warning("Cannot open '%s'\n", url);

	g_free(url);
	g_free(dir);
}

GtkWidget *
create_disk_view(ProcData *procdata)
{
	GtkWidget *disk_box, *disk_hbox;
	GtkWidget *label, *spacer;
	GtkWidget *scrolled;
	GtkWidget *disk_tree;
	GtkListStore *model;
	GtkTreeViewColumn *col;
	GtkCellRenderer *cell;
	guint i;

	const gchar * const titles[] = {
		N_("Device"),
		N_("Directory"),
		N_("Type"),
		N_("Total"),
		N_("Free"),
		N_("Available"),
		N_("Used")
	};

	disk_box = gtk_vbox_new(FALSE, 6);

	gtk_container_set_border_width(GTK_CONTAINER(disk_box), 12);

	label = make_title_label(_("File Systems"));
	gtk_box_pack_start(GTK_BOX(disk_box), label, FALSE, FALSE, 0);

	disk_hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(disk_box), disk_hbox, TRUE, TRUE, 0);

	spacer = gtk_label_new("    ");
	gtk_box_pack_start(GTK_BOX(disk_hbox), spacer, FALSE, FALSE, 0);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
					    GTK_SHADOW_IN);

	gtk_box_pack_start(GTK_BOX(disk_hbox), scrolled, TRUE, TRUE, 0);

	model = gtk_list_store_new(DISK_N_COLUMNS,	/* n columns */
				   GDK_TYPE_PIXBUF,	/* DISK_ICON */
				   G_TYPE_STRING,	/* DISK_DEVICE */
				   G_TYPE_STRING,	/* DISK_DIR */
				   G_TYPE_STRING,	/* DISK_TYPE */
				   G_TYPE_STRING,	/* DISK_TOTAL */
				   G_TYPE_STRING,	/* DISK_FREE */
				   G_TYPE_STRING,	/* DISK_AVAIL */
				   G_TYPE_STRING,	/* DISK_USED */
				   G_TYPE_INT,		/* DISK_USED_PERCENTAGE */
				   G_TYPE_UINT64,	/* DISK_TOTAL_UINT64 */
				   G_TYPE_UINT64,	/* DISK_FREE_UINT64 */
				   G_TYPE_UINT64,	/* DISK_AVAIL_UINT64 */
				   G_TYPE_UINT64	/* DISK_USED_UINT64 */
		);

	disk_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
	g_signal_connect(G_OBJECT(disk_tree), "row-activated", G_CALLBACK(open_dir), NULL);
	procdata->disk_list = disk_tree;
	gtk_container_add(GTK_CONTAINER(scrolled), disk_tree);
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(disk_tree), TRUE);
	g_object_unref(G_OBJECT(model));

	/* icon + device */

	col = gtk_tree_view_column_new();
	cell = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(col, cell, FALSE);
	gtk_tree_view_column_set_attributes(col, cell, "pixbuf", DISK_ICON,
					    NULL);

	cell = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, cell, FALSE);
	gtk_tree_view_column_set_attributes(col, cell, "text", DISK_DEVICE,
					    NULL);
	gtk_tree_view_column_set_title(col, _(titles[0]));
	gtk_tree_view_column_set_sort_column_id(col, 1);
	gtk_tree_view_column_set_reorderable(col, TRUE);
	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(disk_tree), col);

	/* sizes - used */

	for (i = 1; i < G_N_ELEMENTS(titles) - 1; i++) {
		cell = gtk_cell_renderer_text_new();

		switch (i + 1) {
		case DISK_TOTAL:
		case DISK_FREE:
		case DISK_AVAIL:
			g_object_set(cell, "xalign", 1.0f, NULL);
			break;
		}

		col = gtk_tree_view_column_new_with_attributes(_(titles[i]),
							       cell,
							       "text", i + 1,
							       NULL);
		gtk_tree_view_column_set_resizable(col, TRUE);
		gtk_tree_view_column_set_sort_column_id(col, i + 1);
		gtk_tree_view_column_set_reorderable(col, TRUE);
		gtk_tree_view_append_column(GTK_TREE_VIEW(disk_tree), col);
	}

	/* used + percentage */

	col = gtk_tree_view_column_new();
	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "xalign", 1.0f, NULL);
	gtk_tree_view_column_pack_start(col, cell, FALSE);
	gtk_tree_view_column_set_attributes(col, cell, "text", DISK_USED, NULL);
	gtk_tree_view_column_set_title(col, _(titles[G_N_ELEMENTS(titles) - 1]));

	cell = gtk_cell_renderer_progress_new();
	gtk_tree_view_column_pack_start(col, cell, TRUE);
	gtk_tree_view_column_set_attributes(col, cell, "value",
					    DISK_USED_PERCENTAGE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(disk_tree), col);
	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_column_set_sort_column_id(col, DISK_USED);
	gtk_tree_view_column_set_reorderable(col, TRUE);

	/* numeric sort */

	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(model),
					DISK_TOTAL,
					sort_bytes,
					GINT_TO_POINTER(DISK_TOTAL_UINT64),
					NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(model),
					DISK_FREE,
					sort_bytes,
					GINT_TO_POINTER(DISK_FREE_UINT64),
					NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(model),
					DISK_AVAIL,
					sort_bytes,
					GINT_TO_POINTER(DISK_AVAIL_UINT64),
					NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(model),
					DISK_USED,
					sort_bytes,
					GINT_TO_POINTER(DISK_USED_UINT64),
					NULL);

	gtk_widget_show_all(disk_box);

	procman_get_tree_state(procdata->client, disk_tree,
			       "/apps/procman/disktreenew");

	g_signal_connect (G_OBJECT(disk_tree), "columns-changed", 
	                  G_CALLBACK(cb_disk_columns_changed), procdata);

	return disk_box;
}