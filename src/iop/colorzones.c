#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "common/darktable.h"
#include "gui/histogram.h"
#include "develop/develop.h"
#include "control/control.h"
#include "gui/gtk.h"
#include "gui/draw.h"
#include <inttypes.h>
#include <lcms.h>

DT_MODULE

#define DT_IOP_COLORZONES_INSET 5
#define DT_IOP_COLORZONES_CURVE_INFL .3f
#define DT_IOP_COLORZONES_RES 64
#define DT_IOP_COLORZONES_BANDS 6

typedef enum dt_iop_colorzones_channel_t
{
  DT_IOP_COLORZONES_L = 0,
  DT_IOP_COLORZONES_C = 1,
  DT_IOP_COLORZONES_h = 2
}
dt_iop_colorzones_channel_t;

typedef struct dt_iop_colorzones_params_t
{
  int32_t channel;
  float equalizer_x[3][DT_IOP_COLORZONES_BANDS], equalizer_y[3][DT_IOP_COLORZONES_BANDS];
}
dt_iop_colorzones_params_t;

typedef struct dt_iop_colorzones_gui_data_t
{
  dt_draw_curve_t *minmax_curve;        // curve for gui to draw
  GtkHBox *hbox;
  GtkDrawingArea *area;
  GtkComboBox *presets;
  GtkRadioButton *channel_button[3];
  GtkRadioButton *select_button[3];
  double mouse_x, mouse_y, mouse_pick;
  float mouse_radius;
  dt_iop_colorzones_params_t drag_params;
  int dragging;
  int x_move;
  dt_iop_colorzones_channel_t channel;
  double draw_xs[DT_IOP_COLORZONES_RES], draw_ys[DT_IOP_COLORZONES_RES];
  double draw_min_xs[DT_IOP_COLORZONES_RES], draw_min_ys[DT_IOP_COLORZONES_RES];
  double draw_max_xs[DT_IOP_COLORZONES_RES], draw_max_ys[DT_IOP_COLORZONES_RES];
  float band_hist[DT_IOP_COLORZONES_BANDS];
  float band_max;
  cmsHPROFILE hsRGB;
  cmsHPROFILE hLab;
  cmsHTRANSFORM xform;
}
dt_iop_colorzones_gui_data_t;

typedef struct dt_iop_colorzones_data_t
{
  dt_draw_curve_t *curve[3];
  dt_iop_colorzones_channel_t channel;
}
dt_iop_colorzones_data_t;

const char *name()
{
  return _("color zones");
}

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *i, void *o, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  float *in = (float *)i;
  float *out = (float *)o;
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)(piece->data);
  float Lmean = 0.0f, Cmean = 0.0f, hmean = 0.0f;
  for(int k=0;k<10;k++) Lmean += dt_draw_curve_calc_value(d->curve[0], k/9.0)/10.0;
  for(int k=0;k<10;k++) Cmean += dt_draw_curve_calc_value(d->curve[1], k/9.0)/10.0;
  for(int k=0;k<10;k++) hmean += dt_draw_curve_calc_value(d->curve[2], k/9.0)/10.0;
  for(int k=0;k<roi_out->width*roi_out->height;k++)
  {
    const float a = in[1], b = in[2];
    const float h = fmodf(atan2f(b, a) + 2.0*M_PI, 2.0*M_PI)/(2.0*M_PI);
    const float C = sqrtf(b*b + a*a);
    float select = .0;
    switch(d->channel)
    {
      case DT_IOP_COLORZONES_L:
        select = fminf(1.0, in[0]/100.0);
        break;
      case DT_IOP_COLORZONES_C:
        select = fminf(1.0, C/128.0);
        break;
      default: case DT_IOP_COLORZONES_h:
        select = h;
        break;
    }
    const float bl = powf(fabsf(50.0 - in[0])/50.0, 5.0);
    const float Lm = 2.0 * (bl*Lmean + (1.-bl)*dt_draw_curve_calc_value(d->curve[0], select));
    const float Cm = 2.0 * (bl*Cmean + (1.-bl)*dt_draw_curve_calc_value(d->curve[1], select));
    const float hm =       (bl*hmean + (1.-bl)*dt_draw_curve_calc_value(d->curve[2], select)) - .5;
    const float L = 100.0*powf(in[0]*(1.0/100.0), 2.0-Lm);
    out[0] = L;
    out[1] = cosf(2.0*M_PI*(h + hm)) * Cm * C;
    out[2] = sinf(2.0*M_PI*(h + hm)) * Cm * C;
    out += 3; in += 3;
  }
}

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // pull in new params to gegl
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)(piece->data);
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)p1;
#ifdef HAVE_GEGL
  // TODO
