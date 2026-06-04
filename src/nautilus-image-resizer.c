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
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <nautilus-extension.h>

typedef struct _NautilusImageResizerPrivate NautilusImageResizerPrivate;

struct _NautilusImageResizerPrivate
{
	GList *files;

	gchar *suffix;

	int images_resized;
	int images_total;
	gboolean cancelled;

	gchar *size;
	gchar *filter;
	gint jpeg_quality;

	GtkDialog *resize_dialog;
	GtkWidget *resize_button;
	GtkCheckButton *operation_resize_radiobutton;
	GtkCheckButton *operation_compress_radiobutton;
	GtkCheckButton *operation_resize_compress_radiobutton;
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

	GtkWidget *progress_dialog;
	GtkWidget *progress_bar;
	GtkWidget *progress_label;

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
};

#define NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE((o), NAUTILUS_TYPE_IMAGE_RESIZER, NautilusImageResizerPrivate))

G_DEFINE_TYPE(NautilusImageResizer, nautilus_image_resizer, G_TYPE_OBJECT)

enum
{
	PROP_FILES = 1,
};

typedef enum
{
	/* Place Signal Types Here */
	SIGNAL_TYPE_EXAMPLE,
	LAST_SIGNAL
} NautilusImageResizerSignalType;

static void
nautilus_image_resizer_finalize(GObject *object)
{
	NautilusImageResizer *dialog = NAUTILUS_IMAGE_RESIZER(object);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(dialog);

	g_free(priv->suffix);
	g_free(priv->filter);

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
		priv->images_total = g_list_length(priv->files);
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

static void run_op(NautilusImageResizer *resizer);

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

static void
update_apply_button_cb(GtkCheckButton *button, gpointer user_data)
{
	NautilusImageResizer *resizer = NAUTILUS_IMAGE_RESIZER(user_data);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	gboolean resize_selected;
	gboolean compress_selected;
	gboolean operation_selected;
	gboolean resampling_selected;
	gboolean encoding_selected;
	gboolean valid;

	operation_selected = gtk_check_button_get_active(priv->operation_resize_radiobutton) ||
		gtk_check_button_get_active(priv->operation_compress_radiobutton) ||
		gtk_check_button_get_active(priv->operation_resize_compress_radiobutton);
	resize_selected = gtk_check_button_get_active(priv->operation_resize_radiobutton) ||
		gtk_check_button_get_active(priv->operation_resize_compress_radiobutton);
	compress_selected = gtk_check_button_get_active(priv->operation_compress_radiobutton) ||
		gtk_check_button_get_active(priv->operation_resize_compress_radiobutton);
	resampling_selected = gtk_check_button_get_active(priv->quality_high_radiobutton) ||
		gtk_check_button_get_active(priv->quality_balanced_radiobutton) ||
		gtk_check_button_get_active(priv->quality_soft_radiobutton);
	encoding_selected = gtk_check_button_get_active(priv->encoding_quality_radiobutton) ||
		gtk_check_button_get_active(priv->encoding_target_radiobutton);
	valid = operation_selected && (!resize_selected || resampling_selected) && (!compress_selected || encoding_selected);

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
	gtk_widget_set_sensitive(GTK_WIDGET(priv->encoding_quality_radiobutton), compress_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->jpeg_quality_spinbutton), compress_selected);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->encoding_target_radiobutton), compress_selected && priv->target_size_available);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->target_size_spinbutton), compress_selected && priv->target_size_available);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->target_size_unit_combobox), compress_selected && priv->target_size_available);
	gtk_widget_set_sensitive(priv->resize_button, valid);
}

static void
create_progress_dialog(NautilusImageResizer *resizer)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	GtkWidget *content;
	GtkWidget *box;

	priv->progress_dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(priv->progress_dialog), _("Transforming Images"));
	gtk_window_set_modal(GTK_WINDOW(priv->progress_dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(priv->progress_dialog), 360, -1);

	content = gtk_dialog_get_content_area(GTK_DIALOG(priv->progress_dialog));
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_box_append(GTK_BOX(content), box);

	priv->progress_label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(priv->progress_label), 0.0);
	gtk_box_append(GTK_BOX(box), priv->progress_label);

	priv->progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(priv->progress_bar), TRUE);
	gtk_box_append(GTK_BOX(box), priv->progress_bar);

	gtk_window_present(GTK_WINDOW(priv->progress_dialog));
}

