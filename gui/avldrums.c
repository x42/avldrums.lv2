/*
 * Copyright (C) 2013-2016 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

//#define DEVELOP // dump areas
//#define DRUMSHAPE // visualize hotspots

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/avldrums.h"

#define RTK_URI AVL_URI
#define RTK_GUI "ui"
#define LVGL_RESIZEABLE


#ifndef MAX
#define MAX(A,B) ((A) > (B)) ? (A) : (B)
#endif

#ifndef MIN
#define MIN(A,B) ((A) < (B)) ? (A) : (B)
#endif

enum Kit {
	BlackPearl = 1,
	RedZeppelin
};

static const char* drumnames [DRUM_PCS] = {
	"Kick Drum",
	"Snare\nSideStick",
	"Snare Ctr.",
	"Hand Clap",
	"Snare Edge",
	"Floor Tom Ctr.",
	"Closed HiHat",
	"Floor Tom Edge",
	"Pedal HiHat",
	"Tom Ctr.",
	"Semi-Open HiHat",
	"Tom Edge",
	"Swish HiHat",
	"Crash Cymbal 1", // ZildjianA20Crash
	"Crash Cymbal 1\nChoked",
	"Ride Cymbal Tip",  // ZildjianA24Ride
	"Ride Cymbal\nChoked",
	"Ride Cymbal Bell",
	"Tambourine",
	"Splash Cymbal",
	"Cowbell",
	"Crash Cymbal 2", // ZildjianK17Crash
	"Crash Cymbal 2\nChoked",
	"Ride Cymbal\nShank",
	"Crash Cymbal 3", // Paiste2002
	"High Roto Tom",
	"Mid Roto Tom",
	"Low Roto Tom",
	"Maracas"
};

struct kGeometry {
	double cx, cy;
	double dx, dy;
	bool rect; // false: ellipse, true: rectangle
	uint32_t z_index;
};

struct kGeometry drumpos [DRUM_PCS] = {
	{ 0.485401, 0.659020, 0.105318, 0.088634, false,  0}, // Kick
	{ 0.330000, 0.747500, 0.080000, 0.172500, false,  4}, // Snare Side
	{ 0.325000, 0.740000, 0.058750, 0.102500, false,  7}, // Snare
	{ 0, 0, 0, 0, true, 0 },
	{ 0.325000, 0.740000, 0.076250, 0.155000, false,  5}, // Snare Rim
	{ 0.675000, 0.797500, 0.066250, 0.107500, false, 12}, // Floor Tom (center)
	{ 0.166840, 0.698644, 0.093848, 0.166840, false, 10}, // Hi Hat (closed)
	{ 0.671250, 0.807500, 0.100000, 0.195000, false, 10}, // Floor Tom (edge)
	{ 0.239312, 0.887383, 0.048488, 0.089677, false,  0}, // Hi Hat (pedal)
	{ 0.311250, 0.372500, 0.040000, 0.080000, false,  5}, // Tom (center)
	{ 0.131387, 0.813347, 0.032325, 0.060480, false, 18}, // Hi Hat (semi open)
	{ 0.310000, 0.372500, 0.072500, 0.150000, false,  0}, // Tom
	{ 0.199687, 0.772680, 0.035975, 0.068822, false, 17}, // Hi Hat (swish)
	{ 0.142500, 0.325000, 0.101250, 0.172500, false, 35}, // ZildjianA20Crash (left)
	{ 0.147180, 0.332875, 0.125860, 0.211830, false, 30}, // ZildjianA20Crash
	{ 0.641814, 0.479666, 0.084463, 0.189781, false, 25}, // Ride
	{ 0.641814, 0.473410, 0.122523, 0.221064, false, 20}, // Ride (edge)
	{ 0.640772, 0.449426, 0.031804, 0.057351, false, 27}, // Ride (bell)
	{ 0.141814, 0.654849, 0.044838, 0.080292, false, 20}, // Tambourine (above HH)
	{ 0.317744, 0.107290, 0.059147, 0.101788, false,  0}, // Splash
	{ 0.501250, 0.580000, 0.022500, 0.057500, true , 20}, // Cowbell
	{ 0.914568, 0.685252, 0.108813, 0.170863, false, 45}, // ZildjianK17Crash
	{ 0.911279, 0.693260, 0.132737, 0.211830, false, 40}, // ZildjianK17Crash
	{ 0.567258, 0.460897, 0.044838, 0.087591, false, 27}, // Ride (shank)
	{ 0.846715, 0.453597, 0.145985, 0.240876, false, 30}, // Paiste2002
	{ 0, 0, 0, 0, true, 0},
	{ 0, 0, 0, 0, true, 0},
	{ 0, 0, 0, 0, true, 0},
	{ 0, 0, 0, 0, true, 0},
};

typedef struct {
	RobWidget *rw;

	LV2UI_Write_Function write;
	LV2UI_Controller     controller;

	LV2_Atom_Forge   forge;
	AVLLV2URIs       uris;

	PangoFontDescription* font[2];

	int width, height;
	float scale;
	bool size_changed;

	enum Kit kit;
	const char* nfo;

	bool kit_ready;

	float   kit_anim[DRUM_PCS];
	uint8_t kit_velo[DRUM_PCS];

	cairo_surface_t* bg;
	cairo_surface_t* bg_scaled;


	size_t png_readoff;
	int played_note;
	int hover_note;

#ifdef DEVELOP
	double _xc, _yc;
	double _xd, _yd;
	bool   _tt;
#endif
} AvlDrumsLV2UI;

/******************************************************************************
 * texture
 */