#else
  d->channel = (dt_iop_colorzones_channel_t)p->channel;
  for(int ch=0;ch<3;ch++)
  {
    if(d->channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(d->curve[ch], 0, p->equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p->equalizer_y[ch][DT_IOP_COLORZONES_BANDS-2]);
    else
      dt_draw_curve_set_point(d->curve[ch], 0, p->equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p->equalizer_y[ch][0]);
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      dt_draw_curve_set_point(d->curve[ch], k+1, p->equalizer_x[ch][k], p->equalizer_y[ch][k]);
    if(d->channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(d->curve[ch], DT_IOP_COLORZONES_BANDS+1, p->equalizer_x[ch][1]+1.0, p->equalizer_y[ch][1]);
    else
      dt_draw_curve_set_point(d->curve[ch], DT_IOP_COLORZONES_BANDS+1, p->equalizer_x[ch][1]+1.0, p->equalizer_y[ch][DT_IOP_COLORZONES_BANDS-1]);
  }
#endif
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)malloc(sizeof(dt_iop_colorzones_data_t));
  dt_iop_colorzones_params_t *default_params = (dt_iop_colorzones_params_t *)self->default_params;
  piece->data = (void *)d;
  for(int ch=0;ch<3;ch++)
  {
    d->curve[ch] = dt_draw_curve_new(0.0, 1.0);
    (void)dt_draw_curve_add_point(d->curve[ch], default_params->equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, default_params->equalizer_y[ch][DT_IOP_COLORZONES_BANDS-2]);
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      (void)dt_draw_curve_add_point(d->curve[ch], default_params->equalizer_x[ch][k], default_params->equalizer_y[ch][k]);
    (void)dt_draw_curve_add_point(d->curve[ch], default_params->equalizer_x[ch][1]+1.0, default_params->equalizer_y[ch][1]);
  }
  d->channel = (dt_iop_colorzones_channel_t)default_params->channel;
#ifdef HAVE_GEGL
  #error "gegl version not implemeted!"
#endif
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // clean up everything again.
#ifdef HAVE_GEGL
#error "gegl version not implemented!"
#endif
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)(piece->data);
  for(int ch=0;ch<3;ch++) dt_draw_curve_destroy(d->curve[ch]);
  free(piece->data);
}

