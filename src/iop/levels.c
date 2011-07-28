/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.

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
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "iop/levels.h"
#include "gui/presets.h"
#include "develop/develop.h"
#include "control/control.h"
#include "gui/gtk.h"
#include "common/colorspaces.h"
#include "common/opencl.h"

#define DT_GUI_CURVE_EDITOR_INSET 5
#define DT_GUI_CURVE_INFL .3f

DT_MODULE(1)

const char *name()
{
  return _("levels");
}


int
groups ()
{
  return IOP_GROUP_CORRECT;
}

int
flags ()
{
  return IOP_FLAGS_SUPPORTS_BLENDING;
}


//#ifdef HAVE_OPENCL
//int
//process_cl (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
//{
//  dt_iop_levels_data_t *d = (dt_iop_levels_data_t *)piece->data;
//  dt_iop_levels_global_data_t *gd = (dt_iop_levels_global_data_t *)self->data;
//  cl_mem dev_m = NULL;
//  cl_mem dev_coeffs = NULL;
//  cl_int err = -999;

//  const int devid = piece->pipe->devid;
//  size_t sizes[] = {roi_in->width, roi_in->height, 1};
//  dev_m = dt_opencl_copy_host_to_device(devid, d->table, 256, 256, sizeof(float));
//  if (dev_m == NULL) goto error;

//  dev_coeffs = dt_opencl_copy_host_to_device_constant(devid, sizeof(float)*2, d->unbounded_coeffs);
//  if (dev_coeffs == NULL) goto error;
//  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 0, sizeof(cl_mem), (void *)&dev_in);
//  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 1, sizeof(cl_mem), (void *)&dev_out);
//  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 2, sizeof(cl_mem), (void *)&dev_m);
//  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 3, sizeof(cl_mem), (void *)&dev_coeffs);
//  err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_levels, sizes);

//  if(err != CL_SUCCESS) goto error;
//  dt_opencl_release_mem_object(dev_m);
//  dt_opencl_release_mem_object(dev_coeffs);
//  return TRUE;

//error:
//  if (dev_m != NULL) dt_opencl_release_mem_object(dev_m);
//  if (dev_coeffs != NULL) dt_opencl_release_mem_object(dev_coeffs);
//  dt_print(DT_DEBUG_OPENCL, "[opencl_levels] couldn't enqueue kernel! %d\n", err);
//  return FALSE;
//}
//#endif

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *i, void *o, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
//  const int ch = piece->colors;
//  dt_iop_levels_data_t *d = (dt_iop_levels_data_t *)(piece->data);
//#ifdef _OPENMP
//  #pragma omp parallel for default(none) shared(roi_out, i, o, d) schedule(static)
//#endif
//  for(int k=0; k<roi_out->height; k++)
//  {
//    float *in = ((float *)i) + k*ch*roi_out->width;
//    float *out = ((float *)o) + k*ch*roi_out->width;
//    for (int j=0; j<roi_out->width; j++,in+=ch,out+=ch)
//    {
//      // in Lab: correct compressed Luminance for saturation:
//      const float L_in = in[0]/100.0f;
//      out[0] = (L_in < 1.0f) ? d->table[CLAMP((int)(L_in*0xfffful), 0, 0xffff)] :
//        dt_iop_eval_exp(d->unbounded_coeffs, L_in);
        
//      if(in[0] > 0.01f)
//      {
//        out[1] = in[1] * out[0]/in[0];
//        out[2] = in[2] * out[0]/in[0];
//      }
//      else
//      {
//        out[1] = in[1] * out[0]/0.01f;
//        out[2] = in[2] * out[0]/0.01f;
//      }
//    }
//  }
  const int ch = piece->colors;
  dt_iop_levels_data_t *d = (dt_iop_levels_data_t*)(piece->data);
