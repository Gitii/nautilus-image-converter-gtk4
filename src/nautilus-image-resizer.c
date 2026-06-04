/*
 *  nautilus-image-resizer.c
 *
 *  Copyright (C) 2004-2008 Jürg Billeter
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Jürg Billeter <j@bitron.ch>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h> /* for GETTEXT_PACKAGE */
#endif

#include "nautilus-image-resizer.h"

#include <string.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <nautilus-extension.h>

typedef struct _NautilusImageResizerPrivate NautilusImageResizerPrivate;
typedef struct _TransformFileRow TransformFileRow;

struct _TransformFileRow
{
	NautilusFileInfo *file;
	GtkWidget *name_label;
	GtkWidget *original_size_label;
	GtkWidget *new_size_label;
	GtkWidget *status_label;
	goffset original_size;
	goffset new_size;
};

struct _NautilusImageResizerPrivate
{
	GList *files;

	gchar *suffix;

	gchar *size;
	gchar *filter;
	gchar *angle;
	gint jpeg_quality;

	GtkDialog *resize_dialog;
	GtkWidget *cancel_button;
	GtkWidget *resize_button;
	GtkWidget *preview_button;
	GtkWidget *controls_box;
	GtkWidget *files_grid;
	GtkWidget *progress_bar;
	GtkWidget *summary_label;
	GPtrArray *file_rows;
	gboolean running;
	gboolean preview_mode;
	guint current_file_index;
	gchar *current_output_path;
	gchar *current_preview_path;
	GtkCheckButton *operation_resize_radiobutton;
	GtkCheckButton *operation_rotate_radiobutton;
	GtkCheckButton *operation_compress_radiobutton;
	GtkCheckButton *default_size_radiobutton;
	GtkComboBoxText *size_combobox;
	GtkCheckButton *custom_pct_radiobutton;
	GtkSpinButton *pct_spinbutton;
	GtkCheckButton *custom_size_radiobutton;
	GtkSpinButton *width_spinbutton;
	GtkSpinButton *height_spinbutton;
	GtkCheckButton *append_radiobutton;
	GtkEntry *name_entry;
	GtkCheckButton *inplace_radiobutton;

	GtkSpinButton *target_size_spinbutton;
	GtkComboBoxText *target_size_unit_combobox;
	gint target_size_kb;
	gboolean use_target_size;
	gboolean target_size_available;

	GtkCheckButton *quality_high_radiobutton;
	GtkCheckButton *quality_balanced_radiobutton;
	GtkCheckButton *quality_soft_radiobutton;
	GtkCheckButton *encoding_quality_radiobutton;
	GtkSpinButton *jpeg_quality_spinbutton;
	GtkCheckButton *encoding_target_radiobutton;
	GtkCheckButton *default_angle_radiobutton;
	GtkComboBox *angle_combobox;
	GtkCheckButton *custom_angle_radiobutton;
	GtkSpinButton *angle_spinbutton;
};

#define NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE((o), NAUTILUS_TYPE_IMAGE_RESIZER, NautilusImageResizerPrivate))

G_DEFINE_TYPE(NautilusImageResizer, nautilus_image_resizer, G_TYPE_OBJECT)

enum
{
	PROP_FILES = 1,
};

enum
{
	RESPONSE_PREVIEW = 1,
};

typedef enum
{
	/* Place Signal Types Here */
	SIGNAL_TYPE_EXAMPLE,
	LAST_SIGNAL
} NautilusImageResizerSignalType;

static void
transform_file_row_free(gpointer data)
{
	TransformFileRow *row = data;

	if (row == NULL)
		return;

	g_free(row);
}

static void
nautilus_image_resizer_finalize(GObject *object)
{
	NautilusImageResizer *dialog = NAUTILUS_IMAGE_RESIZER(object);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(dialog);

	g_free(priv->suffix);
	g_free(priv->size);
	g_free(priv->filter);
	g_free(priv->angle);
	g_free(priv->current_output_path);
	g_free(priv->current_preview_path);
	if (priv->file_rows != NULL)
		g_ptr_array_unref(priv->file_rows);

	G_OBJECT_CLASS(nautilus_image_resizer_parent_class)->finalize(object);
}