void gui_update(struct dt_iop_module_t *self)
{
  // nothing to do, gui curve is read directly from params during expose event.
  if(!memcmp(self->params, self->default_params, self->params_size))
  {
    dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;
    gtk_combo_box_set_active(g->presets, -1);
  }
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->select_button[p->channel]), TRUE);
  gtk_widget_queue_draw(self->widget);
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_colorzones_params_t));
  module->default_params = malloc(sizeof(dt_iop_colorzones_params_t));
  module->default_enabled = 0; // we're a rather slow and rare op.
  module->priority = 750;
  module->params_size = sizeof(dt_iop_colorzones_params_t);
  module->gui_data = NULL;
  dt_iop_colorzones_params_t tmp;
  for(int ch=0;ch<3;ch++)
  {
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++) tmp.equalizer_x[ch][k] = k/(DT_IOP_COLORZONES_BANDS-1.0);
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++) tmp.equalizer_y[ch][k] = 0.5f;
  }
  tmp.channel = DT_IOP_COLORZONES_h;
  memcpy(module->params, &tmp, sizeof(dt_iop_colorzones_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_colorzones_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

static void
presets_changed (GtkComboBox *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  
  int pos = gtk_combo_box_get_active(widget);
  switch(pos)
  {
    case 0: // red black white
      p->channel = DT_IOP_COLORZONES_h;
      for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      {
        p->equalizer_y[DT_IOP_COLORZONES_L][k] = .5f;
        p->equalizer_y[DT_IOP_COLORZONES_C][k] = .0f;
        p->equalizer_y[DT_IOP_COLORZONES_h][k] = .5f;
        p->equalizer_x[DT_IOP_COLORZONES_L][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
        p->equalizer_x[DT_IOP_COLORZONES_C][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
        p->equalizer_x[DT_IOP_COLORZONES_h][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
      }
      p->equalizer_y[DT_IOP_COLORZONES_C][0] = p->equalizer_y[DT_IOP_COLORZONES_C][DT_IOP_COLORZONES_BANDS-1] = 0.5;
      break;
    case 1: // b/w + skin tones
      p->channel = DT_IOP_COLORZONES_h;
      for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      {
        p->equalizer_y[DT_IOP_COLORZONES_L][k] = .5f;
        p->equalizer_y[DT_IOP_COLORZONES_C][k] = .0f;
        p->equalizer_y[DT_IOP_COLORZONES_h][k] = .5f;
        p->equalizer_x[DT_IOP_COLORZONES_L][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
        p->equalizer_x[DT_IOP_COLORZONES_C][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
        p->equalizer_x[DT_IOP_COLORZONES_h][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
      }
      p->equalizer_y[DT_IOP_COLORZONES_C][0] = p->equalizer_y[DT_IOP_COLORZONES_C][DT_IOP_COLORZONES_BANDS-1] = 0.5;
      p->equalizer_x[DT_IOP_COLORZONES_C][2] = 0.25f;
      p->equalizer_y[DT_IOP_COLORZONES_C][1] = 0.3f;
      break;
    case 2: // polarizing filter
      p->channel = DT_IOP_COLORZONES_C;
      for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      {
        p->equalizer_y[DT_IOP_COLORZONES_L][k] = .5f;
        p->equalizer_y[DT_IOP_COLORZONES_C][k] = .5f;
        p->equalizer_y[DT_IOP_COLORZONES_h][k] = .5f;
        p->equalizer_x[DT_IOP_COLORZONES_L][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
        p->equalizer_x[DT_IOP_COLORZONES_C][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
        p->equalizer_x[DT_IOP_COLORZONES_h][k] = k/(DT_IOP_COLORZONES_BANDS-1.);
      }
      for(int k=2;k<DT_IOP_COLORZONES_BANDS;k++)
      {
        p->equalizer_y[DT_IOP_COLORZONES_C][k] += (k-1.5)/(DT_IOP_COLORZONES_BANDS-2.0) * 0.25;
        p->equalizer_y[DT_IOP_COLORZONES_L][k] -= (k-1.5)/(DT_IOP_COLORZONES_BANDS-2.0) * 0.4;
      }
      break;
    default: // custom
      return;
  }
  if(self->off) gtk_toggle_button_set_active(self->off, 1);
  dt_dev_add_history_item(darktable.develop, self);
  gtk_widget_queue_draw(self->widget);
}

// fills in new parameters based on mouse position (in 0,1)
static void
dt_iop_colorzones_get_params(dt_iop_colorzones_params_t *p, const int ch, const double mouse_x, const double mouse_y, const float rad)
{
  if(p->channel == DT_IOP_COLORZONES_h)
  { // periodic boundary
    for(int k=1;k<DT_IOP_COLORZONES_BANDS-1;k++)
    {
      const float f = expf(-(mouse_x - p->equalizer_x[ch][k])*(mouse_x - p->equalizer_x[ch][k])/(rad*rad));
      p->equalizer_y[ch][k] = (1-f)*p->equalizer_y[ch][k] + f*mouse_y;
    }
    const int m = DT_IOP_COLORZONES_BANDS-1;
    const float mind = fminf((mouse_x - p->equalizer_x[ch][0])*(mouse_x - p->equalizer_x[ch][0]),
                             (mouse_x - p->equalizer_x[ch][m])*(mouse_x - p->equalizer_x[ch][m]));
    const float f = expf(-mind/(rad*rad));
    p->equalizer_y[ch][0] = (1-f)*p->equalizer_y[ch][0] + f*mouse_y;
    p->equalizer_y[ch][m] = (1-f)*p->equalizer_y[ch][m] + f*mouse_y;
  }
  else
  {
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
    {
      const float f = expf(-(mouse_x - p->equalizer_x[ch][k])*(mouse_x - p->equalizer_x[ch][k])/(rad*rad));
      p->equalizer_y[ch][k] = (1-f)*p->equalizer_y[ch][k] + f*mouse_y;
    }
  }
}

static gboolean
colorzones_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t p = *(dt_iop_colorzones_params_t *)self->params;
  int ch = (int)c->channel;
  if(p.channel == DT_IOP_COLORZONES_h)
    dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-2]);
  else
    dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][0]);
  for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++) dt_draw_curve_set_point(c->minmax_curve, k+1, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
  if(p.channel == DT_IOP_COLORZONES_h)
    dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][1]);
  else
    dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-1]);
  const int inset = DT_IOP_COLORZONES_INSET;
  int width = widget->allocation.width, height = widget->allocation.height;
  cairo_surface_t *cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);
  // clear bg
  cairo_set_source_rgb (cr, .2, .2, .2);
  cairo_paint(cr);

  cairo_translate(cr, inset, inset);
  width -= 2*inset; height -= 2*inset;

  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgb (cr, .1, .1, .1);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_stroke(cr);

  cairo_set_source_rgb (cr, .3, .3, .3);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  if(c->mouse_y > 0 || c->dragging)
  {
    // draw min/max curves:
    dt_iop_colorzones_get_params(&p, c->channel, c->mouse_x, 1., c->mouse_radius);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-2]);
    else
      dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][0]);
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      dt_draw_curve_set_point(c->minmax_curve, k+1, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][1]);
    else
      dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-1]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, DT_IOP_COLORZONES_RES, c->draw_min_xs, c->draw_min_ys);

    p = *(dt_iop_colorzones_params_t *)self->params;
    dt_iop_colorzones_get_params(&p, c->channel, c->mouse_x, .0, c->mouse_radius);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-2]);
    else
      dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][0]);
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      dt_draw_curve_set_point(c->minmax_curve, k+1, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][1]);
    else
      dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-1]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, DT_IOP_COLORZONES_RES, c->draw_max_xs, c->draw_max_ys);
  }

  if(self->picked_color[0] == 0.0)
  {
    self->picked_color[0] = 50.0f;
    self->picked_color[1] =  0.0f;
    self->picked_color[2] = -5.0f;
  }
  const float pickC = sqrtf(self->picked_color[1]*self->picked_color[1] + self->picked_color[2]*self->picked_color[2]);
  const int cellsi = 16, cellsj = 9;
  for(int j=0;j<cellsj;j++) for(int i=0;i<cellsi;i++)
  {
    double rgb[3] = {0.5, 0.5, 0.5};
    float jj = 1.0 - (j-.5)/(cellsj-1.), ii = (i+.5)/(cellsi-1.);
    cmsCIELab Lab;
    switch(p.channel)
    {
      case DT_IOP_COLORZONES_L:
        Lab.L = ii * 100.0;
        Lab.a = self->picked_color[1];//0.0f;
        Lab.b = self->picked_color[2];//-5.0f;
        break;
      case DT_IOP_COLORZONES_C:
        Lab.L = 50.0;
        Lab.a = 64.0*ii*self->picked_color[1]/pickC;//0.0f;
        Lab.b = 64.0*ii*self->picked_color[2]/pickC;//-ii * 64.0f;
        break;
      default: // case DT_IOP_COLORZONES_h:
        Lab.L = 50.0;
        Lab.a = cosf(2.0*M_PI*ii) * 64.0f;
        Lab.b = sinf(2.0*M_PI*ii) * 64.0f;
        break;
    }
    const float angle = atan2f(Lab.b, Lab.a);
    switch(c->channel)
    {
      case DT_IOP_COLORZONES_L:
        Lab.L += - 50.0 + 100.0*jj;
        break;
      case DT_IOP_COLORZONES_C:
        Lab.a *= 2.0f*jj;
        Lab.b *= 2.0f*jj;
        break;
      default: // DT_IOP_COLORZONES_h
        Lab.a = cosf(angle + 2.0*M_PI*(jj-.5f)) * 64.0f;
        Lab.b = sinf(angle + 2.0*M_PI*(jj-.5f)) * 64.0f;
        break;
    }
    cmsDoTransform(c->xform, &Lab, rgb, 1);
    cairo_set_source_rgb (cr, rgb[0], rgb[1], rgb[2]);
    cairo_rectangle(cr, width*i/(float)cellsi, height*j/(float)cellsj, width/(float)cellsi-1, height/(float)cellsj-1);
    cairo_fill(cr);
  }

  // draw x positions
  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  cairo_set_line_width(cr, 1.);
  const float arrw = 7.0f;
  for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
  {
    cairo_move_to(cr, width*p.equalizer_x[c->channel][k], height+inset-1);
    cairo_rel_line_to(cr, -arrw*.5f, 0);
    cairo_rel_line_to(cr, arrw*.5f, -arrw);
    cairo_rel_line_to(cr, arrw*.5f, arrw);
    cairo_close_path(cr);
    if(c->x_move == k) cairo_fill(cr);
    else               cairo_stroke(cr);
  }
  
  // draw selected cursor
  cairo_translate(cr, 0, height);

  // cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_line_width(cr, 2.);
  for(int i=0;i<3;i++)
  { // draw curves, selected last.
    int ch = ((int)c->channel+i+1)%3;
    if(i == 2) cairo_set_source_rgba(cr, .7, .7, .7, 1.0);
    else       cairo_set_source_rgba(cr, .7, .7, .7, 0.3);
    p = *(dt_iop_colorzones_params_t *)self->params;
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-2]);
    else
      dt_draw_curve_set_point(c->minmax_curve, 0, p.equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p.equalizer_y[ch][0]);
    for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
      dt_draw_curve_set_point(c->minmax_curve, k+1, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][1]);
    else
      dt_draw_curve_set_point(c->minmax_curve, DT_IOP_COLORZONES_BANDS+1, p.equalizer_x[ch][1]+1.0, p.equalizer_y[ch][DT_IOP_COLORZONES_BANDS-1]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, DT_IOP_COLORZONES_RES, c->draw_xs, c->draw_ys);
    cairo_move_to(cr, 0*width/(float)(DT_IOP_COLORZONES_RES-1), - height*c->draw_ys[0]);
    for(int k=1;k<DT_IOP_COLORZONES_RES;k++) cairo_line_to(cr, k*width/(float)(DT_IOP_COLORZONES_RES-1), - height*c->draw_ys[k]);
    cairo_stroke(cr);
  }

  // draw dots on knots
  cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
  cairo_set_line_width(cr, 1.);
  for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++)
  {
    cairo_arc(cr, width*p.equalizer_x[c->channel][k], - height*p.equalizer_y[c->channel][k], 3.0, 0.0, 2.0*M_PI);
    if(c->x_move == k) cairo_fill(cr);
    else               cairo_stroke(cr);
  }

  if(c->mouse_y > 0 || c->dragging)
  { // draw min/max, if selected
    cairo_set_source_rgba(cr, .7, .7, .7, .6);
    cairo_move_to(cr, 0, - height*c->draw_min_ys[0]);
    for(int k=1;k<DT_IOP_COLORZONES_RES;k++)    cairo_line_to(cr, k*width/(float)(DT_IOP_COLORZONES_RES-1), - height*c->draw_min_ys[k]);
    for(int k=DT_IOP_COLORZONES_RES-2;k>=0;k--) cairo_line_to(cr, k*width/(float)(DT_IOP_COLORZONES_RES-1), - height*c->draw_max_ys[k]);
    cairo_close_path(cr);
    cairo_fill(cr);
    // draw mouse focus circle
    cairo_set_source_rgba(cr, .9, .9, .9, .5);
    const float pos = DT_IOP_COLORZONES_RES * c->mouse_x;
    int k = (int)pos; const float f = k - pos;
    if(k >= DT_IOP_COLORZONES_RES-1) k = DT_IOP_COLORZONES_RES - 2;
    float ht = -height*(f*c->draw_ys[k] + (1-f)*c->draw_ys[k+1]);
    cairo_arc(cr, c->mouse_x*width, ht, c->mouse_radius*width, 0, 2.*M_PI);
    cairo_stroke(cr);
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

  cairo_destroy(cr);
  cairo_t *cr_pixmap = gdk_cairo_create(gtk_widget_get_window(widget));
  cairo_set_source_surface (cr_pixmap, cst, 0, 0);
  cairo_paint(cr_pixmap);
  cairo_destroy(cr_pixmap);
  cairo_surface_destroy(cst);
  return TRUE;
}