#ifdef _OPENMP
#pragma omp parallel for default(none) shared(roi_out, i, o, d) schedule(static)
#endif
  for(int k=0; k<roi_out->height; k++)
  {
    float *in = ((float *)i) + k*ch*roi_out->width;
    float *out = ((float *)o) + k*ch*roi_out->width;
    for (int j=0; j<roi_out->width; j++,in+=ch,out+=ch)
    {
      for(int i = 0; i < ch; i++)
        out[i] = in[i];

      if(in[0] < d->thresh[0] * 100)
        out[0] = 0;
      if(in[0] > d->thresh[1] * 100)
        out[0] = 100;
    }
  }

}

void init_presets (dt_iop_module_so_t *self)
{
  dt_iop_levels_params_t p;
  p.levels_preset = 0;

  p.levels[0] = 0;
  p.levels[1] = 0.5;
  p.levels[2] = 1;
  dt_gui_presets_add_generic(_("unmodified"), self->op, &p, sizeof(p), 1);
}

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1,
                    dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
//  // pull in new params to gegl
//  dt_iop_levels_data_t *d = (dt_iop_levels_data_t *)(piece->data);
//  dt_iop_levels_params_t *p = (dt_iop_levels_params_t *)p1;
//  for(int k=0; k<6; k++)
//    dt_draw_curve_set_point(d->curve, k, p->levels_x[k], p->levels_y[k]);
//  dt_draw_curve_calc_values(d->curve, 0.0f, 1.0f, 0x10000, NULL, d->table);
//  for(int k=0; k<0x10000; k++) d->table[k] *= 100.0f;

//  // now the extrapolation stuff:
//  const float x[4] = {0.7f, 0.8f, 0.9f, 1.0f};
//  const float y[4] = {d->table[CLAMP((int)(x[0]*0x10000ul), 0, 0xffff)],
//                      d->table[CLAMP((int)(x[1]*0x10000ul), 0, 0xffff)],
//                      d->table[CLAMP((int)(x[2]*0x10000ul), 0, 0xffff)],
//                      d->table[CLAMP((int)(x[3]*0x10000ul), 0, 0xffff)]};
//  dt_iop_estimate_exp(x, y, 4, d->unbounded_coeffs);
  int i;
  int j;

  dt_iop_levels_data_t *d = (dt_iop_levels_data_t*)(piece->data);
  dt_iop_levels_params_t *p = (dt_iop_levels_params_t*)p1;

  // First committing the upper and lower thresholds
  d->thresh[0] = p->levels[0];
  d->thresh[1] = p->levels[2];

  // Then calculating the quadratic curve within the bounds
  float x[9];
  float x_inv[9];
  for(i = 0; i < 3; i++)
    x[3 * i] = p->levels[i] * p->levels[i];
  for(i = 0; i < 3; i++)
    x[(3 * i) + 1] = p->levels[i];
  for(i = 0; i < 3; i++)
    x[(3 * i) + 2] = 1;

  // Calculating the inverse
  if(mat3inv(x_inv, x))
  {
    for(i = 0; i < 3; i++)
      d->quadratic_coeffs[i] = 0;
    return;
  }

}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe,
                dt_dev_pixelpipe_iop_t *piece)
{
  // create part of the gegl pipeline
  dt_iop_levels_data_t *d =
      (dt_iop_levels_data_t *)malloc(sizeof(dt_iop_levels_data_t));
  piece->data = (void *)d;
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // clean up everything again.
  free(piece->data);
}

void gui_update(struct dt_iop_module_t *self)
{
  // nothing to do, gui curve is read directly from params during expose event.
  gtk_widget_queue_draw(self->widget);
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_levels_params_t));
  module->default_params = malloc(sizeof(dt_iop_levels_params_t));
  module->default_enabled = 0;
  module->priority = 583; // module order created by iop_dependencies.py, do not edit!
  module->params_size = sizeof(dt_iop_levels_params_t);
  module->gui_data = NULL;
  dt_iop_levels_params_t tmp = (dt_iop_levels_params_t)
  {
    {0, 0.5, 1},
    0
  };
  memcpy(module->params, &tmp, sizeof(dt_iop_levels_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_levels_params_t));
}