static void
nautilus_image_resizer_set_property(GObject *object,
									guint property_id,
									const GValue *value,
									GParamSpec *pspec)
{
	NautilusImageResizer *dialog = NAUTILUS_IMAGE_RESIZER(object);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(dialog);

	switch (property_id)
	{
	case PROP_FILES:
		priv->files = g_value_get_pointer(value);
		break;
	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void
nautilus_image_resizer_get_property(GObject *object,
									guint property_id,
									GValue *value,
									GParamSpec *pspec)
{
	NautilusImageResizer *self = NAUTILUS_IMAGE_RESIZER(object);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(self);

	switch (property_id)
	{
	case PROP_FILES:
		g_value_set_pointer(value, priv->files);
		break;
	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void
nautilus_image_resizer_class_init(NautilusImageResizerClass *klass)
{
	g_type_class_add_private(klass, sizeof(NautilusImageResizerPrivate));

	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *files_param_spec;

	object_class->finalize = nautilus_image_resizer_finalize;
	object_class->set_property = nautilus_image_resizer_set_property;
	object_class->get_property = nautilus_image_resizer_get_property;

	files_param_spec = g_param_spec_pointer("files",
											"Files",
											"Set selected files",
											G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

	g_object_class_install_property(object_class,
									PROP_FILES,
									files_param_spec);
}

static void run_next_file(NautilusImageResizer *resizer);

static void
show_error_dialog(GtkWindow *parent, const char *message)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new(parent,
						GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_OK,
						"%s",
						message);
	g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
	gtk_window_present(GTK_WINDOW(dialog));
}

static GtkWidget *
new_labeled_row(GtkWidget *label, GtkWidget *control, GtkWidget *suffix)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

	gtk_box_append(GTK_BOX(row), label);
	gtk_box_append(GTK_BOX(row), control);
	if (suffix != NULL)
		gtk_box_append(GTK_BOX(row), suffix);

	return row;
}

static gchar *
format_file_size(goffset size)
{
	if (size < 0)
		return g_strdup("-");

	return g_format_size((guint64)size);
}

static gchar *
format_new_file_size(goffset original_size, goffset new_size)
{
	gchar *formatted_size;
	gchar *result;
	gint delta;

	if (new_size < 0)
		return g_strdup("-");

	formatted_size = format_file_size(new_size);
	if (original_size <= 0) {
		return formatted_size;
	}

	delta = (gint)(((new_size - original_size) * 100.0 / original_size) + (new_size >= original_size ? 0.5 : -0.5));
	result = g_strdup_printf("%s (%+d%%)", formatted_size, delta);
	g_free(formatted_size);

	return result;
}

static void
set_file_row_status(TransformFileRow *row, const gchar *status)
{
	gtk_label_set_text(GTK_LABEL(row->status_label), status);
}

static void
set_file_row_new_size(TransformFileRow *row, goffset new_size)
{
	gchar *text;

	row->new_size = new_size;
	text = format_new_file_size(row->original_size, row->new_size);
	gtk_label_set_text(GTK_LABEL(row->new_size_label), text);
	g_free(text);
}

static void
reset_file_rows(NautilusImageResizerPrivate *priv)
{
	guint i;

	if (priv->file_rows == NULL || priv->running)
		return;

	for (i = 0; i < priv->file_rows->len; i++) {
		TransformFileRow *row = g_ptr_array_index(priv->file_rows, i);
		set_file_row_new_size(row, -1);
		set_file_row_status(row, _("Unchanged"));
	}
	if (priv->summary_label != NULL)
		gtk_label_set_text(GTK_LABEL(priv->summary_label), _("Ready"));
}

static GtkWidget *
new_table_label(const gchar *text, gboolean heading)
{
	GtkWidget *label = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	if (heading)
		gtk_widget_add_css_class(label, "heading");

	return label;
}

static goffset
get_file_size(GFile *file)
{
	GFileInfo *info;
	goffset size = -1;

	info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info != NULL) {
		size = g_file_info_get_size(info);
		g_object_unref(info);
	}

	return size;
}

static TransformFileRow *
append_file_row(NautilusImageResizerPrivate *priv, NautilusFileInfo *file, gint row_index)
{
	TransformFileRow *row = g_new0(TransformFileRow, 1);
	GFile *location;
	gchar *name;
	gchar *original_size;

	row->file = file;
	row->new_size = -1;
	location = nautilus_file_info_get_location(file);
	row->original_size = get_file_size(location);
	g_object_unref(location);

	name = nautilus_file_info_get_name(file);
	original_size = format_file_size(row->original_size);
	row->name_label = new_table_label(name, FALSE);
	row->original_size_label = new_table_label(original_size, FALSE);
	row->new_size_label = new_table_label("-", FALSE);
	row->status_label = new_table_label(_("Unchanged"), FALSE);

	gtk_grid_attach(GTK_GRID(priv->files_grid), row->name_label, 0, row_index, 1, 1);
	gtk_grid_attach(GTK_GRID(priv->files_grid), row->original_size_label, 1, row_index, 1, 1);
	gtk_grid_attach(GTK_GRID(priv->files_grid), row->new_size_label, 2, row_index, 1, 1);
	gtk_grid_attach(GTK_GRID(priv->files_grid), row->status_label, 3, row_index, 1, 1);

	g_free(name);
	g_free(original_size);

	return row;
}

static void
update_apply_button_cb(GtkCheckButton *button, gpointer user_data)
{
	NautilusImageResizer *resizer = NAUTILUS_IMAGE_RESIZER(user_data);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	gboolean resize_selected;
	gboolean rotate_selected;
	gboolean compress_selected;
	gboolean operation_selected;
	gboolean resampling_selected;
	gboolean angle_selected;
	gboolean encoding_selected;
	gboolean valid;

	operation_selected = gtk_check_button_get_active(priv->operation_resize_radiobutton) ||
		gtk_check_button_get_active(priv->operation_rotate_radiobutton) ||
		gtk_check_button_get_active(priv->operation_compress_radiobutton);
	resize_selected = gtk_check_button_get_active(priv->operation_resize_radiobutton);
	rotate_selected = gtk_check_button_get_active(priv->operation_rotate_radiobutton);
	compress_selected = gtk_check_button_get_active(priv->operation_compress_radiobutton);
	resampling_selected = gtk_check_button_get_active(priv->quality_high_radiobutton) ||
		gtk_check_button_get_active(priv->quality_balanced_radiobutton) ||
		gtk_check_button_get_active(priv->quality_soft_radiobutton);
	angle_selected = gtk_check_button_get_active(priv->default_angle_radiobutton) ||
		gtk_check_button_get_active(priv->custom_angle_radiobutton);
	encoding_selected = gtk_check_button_get_active(priv->encoding_quality_radiobutton) ||
		gtk_check_button_get_active(priv->encoding_target_radiobutton);
	valid = operation_selected && (!resize_selected || resampling_selected) && (!rotate_selected || angle_selected) && (!compress_selected || encoding_selected);

	gtk_widget_set_sensitive(GTK_WIDGET(priv->default_size_radiobutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->size_combobox), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->custom_pct_radiobutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->pct_spinbutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->custom_size_radiobutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->width_spinbutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->height_spinbutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->quality_high_radiobutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->quality_balanced_radiobutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->quality_soft_radiobutton), resize_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->default_angle_radiobutton), rotate_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->angle_combobox), rotate_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->custom_angle_radiobutton), rotate_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->angle_spinbutton), rotate_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->encoding_quality_radiobutton), compress_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->jpeg_quality_spinbutton), compress_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->encoding_target_radiobutton), compress_selected && priv->target_size_available);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->target_size_spinbutton), compress_selected && priv->target_size_available);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->target_size_unit_combobox), compress_selected && priv->target_size_available);
	gtk_widget_set_sensitive(priv->resize_button, valid);
	gtk_widget_set_sensitive(priv->preview_button, valid);
	reset_file_rows(priv);
}