static cairo_status_t
red_png_read (void* c, unsigned char* d, unsigned int s)
{
#include  "gui/red_zeppelin.png.h"
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)c;
	if (s + ui->png_readoff > sizeof (RedZeppelinPng)) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy (d, &RedZeppelinPng[ui->png_readoff], s);
	ui->png_readoff += s;
	return CAIRO_STATUS_SUCCESS;
}


/******************************************************************************
 * LV2 messaging
 */

static void
msg_to_dsp (AvlDrumsLV2UI* ui, LV2_URID urid)
{
	uint8_t obj_buf[64];
	lv2_atom_forge_set_buffer (&ui->forge, obj_buf, 64);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time (&ui->forge, 0);
	LV2_Atom* msg = (LV2_Atom*)x_forge_object (&ui->forge, &frame, 1, urid);
	lv2_atom_forge_pop (&ui->forge, &frame);
	ui->write (ui->controller, AVL_PORT_CONTROL,
			lv2_atom_total_size (msg), ui->uris.atom_eventTransfer, msg);
}

static void
forge_note (AvlDrumsLV2UI* ui, uint8_t note, uint8_t vel)
{
	uint8_t obj_buf[16];
	uint8_t buffer[3];
	buffer[0] = (vel > 0 ? 0x90 : 0x80);
	buffer[1] = note;
	buffer[2] = vel;

	LV2_Atom midiatom;
	midiatom.type = ui->uris.midi_MidiEvent;
	midiatom.size = 3;

	lv2_atom_forge_set_buffer (&ui->forge, obj_buf, 16);
	lv2_atom_forge_raw (&ui->forge, &midiatom, sizeof(LV2_Atom));
	lv2_atom_forge_raw (&ui->forge, buffer, 3);
	lv2_atom_forge_pad (&ui->forge, sizeof(LV2_Atom) + 3);

	ui->write (ui->controller, AVL_PORT_CONTROL,
			lv2_atom_total_size (&midiatom), ui->uris.atom_eventTransfer, obj_buf);
}


/******************************************************************************
 * Drawing
 */

#define SW(X) (ui->width * (X))
#define SH(Y) (ui->height * (Y))
#define RIM SW (.006)

static void
queue_drum_expose (AvlDrumsLV2UI* ui, uint32_t d)
{
	assert (d < DRUM_PCS);
	struct kGeometry* g = &drumpos[d];
	queue_draw_area (ui->rw,
			SW (g->cx - g->dx) - 1, SH (g->cy - g->dy) -1,
			SW (2 * g->dx) + 2, SH (2 * g->dy) + 2);
}

