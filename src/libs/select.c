#include "common/darktable.h"
#include "control/control.h"
#include "control/conf.h"
#include "libs/lib.h"
#include "gui/gtk.h"
#include <gdk/gdkkeysyms.h>

const char*
name ()
{
  return _("select");
}

static void
button_clicked(GtkWidget *widget, gpointer user_data)
{
  char fullq[1024];
  gchar *query = dt_conf_get_string("plugins/lighttable/query");
  gchar *c = g_strrstr(query, "order by");
  if(c) *c = '\0';
  c = g_strrstr(query, "limit");
  if(c) *c = '\0';
  snprintf(fullq, 1024, "insert into selected_images select id %s", query + 8);
  switch((long int)user_data)
  {
    case 0: // all
      sqlite3_exec(darktable.db, "delete from selected_images", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, fullq, NULL, NULL, NULL);
      break;
    case 1: // none
      sqlite3_exec(darktable.db, "delete from selected_images", NULL, NULL, NULL);
      break;
    case 2: // invert
      sqlite3_exec(darktable.db, "create temp table tmp_selection (imgid integer)", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "insert into tmp_selection select imgid from selected_images", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "delete from selected_images", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, fullq, NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "delete from selected_images where imgid in (select imgid from tmp_selection)", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "delete from tmp_selection", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "drop table tmp_selection", NULL, NULL, NULL);
      break;
    default: // case 3: same film roll
      sqlite3_exec(darktable.db, "create temp table tmp_selection (imgid integer)", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "insert into tmp_selection select imgid from selected_images", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "delete from selected_images", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "insert into selected_images select id from images where film_id in (select film_id from images as a join tmp_selection as b on a.id = b.imgid)", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "delete from tmp_selection", NULL, NULL, NULL);
      sqlite3_exec(darktable.db, "drop table tmp_selection", NULL, NULL, NULL);
      break;
  }
  g_free(query);
  dt_control_queue_draw_all();
}

static void key_accel_callback(void *d)
{
  button_clicked(NULL, d);
}

void
gui_reset (dt_lib_module_t *self)
{
}

void
gui_init (dt_lib_module_t *self)
{
  self->data = NULL;
  self->widget = gtk_vbox_new(TRUE, 5);
  GtkBox *hbox;
  GtkWidget *button;
  hbox = GTK_BOX(gtk_hbox_new(TRUE, 5));

  button = gtk_button_new_with_label(_("select all"));
  gtk_object_set(GTK_OBJECT(button), "tooltip-text", _("select all images in current collection (ctrl-a)"), NULL);
  gtk_box_pack_start(hbox, button, TRUE, TRUE, 0);
  dt_gui_key_accel_register(GDK_CONTROL_MASK, GDK_a, key_accel_callback, (void *)0);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(button_clicked), (gpointer)0);

  button = gtk_button_new_with_label(_("select none"));
  gtk_object_set(GTK_OBJECT(button), "tooltip-text", _("clear selection (ctrl-shift-a)"), NULL);
  gtk_box_pack_start(hbox, button, TRUE, TRUE, 0);
  dt_gui_key_accel_register(GDK_CONTROL_MASK, GDK_A, key_accel_callback, (void *)1);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(button_clicked), (gpointer)1);

  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), TRUE, TRUE, 0);
  hbox = GTK_BOX(gtk_hbox_new(TRUE, 5));

  button = gtk_button_new_with_label(_("invert selection (ctrl-!"));
  gtk_object_set(GTK_OBJECT(button), "tooltip-text", _("select unselected images\nin current collection"), NULL);
  gtk_box_pack_start(hbox, button, TRUE, TRUE, 0);
  dt_gui_key_accel_register(GDK_CONTROL_MASK, GDK_exclam, key_accel_callback, (void *)2);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(button_clicked), (gpointer)2);

  button = gtk_button_new_with_label(_("select film roll"));
  gtk_object_set(GTK_OBJECT(button), "tooltip-text", _("select all images which are in the same\nfilm roll as the selected images"), NULL);
  gtk_box_pack_start(hbox, button, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(button_clicked), (gpointer)3);

  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), TRUE, TRUE, 0);
}

void
gui_cleanup (dt_lib_module_t *self)
{
  dt_gui_key_accel_unregister(key_accel_callback);
}