static gchar *
build_resize_args(NautilusImageResizerPrivate *priv)
{
	gchar *filter;
	gchar *size;
	gchar *args;

	if (priv->size == NULL)
		return g_strdup("");

	filter = g_shell_quote(priv->filter == NULL ? "Lanczos" : priv->filter);
	size = g_shell_quote(priv->size);
	args = g_strdup_printf(" -filter %s -resize %s", filter, size);
	g_free(filter);
	g_free(size);

	return args;
}

static gchar *
build_jpeg_command(NautilusImageResizerPrivate *priv, const gchar *filename, const gchar *new_filename)
{
	gchar *input = g_shell_quote(filename);
	gchar *output = g_shell_quote(new_filename);
	gchar *magick = g_shell_quote(MAGICK_PATH);
	gchar *cjpeg = g_shell_quote(CJPEG_PATH);
	gchar *resize_args = build_resize_args(priv);
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
			magick, input, resize_args, cjpeg, output, output, priv->target_size_kb * 1024, cjpeg, output);
	} else {
		command = g_strdup_printf("%s %s%s ppm:- | %s -quality %d -outfile %s",
			magick, input, resize_args, cjpeg, priv->jpeg_quality, output);
	}

	g_free(input);
	g_free(output);
	g_free(magick);
	g_free(cjpeg);
	g_free(resize_args);

	return command;
}

static gchar *
build_imagemagick_command(NautilusImageResizerPrivate *priv, const gchar *filename, const gchar *new_filename)
{
	gchar *input = g_shell_quote(filename);
	gchar *output = g_shell_quote(new_filename);
	gchar *magick = g_shell_quote(MAGICK_PATH);
	gchar *resize_args = build_resize_args(priv);
	gchar *command;

	command = g_strdup_printf("%s %s%s %s", magick, input, resize_args, output);
	g_free(input);
	g_free(output);
	g_free(magick);
	g_free(resize_args);

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

static void
op_finished(GPid pid, gint status, gpointer data)
{
	NautilusImageResizer *resizer = NAUTILUS_IMAGE_RESIZER(data);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);

	gboolean retry = TRUE;

	NautilusFileInfo *file = NAUTILUS_FILE_INFO(priv->files->data);

	if (status != 0)
	{
		/* resizing failed */
		char *name = nautilus_file_info_get_name(file);

		char *message = g_strdup_printf("'%s' cannot be resized. Check whether you have permission to write to this folder.", name);
		g_free(name);
		show_error_dialog(GTK_WINDOW(priv->progress_dialog), message);
		g_free(message);
		retry = FALSE;
	}
	else if (priv->suffix == NULL)
	{
		/* resize image in place */
		GFile *orig_location = nautilus_file_info_get_location(file);
		GFile *new_location = nautilus_image_resizer_transform_filename(resizer, orig_location);
		g_file_move(new_location, orig_location, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
		g_object_unref(orig_location);
		g_object_unref(new_location);
	}

	if (status == 0 || !retry)
	{
		/* image has been successfully resized (or skipped) */
		priv->images_resized++;
		priv->files = priv->files->next;
	}

	if (!priv->cancelled && priv->files != NULL)
	{
		/* process next image */
		run_op(resizer);
	}
	else
	{
		/* cancel/terminate operation */
		gtk_window_destroy(GTK_WINDOW(priv->progress_dialog));
	}
}
static void
run_op(NautilusImageResizer *resizer)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);

	g_return_if_fail(priv->files != NULL);

	NautilusFileInfo *file = NAUTILUS_FILE_INFO(priv->files->data);

	GFile *orig_location = nautilus_file_info_get_location(file);
	char *filename = g_file_get_path(orig_location);
	GFile *new_location = nautilus_image_resizer_transform_filename(resizer, orig_location);
	char *new_filename = g_file_get_path(new_location);
	g_object_unref(orig_location);
	g_object_unref(new_location);

	gchar *argv[4];
	pid_t pid;
	gchar *command = NULL;
	gboolean spawn_success;
	gchar *mime_type = nautilus_file_info_get_mime_type(file);

	if (g_strcmp0(mime_type, "image/jpeg") == 0 || g_strcmp0(mime_type, "image/jpg") == 0)
		command = build_jpeg_command(priv, filename, new_filename);
	else
		command = build_imagemagick_command(priv, filename, new_filename);

	argv[0] = "/bin/sh";
	argv[1] = "-c";
	argv[2] = command;
	argv[3] = NULL;
	spawn_success = g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL);
	g_free(mime_type);

	if (!spawn_success)
	{
		/* Handle spawn failure */
		g_free(filename);
		g_free(new_filename);
		g_free(command);
		g_warning("Failed to spawn command");
		/* FIXME: We should probably call op_finished with an error */
		return;
	}

	/* This part is now common to all commands */
	g_child_watch_add(pid, op_finished, resizer);

	g_free(filename);
	g_free(new_filename);
	g_free(command);

	char *tmp;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress_bar), (double)(priv->images_resized + 1) / priv->images_total);
	tmp = g_strdup_printf(_("Transforming image: %d of %d"), priv->images_resized + 1, priv->images_total);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(priv->progress_bar), tmp);
	g_free(tmp);

	char *name = nautilus_file_info_get_name(file);
	tmp = g_strdup_printf(_("<i>Transforming \"%s\"</i>"), name);
	g_free(name);
	gtk_label_set_markup(GTK_LABEL(priv->progress_label), tmp);
	g_free(tmp);
}