static gboolean
colorzones_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  const int inset = DT_IOP_COLORZONES_INSET;
  int height = widget->allocation.height - 2*inset, width = widget->allocation.width - 2*inset;
  if(!c->dragging) c->mouse_x = CLAMP(event->x - inset, 0, width)/(float)width;
  c->mouse_y = 1.0 - CLAMP(event->y - inset, 0, height)/(float)height;
  if(c->dragging)
  {
    *p = c->drag_params;
    if(c->x_move >= 0)
    {
      const float mx = CLAMP(event->x - inset, 0, width)/(float)width;
      if(c->x_move > 0 && c->x_move < DT_IOP_COLORZONES_BANDS-1)
      {
        const float minx = p->equalizer_x[c->channel][c->x_move-1]+0.001f;
        const float maxx = p->equalizer_x[c->channel][c->x_move+1]-0.001f;
        p->equalizer_x[c->channel][c->x_move] = fminf(maxx, fmaxf(minx, mx));
      }
    }
    else
    {
      dt_iop_colorzones_get_params(p, c->channel, c->mouse_x, c->mouse_y + c->mouse_pick, c->mouse_radius);
    }
    gtk_combo_box_set_active(c->presets, -1);
    dt_dev_add_history_item(darktable.develop, self);
  }
  else if(event->y > height)
  {
    c->x_move = 0;
    float dist = fabsf(p->equalizer_x[c->channel][0] - c->mouse_x);
    for(int k=1;k<DT_IOP_COLORZONES_BANDS;k++)
    {
      float d2 = fabsf(p->equalizer_x[c->channel][k] - c->mouse_x);
      if(d2 < dist)
      {
        c->x_move = k;
        dist = d2;
      }
    }
  }
  else
  {
    c->x_move = -1;
  }
  gtk_widget_queue_draw(widget);
  gint x, y;
  gdk_window_get_pointer(event->window, &x, &y, NULL);
  return TRUE;
}