#ifdef DRUMSHAPE
static void
drum_path (AvlDrumsLV2UI* ui, cairo_t* cr, struct kGeometry* g)
{
	if (g->rect) {
		cairo_rectangle (cr,
				SW (g->cx - g->dx), SH (g->cy - g->dy),
				SW (2 * g->dx), SH (2 * g->dy));
	} else {
		cairo_matrix_t save_matrix;
		cairo_get_matrix(cr, &save_matrix);
		cairo_translate(cr, SW (g->cx), SH (g->cy));
		cairo_scale(cr, 1.0, SH (g->dy) / SW (g->dx));
		cairo_translate(cr, SW (-g->cx), SH (-g->cy));
		cairo_new_path(cr);
		cairo_arc (cr, SW (g->cx), SH (g->cy), SW (g->dx), 0, 2 * M_PI);
		cairo_set_matrix(cr, &save_matrix);
	}
}
#endif


static bool
expose_event (RobWidget* handle, cairo_t* cr, cairo_rectangle_t* ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip_preserve (cr);
	CairoSetSouerceRGBA(c_trs);
	cairo_fill (cr);

	if (ui->size_changed) {
		float scale = ui->width / (float) cairo_image_surface_get_width (ui->bg);
		if (ui->bg_scaled) {
			cairo_surface_destroy (ui->bg_scaled);
		}
		ui->bg_scaled = cairo_image_surface_create (CAIRO_FORMAT_RGB24, ui->width, ui->height);
		cairo_t* icr = cairo_create (ui->bg_scaled);
		cairo_scale (icr, scale, scale);
		cairo_set_source_surface (icr, ui->bg, 0, 0);
		cairo_paint (icr);
		cairo_destroy (icr);
		ui->size_changed = false;

		char ft[32];
		sprintf(ft, "Sans Bold %dpx", (int) rint(20. * scale));
		pango_font_description_free(ui->font[0]);
		ui->font[0] = pango_font_description_from_string(ft);

		sprintf(ft, "Sans %dpx", (int) rint(18. * scale));
		pango_font_description_free(ui->font[1]);
		ui->font[1] = pango_font_description_from_string(ft);
	}

	const float dt = 1/25.f;

	cairo_set_source_surface (cr, ui->bg_scaled, 0, 0);
	cairo_paint (cr);

	// TODO !ui->kit_ready -> shade

	cairo_set_line_width (cr, 1.5);
	PangoLayout* pl = pango_cairo_create_layout(cr);
	for (int i = 0; i < DRUM_PCS; ++i) {
		if (ui->kit_anim[i] > 0) {
			const double br = .3 + .7 * ui->kit_anim[i];
			const double bg = .3 + .7 * ui->kit_anim[i] * (1.f - ui->kit_velo[i] / 127.f);
			float clr[4];
			clr[0] = br;
			clr[1] = bg;
			clr[2] = .3;
			clr[3] = .9 * ui->kit_anim[i];

			float anim = 1 - ui->kit_anim[i];
			float yoff = anim * 0.1;
			double cx = SW (drumpos[i].cx);
			double cy = SH (drumpos[i].cy - yoff);

			cairo_save (cr);
			cairo_translate (cr, cx, cy);

			int tw, th;
			pango_layout_set_font_description(pl, ui->font[0]);
			pango_layout_set_markup(pl, drumnames[i], -1);
			pango_layout_get_pixel_size(pl, &tw, &th);
			pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
			pango_cairo_update_layout(cr, pl);

			cairo_scale (cr, 1 + .15 * anim, 1 + .15 * anim);
			cairo_translate (cr, ceil (tw / -2.0), ceil (th / -2.0));
			pango_cairo_layout_path(cr, pl);

			cairo_set_source_rgba (cr, 0, 0, 0, clr[3]);
			cairo_stroke_preserve(cr);
			cairo_set_source_rgba (cr, clr[0], clr[1], clr[2], clr[3]);
			cairo_fill (cr);

			cairo_restore (cr);

			if (ui->kit_anim[i] > dt) {
				ui->kit_anim[i] -= dt;
				queue_draw_area (ui->rw, cx - tw * .6, cy - th * .6, tw * 1.2, th * 1.2);
			} else if (ui->kit_anim[i] > 0) {
				ui->kit_anim[i] = 0;
				queue_draw_area (ui->rw, cx - tw * .6, cy - th * .6, tw * 1.2, th * 1.2);
			} else {
				ui->kit_anim[i] = 0;
			}
		}
	}
	g_object_unref(pl);

	if (ui->hover_note >= 0) {
		int i = ui->hover_note;
		static const float c_ann[4] = {0.1, 1.0, 0.1, 1.0};
		write_text_full (cr, drumnames[i], ui->font[1],
				SW (drumpos[i].cx), SH (drumpos[i].cy),
				0, 2, c_ann);
	}

#ifdef DEVELOP
	if (ui->played_note >= 0 && ui->_yd > 0 && ui->_xd > 0) {
		cairo_move_to (cr, SW (ui->_xc), SH (ui->_yc));
		cairo_close_path (cr);
		cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_width (cr, 5.0);
		cairo_set_source_rgba (cr, 1, 0, 0, 1);
		cairo_stroke (cr);
		if (ui->_tt) {
			cairo_rectangle (cr,
					SW (ui->_xc - ui->_xd), SH (ui->_yc - ui->_yd),
					SW (2 * ui->_xd), SH (2 * ui->_yd));
		} else {
			cairo_matrix_t save_matrix;
			cairo_get_matrix(cr, &save_matrix);
			cairo_translate(cr, SW (ui->_xc), SH (ui->_yc));
			cairo_scale(cr, 1.0, SH (ui->_yd) / SW (ui->_xd));
			cairo_translate(cr, SW (-ui->_xc), SH (-ui->_yc));
			cairo_arc (cr, SW (ui->_xc), SH (ui->_yc), SW (ui->_xd), 0, 2 * M_PI);
			cairo_set_matrix(cr, &save_matrix);
		}
		cairo_set_source_rgba (cr, .7, .7, .0, .5);
		cairo_fill (cr);
	}
#endif

#ifdef DRUMSHAPE
	for (int i = 0; i < DRUM_PCS; ++i) {
		float c[4];
		c[0] = rand() / (float)RAND_MAX;
		c[1] = rand() / (float)RAND_MAX;
		c[2] = rand() / (float)RAND_MAX;
		c[3] = 1.0;
		cairo_set_source_rgba (cr, c[0], c[1], c[2], 0.3);
		drum_path (ui, cr, &drumpos[i]);
		cairo_fill (cr);
	}
#endif

#if 0 // VISUALIZE EXPOSE AREA
  float c[4];
	c[0] = rand() / (float)RAND_MAX;
	c[1] = rand() / (float)RAND_MAX;
	c[2] = rand() / (float)RAND_MAX;
	c[3] = 1.0;
  cairo_set_source_rgba (cr, c[0], c[1], c[2], 0.3);
  cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
  cairo_fill(cr);
#endif
	return TRUE;
}

