/*
 * Copyright (C) 2013-2023 Robin Gareus <robin@gareus.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/avldrums.h"
#include "../gui/cairoblur.h"

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
	RedZeppelin,
	BlondeBop,
	BlondeBopHR,
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
	"Crash Cymbal 3", // Paiste2002 / China
	"Maracas/Shaker" // Maracas or Shaker
};

/* map_rec[ColorIndex] = drum
 *
 * When the map is scaled, the border of two
 * map colors is interpolated / antialised.
 *
 * This map allows to use neighboring colors
 * for different hits on the same drum.
 *
 * While also avoiding antialiasing on
 * overlapping unrelated pieces.
 */
static const uint8_t map_red[DRUM_PCS] = {
	0, // Kick
	2, // Snare Center
	4, // Snare Rim
	1, // Snare Side
	3, // Hand Clap

	12, // Hi Hat (swish - outside)
	10, // Hi Hat (semi open)
	6, // Hi Hat (closed - inside)
	7, // Floor Tom (edge)
	5, // Floor Tom (center)

	8, // Hi Hat (pedal)
	11, // Tom edge
	9, // Tom center
	13, // Crash 1
	14, // Crash 1 choked (overlaps with Tom 11,9 in redzep)

	16, // Ride edge/choked  (out)
	23, // Ride shank
	15, // Ride tip
	17, // Ride bell (inside)
	18, // Tambourine

	22, // Crash2 choked (out)
	21, // Crash2 (in)
	20, // Cowbell
	24, // Crash3 / China
	19, // Splash

	25 // Maracas / Shaker
};

static const int drumgroup [DRUM_PCS][2] = {
	{-1, -1}, // 0: Kick
	{ 2,  4}, // 1: Snare Side
	{ 1,  4}, // 2: Snare
	{-1, -1}, // 3: Hand Clap
	{ 2, -1}, // 4: Snare Rim
	{ 7, 25}, // 5: Floor Tom (center)
	{10, 12}, // 6: Hi Hat (closed)
	{ 5, 25}, // 7: Floor Tom (edge)
	{ 1, -1}, // 8: Hi Hat (pedal)
	{11, -1}, // 9: Tom (center)
	{ 6, 12}, // 10: Hi Hat (semi open)
	{ 9, -1}, // 11: Tom (edge)
	{ 6, 10}, // 12: Hi Hat (swish)
	{14, -1}, // 13: SabianCrash (left)
	{13, -1}, // 14: SabianCrash (choked)
	{23, 17}, // 15: Ride (tip)
	{15, 23}, // 16: Ride (edge/choked)
	{15, 23}, // 17: Ride (bell)
	{-1, -1}, // 18: Tambourine (below HH)
	{-1, -1}, // 19: Splash
	{-2,  1}, // 20: Cowbell
	{22, -1}, // 21: SabianAAXCrash
	{21, -1}, // 22: SabianAAXCrash
	{15, 16}, // 23: Ride (shank)
	{-1, -1}, // 24: Paiste2002 / China
	{-2, -1}, // 25: Maracas / Shaker // 5, 7
};

struct kGeometry {
	double cx, cy;
	double dx, dy;
};