void init_global(dt_iop_module_so_t *module)
{
  const int program = 2; // basic.cl, from programs.conf
  dt_iop_levels_global_data_t *gd = (dt_iop_levels_global_data_t *)malloc(sizeof(dt_iop_levels_global_data_t));
  module->data = gd;
  gd->kernel_levels = dt_opencl_create_kernel(program, "levels");
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_levels_global_data_t *gd = (dt_iop_levels_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_levels);
  free(module->data);
  module->data = NULL;
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_levels_gui_data_t));
  dt_iop_levels_gui_data_t *c = (dt_iop_levels_gui_data_t *)self->gui_data;

  c->mouse_x = c->mouse_y = -1.0;
  c->dragging = 0;
  self->widget = GTK_WIDGET(gtk_vbox_new(FALSE, 5));
  c->area = GTK_DRAWING_AREA(gtk_drawing_area_new());
  GtkWidget *asp = gtk_aspect_frame_new(NULL, 0.5, 0.5, 1.0, TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), asp, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(asp), GTK_WIDGET(c->area));
  // gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(c->area), TRUE, TRUE, 0);
  gtk_drawing_area_size(c->area, 258, 258);
  g_object_set (GTK_OBJECT(c->area), "tooltip-text", _("abscissa: input, ordinate: output \nworks on L channel"), (char *)NULL);

  gtk_widget_add_events(GTK_WIDGET(c->area), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (G_OBJECT (c->area), "expose-event",
                    G_CALLBACK (dt_iop_levels_expose), self);
  g_signal_connect (G_OBJECT (c->area), "button-press-event",
                    G_CALLBACK (dt_iop_levels_button_press), self);
  g_signal_connect (G_OBJECT (c->area), "button-release-event",
                    G_CALLBACK (dt_iop_levels_button_release), self);
  g_signal_connect (G_OBJECT (c->area), "motion-notify-event",
                    G_CALLBACK (dt_iop_levels_motion_notify), self);
  g_signal_connect (G_OBJECT (c->area), "leave-notify-event",
                    G_CALLBACK (dt_iop_levels_leave_notify), self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  free(self->gui_data);
  self->gui_data = NULL;
}


static gboolean dt_iop_levels_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_levels_gui_data_t *c = (dt_iop_levels_gui_data_t *)self->gui_data;
  c->mouse_x = c->mouse_y = -1.0;
  if(!c->dragging)
    c->handle_move = -1;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean dt_iop_levels_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_levels_gui_data_t *c = (dt_iop_levels_gui_data_t *)self->gui_data;
  dt_iop_levels_params_t *p = (dt_iop_levels_params_t *)self->params;
  const int inset = DT_GUI_CURVE_EDITOR_INSET;
  int width = widget->allocation.width, height = widget->allocation.height;
  cairo_surface_t *cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);
  // clear bg
  cairo_set_source_rgb (cr, .2, .2, .2);
  cairo_paint(cr);

  cairo_translate(cr, inset, inset);
  width -= 2*inset;
  height -= 2*inset;

  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgb (cr, .1, .1, .1);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_stroke(cr);

  cairo_set_source_rgb (cr, .3, .3, .3);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  // draw grid
  cairo_set_line_width(cr, .4);
  cairo_set_source_rgb (cr, .1, .1, .1);
  dt_draw_grid(cr, 4, 0, 0, width, height);

  // Drawing the vertical line indicators
  cairo_set_line_width(cr, 2.);

  for(int k = 0; k < 3; k++)
  {
    if(k == c->handle_move)
      cairo_set_source_rgb(cr, 1, 1, 1);
    else
      cairo_set_source_rgb(cr, .7, .7, .7);

    cairo_move_to(cr, width*p->levels[k], height);
    cairo_rel_line_to(cr, 0, -height);
    cairo_stroke(cr);
  }

  // draw x positions
  cairo_set_line_width(cr, 1.);
  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  const float arrw = 7.0f;
  for(int k=0; k<3; k++)
  {
    cairo_move_to(cr, width*p->levels[k], height+inset-1);
    cairo_rel_line_to(cr, -arrw*.5f, 0);
    cairo_rel_line_to(cr, arrw*.5f, -arrw);
    cairo_rel_line_to(cr, arrw*.5f, arrw);
    cairo_close_path(cr);
    if(c->handle_move == k)
      cairo_fill(cr);
    else
      cairo_stroke(cr);
  }

  cairo_translate(cr, 0, height);

  // draw lum histogram in background
  dt_develop_t *dev = darktable.develop;
  float *hist, hist_max;
  hist = dev->histogram_pre;
  hist_max = dev->histogram_pre_max;
  if(hist_max > 0)
  {
    cairo_save(cr);
    cairo_scale(cr, width/63.0, -(height-5)/(float)hist_max);
    cairo_set_source_rgba(cr, .2, .2, .2, 0.5);
    dt_draw_histogram_8(cr, hist, 3);
    cairo_restore(cr);
  }

  // Cleaning up
  cairo_destroy(cr);
  cairo_t *cr_pixmap = gdk_cairo_create(gtk_widget_get_window(widget));
  cairo_set_source_surface (cr_pixmap, cst, 0, 0);
  cairo_paint(cr_pixmap);
  cairo_destroy(cr_pixmap);
  cairo_surface_destroy(cst);
  return TRUE;
}

