/*
    This file is part of darktable,
    copyright (c) 2009--2010 johannes hanika.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "common/darktable.h"
#include "common/debug.h"
#include "develop/blend.h"
#include "develop/develop.h"
#include "gui/gtk.h"
#include "gui/accelerators.h"
#include "dtgtk/tristatebutton.h"
#include <stdlib.h>


static const int   dt_gui_presets_exposure_value_cnt = 24;
static const float dt_gui_presets_exposure_value[] =
{
  0., 1./8000, 1./4000, 1./2000, 1./1000, 1./1000, 1./500, 1./250, 1./125, 1./60, 1./30, 1./15,
  1./15, 1./8, 1./4, 1./2, 1, 2, 4, 8, 15, 30, 60, FLT_MAX
};
static const char* dt_gui_presets_exposure_value_str[] =
{
  "0", "1/8000", "1/4000", "1/2000", "1/1000", "1/1000", "1/500", "1/250", "1/125", "1/60", "1/30", "1/15",
  "1/15", "1/8", "1/4", "1/2", "1\"", "2\"", "4\"", "8\"", "15\"", "30\"", "60\"", "+"
};
static const int   dt_gui_presets_aperture_value_cnt = 19;
static const float dt_gui_presets_aperture_value[] = {0, 0.5, 0.7, 1.0, 1.4, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0,
    22.0, 32.0, 45.0, 64.0, 90.0, 128.0, FLT_MAX
                                                     };
static const char* dt_gui_presets_aperture_value_str[] = {"f/0", "f/0.5", "f/0.7", "f/1.0", "f/1.4", "f/2",
    "f/2.8", "f/4", "f/5.6", "f/8", "f/11", "f/16", "f/22", "f/32", "f/45", "f/64", "f/90", "f/128", "f/+"
                                                         };

typedef struct dt_gui_presets_edit_dialog_t
{
  dt_iop_module_t *module;
  GtkEntry *name, *description;
  GtkCheckButton *autoapply, *filter;
  GtkBox *details;
  GtkEntry *model, *maker, *lens;
  GtkSpinButton *iso_min, *iso_max;
  GtkComboBox *exposure_min, *exposure_max;
  GtkComboBox *aperture_min, *aperture_max;
  GtkSpinButton *focal_length_min, *focal_length_max;
  gchar *original_name;
  gint old_id;
}
dt_gui_presets_edit_dialog_t;

void dt_gui_presets_init()
{
  // this is also called for non-gui applications linking to libdarktable!
  // so beware, don't use any darktable.gui stuff here .. (or change this behaviour it in darktable.c)
  // create table or fail if it is already there.
  sqlite3_exec(dt_database_get(darktable.db), "create table presets "
               "(name varchar, description varchar, operation varchar, op_version integer, op_params blob, enabled integer, "
               "blendop_params blob, "
               "model varchar, maker varchar, lens varchar, "
               "iso_min real, iso_max real, exposure_min real, exposure_max real, aperture_min real, aperture_max real, "
               "focal_length_min real, focal_length_max real, "
               "writeprotect integer, autoapply integer, filter integer, def integer, isldr integer)", NULL, NULL, NULL);

  // add the op_version field; fail silently if it's already there
  sqlite3_exec(dt_database_get(darktable.db), "alter table presets add column op_version integer", NULL, NULL, NULL);

  // remove auto generated presets from plugins, not the user included ones.
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "delete from presets where writeprotect=1", NULL, NULL, NULL);
}

void dt_gui_presets_add_generic(const char *name, dt_dev_operation_t op, const int32_t version, const void *params, const int32_t params_size, const int32_t enabled)
{
  sqlite3_stmt *stmt;
  dt_develop_blend_params_t default_blendop_params = {DEVELOP_BLEND_DISABLED,100.0,0};
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from presets where name=?1 and operation=?2 and op_version=?3", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, name, strlen(name), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, version);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into presets (name, description, operation, op_version, op_params, enabled, blendop_params, model, maker, lens, "
             "iso_min, iso_max, exposure_min, exposure_max, aperture_min, aperture_max, focal_length_min, focal_length_max, "
             "writeprotect, autoapply, filter, def, isldr) "
             "values (?1, '', ?2, ?3, ?4, ?5, ?6, '%', '%', '%', 0, 51200, 0, 10000000, 0, 100000000, 0, 1000, 1, 0, 0, 0, 0)", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, name, strlen(name), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, version);
  DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 4, params, params_size, SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 5, enabled);
  DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 6, &default_blendop_params, sizeof(dt_develop_blend_params_t), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

static gchar*
get_preset_name(GtkMenuItem *menuitem)
{
  const gchar *name = gtk_label_get_label(GTK_LABEL(gtk_bin_get_child(GTK_BIN(menuitem))));
  const gchar *c = name;
  // remove <-> markup tag at beginning.
  if(*c == '<')
  {
    while(*c != '>') c++;
    c++;
  }
  gchar *pn = g_strdup(c);
  gchar *c2 = pn;
  // possibly remove trailing <-> markup tag
  while(*c2 != '<' && *c2 != '\0') c2++;
  if(*c2 == '<') *c2 = '\0';
  c2 = g_strrstr(pn, _("(default)"));
  if(c2 && c2 > pn) *(c2-1) = '\0';
  return pn;
}

static gchar*
get_active_preset_name(dt_iop_module_t *module)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select name, op_params, blendop_params, writeprotect from presets where operation=?1 and op_version=?2", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, strlen(module->op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
  gchar *name = NULL;
  // collect all presets for op from db
  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    void *op_params = (void *)sqlite3_column_blob(stmt, 1);
    int32_t op_params_size = sqlite3_column_bytes(stmt, 1);
    void *blendop_params = (void *)sqlite3_column_blob(stmt, 2);
    int32_t bl_params_size = sqlite3_column_bytes(stmt, 2);
    if(!memcmp(module->params, op_params, MIN(op_params_size, module->params_size)) && 
       !memcmp(module->blend_params, blendop_params, MIN(bl_params_size, sizeof(dt_develop_blend_params_t))))
    {
      name = g_strdup((char *)sqlite3_column_text(stmt, 0));
      break;
    }
  }
  sqlite3_finalize(stmt);
  return name;
}

static void
menuitem_delete_preset (GtkMenuItem *menuitem, dt_iop_module_t *module)
{
  sqlite3_stmt *stmt;
  gchar *name = get_active_preset_name(module);
  if(name == NULL) return;

  GtkWidget *window = dt_ui_main_window(darktable.gui->ui);
  GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_YES_NO,
                                            _("do you really want to delete the preset `%s'?"), name);
  gtk_window_set_title(GTK_WINDOW (dialog), _("delete preset?"));
  if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES)
  {
    char tmp_path[1024];
    snprintf(tmp_path,1024,"%s/%s",_("preset"),name);
    dt_accel_deregister_iop(module,tmp_path);
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from presets where name=?1 and operation=?2 and op_version=?3 and writeprotect=0", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, name, strlen(name), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, module->op, strlen(module->op), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, module->version());
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  g_free(name);
  gtk_widget_destroy (dialog);
}

static void
edit_preset_response(GtkDialog *dialog, gint response_id, dt_gui_presets_edit_dialog_t *g)
{
  if(response_id == GTK_RESPONSE_ACCEPT)
  {
    // now delete preset, so we can re-insert the new values:
    sqlite3_stmt *stmt;
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from presets where rowid=?1", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, g->old_id );
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    //rename accerelartors
    char path[1024];
    snprintf(path,1024,"%s/%s",_("preset"),g->original_name);
    dt_accel_rename_preset_iop(g->module,path,gtk_entry_get_text(g->name));
    // commit all the user input fields
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "insert into presets (name, description, operation, op_version, op_params, enabled, blendop_params, "
            "model, maker, lens, iso_min, iso_max, exposure_min, exposure_max, aperture_min, aperture_max, "
            "focal_length_min, focal_length_max, writeprotect, autoapply, filter, def, isldr) "
            "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, 0, ?19, ?20, 0, 0)", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, gtk_entry_get_text(g->name), strlen(gtk_entry_get_text(g->name)), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, gtk_entry_get_text(g->description), strlen(gtk_entry_get_text(g->description)), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, g->module->op, strlen(g->module->op), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 4, g->module->version() );
    DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 5, g->module->params, g->module->params_size, SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 6, g->module->enabled);
    DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 7, g->module->blend_params, sizeof(dt_develop_blend_params_t), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 8, gtk_entry_get_text(g->model), strlen(gtk_entry_get_text(g->model)), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 9, gtk_entry_get_text(g->maker), strlen(gtk_entry_get_text(g->maker)), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 10, gtk_entry_get_text(g->lens), strlen(gtk_entry_get_text(g->lens)), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt,  11, gtk_spin_button_get_value(g->iso_min));
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 12, gtk_spin_button_get_value(g->iso_max));
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 13, dt_gui_presets_exposure_value[gtk_combo_box_get_active(g->exposure_min)]);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 14, dt_gui_presets_exposure_value[gtk_combo_box_get_active(g->exposure_max)]);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 15, dt_gui_presets_aperture_value[gtk_combo_box_get_active(g->aperture_min)]);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 16, dt_gui_presets_aperture_value[gtk_combo_box_get_active(g->aperture_max)]);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 17, gtk_spin_button_get_value(g->focal_length_min));
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 18, gtk_spin_button_get_value(g->focal_length_max));
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 19, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->autoapply)));
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 20, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->filter)));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  gtk_widget_destroy(GTK_WIDGET(dialog));
  g_free(g->original_name);
  free(g);
}

static void
check_buttons_activated (GtkCheckButton *button, dt_gui_presets_edit_dialog_t *g)
{
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->autoapply)) || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->filter)))
  {
    gtk_widget_set_visible(GTK_WIDGET(g->details), TRUE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->details), FALSE);
    gtk_widget_show_all (GTK_WIDGET(g->details));
    gtk_widget_set_no_show_all(GTK_WIDGET(g->details), TRUE);
  }
  else
    gtk_widget_set_visible(GTK_WIDGET(g->details), FALSE);
}

static void
edit_preset (const char *name_in, dt_iop_module_t *module)
{
  gchar *name = NULL;
  if(name_in == NULL)
  {
    name = get_active_preset_name(module);
    if(name == NULL) return;
  }
  else name = g_strdup(name_in);

  GtkWidget *dialog;
  /* Create the widgets */
  char title[1024];
  GtkWidget *window = dt_ui_main_window(darktable.gui->ui);
  snprintf(title, 1024, _("edit `%s' for module `%s'"), name, module->name());
  dialog = gtk_dialog_new_with_buttons (title,
                                        GTK_WINDOW(window),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                        NULL);
  GtkContainer *content_area = GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog)));
  GtkWidget *alignment = gtk_alignment_new(0.5, 0.5, 1.0, 1.0);
  gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 5, 5, 5, 5);
  gtk_container_add (content_area, alignment);
  GtkBox *box = GTK_BOX(gtk_vbox_new(FALSE, 5));
  gtk_container_add (GTK_CONTAINER(alignment), GTK_WIDGET(box));
  GtkWidget *label;
  // GtkBox *vbox1 = GTK_BOX(gtk_vbox_new(TRUE, 5));

  GtkBox *vbox2 = GTK_BOX(gtk_vbox_new(TRUE, 5));
  GtkBox *vbox3 = GTK_BOX(gtk_vbox_new(TRUE, 5));
  GtkBox *vbox4 = GTK_BOX(gtk_vbox_new(TRUE, 5));

  dt_gui_presets_edit_dialog_t *g = (dt_gui_presets_edit_dialog_t *)malloc(sizeof(dt_gui_presets_edit_dialog_t));
  g->old_id = -1;
  g->original_name = name;
  g->module = module;
  g->name = GTK_ENTRY(gtk_entry_new());
  gtk_entry_set_text(g->name, name);
  gtk_box_pack_start(box, GTK_WIDGET(g->name), FALSE, FALSE, 0);
  g_object_set(G_OBJECT(g->name), "tooltip-text", _("name of the preset"), (char *)NULL);

  g->description = GTK_ENTRY(gtk_entry_new());
  gtk_box_pack_start(box, GTK_WIDGET(g->description), FALSE, FALSE, 0);
  g_object_set(G_OBJECT(g->description), "tooltip-text", _("description or further information"), (char *)NULL);

  g->autoapply = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("auto apply this preset to matching images")));
  gtk_box_pack_start(box, GTK_WIDGET(g->autoapply), FALSE, FALSE, 0);
  g->filter = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("only show this preset for matching images")));
  g_object_set(G_OBJECT(g->filter), "tooltip-text", _("be very careful with this option. this might be the last time you see your preset."), (char *)NULL);
  gtk_box_pack_start(box, GTK_WIDGET(g->filter), FALSE, FALSE, 0);
  g_signal_connect(G_OBJECT(g->autoapply), "toggled", G_CALLBACK(check_buttons_activated), g);
  g_signal_connect(G_OBJECT(g->filter),    "toggled", G_CALLBACK(check_buttons_activated), g);

  g->details   = GTK_BOX(gtk_hbox_new(FALSE, 0));
  GtkBox *hbox = GTK_BOX(gtk_hbox_new(TRUE, 5));
  gtk_box_pack_start(box,  GTK_WIDGET(g->details),  FALSE, FALSE, 0);
  gtk_box_pack_start(g->details, GTK_WIDGET(hbox),  FALSE, FALSE, 0);
  gtk_box_pack_start(hbox, GTK_WIDGET(vbox2), TRUE, TRUE, 0);
  gtk_box_pack_start(hbox, GTK_WIDGET(vbox3), TRUE, TRUE, 0);
  gtk_box_pack_start(hbox, GTK_WIDGET(vbox4), TRUE, TRUE, 0);

  // model, maker, lens
  g->model = GTK_ENTRY(gtk_entry_new());
  g_object_set(G_OBJECT(g->model), "tooltip-text", _("string to match model (use % as wildcard)"), (char *)NULL);
  label = gtk_label_new(_("model"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(vbox2, label, FALSE, FALSE, 0);
  gtk_box_pack_start(vbox3, GTK_WIDGET(g->model), FALSE, FALSE, 0);
  gtk_box_pack_start(vbox4, gtk_label_new(""), FALSE, FALSE, 0);
  g->maker = GTK_ENTRY(gtk_entry_new());
  g_object_set(G_OBJECT(g->maker), "tooltip-text", _("string to match maker (use % as wildcard)"), (char *)NULL);
  label = gtk_label_new(_("maker"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(vbox2, label, FALSE, FALSE, 0);
  gtk_box_pack_start(vbox3, GTK_WIDGET(g->maker), FALSE, FALSE, 0);
  gtk_box_pack_start(vbox4, gtk_label_new(""), FALSE, FALSE, 0);
  g->lens  = GTK_ENTRY(gtk_entry_new());
  g_object_set(G_OBJECT(g->lens), "tooltip-text", _("string to match lens (use % as wildcard)"), (char *)NULL);
  label = gtk_label_new(_("lens"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(vbox2, label, FALSE, FALSE, 0);
  gtk_box_pack_start(vbox3, GTK_WIDGET(g->lens), FALSE, FALSE, 0);
  gtk_box_pack_start(vbox4, gtk_label_new(""), FALSE, FALSE, 0);

  // iso
  label = gtk_label_new(_("iso"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(vbox2, label, FALSE, FALSE, 0);
  g->iso_min = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 51200, 100));
  g_object_set(G_OBJECT(g->iso_min), "tooltip-text", _("minimum iso value"), (char *)NULL);
  gtk_spin_button_set_digits(g->iso_min, 0);
  gtk_box_pack_start(vbox3, GTK_WIDGET(g->iso_min), FALSE, FALSE, 0);
  g->iso_max = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 51200, 100));
  g_object_set(G_OBJECT(g->iso_max), "tooltip-text", _("maximum iso value"), (char *)NULL);
  gtk_spin_button_set_digits(g->iso_max, 0);
  gtk_box_pack_start(vbox4, GTK_WIDGET(g->iso_max), FALSE, FALSE, 0);

  // exposure
  label = gtk_label_new(_("exposure"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(vbox2, label, FALSE, FALSE, 0);
  g->exposure_min = GTK_COMBO_BOX(gtk_combo_box_new_text());
  g->exposure_max = GTK_COMBO_BOX(gtk_combo_box_new_text());
  g_object_set(G_OBJECT(g->exposure_min), "tooltip-text", _("minimum exposure time"), (char *)NULL);
  g_object_set(G_OBJECT(g->exposure_max), "tooltip-text", _("maximum exposure time"), (char *)NULL);
  for(int k=0; k<dt_gui_presets_exposure_value_cnt; k++)
    gtk_combo_box_append_text(g->exposure_min, dt_gui_presets_exposure_value_str[k]);
  for(int k=0; k<dt_gui_presets_exposure_value_cnt; k++)
    gtk_combo_box_append_text(g->exposure_max, dt_gui_presets_exposure_value_str[k]);
  gtk_box_pack_start(vbox3, GTK_WIDGET(g->exposure_min), FALSE, FALSE, 0);
  gtk_box_pack_start(vbox4, GTK_WIDGET(g->exposure_max), FALSE, FALSE, 0);

  // aperture
  label = gtk_label_new(_("aperture"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(vbox2, label, FALSE, FALSE, 0);
  g->aperture_min = GTK_COMBO_BOX(gtk_combo_box_new_text());
  g->aperture_max = GTK_COMBO_BOX(gtk_combo_box_new_text());
  g_object_set(G_OBJECT(g->aperture_min), "tooltip-text", _("minimum aperture value"), (char *)NULL);
  g_object_set(G_OBJECT(g->aperture_max), "tooltip-text", _("maximum aperture value"), (char *)NULL);
  for(int k=0; k<dt_gui_presets_aperture_value_cnt; k++)
    gtk_combo_box_append_text(g->aperture_min, dt_gui_presets_aperture_value_str[k]);
  for(int k=0; k<dt_gui_presets_aperture_value_cnt; k++)
    gtk_combo_box_append_text(g->aperture_max, dt_gui_presets_aperture_value_str[k]);
  gtk_box_pack_start(vbox3, GTK_WIDGET(g->aperture_min), FALSE, FALSE, 0);
  gtk_box_pack_start(vbox4, GTK_WIDGET(g->aperture_max), FALSE, FALSE, 0);

  // focal length
  label = gtk_label_new(_("focal length"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(vbox2, label, FALSE, FALSE, 0);
  g->focal_length_min = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 1000, 10));
  gtk_spin_button_set_digits(g->focal_length_min, 0);
  gtk_box_pack_start(vbox3, GTK_WIDGET(g->focal_length_min), FALSE, FALSE, 0);
  g->focal_length_max = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 1000, 10));
  g_object_set(G_OBJECT(g->focal_length_min), "tooltip-text", _("minimum focal length"), (char *)NULL);
  g_object_set(G_OBJECT(g->focal_length_max), "tooltip-text", _("maximum focal length"), (char *)NULL);
  gtk_spin_button_set_digits(g->focal_length_max, 0);
  gtk_box_pack_start(vbox4, GTK_WIDGET(g->focal_length_max), FALSE, FALSE, 0);

  gtk_widget_set_no_show_all(GTK_WIDGET(g->details), TRUE);

  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select rowid, description, model, maker, lens, iso_min, iso_max, exposure_min, exposure_max, aperture_min, aperture_max, focal_length_min, focal_length_max, autoapply, filter from presets where name = ?1 and operation = ?2 and op_version = ?3", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, name, strlen(name), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, module->op, strlen(module->op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, module->version() );
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    g->old_id = sqlite3_column_int(stmt, 0);
    gtk_entry_set_text(g->description, (const char *)sqlite3_column_text(stmt, 1));
    gtk_entry_set_text(g->model, (const char *)sqlite3_column_text(stmt, 2));
    gtk_entry_set_text(g->maker, (const char *)sqlite3_column_text(stmt, 3));
    gtk_entry_set_text(g->lens,  (const char *)sqlite3_column_text(stmt, 4));
    gtk_spin_button_set_value(g->iso_min, sqlite3_column_double(stmt, 5));
    gtk_spin_button_set_value(g->iso_max, sqlite3_column_double(stmt, 6));

    float val = sqlite3_column_double(stmt, 7);
    int k=0;
    for(; k<dt_gui_presets_exposure_value_cnt&&val>dt_gui_presets_exposure_value[k]; k++);
    gtk_combo_box_set_active(g->exposure_min, k);
    val = sqlite3_column_double(stmt, 8);
    for(k=0; k<dt_gui_presets_exposure_value_cnt&&val>dt_gui_presets_exposure_value[k]; k++);
    gtk_combo_box_set_active(g->exposure_max, k);
    val = sqlite3_column_double(stmt, 9);
    for(k=0; k<dt_gui_presets_aperture_value_cnt&&val>dt_gui_presets_aperture_value[k]; k++);
    gtk_combo_box_set_active(g->aperture_min, k);
    val = sqlite3_column_double(stmt, 10);
    for(k=0; k<dt_gui_presets_aperture_value_cnt&&val>dt_gui_presets_aperture_value[k]; k++);
    gtk_combo_box_set_active(g->aperture_max, k);
    gtk_spin_button_set_value(g->focal_length_min, sqlite3_column_double(stmt, 11));
    gtk_spin_button_set_value(g->focal_length_max, sqlite3_column_double(stmt, 12));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->autoapply), sqlite3_column_int(stmt, 13));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->filter),    sqlite3_column_int(stmt, 14));
  }
  else
  {
    gtk_entry_set_text(g->description, "");
    gtk_entry_set_text(g->model, "%");
    gtk_entry_set_text(g->maker, "%");
    gtk_entry_set_text(g->lens,  "%");
    gtk_spin_button_set_value(g->iso_min, 0);
    gtk_spin_button_set_value(g->iso_max, 51200);

    float val = 0;
    int k=0;
    for(; k<dt_gui_presets_exposure_value_cnt&&val>dt_gui_presets_exposure_value[k]; k++);
    gtk_combo_box_set_active(g->exposure_min, k);
    val = 100000000;
    for(k=0; k<dt_gui_presets_exposure_value_cnt&&val>dt_gui_presets_exposure_value[k]; k++);
    gtk_combo_box_set_active(g->exposure_max, k);
    val = 0;
    for(k=0; k<dt_gui_presets_aperture_value_cnt&&val>dt_gui_presets_aperture_value[k]; k++);
    gtk_combo_box_set_active(g->aperture_min, k);
    val = 100000000;
    for(k=0; k<dt_gui_presets_aperture_value_cnt&&val>dt_gui_presets_aperture_value[k]; k++);
    gtk_combo_box_set_active(g->aperture_max, k);
    gtk_spin_button_set_value(g->focal_length_min, 0);
    gtk_spin_button_set_value(g->focal_length_max, 1000);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->autoapply), 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->filter),    0);
  }
  sqlite3_finalize(stmt);

  g_signal_connect (dialog, "response", G_CALLBACK (edit_preset_response), g);
  gtk_widget_show_all (dialog);
}