/* areas for visual invalidate and center position for text */
struct kGeometry pos_redzep [DRUM_PCS] = {
	{ 0.494497, 0.665689, 0.103448, 0.167155 }, // Kick
	{ 0.335290, 0.652493, 0.079237, 0.168622 }, // Snare Side
	{ 0.335290, 0.652493, 0.079237, 0.168622 }, // Snare
	{ 0.491563, 0.343109, 0.045488, 0.095308 }, // Hand Clap
	{ 0.335290, 0.652493, 0.079237, 0.168622 }, // Snare Rim
	{ 0.680851, 0.662757, 0.099046, 0.184751 }, // Floor Tom (center)
	{ 0.172414, 0.699413, 0.097579, 0.175953 }, // Hi Hat (closed)
	{ 0.680851, 0.662757, 0.099046, 0.184751 }, // Floor Tom (edge)
	{ 0.302274, 0.920821, 0.060895, 0.083578 }, // Hi Hat (pedal)
	{ 0.311079, 0.341642, 0.077770, 0.155425 }, // Tom (center)
	{ 0.172414, 0.699413, 0.097579, 0.175953 }, // Hi Hat (semi open)
	{ 0.311079, 0.341642, 0.077770, 0.155425 }, // Tom
	{ 0.172414, 0.699413, 0.097579, 0.175953 }, // Hi Hat (swish)
	{ 0.152605, 0.243402, 0.132062, 0.240469 }, // ZildjianA20Crash (left)
	{ 0.152605, 0.243402, 0.132062, 0.240469 }, // ZildjianA20Crash
	{ 0.643434, 0.312317, 0.113720, 0.211144 }, // Ride
	{ 0.643434, 0.312317, 0.113720, 0.211144 }, // Ride (edge)
	{ 0.643434, 0.312317, 0.113720, 0.211144 }, // Ride (bell)
	{ 0.219369, 0.894428, 0.061629, 0.105572 }, // Tambourine (below HH)
	{ 0.324285, 0.105572, 0.058694, 0.108504 }, // Splash
	{ 0.495965, 0.508798, 0.024945, 0.067449 }, // Cowbell
	{ 0.872340, 0.558651, 0.118855, 0.208211 }, // ZildjianK17Crash
	{ 0.872340, 0.558651, 0.118855, 0.208211 }, // ZildjianK17Crash
	{ 0.643434, 0.312317, 0.113720, 0.211144 }, // Ride (shank)
	{ 0.795304, 0.309384, 0.131328, 0.218475 }, // Paiste2002
	{ 0.735877, 0.736070, 0.068966, 0.111437 }  // Maracas
};

struct kGeometry pos_blackperl [DRUM_PCS] = {
	{ 0.466111, 0.671533, 0.098019, 0.155370 }, // Kick
	{ 0.305005, 0.673618, 0.082899, 0.161627 }, // Snare Side
	{ 0.305005, 0.673618, 0.082899, 0.161627 }, // Snare
	{ 0.460375, 0.359750, 0.040667, 0.083420 }, // Hand Clap
	{ 0.305005, 0.673618, 0.082899, 0.161627 }, // Snare Rim
	{ 0.635036, 0.690302, 0.088634, 0.168926 }, // Floor Tom (center)
	{ 0.143379, 0.719499, 0.095933, 0.180396 }, // Hi Hat (closed)
	{ 0.635036, 0.690302, 0.088634, 0.168926 }, // Floor Tom (edge)
	{ 0.270073, 0.921794, 0.063087, 0.080292 }, // Hi Hat (pedal)
	{ 0.316475, 0.368092, 0.066736, 0.128259 }, // Tom (center)
	{ 0.143379, 0.719499, 0.095933, 0.180396 }, // Hi Hat (semi open)
	{ 0.316475, 0.368092, 0.066736, 0.128259 }, // Tom
	{ 0.143379, 0.719499, 0.095933, 0.180396 }, // Hi Hat (swish)
	{ 0.112617, 0.258603, 0.108968, 0.191867 }, // SabianCrash (left)
	{ 0.112617, 0.258603, 0.108968, 0.191867 }, // SabianCrash
	{ 0.610010, 0.308655, 0.100626, 0.185610 }, // Ride
	{ 0.610010, 0.308655, 0.100626, 0.185610 }, // Ride (edge)
	{ 0.610010, 0.308655, 0.100626, 0.185610 }, // Ride (bell)
	{ 0.191345, 0.890511, 0.059958, 0.110532 }, // Tambourine (below HH)
	{ 0.288321, 0.106361, 0.058916, 0.108446 }, // Splash
	{ 0.469760, 0.556830, 0.023462, 0.067779 }, // Cowbell
	{ 0.843066, 0.548488, 0.115746, 0.210636 }, // SabianAAXCrash
	{ 0.843066, 0.548488, 0.115746, 0.210636 }, // SabianAAXCrash
	{ 0.610010, 0.308655, 0.100626, 0.185610 }, // Ride (shank)
	{ 0.764338, 0.301356, 0.131387, 0.216893 }, // Paiste2002
	{ 0.684046, 0.736184, 0.061522, 0.110532 }, // Maracas
};