static void
nautilus_image_resizer_response_cb(GtkDialog *dialog, gint response_id, gpointer user_data)
{
	NautilusImageResizer *resizer = NAUTILUS_IMAGE_RESIZER(user_data);
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);

	if (response_id == GTK_RESPONSE_OK)
	{
		gboolean resize_selected = gtk_check_button_get_active(priv->operation_resize_radiobutton) ||
			gtk_check_button_get_active(priv->operation_resize_compress_radiobutton);
		gboolean compress_selected = gtk_check_button_get_active(priv->operation_compress_radiobutton) ||
			gtk_check_button_get_active(priv->operation_resize_compress_radiobutton);

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

		create_progress_dialog(resizer);
		run_op(resizer);
	}

	gtk_window_destroy(GTK_WINDOW(dialog));
}
static void
nautilus_image_resizer_init(NautilusImageResizer *resizer)
{
	NautilusImageResizerPrivate *priv = NAUTILUS_IMAGE_RESIZER_GET_PRIVATE(resizer);
	GtkWidget *content;
	GtkWidget *box;
	GtkWidget *section;
	GtkWidget *label;
	GtkWidget *row;

	priv->resize_dialog = GTK_DIALOG(gtk_dialog_new());
	gtk_window_set_title(GTK_WINDOW(priv->resize_dialog), _("Transform Images"));
	gtk_window_set_modal(GTK_WINDOW(priv->resize_dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(priv->resize_dialog), 420, -1);
	gtk_dialog_add_button(priv->resize_dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
	priv->resize_button = gtk_dialog_add_button(priv->resize_dialog, _("_Apply"), GTK_RESPONSE_OK);
	gtk_widget_set_sensitive(priv->resize_button, FALSE);
	gtk_dialog_set_default_response(priv->resize_dialog, GTK_RESPONSE_OK);

	content = gtk_dialog_get_content_area(priv->resize_dialog);
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_box_append(GTK_BOX(content), box);

	label = gtk_label_new(_("Operation"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	gtk_box_append(GTK_BOX(box), label);

	section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(box), section);
	priv->operation_resize_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Resize dimensions")));
	gtk_check_button_set_active(priv->operation_resize_radiobutton, TRUE);
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->operation_resize_radiobutton));
	priv->operation_compress_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Re-encode/compress only")));
	gtk_check_button_set_group(priv->operation_compress_radiobutton, priv->operation_resize_radiobutton);
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->operation_compress_radiobutton));
	priv->operation_resize_compress_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Resize and re-encode")));
	gtk_check_button_set_group(priv->operation_resize_compress_radiobutton, priv->operation_resize_radiobutton);
	gtk_box_append(GTK_BOX(section), GTK_WIDGET(priv->operation_resize_compress_radiobutton));
	g_signal_connect(priv->operation_resize_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->operation_compress_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);
	g_signal_connect(priv->operation_resize_compress_radiobutton, "toggled", G_CALLBACK(update_apply_button_cb), resizer);

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

	priv->custom_pct_radiobutton = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Scale:")));
	gtk_check_button_set_group(priv->custom_pct_radiobutton, priv->default_size_radiobutton);
	priv->pct_spinbutton = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 100, 1));
	gtk_spin_button_set_value(priv->pct_spinbutton, 50);
	row = new_labeled_row(GTK_WIDGET(priv->custom_pct_radiobutton), GTK_WIDGET(priv->pct_spinbutton), gtk_label_new("%"));
	gtk_box_append(GTK_BOX(section), row);

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
	gboolean has_png = FALSE;

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
			else if (g_strcmp0(mime_type, "image/png") == 0)
			{
				has_png = TRUE;
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
										_("This option is only available for JPEG or PNG files."));
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