static void
menuitem_edit_preset (GtkMenuItem *menuitem, dt_iop_module_t *module)
{
  edit_preset (NULL, module);
}

static void
menuitem_new_preset (GtkMenuItem *menuitem, dt_iop_module_t *module)
{
  // add new preset
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "delete from presets where name=?1 and operation=?2 and op_version=?3", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, _("new preset"), strlen(_("new preset")), SQLITE_STATIC);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, module->op, strlen(module->op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, module->version());
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  // create a shortcut for the new entry
  char path[1024];
  snprintf(path,1024,"preset/%s",_("new preset"));
  dt_accel_register_iop(module->so,FALSE,path,0,0);
  dt_accel_connect_preset_iop(module,_("new preset"));
  // then show edit dialog
  edit_preset (_("new preset"), module);
}

static void
menuitem_pick_preset (GtkMenuItem *menuitem, dt_iop_module_t *module)
{
  gchar *name = get_preset_name(menuitem);
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select op_params, enabled, blendop_params from presets where operation = ?1 and op_version = ?2 and name = ?3", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, strlen(module->op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, name, strlen(name), SQLITE_TRANSIENT);

  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    const void *op_params = sqlite3_column_blob(stmt, 0);
    int op_length  = sqlite3_column_bytes(stmt, 0);
    int enabled = sqlite3_column_int(stmt, 1);
    const void *blendop_params = sqlite3_column_blob(stmt, 2);
    int bl_length = sqlite3_column_bytes(stmt, 2);
    if(op_params && (op_length == module->params_size))
    {
      memcpy(module->params, op_params, op_length);
      module->enabled = enabled;
    }
    if (blendop_params && (bl_length == sizeof(dt_develop_blend_params_t)))
    {
      memcpy(module->blend_params, blendop_params, sizeof(dt_develop_blend_params_t));
    }
    else
    {
      memcpy(module->blend_params, module->default_blendop_params, sizeof(dt_develop_blend_params_t));
    }
  }
  sqlite3_finalize(stmt);
  g_free(name);
  dt_iop_gui_update(module);
  dt_dev_add_history_item(darktable.develop, module, FALSE);
  gtk_widget_queue_draw(module->widget);
}