struct kGeometry pos_blondebop [DRUM_PCS] = {
	{ 0.465000, 0.577500, 0.090000, 0.140000 }, // Kick
	{ 0.335000, 0.610000, 0.077500, 0.147500 }, // Snare Side
	{ 0.335000, 0.610000, 0.077500, 0.147500 }, // Snare
	{ 0.440000, 0.255000, 0.038750, 0.085000 }, // Hand Clap
	{ 0.335000, 0.610000, 0.077500, 0.147500 }, // Snare Rim
	{ 0.583750, 0.655000, 0.076250, 0.152500 }, // Floor Tom (center)
	{ 0.201250, 0.695000, 0.092500, 0.165000 }, // Hi Hat (closed)
	{ 0.583750, 0.655000, 0.076250, 0.152500 }, // Floor Tom (edge)
	{ 0.332500, 0.947500, 0.030000, 0.067500 }, // Hi Hat (pedal)
	{ 0.372500, 0.347500, 0.056250, 0.110000 }, // Tom (center)
	{ 0.201250, 0.695000, 0.092500, 0.165000 }, // Hi Hat (semi open)
	{ 0.372500, 0.347500, 0.056250, 0.110000 }, // Tom
	{ 0.201250, 0.687500, 0.087500, 0.167500 }, // Hi Hat (swish)
	{ 0.196250, 0.255000, 0.108750, 0.185000 }, // Paiste Crash (left)
	{ 0.196250, 0.255000, 0.108750, 0.185000 }, // Paiste Crash
	{ 0.563750, 0.297500, 0.101250, 0.182500 }, // Ride
	{ 0.563750, 0.297500, 0.101250, 0.182500 }, // Ride (edge)
	{ 0.563750, 0.297500, 0.101250, 0.182500 }, // Ride (bell)
	{ 0.266250, 0.875000, 0.066250, 0.122500 }, // Tambourine (below HH)
	{ 0.352500, 0.125000, 0.082500, 0.132500 }, // Splash
	{ 0.461250, 0.442500, 0.037500, 0.047500 }, // Cowbell
	{ 0.691250, 0.407500, 0.101250, 0.167500 }, // SabianAAXCrash
	{ 0.691250, 0.407500, 0.101250, 0.167500 }, // SabianAAXCrash
	{ 0.563750, 0.297500, 0.101250, 0.182500 }, // Ride (shank)
	{ 0.771250, 0.667500, 0.130000, 0.220000 }, // China
	{ 0.580000, 0.637500, 0.063750, 0.135000 }, // Shaker
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
	cairo_surface_t* map;
	cairo_surface_t* map_scaled;
	cairo_surface_t* anim_alpha[DRUM_PCS];

	size_t png_readoff;
	size_t map_readoff;
	int played_note;
	int hover_note;
	bool show_hotzones;
	bool show_highlight;
	bool show_text;
	bool show_menu;
	uint8_t m_vel;

	struct kGeometry* drumpos;

#ifdef DEVELOP
	double _xc, _yc;
	double _xd, _yd;
#endif
} AvlDrumsLV2UI;

/******************************************************************************
 * texture
 */

static cairo_status_t
img_png_read (void* c, unsigned char* d, unsigned int s)
{
#include  "gui/red_zeppelin.png.h"
#include  "gui/black_pearl.png.h"
#include  "gui/blonde_bop.png.h"
#include  "gui/blonde_bop_hr.png.h"

	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)c;
	const unsigned char* img;
	size_t len;
	switch (ui->kit) {
		case RedZeppelin:
			img = RedZeppelinPng;
			len = sizeof (RedZeppelinPng);
			break;
		case BlondeBop:
			img = BlondeBopPng;
			len = sizeof (BlondeBopPng);
			break;
		case BlondeBopHR:
			img = BlondeBopHRPng;
			len = sizeof (BlondeBopHRPng);
			break;
		default:
			img = BlackPearlPng;
			len = sizeof (BlackPearlPng);
			break;
	}
	if (s + ui->png_readoff > len) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy (d, &img[ui->png_readoff], s);
	ui->png_readoff += s;
	return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