#undef SW
#undef SH

/******************************************************************************
 * UI event handling
 */

static float
event_inside (AvlDrumsLV2UI* ui, struct kGeometry* g, double xx, double yy, uint32_t minz)
{
	if (g->rect) {
		if (g->cx - g->dx < xx && xx < g->cx + g->dx && g->cy - g->dy < yy && yy < g->cy + g->dy) {
			if (g->z_index >= minz) {
				return 1;
			}
		}
	} else {
		if (g->dx == 0 || g->dy == 0) { return 0; }

#define SQ(X) ((X) * (X))
		const double d = SQ(xx - g->cx) / SQ(g->dx) + SQ(yy - g->cy) / SQ(g->dy);
		if (d < 1 && g->z_index >= minz) { return 1; }
#undef SQ
	}
	return 0;
}

static int
find_note (AvlDrumsLV2UI* ui, RobTkBtnEvent* ev, float* velocity)
{
	double xx = ev->x / (double) ui->width;
	double yy = ev->y / (double) ui->height;

	int n = -1;
	uint32_t minz = 0;
	float vel = 0;
	for (int i = 0; i < DRUM_PCS; ++i) {
		float v;
		if (0 < (v = event_inside (ui, &drumpos[i], xx, yy, minz))) {
			minz = drumpos[i].z_index;
			n = i;
			vel = v;
		}
	}
	if (n >= 0 && velocity) {
		*velocity = vel;
	}
	return n;
}

static RobWidget*
mousemove (RobWidget* handle, RobTkBtnEvent *ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
#ifdef DEVELOP
	if (ui->played_note >= 0) {
		double xp = ev->x / (double) ui->width;
		double yp = ev->y / (double) ui->height;
		ui->_xd = fabs (xp - ui->_xc);
		ui->_yd = fabs (yp - ui->_yc);
		printf ("{ %f, %f, %f, %f, %5s, 0 },\n",
				ui->_xc, ui->_yc, ui->_xd, ui->_yd, ui->_tt ? "true" : "false");
		queue_draw (ui->rw);
	}
#else
	int n = find_note (ui, ev, NULL);
	if (ui->hover_note != n) {
		// TODO position.. + area
		ui->hover_note = n;
		queue_draw (ui->rw);
	}
#endif
	return NULL;
}