static void
menuitem_store_default (GtkMenuItem *menuitem, dt_iop_module_t *module)
{
  gchar *name = get_active_preset_name(module);
  if(name == NULL) return;
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set def=0 where operation=?1 and op_version=?2", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, strlen(module->op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set def=1 where operation=?1 and op_version=?2 and name=?3", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, strlen(module->op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  dt_iop_load_default_params(module);
}

static void
menuitem_factory_default (GtkMenuItem *menuitem, dt_iop_module_t *module)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set def=0 where operation=?1 and op_version=?2", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, strlen(module->op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  dt_iop_load_default_params(module);
}


void dt_gui_favorite_presets_menu_show()
{
  sqlite3_stmt *stmt;
  GtkMenu *menu = darktable.gui->presets_popup_menu;
  if(menu)
    gtk_widget_destroy(GTK_WIDGET(menu));
  darktable.gui->presets_popup_menu = GTK_MENU(gtk_menu_new());
  menu = darktable.gui->presets_popup_menu;
  gboolean presets = FALSE; /* TRUE if we have at least one menu entry */

  GList *modules = darktable.develop->iop;
  if (modules)
  {
    do
    {
      dt_iop_module_t *iop = (dt_iop_module_t *)modules->data;

      /* check if module is favorite */
      if(iop->showhide && dtgtk_tristatebutton_get_state (DTGTK_TRISTATEBUTTON(iop->showhide))==2)
      {
	/* create submenu for module */
	GtkMenuItem *smi = (GtkMenuItem*)gtk_menu_item_new_with_label(iop->name());
	GtkMenu *sm = (GtkMenu*) gtk_menu_new();
	gtk_menu_item_set_submenu(smi,GTK_WIDGET(sm));

	/* query presets for module */
	DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select name, op_params, writeprotect, description, blendop_params, op_version from presets where operation=?1 order by writeprotect desc, rowid", -1, &stmt, NULL);
	DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, iop->op, strlen(iop->op), SQLITE_TRANSIENT);

	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
	  GtkMenuItem *mi = (GtkMenuItem *)gtk_menu_item_new_with_label((char *)sqlite3_column_text(stmt,0));
	  g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_pick_preset), iop);
	  gtk_menu_shell_append(GTK_MENU_SHELL(sm), GTK_WIDGET(mi));
	}
	
	sqlite3_finalize(stmt);
	
	/* add submenu to main menu if we got any presets */
	if(g_list_length(gtk_container_get_children(GTK_CONTAINER(sm))) > 0)
        {
	  gtk_menu_shell_append(GTK_MENU_SHELL(menu), GTK_WIDGET(smi));
          presets = TRUE;
        }  
	
      }

    } while ((modules=g_list_next(modules))!=NULL);
  }

  if (!presets)
  {
    gtk_widget_destroy(GTK_WIDGET(menu));
    darktable.gui->presets_popup_menu = NULL;
  }

}