map_png_read (void* c, unsigned char* d, unsigned int s)
{
#include  "gui/red_zeppelin.map.h"
#include  "gui/black_pearl.map.h"
#include  "gui/blonde_bop.map.h"

	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)c;
	const unsigned char* img;
	size_t len;
	switch (ui->kit) {
		case RedZeppelin:
			img = RedZeppelinMap;
			len = sizeof (RedZeppelinMap);
			break;
		case BlondeBop:
		case BlondeBopHR:
			img = BlondeBopMap;
			len = sizeof (BlondeBopMap);
			break;
		default:
			img = BlackPearlMap;
			len = sizeof (BlackPearlMap);
			break;
	}
	if (s + ui->map_readoff > len) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy (d, &img[ui->map_readoff], s);
	ui->map_readoff += s;
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
	struct kGeometry* g = &ui->drumpos[d];
	queue_draw_area (ui->rw,
			SW (g->cx - g->dx) - 1, SH (g->cy - g->dy) -1,
			SW (2 * g->dx) + 2, SH (2 * g->dy) + 2);
}

static void
outline_text (
		cairo_t* cr,
		PangoLayout* pl,
		PangoFontDescription* font,
		const char* txt,
		const float cx, const float cy, const float scale,
		const float* const c_txt,
		const float* const c_out,
		int* twp, int* thp)
{
	cairo_save (cr);
	cairo_translate (cr, cx, cy);

	int tw, th;
	pango_layout_set_font_description(pl, font);
	pango_layout_set_markup(pl, txt, -1);
	pango_layout_get_pixel_size(pl, &tw, &th);
	pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
	pango_cairo_update_layout(cr, pl);

	cairo_scale (cr, scale, scale);
	cairo_translate (cr, ceil (tw / -2.0), ceil (th / -2.0));
	pango_cairo_layout_path(cr, pl);

	CairoSetSouerceRGBA (c_out);
	cairo_stroke_preserve(cr);
	CairoSetSouerceRGBA (c_txt);
	cairo_fill (cr);

	cairo_restore (cr);
	if (twp) { *twp = tw; }
	if (thp) { *thp = th; }
}