static gboolean
colorzones_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  c->drag_params = *(dt_iop_colorzones_params_t *)self->params;
  const int inset = DT_IOP_COLORZONES_INSET;
  int height = widget->allocation.height - 2*inset, width = widget->allocation.width - 2*inset;
  c->mouse_pick = dt_draw_curve_calc_value(c->minmax_curve, CLAMP(event->x - inset, 0, width)/(float)width);
  c->mouse_pick -= 1.0 - CLAMP(event->y - inset, 0, height)/(float)height;
  c->dragging = 1;
  return TRUE;
}

static gboolean
colorzones_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  c->dragging = 0;
  return TRUE;
}

static gboolean
colorzones_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  if(!c->dragging) c->mouse_x = c->mouse_y = -1.0;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean
colorzones_scrolled(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  if(event->direction == GDK_SCROLL_UP   && c->mouse_radius > 0.2/DT_IOP_COLORZONES_BANDS) c->mouse_radius *= 0.9; //0.7;
  if(event->direction == GDK_SCROLL_DOWN && c->mouse_radius < 1.0) c->mouse_radius *= (1.0/0.9); //1.42;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static void
colorzones_button_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  if(gtk_toggle_button_get_active(togglebutton))
  {
    for(int k=0;k<3;k++) if(c->channel_button[k] == GTK_RADIO_BUTTON(togglebutton))
    {
      c->channel = (dt_iop_colorzones_channel_t)k;
      gtk_widget_queue_draw(self->widget);
      return;
    }
  }
}