static void
dt_gui_presets_popup_menu_show_internal(dt_dev_operation_t op, int32_t version, dt_iop_params_t *params, int32_t params_size, dt_develop_blend_params_t *bl_params, dt_iop_module_t *module, const dt_image_t *image, void (*pick_callback)(GtkMenuItem*,void*), void *callback_data)
{
  GtkMenu *menu = darktable.gui->presets_popup_menu;
  if(menu)
    gtk_widget_destroy(GTK_WIDGET(menu));
  darktable.gui->presets_popup_menu = GTK_MENU(gtk_menu_new());
  menu = darktable.gui->presets_popup_menu;

  GtkWidget *mi;
  int active_preset = -1, cnt = 0, writeprotect = 0, selected_default = 0;
  sqlite3_stmt *stmt;
  // order: get shipped defaults first
  if(image)
  {
    // only matching if filter is on:
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select name, op_params, writeprotect, description, blendop_params, op_version from presets where operation=?1 and "
                                "(filter=0 or ( "
                                "?2 like model and ?3 like maker and ?4 like lens and "
                                "?5 between iso_min and iso_max and "
                                "?6 between exposure_min and exposure_max and "
                                "?7 between aperture_min and aperture_max and "
                                "?8 between focal_length_min and focal_length_max and "
                                "(isldr = 0 or isldr=?9) ) )"
                                "order by writeprotect desc, rowid", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, op, strlen(op), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, image->exif_model, strlen(image->exif_model), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, image->exif_maker, strlen(image->exif_maker), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 4, image->exif_lens,  strlen(image->exif_lens),  SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 5, image->exif_iso);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 6, image->exif_exposure);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 7, image->exif_aperture);
    DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 8, image->exif_focal_length);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 9, dt_image_is_ldr(image));
  }
  else
  {
    // don't know for which image. show all we got:
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select name, op_params, writeprotect, description, blendop_params, op_version from presets where operation=?1 order by writeprotect desc, rowid", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, op, strlen(op), SQLITE_TRANSIENT);
  }
  // collect all presets for op from db
  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    void *op_params = (void *)sqlite3_column_blob(stmt, 1);
    int32_t op_params_size = sqlite3_column_bytes(stmt, 1);
    void *blendop_params = (void *)sqlite3_column_blob(stmt, 4);
    int32_t bl_params_size = sqlite3_column_bytes(stmt, 4);
    int32_t preset_version = sqlite3_column_int(stmt, 5);
    int32_t isdefault = 0;
    int32_t isdisabled = (preset_version == version ? 0 : 1);
    if(module && !memcmp(module->default_params, op_params, MIN(op_params_size, module->params_size)) && 
                 !memcmp(module->default_blendop_params, blendop_params, MIN(bl_params_size, sizeof(dt_develop_blend_params_t)))) isdefault = 1;
    if(!memcmp(params, op_params, MIN(op_params_size, params_size)) && 
       !memcmp(bl_params, blendop_params, MIN(bl_params_size, sizeof(dt_develop_blend_params_t))))
    {
      active_preset = cnt;
      writeprotect = sqlite3_column_int(stmt, 2);
      char *markup;
      mi = gtk_menu_item_new_with_label("");
      if(isdefault)
      {
        selected_default = 1;
        markup = g_markup_printf_escaped ("<span weight=\"bold\">%s %s</span>", sqlite3_column_text(stmt, 0), _("(default)"));
      }
      else
        markup = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", sqlite3_column_text(stmt, 0));
      gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child(GTK_BIN(mi))), markup);
      g_free (markup);
    }
    else
    {
      if(isdefault)
      {
        char *markup;
        mi = gtk_menu_item_new_with_label("");
        markup = g_markup_printf_escaped ("%s %s", sqlite3_column_text(stmt, 0), _("(default)"));
        gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child(GTK_BIN(mi))), markup);
        g_free (markup);
      }
      else mi = gtk_menu_item_new_with_label((const char *)sqlite3_column_text(stmt, 0));
    }

    if(isdisabled)
    {
      gtk_widget_set_sensitive(mi, 0);
      g_object_set(G_OBJECT(mi), "tooltip-text", "Disabled: Wrong module version.", (char *)NULL);
    }
    else
    {
      if(module) g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_pick_preset), module);
      else if(pick_callback) g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(pick_callback), callback_data);
      g_object_set(G_OBJECT(mi), "tooltip-text", sqlite3_column_text(stmt, 3), (char *)NULL);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    cnt ++;
  }
  sqlite3_finalize(stmt);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

  if(module)
  {
    if(active_preset >= 0)
    {
      if(!writeprotect)
      {
        mi = gtk_menu_item_new_with_label(_("edit this preset.."));
        g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_edit_preset), module);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);

        mi = gtk_menu_item_new_with_label(_("delete this preset"));
        g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_delete_preset), module);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
      }

      if(!selected_default)
      {
        // only show if it is not the default already
        mi = gtk_menu_item_new_with_label(_("use preset as default"));
        g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_store_default), module);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
      }
    }
    else
    {
      mi = gtk_menu_item_new_with_label(_("store new preset.."));
      g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_new_preset), module);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }

    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select * from presets where operation = ?1 and op_version = ?2 and def=1", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, module->op, strlen(module->op), SQLITE_TRANSIENT);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, module->version());
    if(sqlite3_step(stmt) == SQLITE_ROW)
    {
      // only show this if presets already contains a default
      mi = gtk_menu_item_new_with_label(_("remove default"));
      g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_factory_default), module);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    sqlite3_finalize(stmt);
  }
}