static gchar *
build_transform_args(NautilusImageResizerPrivate *priv)
{
	GString *args = g_string_new("");

	if (priv->size != NULL) {
		gchar *filter = g_shell_quote(priv->filter == NULL ? "Lanczos" : priv->filter);
		gchar *size = g_shell_quote(priv->size);

		g_string_append_printf(args, " -filter %s -resize %s", filter, size);
		g_free(filter);
		g_free(size);
	}

	if (priv->angle != NULL) {
		gchar *angle = g_shell_quote(priv->angle);

		g_string_append_printf(args, " -rotate %s -orient TopLeft", angle);
		g_free(angle);
	}

	return g_string_free(args, FALSE);
}

static gchar *
build_jpeg_command(NautilusImageResizerPrivate *priv, const gchar *filename, const gchar *new_filename)
{
	gchar *input = g_shell_quote(filename);
	gchar *output = g_shell_quote(new_filename);
	gchar *magick = g_shell_quote(MAGICK_PATH);
	gchar *cjpeg = g_shell_quote(CJPEG_PATH);
	gchar *transform_args = build_transform_args(priv);
	gchar *command;

	if (priv->use_target_size) {
		command = g_strdup_printf(
			"tmp=$(mktemp) && %s %s%s ppm:$tmp && "
			"lo=1; hi=95; best=; "
			"while [ $lo -le $hi ]; do "
			"q=$(((lo + hi) / 2)); "
			"%s -quality $q -outfile %s $tmp || exit 1; "
			"s=$(wc -c < %s); "
			"if [ $s -le %d ]; then best=$q; lo=$((q + 1)); else hi=$((q - 1)); fi; "
			"done; "
			"if [ -n \"$best\" ]; then %s -quality $best -outfile %s $tmp; r=$?; else r=1; fi; "
			"rm -f $tmp; exit $r",
			magick, input, transform_args, cjpeg, output, output, priv->target_size_kb * 1024, cjpeg, output);
	} else {
		command = g_strdup_printf("%s %s%s ppm:- | %s -quality %d -outfile %s",
			magick, input, transform_args, cjpeg, priv->jpeg_quality, output);
	}

	g_free(input);
	g_free(output);
	g_free(magick);
	g_free(cjpeg);
	g_free(transform_args);

	return command;
}

static gchar *
build_imagemagick_command(NautilusImageResizerPrivate *priv, const gchar *filename, const gchar *new_filename)
{
	gchar *input = g_shell_quote(filename);
	gchar *output = g_shell_quote(new_filename);
	gchar *magick = g_shell_quote(MAGICK_PATH);
	gchar *transform_args = build_transform_args(priv);
	gchar *command;

	command = g_strdup_printf("%s %s%s %s", magick, input, transform_args, output);
	g_free(input);
	g_free(output);
	g_free(magick);
	g_free(transform_args);

	return command;
}