static RobWidget*
mousedown (RobWidget* handle, RobTkBtnEvent *ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
#ifdef DEVELOP
	ui->_xc = ev->x / (double) ui->width;
	ui->_yc = ev->y / (double) ui->height;
	ui->_xd = 0;
	ui->_yd = 0;
	ui->_tt = ev->button == 1 ? false : true;
	printf ("{ %f, %f, %f, %f, %5s, 0 },\n",
			ui->_xc, ui->_yc, ui->_xd, ui->_yd, ui->_tt ? "true" : "false");
	queue_draw (ui->rw);
#endif

	if (!ui->kit_ready) {
		return NULL;
	}

	int n = find_note (ui, ev, NULL);
	if (n >= 0) {
		ui->played_note = n + 36;
		forge_note (ui, ui->played_note, 0x7f); // TODO velocity
	}
	return handle;
}

static RobWidget*
mouseup (RobWidget* handle, RobTkBtnEvent *event)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	if (ui->played_note >= 0) {
		forge_note (ui, ui->played_note, 0);
	}
	ui->played_note = -1;
#ifdef DEVELOP
	queue_draw (ui->rw);
#endif
	return NULL;
}

static void
size_request (RobWidget* rw, int *w, int *h)
{
	*w = 800;
	*h = 400;
}

static void
size_limit (RobWidget* rw, int *w, int *h)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (rw);
	int dflw, dflh;
	size_request (rw, &dflw, &dflh);
	float scale = MIN (*w / (float)dflw, *h / (float)dflh);
	if (scale < .5 ) scale = .5;
	if (scale > 3.5 ) scale = 3.5;
	ui->scale = scale;
	ui->width = rint (dflw * scale);
	ui->height = rint (dflh * scale);
	robwidget_set_size(rw, ui->width, ui->height); // single top-level
	*w = ui->width;
	*h = ui->height;
	ui->size_changed = true;
	queue_draw (rw);
}

static void
size_allocate (RobWidget* rw, int w, int h)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (rw);
	int ww = w;
	int wh = h;
	size_limit (rw, &ww, &wh);
	robwidget_set_alignment (rw, .5, .5);
	rw->area.x = rint ((w - rw->area.width) * rw->xalign);
	rw->area.y = rint ((h - rw->area.height) * rw->yalign);
}

/******************************************************************************
 * LV2 callbacks
 */

static void
ui_enable (LV2UI_Handle handle)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)handle;
	msg_to_dsp (ui, ui->uris.ui_on);
}

static void
ui_disable (LV2UI_Handle handle) {
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)handle;
	msg_to_dsp (ui, ui->uris.ui_off);
}

