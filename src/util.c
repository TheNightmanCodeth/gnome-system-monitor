#include <config.h>

#include <glib/gi18n.h>
#include <glib/gstring.h>
#include <gtk/gtk.h>

#include <libgnomevfs/gnome-vfs-utils.h>

#include <stddef.h>

#include "util.h"


void _procman_array_gettext_init(const char * strings[], size_t n)
{
	size_t i;

	for(i = 0; i < n; ++i)
	{
		if(strings[i] != NULL)
			strings[i] = _(strings[i]);
	}
}


static char *
mnemonic_safe_process_name(const char *process_name)
{
	const char *p;
	GString *name;

	name = g_string_new ("");

	for(p = process_name; *p; ++p)
	{
		g_string_append_c (name, *p);

		if(*p == '_')
			g_string_append_c (name, '_');
	}

	return g_string_free (name, FALSE);
}


GtkWidget*
procman_make_label_for_mmaps_or_ofiles(const char *format,
					     const char *process_name,
					     unsigned pid)
{
	GtkWidget *label;
	char *name, *title;

	name = mnemonic_safe_process_name (process_name);
	title = g_strdup_printf(format, name, pid);
	label = gtk_label_new_with_mnemonic (title);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0f, 0.5f);

	g_free (title);
	g_free (name);

	return label;
}



#define KIBIBYTE_FACTOR 1024.0
#define MEBIBYTE_FACTOR (1024.0 * 1024.0)
#define GIBIBYTE_FACTOR (1024.0 * 1024.0 * 1024.0)


/**
 * SI_gnome_vfs_format_file_size_for_display:
 * @size:
 * 
 * Formats the file size passed in @bytes in a way that is easy for
 * the user to read. Gives the size in bytes, kibibytes, mebibytes or
 * gibibytes, choosing whatever is appropriate.
 * 
 * Returns: a newly allocated string with the size ready to be shown.
 **/

gchar*
SI_gnome_vfs_format_file_size_for_display (GnomeVFSFileSize size)
{
	if (size < (GnomeVFSFileSize) KIBIBYTE_FACTOR) {
		return g_strdup_printf (dngettext(GETTEXT_PACKAGE, "%u byte", "%u bytes",(guint) size), (guint) size);
	} else {
		gdouble displayed_size;

		if (size < (GnomeVFSFileSize) MEBIBYTE_FACTOR) {
			displayed_size = (gdouble) size / KIBIBYTE_FACTOR;
			return g_strdup_printf (_("%.1f KiB"),
						       displayed_size);
		} else if (size < (GnomeVFSFileSize) GIBIBYTE_FACTOR) {
			displayed_size = (gdouble) size / MEBIBYTE_FACTOR;
			return g_strdup_printf (_("%.1f MiB"),
						       displayed_size);
		} else {
			displayed_size = (gdouble) size / GIBIBYTE_FACTOR;
			return g_strdup_printf (_("%.1f GiB"),
						       displayed_size);
		}
	}
}



gboolean
load_symbols(const char *module, ...)
{
	GModule *mod;
	gboolean found_all = TRUE;
	va_list args;

	mod = g_module_open(module, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);

	if (!mod)
		return FALSE;

	procman_debug("Found %s", module);

	va_start(args, module);

	while (1) {
		const char *name;
		void **symbol;

		name = va_arg(args, char*);

		if (!name)
			break;

		symbol = va_arg(args, void**);

		if (g_module_symbol(mod, name, symbol)) {
			procman_debug("Loaded %s from %s", name, module);
		}
		else {
			procman_debug("Could not load %s from %s", name, module);
			found_all = FALSE;
			break;
		}
	}

	va_end(args);


	if (found_all)
		g_module_make_resident(mod);
	else
		g_module_close(mod);

	return found_all;
}


static gboolean
is_debug_enabled(void)
{
	static gboolean init;
	static gboolean enabled;

	if (!init) {
		enabled = g_getenv("GNOME_SYSTEM_MONITOR_DEBUG") != NULL;
		init = TRUE;
	}

	return enabled;
}



void
procman_debug(const char *format, ...)
{
	va_list args;
	char *msg;

	if (G_LIKELY(!is_debug_enabled()))
		return;

	va_start(args, format);
	msg = g_strdup_vprintf(format, args);
	va_end(args);

	g_debug(msg);

	g_free(msg);
}