static GFile *
nautilus_image_resizer_transform_filename(NautilusImageResizer *resizer, GFile *orig_file)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);

	GFile *parent_file, *new_file;
	char *basename, *extension, *new_basename;

	g_return_val_if_fail(G_IS_FILE(orig_file), NULL);

	parent_file = g_file_get_parent(orig_file);

	basename = g_strdup(g_file_get_basename(orig_file));

	extension = g_strdup(strrchr(basename, '.'));
	if (extension != NULL)
		basename[strlen(basename) - strlen(extension)] = '\0';

	new_basename = g_strdup_printf("%s%s%s", basename, priv->suffix == NULL ? ".tmp" : priv->suffix, extension == NULL ? "" : extension);
	g_free(basename);
	g_free(extension);

	new_file = g_file_get_child(parent_file, new_basename);

	g_object_unref(parent_file);
	g_free(new_basename);

	return new_file;
}

static gchar *
create_preview_filename(const gchar *filename)
{
	const gchar *extension = strrchr(filename, '.');
	gchar *basename;
	gchar *path;

	if (extension == NULL)
		extension = ".img";

	basename = g_strdup_printf("nautilus-image-converter-preview-%u%s", g_random_int(), extension);
	path = g_build_filename(g_get_tmp_dir(), basename, NULL);
	g_free(basename);

	return path;
}

static gchar *
build_command_for_file(NautilusImageResizerPrivate *priv, NautilusFileInfo *file, const gchar *filename, const gchar *output_path)
{
	gchar *mime_type;
	gchar *command;

	mime_type = nautilus_file_info_get_mime_type(file);
	if (g_strcmp0(mime_type, "image/jpeg") == 0 || g_strcmp0(mime_type, "image/jpg") == 0)
		command = build_jpeg_command(priv, filename, output_path);
	else
		command = build_imagemagick_command(priv, filename, output_path);
	g_free(mime_type);

	return command;
}

static void
set_controls_running(NautilusImageResizerPrivate *priv, gboolean running)
{
	priv->running = running;
	gtk_widget_set_sensitive(priv->controls_box, !running);
	gtk_widget_set_sensitive(priv->preview_button, !running);
	gtk_widget_set_sensitive(priv->resize_button, !running);
	gtk_widget_set_sensitive(priv->cancel_button, !running);
}

static void
finish_run(NautilusImageResizer *resizer)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	const gchar *summary;

	set_controls_running(priv, FALSE);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress_bar), 0.0);
	summary = priv->preview_mode ? _("Preview complete") : _("Transform complete");
	gtk_label_set_text(GTK_LABEL(priv->summary_label), summary);
}

static void
op_finished(GPid pid, gint status, gpointer data)
{
	NautilusImageResizer *resizer = NAUTILUS_IMAGE_RESIZER(data);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	TransformFileRow *row = g_ptr_array_index(priv->file_rows, priv->current_file_index);
	goffset new_size = -1;

	if (status == 0) {
		if (!priv->preview_mode && priv->suffix == NULL) {
			GFile *orig_location = nautilus_file_info_get_location(row->file);
			GFile *new_location = g_file_new_for_path(priv->current_output_path);

			if (!g_file_move(new_location, orig_location, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL)) {
				status = 1;
			}
			g_object_unref(orig_location);
			g_object_unref(new_location);
		}
	}

	if (status == 0) {
		GFile *output_file;

		if (!priv->preview_mode && priv->suffix == NULL)
			output_file = nautilus_file_info_get_location(row->file);
		else
			output_file = g_file_new_for_path(priv->current_output_path);
		new_size = get_file_size(output_file);
		g_object_unref(output_file);
		set_file_row_new_size(row, new_size);
		set_file_row_status(row, priv->preview_mode ? _("Previewed") : _("Transformed"));
	} else {
		set_file_row_status(row, _("Error"));
	}

	if (priv->preview_mode && priv->current_preview_path != NULL)
		g_remove(priv->current_preview_path);
	g_clear_pointer(&priv->current_output_path, g_free);
	g_clear_pointer(&priv->current_preview_path, g_free);
	g_spawn_close_pid(pid);

	priv->current_file_index++;
	run_next_file(resizer);
}

static void
run_next_file(NautilusImageResizer *resizer)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	TransformFileRow *row;
	NautilusFileInfo *file;
	GFile *orig_location;
	GFile *new_location;
	gchar *filename;
	gchar *command;
	gchar *argv[4];
	gchar *progress_text;
	pid_t pid;
	gboolean spawn_success;

	if (priv->current_file_index >= priv->file_rows->len) {
		finish_run(resizer);
		return;
	}

	row = g_ptr_array_index(priv->file_rows, priv->current_file_index);
	file = row->file;
	set_file_row_status(row, priv->preview_mode ? _("Previewing") : _("Transforming"));

	orig_location = nautilus_file_info_get_location(file);
	filename = g_file_get_path(orig_location);
	if (priv->preview_mode) {
		priv->current_preview_path = create_preview_filename(filename);
		priv->current_output_path = g_strdup(priv->current_preview_path);
	} else {
		new_location = nautilus_image_resizer_transform_filename(resizer, orig_location);
		priv->current_output_path = g_file_get_path(new_location);
		g_object_unref(new_location);
	}
	g_object_unref(orig_location);

	command = build_command_for_file(priv, file, filename, priv->current_output_path);
	argv[0] = "/bin/sh";
	argv[1] = "-c";
	argv[2] = command;
	argv[3] = NULL;
	spawn_success = g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL);

	g_free(filename);
	g_free(command);

	if (!spawn_success) {
		set_file_row_status(row, _("Error"));
		if (priv->preview_mode && priv->current_preview_path != NULL)
			g_remove(priv->current_preview_path);
		g_clear_pointer(&priv->current_output_path, g_free);
		g_clear_pointer(&priv->current_preview_path, g_free);
		priv->current_file_index++;
		run_next_file(resizer);
		return;
	}

	g_child_watch_add(pid, op_finished, resizer);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress_bar), (double)(priv->current_file_index + 1) / priv->file_rows->len);
	progress_text = g_strdup_printf(priv->preview_mode ? _("Previewing %u of %u") : _("Transforming %u of %u"),
		priv->current_file_index + 1, priv->file_rows->len);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(priv->progress_bar), progress_text);
	gtk_label_set_text(GTK_LABEL(priv->summary_label), progress_text);
	g_free(progress_text);
}