static LV2UI_Handle
instantiate (
		void* const               ui_toplevel,
		const LV2UI_Descriptor*   descriptor,
		const char*               plugin_uri,
		const char*               bundle_path,
		LV2UI_Write_Function      write_function,
		LV2UI_Controller          controller,
		RobWidget**               widget,
		const LV2_Feature* const* features)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)calloc (1, sizeof (AvlDrumsLV2UI));
	LV2_URID_Map* map = NULL;
	*widget = NULL;

	if (!ui) {
		fprintf (stderr, "avldrums.lv2: out of memory.\n");
		return NULL;
	}

	if      (!strcmp (plugin_uri, RTK_URI "BlackPearl"))       { ui->kit = BlackPearl; }
	else if (!strcmp (plugin_uri, RTK_URI "BlackPearlMulti"))  { ui->kit = BlackPearl; }
	else if (!strcmp (plugin_uri, RTK_URI "RedZeppelin"))      { ui->kit = RedZeppelin; }
	else if (!strcmp (plugin_uri, RTK_URI "RedZeppelinMulti")) { ui->kit = RedZeppelin; }

	if (ui->kit == 0) {
		free (ui);
		return NULL;
	}

	for (int i = 0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_URID__map)) {
			map = (LV2_URID_Map*)features[i]->data;
		}
	}

	if (!map) {
		free (ui);
		return NULL;
	}

	ui->write        = write_function;
	ui->controller   = controller;
	ui->nfo          = robtk_info (ui_toplevel);
	ui->played_note  = -1;
	ui->hover_note   = -1;
	ui->kit_ready    = false;
	ui->png_readoff  = 0;
	ui->size_changed = true;

	for (int i = 0; i < DRUM_PCS; ++i) {
		ui->kit_anim[i] = 0;
		ui->kit_velo[i] = 0;
	}

	lv2_atom_forge_init (&ui->forge, map);
	map_avldrums_uris (map, &ui->uris);

	/* GUI layout */
	ui->font[0] = pango_font_description_from_string("Sans Bold 16px");
	ui->font[1] = pango_font_description_from_string("Sans 14px");

	ui->rw = robwidget_new (ui);
	robwidget_make_toplevel (ui->rw, ui_toplevel);
	ROBWIDGET_SETNAME (ui->rw, "drums");

	robwidget_set_expose_event (ui->rw, expose_event); // depending on kit
	robwidget_set_size_request (ui->rw, size_request);
	robwidget_set_size_allocate (ui->rw, size_allocate);
	robwidget_set_size_limit (ui->rw, size_limit); // XXX
	robwidget_set_size_default (ui->rw, size_request);
	robwidget_set_mouseup (ui->rw, mouseup);
	robwidget_set_mousedown (ui->rw, mousedown);
	robwidget_set_mousemove (ui->rw, mousemove);

	ui->bg = cairo_image_surface_create_from_png_stream (red_png_read, ui);

	*widget = ui->rw;
	return ui;
}

static enum LVGLResize
plugin_scale_mode (LV2UI_Handle handle)
{
	return LVGL_LAYOUT_TO_FIT;
}

static void
cleanup (LV2UI_Handle handle)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)handle;
	robwidget_destroy (ui->rw);
	cairo_surface_destroy (ui->bg);
	if (ui->bg_scaled) {
		cairo_surface_destroy (ui->bg_scaled);
	}
	pango_font_description_free(ui->font[0]);
	pango_font_description_free(ui->font[1]);
	free (ui);
}

static const void*
extension_data (const char* uri)
{
	return NULL;
}

static void
port_event (
		LV2UI_Handle handle,
		uint32_t     port_index,
		uint32_t     buffer_size,
		uint32_t     format,
		const void*  buffer)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)handle;

	if (format == ui->uris.atom_eventTransfer && port_index == AVL_PORT_NOTIFY) {
		LV2_Atom* atom = (LV2_Atom*)buffer;
		if (atom->type == ui->uris.atom_Blank || atom->type == ui->uris.atom_Object) {
			LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;
			const LV2_Atom *a0 = NULL;

			if (obj->body.otype == ui->uris.drumkit) {
				if (1 == lv2_atom_object_get (obj, ui->uris.loaded, &a0, NULL)
						&& a0->type == ui->uris.atom_Bool
					 ) {
					const bool ok = ((LV2_Atom_Bool*)a0)->body;
					if (ok != ui->kit_ready) {
						ui->kit_ready = ok;
						queue_draw (ui->rw);
					}
				}
			}
			else if (obj->body.otype == ui->uris.drumhit) {
				if (1 == lv2_atom_object_get (obj, ui->uris.drumhits, &a0, NULL)
						&& a0->type == ui->uris.atom_Vector
					 ) {
					LV2_Atom_Vector* voi = (LV2_Atom_Vector*)LV2_ATOM_BODY (a0);
					assert (voi->atom.type == ui->uris.atom_Int);
					const size_t n_elem = (a0->size - sizeof (LV2_Atom_Vector_Body)) / voi->atom.size;
					assert (n_elem == DRUM_PCS);
					const int32_t* data = (int32_t*) LV2_ATOM_BODY (&voi->atom);

					if (ui->kit_ready) {
						/* set/start animation */
						for (int i = 0; i < DRUM_PCS; ++i) {
							if (data[i] > 0) {
								ui->kit_anim [i] = 1.0;
								ui->kit_velo [i] = data[i];
								queue_drum_expose (ui, i);
							}
						}
					} /* kit ready */
				}
			} /* drumhit */
		} /* atom object */
	} /* atom message */
}
