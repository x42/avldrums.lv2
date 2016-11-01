/*
 * Copyright (C) 2016 Robin Gareus <robin@gareus.org>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef AVLdrums_URIS_H
#define AVLdrums_URIS_H

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>

#define AVL_URI "http://gareus.org/oss/lv2/avldrums#"

#ifdef HAVE_LV2_1_8
#define x_forge_object lv2_atom_forge_object
#else
#define x_forge_object lv2_atom_forge_blank
#endif

#define DRUM_PCS 29

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Object;
	LV2_URID atom_Vector;
	LV2_URID atom_Float;
	LV2_URID atom_Bool;
	LV2_URID atom_Int;
	LV2_URID atom_eventTransfer;
	LV2_URID midi_MidiEvent;
	LV2_URID ui_on;
	LV2_URID ui_off;
	LV2_URID drumkit;
	LV2_URID drumhit;
	LV2_URID loaded;
	LV2_URID drumhits;
} AVLLV2URIs;

typedef enum {
	AVL_PORT_CONTROL = 0,
	AVL_PORT_NOTIFY,
	AVL_PORT_OUT_L,
	AVL_PORT_OUT_R,
	AVL_PORT_OUT_HiHat,
	AVL_PORT_OUT_Tom,
	AVL_PORT_OUT_FloorTom,
	AVL_PORT_OUT_Overheads_L,
	AVL_PORT_OUT_Overheads_R,
	AVL_PORT_OUT_Percussion_L,
	AVL_PORT_OUT_Percussion_R,
	AVL_PORT_LAST
} PortIndex;

#define AVL_PORT_OUT_Kick AVL_PORT_OUT_L
#define AVL_PORT_OUT_Snare AVL_PORT_OUT_R

static inline void
map_avldrums_uris (LV2_URID_Map* map, AVLLV2URIs* uris) {
	uris->atom_Blank         = map->map (map->handle, LV2_ATOM__Blank);
	uris->atom_Object        = map->map (map->handle, LV2_ATOM__Object);
	uris->atom_Vector        = map->map (map->handle, LV2_ATOM__Vector);
	uris->atom_Float         = map->map (map->handle, LV2_ATOM__Float);
	uris->atom_Bool          = map->map (map->handle, LV2_ATOM__Bool);
	uris->atom_Int           = map->map (map->handle, LV2_ATOM__Int);
	uris->atom_eventTransfer = map->map (map->handle, LV2_ATOM__eventTransfer);
	uris->midi_MidiEvent     = map->map (map->handle, LV2_MIDI__MidiEvent);

	uris->ui_on              = map->map (map->handle, AVL_URI "ui_on");
	uris->ui_off             = map->map (map->handle, AVL_URI "ui_off");
	uris->drumkit            = map->map (map->handle, AVL_URI "drumkit");
	uris->drumhit            = map->map (map->handle, AVL_URI "drumhit");
	uris->loaded             = map->map (map->handle, AVL_URI "loaded");
	uris->drumhits           = map->map (map->handle, AVL_URI "drumhits");
}

#endif