void dt_gui_presets_popup_menu_show_for_params(dt_dev_operation_t op, int32_t version, void *params, int32_t params_size, void *blendop_params, const dt_image_t *image, void (*pick_callback)(GtkMenuItem*,void*), void *callback_data)
{
  dt_gui_presets_popup_menu_show_internal(op, version, params, params_size, blendop_params, NULL, image, pick_callback, callback_data);
}

void dt_gui_presets_popup_menu_show_for_module(dt_iop_module_t *module)
{
  dt_gui_presets_popup_menu_show_internal(module->op, module->version(), module->params, module->params_size, module->blend_params, module, &module->dev->image_storage, NULL, NULL);
}

void dt_gui_presets_update_mml(const char *name, dt_dev_operation_t op, const int32_t version, const char *maker, const char *model, const char *lens)
{
  char tmp[1024];
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set maker=?1, model=?2, lens=?3 where operation=?4 and op_version=?5 and name=?6", -1, &stmt, NULL);
  snprintf(tmp, 1024, "%%%s%%", maker);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 1, tmp, strlen(tmp), SQLITE_TRANSIENT);
  snprintf(tmp, 1024, "%%%s%%", model);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, tmp, strlen(tmp), SQLITE_TRANSIENT);
  snprintf(tmp, 1024, "%%%s%%", lens);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, tmp, strlen(tmp), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 4, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 5, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 6, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_gui_presets_update_iso(const char *name, dt_dev_operation_t op, const int32_t version, const float min, const float max)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set iso_min=?1, iso_max=?2 where operation=?3 and op_version=?4 and name=?5", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 1, min);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 2, max);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 4, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 5, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_gui_presets_update_av(const char *name, dt_dev_operation_t op, const int32_t version, const float min, const float max)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set aperture_min=?1, aperture_max=?2 where operation=?3 and op_version=?4 and name=?5", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 1, min);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 2, max);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 4, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 5, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_gui_presets_update_tv(const char *name, dt_dev_operation_t op, const int32_t version, const float min, const float max)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set exposure_min=?1, exposure_max=?2 where operation=?3 and op_version=?4 and name=?5", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 1, min);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 2, max);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 4, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 5, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_gui_presets_update_fl(const char *name, dt_dev_operation_t op, const int32_t version, const float min, const float max)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set focal_length_min=?1, focal_length_max=?2 where operation=?3 and op_version=?4 and name=?5", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 1, min);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 2, max);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 4, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 5, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_gui_presets_update_ldr(const char *name, dt_dev_operation_t op, const int32_t version, const int ldrflag)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set isldr=?1 where operation=?2 and op_version=?3 and name=?4", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, ldrflag);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 4, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_gui_presets_update_autoapply(const char *name, dt_dev_operation_t op, const int32_t version, const int autoapply)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set autoapply=?1 where operation=?2 and op_version=?3 and name=?4", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, autoapply);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 4, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_gui_presets_update_filter(const char *name, dt_dev_operation_t op, const int32_t version, const int filter)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "update presets set filter=?1 where operation=?2 and op_version=?3 and name=?4", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, filter);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, op, strlen(op), SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, version);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, name, strlen(name), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