static bool
expose_event (RobWidget* handle, cairo_t* cr, cairo_rectangle_t* ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip_preserve (cr);
	CairoSetSouerceRGBA(c_trs);
	cairo_fill (cr);

	if (ui->size_changed) {
		float scale_i = ui->width / (float) cairo_image_surface_get_width (ui->bg);
		float scale_m = ui->width / (float) cairo_image_surface_get_width (ui->map);
		if (ui->bg_scaled) { cairo_surface_destroy (ui->bg_scaled); }
		if (ui->map_scaled) { cairo_surface_destroy (ui->map_scaled); }

		ui->bg_scaled = cairo_image_surface_create (CAIRO_FORMAT_RGB24, ui->width, ui->height);
		ui->map_scaled = cairo_image_surface_create (CAIRO_FORMAT_RGB24, ui->width, ui->height);

		cairo_t* icr = cairo_create (ui->bg_scaled);
		cairo_scale (icr, scale_i, scale_i);
		cairo_set_source_surface (icr, ui->bg, 0, 0);
		cairo_paint (icr);
		cairo_destroy (icr);

		icr = cairo_create (ui->map_scaled);
		cairo_scale (icr, scale_m, scale_m);
		cairo_set_source_surface (icr, ui->map, 0, 0);
		cairo_paint (icr);
		cairo_destroy (icr);
		cairo_surface_flush (ui->map_scaled);

		for (int i = 0; i < DRUM_PCS; ++i) {
			int d = map_red[i];
			struct kGeometry* g = &ui->drumpos[d];
			int ww = SW (2 * g->dx);
			int hh = SH (2 * g->dy);
			int xoff = SW (g->cx - g->dx);
			int yoff = SH (g->cy - g->dy);

			if (ui->anim_alpha[d]) { cairo_surface_destroy (ui->anim_alpha[d]); }
			ui->anim_alpha[d] = cairo_image_surface_create (CAIRO_FORMAT_A8, ww, hh);

			int src_stride = cairo_image_surface_get_stride (ui->map_scaled);
			int dst_stride = cairo_image_surface_get_stride (ui->anim_alpha[d]);
			unsigned char* src = cairo_image_surface_get_data (ui->map_scaled);
			unsigned char* dst = cairo_image_surface_get_data (ui->anim_alpha[d]);

			for (int yy = 0; yy < hh; ++yy) {
				if (yy + yoff < 0 || yy + yoff >= ui->height) { continue; }
				uint32_t sy = (yy + yoff) * src_stride;
				uint32_t dy = yy * dst_stride;
				for (int xx = 0; xx < ww; ++xx) {
					if (xx + xoff < 0 || xx + xoff >= ui->width) { continue; }
					uint32_t sp = sy + (xx + xoff) * 4;
					uint32_t dp = dy + xx;

					int clr = ((src[sp+2] & 0xff) - 10) / 9;
					if (i == clr) {
						dst[dp] = 0xff;
					}
				}
			}

			cairo_surface_mark_dirty(ui->anim_alpha[d]);
			blur_image_surface(ui->anim_alpha[d], ww);

#if 1 // remove blur outside of active areas
			if (drumgroup[d][0] == -2) {
				/* allow bleed */
				continue;
			}

			cairo_surface_mark_dirty(ui->anim_alpha[d]);
			for (int yy = 0; yy < hh; ++yy) {
				if (yy + yoff < 0 || yy + yoff >= ui->height) { continue; }
				uint32_t sy = (yy + yoff) * src_stride;
				uint32_t dy = yy * dst_stride;
				for (int xx = 0; xx < ww; ++xx) {
					if (xx + xoff < 0 || xx + xoff >= ui->width) { continue; }
					uint32_t sp = sy + (xx + xoff) * 4;
					uint32_t dp = dy + xx;

					if ((src[sp+2] & 0xff) == 0) {
						dst[dp] = 0x00;
						continue;
					}

					int clr = ((src[sp+2] & 0xff) - 10) / 9;
					if (clr < 0 || clr >= DRUM_PCS) {
						dst[dp] = 0x00;
						continue;
					}

					/* allow overlap with same same piece */
					clr = map_red[clr];
					if (d == clr) {
						continue;
					}
					if (drumgroup[d][0] >= 0 && drumgroup[d][0] == clr) {
						continue;
					}
					if (drumgroup[d][1] >= 0 && drumgroup[d][1] == clr) {
						continue;
					}
					dst[dp] = 0x00;
				}
			}
#endif
		}

		ui->size_changed = false;

		/* scale fonts */
		char ft[32];
		sprintf(ft, "Sans Bold %dpx", (int) rint(20. * scale_m));
		pango_font_description_free(ui->font[0]);
		ui->font[0] = pango_font_description_from_string(ft);

		sprintf(ft, "Sans %dpx", (int) rint(20. * scale_m));
		pango_font_description_free(ui->font[1]);
		ui->font[1] = pango_font_description_from_string(ft);
	}

	/* draw background */
	cairo_set_source_surface (cr, ui->bg_scaled, 0, 0);
	cairo_paint (cr);

	// TODO !ui->kit_ready -> shade

	if (ui->show_hotzones) {
		cairo_set_operator (cr, CAIRO_OPERATOR_OVERLAY);
		cairo_set_source_surface (cr, ui->map_scaled, 0, 0);
		cairo_paint_with_alpha (cr, .65);
	}

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	/* prepare text */
	cairo_set_line_width (cr, 2.0);
	PangoLayout* pl = pango_cairo_create_layout(cr);

	for (int i = 0; i < DRUM_PCS; ++i) {
		if (ui->kit_anim[i] <= 0) { continue; }

		float dt = 1 / 15.f;
		const double br = .3 + .7 * ui->kit_anim[i];

		if (ui->show_highlight) {
			struct kGeometry* g = &ui->drumpos[i];
			const int xoff = SW (g->cx - g->dx);
			const int yoff = SH (g->cy - g->dy);

			// hack for 'difference' :(
			double bg = .3 + .7 * ui->kit_anim[i] * (ui->kit_velo[i] / 127.f);
			float clr[4];
			clr[2] = br * .8 * ui->kit_anim[i];
			clr[1] = bg * .8 * ui->kit_anim[i];
			clr[0] = 0 ; // .3 * .5 * ui->kit_anim[i];
			clr[3] = 1.0;

			cairo_save (cr);
			// TODO find a way to alpha-mask AND paint with custom alpha.
			cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
			cairo_rectangle (cr, xoff, yoff, SW (2 * g->dx), SH (2 * g->dy));
			cairo_clip (cr);
			CairoSetSouerceRGBA (clr);
			cairo_mask_surface (cr, ui->anim_alpha[i], xoff, yoff);
			//cairo_paint_with_alpha (cr, .5 * ui->kit_anim[i]);
			cairo_restore (cr);
		}

		int tw = 0;
		int th = 0;
		double cx = 0;
		double cy = 0;

		if (ui->show_text) {
			double bg = .2 + .8 * ui->kit_anim[i] * (1.f - ui->kit_velo[i] / 127.f);
			char txt[64];
			float c_txt[4];
			float c_out[4];
			c_txt[0] = br;
			c_txt[1] = bg;
			c_txt[2] = .3;
			c_txt[3] = .9 * ui->kit_anim[i];
			c_out[0] = c_out[1] = c_out[2] = 0;
			c_out[3] = .9 * ui->kit_anim[i];

			float anim = 1 - ui->kit_anim[i];
			cx = SW (ui->drumpos[i].cx);
			cy = SH (ui->drumpos[i].cy - anim * 0.1);
			snprintf (txt, sizeof(txt), "%s\n(%d)", drumnames[i], ui->kit_velo[i]);
			txt[sizeof(txt) - 1] = 0;

			outline_text (cr, pl, ui->font[0], txt, cx, cy, 1 + .15 * anim, c_txt, c_out, &tw, &th);
			dt = 1 / 25.f;
		}

		if (ui->kit_anim[i] > dt) {
			ui->kit_anim[i] -= dt;
			queue_drum_expose (ui, i);
			if (tw > 0) {
				queue_draw_area (ui->rw, cx - tw * .6, cy - th * .6, tw * 1.2, th * 1.2);
			}
		} else if (ui->kit_anim[i] > 0) {
			ui->kit_anim[i] = 0;
			queue_drum_expose (ui, i);
			if (tw > 0) {
				queue_draw_area (ui->rw, cx - tw * .6, cy - th * .6, tw * 1.2, th * 1.2);
			}
		} else {
			ui->kit_anim[i] = 0;
		}
	}

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	if (ui->show_menu) {
		cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
		cairo_set_source_rgba (cr, 0, 0, 0, 0.5);
		cairo_fill (cr);

		float bt_w = ui->width / 7.f;
		float bt_h = ui->height / 3.f;
		write_text_full (cr, "Display Animation Mode", ui->font[0], floor(ui->width * .5), floor(bt_h * 0.5), 0, 2, c_wht);
		static const char* mode[3] = { "Text\nOnly", "Highlight\nOnly", "Text\nand\nHighlight"};
		for (int i = 0; i < 3; ++i) {
			float x0 = floor ((1 + 2 * i) * bt_w);
			float y0 = floor (bt_h);
			rounded_rectangle (cr, x0, y0, floor (bt_w), floor (bt_h), 8);
			CairoSetSouerceRGBA(c_wht);
			cairo_set_line_width(cr, 1.5);
			cairo_stroke_preserve (cr);
			cairo_set_source_rgba (cr, .2, .2, .2, 1.0);
			if (ui->show_text && ui->show_highlight) {
				if (i == 2) { cairo_set_source_rgba (cr, .1, .7, .1, 1.0); }
			}
			else if (ui->show_text) {
				if (i == 0) { cairo_set_source_rgba (cr, .1, .7, .1, 1.0); }
			}
			else if (ui->show_highlight) {
				if (i == 1) { cairo_set_source_rgba (cr, .1, .7, .1, 1.0); }
			}
			cairo_fill (cr);
			write_text_full (cr, mode[i], ui->font[1], floor(x0 + bt_w * .5), floor(y0 + bt_h * .5), 0, 2, c_wht);
		}
	}
	else if (ui->hover_note >= 0) {
		char txt[192];
		const int i = ui->hover_note;
		outline_text (cr, pl, ui->font[1], drumnames[i],
				SW (ui->drumpos[i].cx), SH (ui->drumpos[i].cy), 1.0,
				c_wht, c_blk, NULL, NULL);

		static const char notename[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
		int mn = (ui->hover_note + 36) % 12;
		int mo = (ui->hover_note + 36) / 12 - 1;

		snprintf (txt, sizeof(txt), "<markup> Audition Velocity: %3d <small><i>(change with mouse-wheel)</i></small>  -  MIDI-Note: %-2s%d (%d)</markup>", ui->m_vel, notename[mn], mo, 36 + ui->hover_note);
		write_text_full (cr, txt, ui->font[1], 0, ui->height, 0, -6, c_wht);
	}
	g_object_unref(pl);

#ifdef DEVELOP
	if (ui->played_note >= 0 && ui->_yd > 0 && ui->_xd > 0) {
		cairo_move_to (cr, SW (ui->_xc), SH (ui->_yc));
		cairo_close_path (cr);
		cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_width (cr, 5.0);
		cairo_set_source_rgba (cr, 1, 0, 0, 1);
		cairo_stroke (cr);
		cairo_rectangle (cr,
				SW (ui->_xc - ui->_xd), SH (ui->_yc - ui->_yd),
				SW (2 * ui->_xd), SH (2 * ui->_yd));
		cairo_set_source_rgba (cr, .7, .7, .0, .5);
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

static int
find_note (AvlDrumsLV2UI* ui, RobTkBtnEvent* ev)
{
	if (!ui->map) { return -1; }
	unsigned char* img = cairo_image_surface_get_data (ui->map);
	const int x = rint (ev->x * 1024.f / (float)ui->width);
	const int y = rint (ev->y * 512.f / (float)ui->height);
	if (x < 0 || x >= cairo_image_surface_get_width (ui->map)) {
		return -1;
	}
	if (y < 0 || y >= cairo_image_surface_get_height (ui->map)) {
		return -1;
	}
	uint32_t p = y * cairo_image_surface_get_stride  (ui->map) + x * 4;
	// color index (28 colors)
	// for (i = 10; i < 256; i += 9) { R, G, B =  i, ((21 * i) % 256), (17 * i) % 256 }
	int c = ((img[p+2] & 0xff) - 10) / 9;
	if (c >= DRUM_PCS || c < 0) {
		return -1;
	}
	return map_red[c];
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
		printf ("{ %f, %f, %f, %f },\n", ui->_xc, ui->_yc, ui->_xd, ui->_yd);
		queue_draw (ui->rw);
	}
	return NULL;
#endif

	int n = find_note (ui, ev);
	if (ui->hover_note != n) {
		// TODO position.. + area
		ui->hover_note = n;
		queue_draw (ui->rw);
	}
	return NULL;
}

static RobWidget*
mousedown (RobWidget* handle, RobTkBtnEvent *ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	if (ev->button != 1) {
		return NULL;
	}
#ifdef DEVELOP
	ui->_xc = ev->x / (double) ui->width;
	ui->_yc = ev->y / (double) ui->height;
	ui->_xd = 0;
	ui->_yd = 0;
	printf ("{ %f, %f, %f, %f },\n", ui->_xc, ui->_yc, ui->_xd, ui->_yd);
	queue_draw (ui->rw);
#endif

	if (ui->show_menu) {
		// handle on mouse-up
		return handle;
	}

	if (!ui->kit_ready) {
		return NULL;
	}

	/* top-right (kit-name) */
	if ((ev->x / (double) ui->width) > (ui->kit == RedZeppelin ? 0.73 : .77) && (ev->y / (double) ui->height) < .09) {
		ui->show_hotzones = true;
		queue_draw (ui->rw);
		return handle;
	}

	int n = find_note (ui, ev);
	if (n >= 0) {
		ui->played_note = n + 36;
		forge_note (ui, ui->played_note, ui->m_vel);
	}
	return handle;
}

static RobWidget*
mouseup (RobWidget* handle, RobTkBtnEvent *ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);

	if (ev->button == 3 && !ui->show_menu) {
		ui->show_menu = true;
		queue_draw (ui->rw);
		return NULL;
	}

	if (ev->button != 1) {
		return NULL;
	}

	if (ui->show_menu) {
		float bt_w = ui->width / 7.f;
		float bt_h = ui->height / 3.f;
		int xp = floor (ev->x / bt_w);
		int yp = floor (ev->y / bt_h);
		if ((xp & 1) == 0 || (yp & 1) == 0) {
			return NULL;
		}
		const int pos = (xp - 1) / 2;
		switch (pos) {
			case 0:
				ui->show_text = true;
				ui->show_highlight = false;
				break;
			case 1:
				ui->show_text = false;
				ui->show_highlight = true;
				break;
			case 2:
				ui->show_text = true;
				ui->show_highlight = true;
				break;
			default:
				return NULL;
				break;
		}
		ui->show_menu = false;
		queue_draw (ui->rw);
		return NULL;
	}

	if (ui->played_note >= 0) {
		forge_note (ui, ui->played_note, 0);
	}
	if (ui->show_hotzones) {
		ui->show_hotzones = false;
		queue_draw (ui->rw);
	}
	ui->played_note = -1;
#ifdef DEVELOP
	queue_draw (ui->rw);
#endif
	return NULL;
}

static RobWidget*
mousescroll (RobWidget* handle, RobTkBtnEvent *ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	bool changed = false;
	switch (ev->direction) {
		case ROBTK_SCROLL_RIGHT:
		case ROBTK_SCROLL_UP:
			if (ui->m_vel < 127) {
				++ui->m_vel;
				changed = true;
			}
			break;
		case ROBTK_SCROLL_LEFT:
		case ROBTK_SCROLL_DOWN:
			if (ui->m_vel > 1) {
				--ui->m_vel;
				changed = true;
			}
			break;
	}
	if (changed) {
		int h = 20 * ui->scale;
		queue_draw_area (ui->rw, 0, ui->height - h, ui->width, h);
	}
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
	//AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (rw);
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
	else if (!strcmp (plugin_uri, RTK_URI "BlondeBop"))        { ui->kit = BlondeBop; }
	else if (!strcmp (plugin_uri, RTK_URI "BlondeBopMulti"))   { ui->kit = BlondeBop; }
	else if (!strcmp (plugin_uri, RTK_URI "BlondeBopHR"))      { ui->kit = BlondeBopHR; }
	else if (!strcmp (plugin_uri, RTK_URI "BlondeBopHRMulti")) { ui->kit = BlondeBopHR; }

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

	ui->write          = write_function;
	ui->controller     = controller;
	ui->nfo            = robtk_info (ui_toplevel);
	ui->played_note    = -1;
	ui->hover_note     = -1;
	ui->kit_ready      = false;
	ui->png_readoff    = 0;
	ui->map_readoff    = 0;
	ui->size_changed   = true;
	ui->show_hotzones  = false;
	ui->show_text      = false;
	ui->show_highlight = true;
	ui->show_menu      = false;
	ui->m_vel          = 100;

	switch (ui->kit) {
		case RedZeppelin:
			ui->drumpos = pos_redzep;
			break;
		case BlondeBop:
		case BlondeBopHR:
			ui->drumpos = pos_blondebop;
			break;
		default:
			ui->drumpos = pos_blackperl;
			break;
	}

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

	robwidget_set_expose_event (ui->rw, expose_event);
	robwidget_set_size_request (ui->rw, size_request);
	robwidget_set_size_allocate (ui->rw, size_allocate);
	robwidget_set_size_limit (ui->rw, size_limit);
	robwidget_set_size_default (ui->rw, size_request);
	robwidget_set_mouseup (ui->rw, mouseup);
	robwidget_set_mousedown (ui->rw, mousedown);
	robwidget_set_mousemove (ui->rw, mousemove);
	robwidget_set_mousescroll (ui->rw, mousescroll);

	ui->bg = cairo_image_surface_create_from_png_stream (img_png_read, ui);
	ui->map = cairo_image_surface_create_from_png_stream (map_png_read, ui);

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
	cairo_surface_destroy (ui->map);
	if (ui->bg_scaled) { cairo_surface_destroy (ui->bg_scaled); }
	if (ui->map_scaled) { cairo_surface_destroy (ui->map_scaled); }
	for (int i = 0; i < DRUM_PCS; ++i) {
		if (ui->anim_alpha[i]) { cairo_surface_destroy (ui->anim_alpha[i]); }
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
#ifndef NDEBUG
					const size_t n_elem = (a0->size - sizeof (LV2_Atom_Vector_Body)) / voi->atom.size;
					assert (n_elem == DRUM_PCS);
#endif
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