static void
nautilus_image_resizer_response_cb(GtkDialog *dialog, gint response_id, gpointer user_data)
{
	NautilusImageResizer *resizer = NAUTILUS_IMAGE_RESIZER(user_data);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);

	if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
	{
		gtk_window_destroy(GTK_WINDOW(dialog));
		return;
	}

	if (response_id == GTK_RESPONSE_OK || response_id == RESPONSE_PREVIEW)
	{
		gboolean resize_selected = gtk_check_button_get_active(priv->operation_resize_radiobutton);
		gboolean rotate_selected = gtk_check_button_get_active(priv->operation_rotate_radiobutton);
		gboolean compress_selected = gtk_check_button_get_active(priv->operation_compress_radiobutton);
		guint i;

		g_clear_pointer(&priv->size, g_free);
		g_clear_pointer(&priv->filter, g_free);
		g_clear_pointer(&priv->angle, g_free);
		g_clear_pointer(&priv->suffix, g_free);
		priv->use_target_size = FALSE;
		priv->jpeg_quality = 90;

		if (resize_selected) {
			if (gtk_check_button_get_active(priv->quality_high_radiobutton))
			{
				priv->filter = g_strdup("Lanczos");
			}
			else if (gtk_check_button_get_active(priv->quality_balanced_radiobutton))
			{
				priv->filter = g_strdup("Mitchell");
			}
			else if (gtk_check_button_get_active(priv->quality_soft_radiobutton))
			{
				priv->filter = g_strdup("Triangle");
			}
			else
			{
				show_error_dialog(GTK_WINDOW(dialog), _("Please select a resampling filter."));
				return;
			}
		}

		if (rotate_selected) {
			if (gtk_check_button_get_active(priv->default_angle_radiobutton)) {
				switch (gtk_combo_box_get_active(priv->angle_combobox)) {
				case 0:
					priv->angle = g_strdup("90");
					break;
				case 1:
					priv->angle = g_strdup("-90");
					break;
				case 2:
					priv->angle = g_strdup("180");
					break;
				default:
					g_assert_not_reached();
				}
			} else if (gtk_check_button_get_active(priv->custom_angle_radiobutton)) {
				priv->angle = g_strdup_printf("%d", (int)gtk_spin_button_get_value(priv->angle_spinbutton));
			} else {
				show_error_dialog(GTK_WINDOW(dialog), _("Please select a rotation angle."));
				return;
			}
		}

		if (compress_selected) {
			if (gtk_check_button_get_active(priv->encoding_target_radiobutton)) {
				gint size_val = (gint)gtk_spin_button_get_value(priv->target_size_spinbutton);
				gint active_unit = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->target_size_unit_combobox));

				priv->target_size_kb = (active_unit == 1) ? size_val * 1024 : size_val;
				priv->use_target_size = TRUE;
			} else {
				priv->jpeg_quality = (gint)gtk_spin_button_get_value(priv->jpeg_quality_spinbutton);
			}
		}

		if (gtk_check_button_get_active(priv->append_radiobutton))
		{
			if (strlen(gtk_editable_get_text(GTK_EDITABLE(priv->name_entry))) == 0)
			{
				show_error_dialog(GTK_WINDOW(dialog), _("Please enter a valid filename suffix!"));
				return;
			}
			priv->suffix = g_strdup(gtk_editable_get_text(GTK_EDITABLE(priv->name_entry)));
		}
		if (!resize_selected)
		{
			priv->size = NULL;
		}
		else if (gtk_check_button_get_active(priv->default_size_radiobutton))
		{
			priv->size = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->size_combobox));
		}
		else if (gtk_check_button_get_active(priv->custom_pct_radiobutton))
		{
			priv->size = g_strdup_printf("%d%%", (int)gtk_spin_button_get_value(priv->pct_spinbutton));
		}
		else
		{
			priv->size = g_strdup_printf("%dx%d", (int)gtk_spin_button_get_value(priv->width_spinbutton), (int)gtk_spin_button_get_value(priv->height_spinbutton));
		}

		for (i = 0; i < priv->file_rows->len; i++) {
			TransformFileRow *row = g_ptr_array_index(priv->file_rows, i);
			set_file_row_new_size(row, -1);
			set_file_row_status(row, _("Queued"));
		}

		priv->preview_mode = (response_id == RESPONSE_PREVIEW);
		priv->current_file_index = 0;
		set_controls_running(priv, TRUE);
		run_next_file(resizer);
	}
}
static void
nautilus_image_resizer_init(NautilusImageResizer *resizer)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	GtkWidget *content;
	GtkWidget *box;
	GtkWidget *outer_box;
	GtkWidget *controls_scroll;
	GtkWidget *files_box;
	GtkWidget *files_scroll;
	GtkWidget *section;
	GtkWidget *label;
	GtkWidget *row;
	GtkComboBoxText *angle_combo;
	GList *file_list;
	gint file_row_index;

	priv->resize_dialog = GTK_DIALOG(gtk_dialog_new());
	gtk_window_set_title(GTK_WINDOW(priv->resize_dialog), _("Transform Images"));
	gtk_window_set_modal(GTK_WINDOW(priv->resize_dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(priv->resize_dialog), 980, 620);
	priv->cancel_button = gtk_dialog_add_button(priv->resize_dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
	priv->preview_button = gtk_dialog_add_button(priv->resize_dialog, _("_Preview"), RESPONSE_PREVIEW);
	priv->resize_button = gtk_dialog_add_button(priv->resize_dialog, _("_Apply"), GTK_RESPONSE_OK);
	gtk_widget_set_sensitive(priv->resize_button, FALSE);
	gtk_widget_set_sensitive(priv->preview_button, FALSE);
	gtk_dialog_set_default_response(priv->resize_dialog, GTK_RESPONSE_OK);

	content = gtk_dialog_get_content_area(priv->resize_dialog);
	outer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_widget_set_margin_top(outer_box, 12);
	gtk_widget_set_margin_bottom(outer_box, 12);
	gtk_widget_set_margin_start(outer_box, 12);
	gtk_widget_set_margin_end(outer_box, 12);
	gtk_box_append(GTK_BOX(content), outer_box);

	controls_scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(controls_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(controls_scroll, 360, -1);
	gtk_box_append(GTK_BOX(outer_box), controls_scroll);

	priv->controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(controls_scroll), priv->controls_box);
	box = priv->controls_box;

	files_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_hexpand(files_box, TRUE);
	gtk_box_append(GTK_BOX(outer_box), files_box);
	label = gtk_label_new(_("Selected Files"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(files_box), label);

	files_scroll = gtk_scrolled_window_new();
	gtk_widget_set_hexpand(files_scroll, TRUE);
	gtk_widget_set_vexpand(files_scroll, TRUE);
	gtk_box_append(GTK_BOX(files_box), files_scroll);
	priv->files_grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(priv->files_grid), 16);
	gtk_grid_set_row_spacing(GTK_GRID(priv->files_grid), 6);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(files_scroll), priv->files_grid);
	gtk_grid_attach(GTK_GRID(priv->files_grid), new_table_label(_("File"), TRUE), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(priv->files_grid), new_table_label(_("Original Size"), TRUE), 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(priv->files_grid), new_table_label(_("New Size"), TRUE), 2, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(priv->files_grid), new_table_label(_("Status"), TRUE), 3, 0, 1, 1);
	priv->file_rows = g_ptr_array_new_with_free_func(transform_file_row_free);
	file_row_index = 1;
	for (file_list = priv->files; file_list != NULL; file_list = file_list->next) {
		TransformFileRow *file_row = append_file_row(priv, NAUTILUS_FILE_INFO(file_list->data), file_row_index++);
		g_ptr_array_add(priv->file_rows, file_row);
	}

	priv->summary_label = gtk_label_new(_("Ready"));
	gtk_label_set_xalign(GTK_LABEL(priv->summary_label), 0.0);
	gtk_box_append(GTK_BOX(files_box), priv->summary_label);
	priv->progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(priv->progress_bar), TRUE);
	gtk_box_append(GTK_BOX(files_box), priv->progress_bar);

	label = gtk_label_new(_("Operation"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(box), label);

	section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(box), section);
	priv->operation_resize_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Resize dimensions")));
	gtk_check_button_set_active(priv->operation_resize_radiobutton, TRUE);
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->operation_resize_radiobutton));
	priv->operation_rotate_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Rotate")));
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->operation_rotate_radiobutton));
	priv->operation_compress_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Re-encode/compress")));
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->operation_compress_radiobutton));
	g_signal_connect(priv->operation_resize_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->operation_rotate_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->operation_compress_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);

	label = gtk_label_new(_("Dimensions"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(box), label);

	section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(box), section);

	priv->default_size_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Select a size:")));
	gtk_check_button_set_active(priv->default_size_radiobutton, TRUE);
	priv->size_combobox = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
	gtk_combo_box_text_append_text(priv->size_combobox, "96x96");
	gtk_combo_box_text_append_text(priv->size_combobox, "128x128");
	gtk_combo_box_text_append_text(priv->size_combobox, "640x480");
	gtk_combo_box_text_append_text(priv->size_combobox, "800x600");
	gtk_combo_box_text_append_text(priv->size_combobox, "1024x768");
	gtk_combo_box_text_append_text(priv->size_combobox, "1280x960");
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->size_combobox), 4);
	row = new_labeled_row(GTK_WIDGET(priv->default_size_radiobutton), GTK_WIDGET(priv->size_combobox), gtk_label_new(_("pixels")));
	gtk_box_append(GTK_BOX(section), row);
	g_signal_connect(priv->size_combobox, "changed", G_CALLBACK(update_apply_button_cb), resizer);

	priv->custom_pct_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Scale:")));
	gtk_check_button_set_group(priv->custom_pct_radiobutton, priv->default_size_radiobutton);
	priv->pct_spinbutton = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 100, 1));
	gtk_spin_button_set_value(priv->pct_spinbutton, 50);
	row = new_labeled_row(GTK_WIDGET(priv->custom_pct_radiobutton), GTK_WIDGET(priv->pct_spinbutton), gtk_label_new("%"));
	gtk_box_append(GTK_BOX(section), row);
	g_signal_connect(priv->custom_pct_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->pct_spinbutton, "value-changed", G_CALLBACK(update_apply_button_cb), resizer);

	priv->custom_size_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Custom size:")));
	gtk_check_button_set_group(priv->custom_size_radiobutton, priv->default_size_radiobutton);
	priv->width_spinbutton = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 9999, 1));
	gtk_spin_button_set_value(priv->width_spinbutton, 1000);
	priv->height_spinbutton = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 9999, 1));
	gtk_spin_button_set_value(priv->height_spinbutton, 1000);
	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(priv->custom_size_radiobutton));
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(priv->width_spinbutton));
	gtk_box_append(GTK_BOX(row), gtk_label_new("x"));
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(priv->height_spinbutton));
	gtk_box_append(GTK_BOX(row), gtk_label_new(_("pixels")));
	gtk_box_append(GTK_BOX(section), row);
	g_signal_connect(priv->custom_size_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->width_spinbutton, "value-changed", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->height_spinbutton, "value-changed", G_CALLBACK(update_apply_button_cb), resizer);

	label = gtk_label_new(_("Resampling"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(box), label);

	section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(box), section);
	priv->quality_high_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Sharp/detailed: Lanczos")));
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->quality_high_radiobutton));
	priv->quality_balanced_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Balanced: Mitchell")));
	gtk_check_button_set_group(priv->quality_balanced_radiobutton, priv->quality_high_radiobutton);
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->quality_balanced_radiobutton));
	priv->quality_soft_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Smooth/small: Triangle")));
	gtk_check_button_set_group(priv->quality_soft_radiobutton, priv->quality_high_radiobutton);
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->quality_soft_radiobutton));
	g_signal_connect(priv->quality_high_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->quality_balanced_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->quality_soft_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);

	label = gtk_label_new(_("Rotation"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(box), label);

	section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(box), section);
	priv->default_angle_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Select an angle:")));
	gtk_check_button_set_active(priv->default_angle_radiobutton, TRUE);
	angle_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
	gtk_combo_box_text_append_text(angle_combo, _("90 degrees clockwise"));
	gtk_combo_box_text_append_text(angle_combo, _("90 degrees counter-clockwise"));
	gtk_combo_box_text_append_text(angle_combo, _("180 degrees"));
	priv->angle_combobox = GTK_COMBO_BOX(angle_combo);
	gtk_combo_box_set_active(priv->angle_combobox, 0);
	row = new_labeled_row(GTK_WIDGET(priv->default_angle_radiobutton), GTK_WIDGET(priv->angle_combobox), NULL);
	gtk_box_append(GTK_BOX(section), row);
	g_signal_connect(priv->angle_combobox, "changed", G_CALLBACK(update_apply_button_cb), resizer);
	priv->custom_angle_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Custom angle:")));
	gtk_check_button_set_group(priv->custom_angle_radiobutton, priv->default_angle_radiobutton);
	priv->angle_spinbutton = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 360, 1));
	gtk_spin_button_set_value(priv->angle_spinbutton, 90);
	row = new_labeled_row(GTK_WIDGET(priv->custom_angle_radiobutton), GTK_WIDGET(priv->angle_spinbutton), gtk_label_new(_("degrees clockwise")));
	gtk_box_append(GTK_BOX(section), row);
	g_signal_connect(priv->default_angle_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->custom_angle_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->angle_spinbutton, "value-changed", G_CALLBACK(update_apply_button_cb), resizer);

	label = gtk_label_new(_("Encoding"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(box), label);

	section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(box), section);
	priv->encoding_quality_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("JPEG quality:")));
	gtk_check_button_set_active(priv->encoding_quality_radiobutton, TRUE);
	priv->jpeg_quality_spinbutton = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 100, 1));
	gtk_spin_button_set_value(priv->jpeg_quality_spinbutton, 85);
	row = new_labeled_row(GTK_WIDGET(priv->encoding_quality_radiobutton), GTK_WIDGET(priv->jpeg_quality_spinbutton), NULL);
	gtk_box_append(GTK_BOX(section), row);
	g_signal_connect(priv->jpeg_quality_spinbutton, "value-changed", G_CALLBACK(update_apply_button_cb), resizer);
	priv->encoding_target_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Target file size:")));
	gtk_check_button_set_group(priv->encoding_target_radiobutton, priv->encoding_quality_radiobutton);
	priv->target_size_spinbutton = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 10000, 1));
	gtk_spin_button_set_value(priv->target_size_spinbutton, 50);
	priv->target_size_unit_combobox = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
	gtk_combo_box_text_append_text(priv->target_size_unit_combobox, "KB");
	gtk_combo_box_text_append_text(priv->target_size_unit_combobox, "MB");
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->target_size_unit_combobox), 0);
	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(priv->encoding_target_radiobutton));
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(priv->target_size_spinbutton));
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(priv->target_size_unit_combobox));
	gtk_box_append(GTK_BOX(section), row);
	g_signal_connect(priv->encoding_quality_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->encoding_target_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->target_size_spinbutton, "value-changed", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->target_size_unit_combobox, "changed", G_CALLBACK(update_apply_button_cb), resizer);

	label = gtk_label_new(_("Output"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(box), label);

	section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(box), section);
	priv->append_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Append")));
	priv->name_entry = GTK_ENTRY(gtk_entry_new());
	gtk_editable_set_text(GTK_EDITABLE(priv->name_entry), ".resized");
	row = new_labeled_row(GTK_WIDGET(priv->append_radiobutton), GTK_WIDGET(priv->name_entry), NULL);
	gtk_box_append(GTK_BOX(section), row);
	priv->inplace_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Replace originals")));
	gtk_check_button_set_group(priv->inplace_radiobutton, priv->append_radiobutton);
	gtk_check_button_set_active(priv->append_radiobutton, TRUE);
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->inplace_radiobutton));

	priv->use_target_size = FALSE;
	priv->target_size_kb = 50;
	priv->target_size_available = TRUE;

	/*
	 *
	 * --- START OF UPDATED CODE ---
	 *
	 */

	gboolean cjpeg_available = g_file_test(CJPEG_PATH, G_FILE_TEST_IS_EXECUTABLE);
	gboolean convert_available = g_file_test(MAGICK_PATH, G_FILE_TEST_IS_EXECUTABLE);

	gboolean all_are_supported = TRUE;
	gboolean has_jpeg = FALSE;

	if (convert_available) /* 'convert' is the minimum requirement now */
	{
		GList *file_list = priv->files;
		for (; file_list != NULL; file_list = file_list->next)
		{
			NautilusFileInfo *file = NAUTILUS_FILE_INFO(file_list->data);
			gchar *mime_type = nautilus_file_info_get_mime_type(file);

			if (g_strcmp0(mime_type, "image/jpeg") == 0)
			{
				has_jpeg = TRUE;
			}
			else
			{
				/* Unsupported file type found */
				all_are_supported = FALSE;
				g_free(mime_type);
				break;
			}
			g_free(mime_type);
		}
	}
	else
	{
		all_are_supported = FALSE; /* Can't do anything without convert */
	}

	/* 3. If any file is unsupported, disable the target-size encoder option */
	if (!all_are_supported || !convert_available)
	{
		priv->target_size_available = FALSE;

		if (!convert_available)
		{
			gtk_widget_set_tooltip_text(GTK_WIDGET(priv->encoding_target_radiobutton),
										_("'convert' (ImageMagick) not found. Please install it to use this feature."));
		}
		else
		{
			gtk_widget_set_tooltip_text(GTK_WIDGET(priv->encoding_target_radiobutton),
									_("Target file size is only available when all selected files are JPEG images."));
		}
	}
	/* 4. If we have JPEGs but no mozjpeg, warn the user */
	else if (has_jpeg && !cjpeg_available)
	{
		priv->target_size_available = FALSE;
		gtk_widget_set_tooltip_text(GTK_WIDGET(priv->encoding_target_radiobutton),
									_("mozjpeg cjpeg is not available for JPEG target-size encoding."));
	}
	else
	{
		/* 5. THIS IS THE NEW BLOCK: Set the default tooltip */
		gtk_widget_set_tooltip_text(GTK_WIDGET(priv->encoding_target_radiobutton),
									_("Sets an approximate target size. The final size may vary and won't reduce further than the image's maximum compression."));
	}
	/* --- END OF UPDATED CODE ---
	 *
	 */

	/* Set default item in combo box */
	/* gtk_combo_box_set_active (priv->size_combobox, 4); 1024x768 */

	/* Connect signal */
	g_signal_connect(G_OBJECT(priv->resize_dialog), "response", (GCallback)nautilus_image_resizer_response_cb, resizer);
	update_apply_button_cb(NULL, resizer);
}

NautilusImageResizer *
nautilus_image_resizer_new(GList *files)
{
	return g_object_new(NAUTILUS_TYPE_IMAGE_RESIZER, "files", files, NULL);
}

void nautilus_image_resizer_show_dialog(NautilusImageResizer *resizer)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);

	gtk_window_present(GTK_WINDOW(priv->resize_dialog));
}