static gboolean dt_iop_levels_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_levels_gui_data_t *c = (dt_iop_levels_gui_data_t *)self->gui_data;
  dt_iop_levels_params_t *p = (dt_iop_levels_params_t *)self->params;
  const int inset = DT_GUI_CURVE_EDITOR_INSET;
  int height = widget->allocation.height - 2*inset, width = widget->allocation.width - 2*inset;
  if(!c->dragging)
    c->mouse_x = CLAMP(event->x - inset, 0, width);
  c->mouse_y = CLAMP(event->y - inset, 0, height);

  if(c->dragging)
  {
    if(c->handle_move >= 0)
    {
      if(c->handle_move >= 0 && c->handle_move < 3)
      {
        const float mx = (CLAMP(event->x - inset, 0, width)) / (float)width;

        float min_x = 0;
        float max_x = 1;

        if(c->handle_move > 0)
          min_x = fmaxf(0, p->levels[c->handle_move - 1] + 0.01);
        if(c->handle_move < 2)
          max_x = fminf(1, p->levels[c->handle_move + 1] - 0.01);

        p->levels[c->handle_move] =
            fminf(max_x, fmaxf(min_x, mx));
      }
    }
    dt_dev_add_history_item(darktable.develop, self, TRUE);
  }
  else
  {
    c->handle_move = 0;
    const float mx = CLAMP(event->x - inset, 0, width)/(float)width;
    float dist = fabsf(p->levels[0] - mx);
    for(int k=1; k<3; k++)
    {
      float d2 = fabsf(p->levels[k] - mx);
      if(d2 < dist)
      {
        c->handle_move = k;
        dist = d2;
      }
    }
  }
  gtk_widget_queue_draw(widget);

  gint x, y;
  gdk_window_get_pointer(event->window, &x, &y, NULL);
  return TRUE;
}

static gboolean dt_iop_levels_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  // set active point
  if(event->button == 1)
  {
    dt_iop_module_t *self = (dt_iop_module_t *)user_data;
    dt_iop_levels_gui_data_t *c = (dt_iop_levels_gui_data_t *)self->gui_data;
    c->dragging = 1;
    return TRUE;
  }
  return FALSE;
}

static gboolean dt_iop_levels_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  if(event->button == 1)
  {
    dt_iop_module_t *self = (dt_iop_module_t *)user_data;
    dt_iop_levels_gui_data_t *c = (dt_iop_levels_gui_data_t *)self->gui_data;
    c->dragging = 0;
    return TRUE;
  }
  return FALSE;
}