#if 0
static void
request_pick_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  self->request_color_pick = gtk_toggle_button_get_active(togglebutton);
}
#endif

static void
colorzones_select_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
  if(darktable.gui->reset) return;
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  if(gtk_toggle_button_get_active(togglebutton))
  {
    for(int k=0;k<3;k++) if(c->select_button[k] == GTK_RADIO_BUTTON(togglebutton))
    {
      memcpy(p, self->default_params, sizeof(dt_iop_colorzones_params_t));
      p->channel = (dt_iop_colorzones_channel_t)k;
      dt_dev_add_history_item(darktable.develop, self);
      gtk_widget_queue_draw(self->widget);
      return;
    }
  }
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_colorzones_gui_data_t));
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;

  c->channel = DT_IOP_COLORZONES_C;
  int ch = (int)c->channel;
  c->minmax_curve = dt_draw_curve_new(0.0, 1.0);
  (void)dt_draw_curve_add_point(c->minmax_curve, p->equalizer_x[ch][DT_IOP_COLORZONES_BANDS-2]-1.0, p->equalizer_y[ch][DT_IOP_COLORZONES_BANDS-2]);
  for(int k=0;k<DT_IOP_COLORZONES_BANDS;k++) (void)dt_draw_curve_add_point(c->minmax_curve, p->equalizer_x[ch][k], p->equalizer_y[ch][k]);
  (void)dt_draw_curve_add_point(c->minmax_curve, p->equalizer_x[ch][1]+1.0, p->equalizer_y[ch][1]);
  c->mouse_x = c->mouse_y = c->mouse_pick = -1.0;
  c->dragging = 0;
  c->x_move = -1;
  c->mouse_radius = 1.0/DT_IOP_COLORZONES_BANDS;
  self->widget = GTK_WIDGET(gtk_vbox_new(FALSE, 0));
  c->area = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(c->area), TRUE, TRUE, 5);
  gtk_drawing_area_size(c->area, 195, 195);

  gtk_widget_add_events(GTK_WIDGET(c->area), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (G_OBJECT (c->area), "expose-event",
                    G_CALLBACK (colorzones_expose), self);
  g_signal_connect (G_OBJECT (c->area), "button-press-event",
                    G_CALLBACK (colorzones_button_press), self);
  g_signal_connect (G_OBJECT (c->area), "button-release-event",
                    G_CALLBACK (colorzones_button_release), self);
  g_signal_connect (G_OBJECT (c->area), "motion-notify-event",
                    G_CALLBACK (colorzones_motion_notify), self);
  g_signal_connect (G_OBJECT (c->area), "leave-notify-event",
                    G_CALLBACK (colorzones_leave_notify), self);
  g_signal_connect (G_OBJECT (c->area), "scroll-event",
                    G_CALLBACK (colorzones_scrolled), self);
  // init gtk stuff
  c->hbox = GTK_HBOX(gtk_hbox_new(FALSE, 0));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(c->hbox), FALSE, FALSE, 5);

  c->channel_button[0] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL, _("luma (L)")));
  c->channel_button[1] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(c->channel_button[0], _("colorness (C)")));
  c->channel_button[2] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(c->channel_button[0], _("hue (h)")));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c->channel_button[c->channel]), TRUE);

  g_signal_connect (G_OBJECT (c->channel_button[0]), "toggled",
                    G_CALLBACK (colorzones_button_toggled), self);
  g_signal_connect (G_OBJECT (c->channel_button[1]), "toggled",
                    G_CALLBACK (colorzones_button_toggled), self);
  g_signal_connect (G_OBJECT (c->channel_button[2]), "toggled",
                    G_CALLBACK (colorzones_button_toggled), self);

  gtk_box_pack_end(GTK_BOX(c->hbox), GTK_WIDGET(c->channel_button[2]), FALSE, FALSE, 5);
  gtk_box_pack_end(GTK_BOX(c->hbox), GTK_WIDGET(c->channel_button[1]), FALSE, FALSE, 5);
  gtk_box_pack_end(GTK_BOX(c->hbox), GTK_WIDGET(c->channel_button[0]), FALSE, FALSE, 5);

  // select by which dimension
  GtkHBox *hbox = GTK_HBOX(gtk_hbox_new(FALSE, 0));
  GtkLabel *label = GTK_LABEL(gtk_label_new(_("select color by")));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(label), FALSE, FALSE, 5);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), FALSE, FALSE, 5);
  c->hbox = GTK_HBOX(gtk_hbox_new(FALSE, 0));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(c->hbox), FALSE, FALSE, 5);

  c->select_button[0] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL, _("luma (L)")));
  c->select_button[1] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(c->select_button[0], _("colorness (C)")));
  c->select_button[2] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(c->select_button[0], _("hue (h)")));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c->select_button[p->channel]), TRUE);

  g_signal_connect (G_OBJECT (c->select_button[0]), "toggled",
                    G_CALLBACK (colorzones_select_toggled), self);
  g_signal_connect (G_OBJECT (c->select_button[1]), "toggled",
                    G_CALLBACK (colorzones_select_toggled), self);
  g_signal_connect (G_OBJECT (c->select_button[2]), "toggled",
                    G_CALLBACK (colorzones_select_toggled), self);

  gtk_box_pack_end(GTK_BOX(c->hbox), GTK_WIDGET(c->select_button[2]), FALSE, FALSE, 5);
  gtk_box_pack_end(GTK_BOX(c->hbox), GTK_WIDGET(c->select_button[1]), FALSE, FALSE, 5);
  gtk_box_pack_end(GTK_BOX(c->hbox), GTK_WIDGET(c->select_button[0]), FALSE, FALSE, 5);

  hbox = GTK_HBOX(gtk_hbox_new(FALSE, 0));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), FALSE, FALSE, 5);
  label = GTK_LABEL(gtk_label_new(_("presets")));
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(label), FALSE, FALSE, 5);
  c->presets = GTK_COMBO_BOX(gtk_combo_box_new_text());
  gtk_combo_box_append_text(GTK_COMBO_BOX(c->presets), _("black white red"));
  gtk_combo_box_append_text(GTK_COMBO_BOX(c->presets), _("black white and skin tones"));
  gtk_combo_box_append_text(GTK_COMBO_BOX(c->presets), _("polarizing filter"));
  gtk_box_pack_end(GTK_BOX(hbox), GTK_WIDGET(c->presets), FALSE, FALSE, 5);
  g_signal_connect (G_OBJECT (c->presets), "changed",
                    G_CALLBACK (presets_changed),
                    (gpointer)self);
  c->hsRGB = cmsCreate_sRGBProfile();
  c->hLab  = cmsCreateLabProfile(NULL);
  c->xform = cmsCreateTransform(c->hLab, TYPE_Lab_DBL, c->hsRGB, TYPE_RGB_DBL, 
      INTENT_PERCEPTUAL, 0);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_draw_curve_destroy(c->minmax_curve);
  free(self->gui_data);
  self->gui_data = NULL;
}
