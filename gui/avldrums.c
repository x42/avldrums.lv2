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
	BlackPearl = 0,
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
	"Crash Cymbal 1",
	"Crash Cymbal 1\nChoked",
	"Ride Cymbal Tip",
	"Ride Cymbal\nChoked",
	"Ride Cymbal Bell",
	"Tambourine",
	"Splash Cymbal",
	"Cowbell",
	"Crash Cymbal 2",
	"Crash Cymbal 2\nChoked",
	"Ride Cymbal\nShank",
	"Crash Cymbal 3",
	"High Roto Tom",
	"Mid Roto Tom",
	"Low Roto Tom",
	"Maracas"
};

typedef struct {
	RobWidget *rw;

	LV2UI_Write_Function write;
	LV2UI_Controller     controller;

	LV2_Atom_Forge   forge;
	AVLLV2URIs       uris;

	PangoFontDescription* font;

	int width, height;
	float scale;

	enum Kit kit;
	const char* nfo;

	bool kit_ready;

	float   kit_anim[DRUM_PCS];
	uint8_t kit_velo[DRUM_PCS];

	int note;
} AvlDrumsLV2UI;

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
static bool
animation_counters (AvlDrumsLV2UI* ui) {
	const float dt = 1/25.f;
	bool active = false;
	for (int i = 0; i < DRUM_PCS; ++i) {
		if (ui->kit_anim[i] > dt) {
			ui->kit_anim[i] -= dt;
			active = true;
		}
		else if (ui->kit_anim[i] > 0) {
			ui->kit_anim[i] = 0;
			active = true;
		}
		else {
			ui->kit_anim[i] = 0;
		}
	}
	return active;
}

static bool
expose_event (RobWidget* handle, cairo_t* cr, cairo_rectangle_t* ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip_preserve (cr);
	CairoSetSouerceRGBA(c_trs);
	cairo_fill (cr);

	for (int i = 0; i < DRUM_PCS; ++i) {
		// TODO note to area
		int col = i % 6;
		int row = i / 6; // 0.. 5
		const int ww = ui->width / 6;
		const int hh = ui->height / 5;

		cairo_rectangle (cr, col * ww, row * hh, ww, hh);
		if (ui->kit_anim[i] > 0) {
			const double br = .2 + .8 * ui->kit_anim[i];
			const double bg = .2 + .8 * ui->kit_anim[i] * (1.f - ui->kit_velo[i] / 127.f);
			cairo_set_source_rgba (cr, br, bg, .2, 1);
		} else {
			cairo_set_source_rgba (cr, .2, .2, .2, 1);
		}
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, .7, .7, .7, 1);
		cairo_stroke (cr);
		write_text_full (cr, drumnames[i], ui->font,
				(col + .5) * ww, (row  + .5) * hh,
				0, 2, c_wht);
	}

	if (animation_counters (ui)) {
		queue_draw (ui->rw);
	}
	return TRUE;

}

/******************************************************************************
 * UI event handling
 */

static RobWidget*
mousedown (RobWidget* handle, RobTkBtnEvent *ev)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	// TODO area to note
	const int ww = ui->width / 6;
	const int hh = ui->height / 5;
	int col = ev->x / ww;
	int row = ev->y / hh;

	int n = row * 6 + col;
	if (n >= 0 && n < DRUM_PCS) {
		ui->note = n + 36;
		forge_note (ui, ui->note, 0x7f);
	}
	return handle;
}

static RobWidget*
mouseup (RobWidget* handle, RobTkBtnEvent *event)
{
	AvlDrumsLV2UI* ui = (AvlDrumsLV2UI*)GET_HANDLE (handle);
	queue_draw (ui->rw);
	if (ui->note >= 0) {
		forge_note (ui, ui->note, 0);
	}
	ui->note = -1;
	return NULL;
}

static void
size_request (RobWidget* rw, int *w, int *h)
{
	*w = 600;
	*h = 500;
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
	queue_draw (rw);
}

static void
size_allocate (RobWidget* rw, int w, int h)
{
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

	if      (!strcmp (plugin_uri, RTK_URI "BlackPearl"))  { ui->kit = BlackPearl; }
	else if (!strcmp (plugin_uri, RTK_URI "RedZeppelin")) { ui->kit = RedZeppelin; }

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

	ui->write      = write_function;
	ui->controller = controller;
	ui->nfo        = robtk_info (ui_toplevel);
	ui->note       = -1;
	ui->kit_ready  = false;

	for (int i = 0; i < DRUM_PCS; ++i) {
		ui->kit_anim[i] = 0;
		ui->kit_velo[i] = 0;
	}

	lv2_atom_forge_init (&ui->forge, map);
	map_avldrums_uris (map, &ui->uris);

	/* GUI layout */
	ui->font = pango_font_description_from_string("Sans 10px");

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
	pango_font_description_free(ui->font);
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
						bool redraw = false;
						for (int i = 0; i < DRUM_PCS; ++i) {
							if (data[i] > 0) {
								ui->kit_anim [i] = 1.0;
								ui->kit_velo [i] = data[i];
								redraw = 1;
							}
						}
						if (redraw) {
							queue_draw (ui->rw);
						}
					} /* kit ready */
				}
			} /* drumhit */
		} /* atom object */
	} /* atom message */

	if ( format != 0 || port_index < AVL_OUT_GAIN) {
		return;
	}

#if 0
	ui->disable_signals = true;
	if (port_index == AVL_OUT_GAIN) {
		// ...
	}
	ui->disable_signals = false;
#endif
}
