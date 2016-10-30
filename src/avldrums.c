/* avldrums -- simple & robust x-platform fluidsynth LV2
 *
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>

#include "midnam_lv2.h"

#include "fluidsynth.h"
#include "avldrums.h"

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

enum {
  CMD_APPLY    = 0,
  CMD_FREE     = 1,
};

typedef struct {
	/* ports */
	const LV2_Atom_Sequence* control;
  LV2_Atom_Sequence*       notify;

	float* p_ports[AVL_PORT_LAST];
	float  v_ports[AVL_PORT_LAST];

	/* fluid synth */
	fluid_settings_t* settings;
	fluid_synth_t*    synth;
	int               synthId;

	/* lv2 URIDs */
	AVLLV2URIs uris;

	/* lv2 extensions */
	LV2_Log_Log*         log;
	LV2_Log_Logger       logger;
  LV2_Worker_Schedule* schedule;
	LV2_Midnam*          midnam;
	LV2_Atom_Forge       forge;
	LV2_Atom_Forge_Frame frame;

	/* state */
	bool panic;
	bool initialized;
	bool inform_ui;
	bool ui_active;

	char current_sf2_file_path[1024];
	char queue_sf2_file_path[1024];
	bool reinit_in_progress; // set in run, cleared in work_response
	bool queue_reinit; // set in restore, cleared in work_response

	fluid_midi_event_t* fmidi_event;

} AVLSynth;

/* *****************************************************************************
 * helpers
 */

static bool
load_sf2 (AVLSynth* self, const char* fn)
{
	const int synth_id = fluid_synth_sfload (self->synth, fn, 1);

	if (synth_id == FLUID_FAILED) {
		return false;
	}

	fluid_sfont_t* const sfont = fluid_synth_get_sfont_by_id (self->synth, synth_id);
	if (!sfont) {
		return false;
	}

	int chn;
	fluid_preset_t preset;
	sfont->iteration_start (sfont);
	for (chn = 0; sfont->iteration_next (sfont, &preset) && chn < 16; ++chn) {
		fluid_synth_program_select (self->synth, chn, synth_id,
				preset.get_banknum (&preset), preset.get_num (&preset));
	}

	if (chn == 0) {
		return false;
	}

	return true;
}

static void
inform_ui (AVLSynth* self)
{
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time (&self->forge, 0);
	x_forge_object (&self->forge, &frame, 1, self->uris.drumkit);
	lv2_atom_forge_property_head (&self->forge, self->uris.loaded, 0);
	lv2_atom_forge_bool (&self->forge, strlen (self->current_sf2_file_path) > 0);
	lv2_atom_forge_pop (&self->forge, &frame);
}

static void
kick_ui (AVLSynth* self, int* n)
{
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time (&self->forge, 0);
	x_forge_object (&self->forge, &frame, 1, self->uris.drumhit);
	lv2_atom_forge_property_head (&self->forge, self->uris.drumhits, 0);
	lv2_atom_forge_vector (&self->forge, sizeof(int32_t), self->uris.atom_Int, DRUM_PCS, n);
	lv2_atom_forge_pop (&self->forge, &frame);
}

static float
db_to_coeff (float db)
{
	if (db <= -80) { return 0; }
	else if (db >=  20) { return 10; }
	return powf (10.f, .05f * db);
}

static int file_exists (const char *filename) {
	struct stat s;
	if (!filename || strlen(filename) < 1) return 0;
	int result= stat (filename, &s);
	if (result != 0) return 0; /* stat() failed */
	if (S_ISREG(s.st_mode)) return 1; /* is a regular file - ok */
	return 0;
}

/* *****************************************************************************
 * LV2 Plugin
 */

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	static const char* kits [] = {
		"Black_Pearl_4B-1.1.sf2",
		"Red_Zeppelin_4-1.1.sf2"
	};

	int kit = -1;
	if (!strcmp (descriptor->URI, AVL_URI "BlackPearl")) {
		kit = 0;
	}
	else if (!strcmp (descriptor->URI, AVL_URI "RedZeppelin")) {
		kit = 1;
	}

	AVLSynth* self = (AVLSynth*)calloc (1, sizeof (AVLSynth));

	if (!self || !bundle_path || kit < 0) {
		return NULL;
	}

	LV2_URID_Map* map = NULL;

	for (int i=0; features[i] != NULL; ++i) {
		if (!strcmp (features[i]->URI, LV2_URID__map)) {
			map = (LV2_URID_Map*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_LOG__log)) {
			self->log = (LV2_Log_Log*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_WORKER__schedule)) {
			self->schedule = (LV2_Worker_Schedule*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_MIDNAM__update)) {
			self->midnam = (LV2_Midnam*)features[i]->data;
		}
	}

	lv2_log_logger_init (&self->logger, map, self->log);

	if (!map) {
		lv2_log_error (&self->logger, "avldrums.lv2: Host does not support urid:map\n");
		free (self);
		return NULL;
	}

	if (!self->schedule) {
		lv2_log_error (&self->logger, "avldrums.lv2: Host does not support worker:schedule\n");
		free (self);
		return NULL;
	}

	snprintf (self->queue_sf2_file_path, sizeof (self->queue_sf2_file_path), "%s" PATH_SEP "%s",
			bundle_path, kits[kit]);
	self->queue_sf2_file_path[sizeof(self->queue_sf2_file_path) - 1] = '\0';
	
	if (!file_exists (self->queue_sf2_file_path)) {
		lv2_log_error (&self->logger, "avldrums.lv2: Cannot find drumkit soundfont: '%s'\n", self->queue_sf2_file_path);
		free (self);
		return NULL;
	}

	/* initialize fluid synth */
	self->settings = new_fluid_settings ();

	if (!self->settings) {
		lv2_log_error (&self->logger, "avldrums.lv2: cannot allocate Fluid Settings\n");
		free (self);
		return NULL;
	}

	fluid_settings_setnum (self->settings, "synth.sample-rate", rate);
	fluid_settings_setint (self->settings, "synth.parallel-render", 1);
	fluid_settings_setint (self->settings, "synth.threadsafe-api", 0);

	self->synth = new_fluid_synth (self->settings);

	if (!self->synth) {
		lv2_log_error (&self->logger, "avldrums.lv2: cannot allocate Fluid Synth\n");
    delete_fluid_settings (self->settings);
		free (self);
		return NULL;
	}

	fluid_synth_set_gain (self->synth, 1.0f);
	fluid_synth_set_polyphony (self->synth, 32);
	fluid_synth_set_sample_rate (self->synth, (float)rate);

	self->fmidi_event = new_fluid_midi_event ();

	if (!self->fmidi_event) {
		lv2_log_error (&self->logger, "avldrums.lv2: cannot allocate Fluid Event\n");
		delete_fluid_synth (self->synth);
    delete_fluid_settings (self->settings);
		free (self);
		return NULL;
	}

	/* initialize plugin state */

	self->panic = false;
	self->inform_ui = false;
	self->ui_active = false;
	self->initialized = false;
	self->reinit_in_progress = false;
	self->queue_reinit = true;

	lv2_atom_forge_init (&self->forge, map);
	map_avldrums_uris (map, &self->uris);

	return (LV2_Handle)self;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	AVLSynth* self = (AVLSynth*)instance;

	switch (port) {
		case AVL_PORT_CONTROL:
			self->control = (const LV2_Atom_Sequence*)data;
			break;
		case AVL_PORT_NOTIFY:
			self->notify = (LV2_Atom_Sequence*)data;
			break;
		default:
			if (port < AVL_PORT_LAST) {
				self->p_ports[port] = (float*)data;
			}
			break;
	}
}

static void
deactivate (LV2_Handle instance)
{
	AVLSynth* self = (AVLSynth*)instance;
	self->panic = true;
}

static void
run (LV2_Handle instance, uint32_t n_samples)
{
	AVLSynth* self = (AVLSynth*)instance;

	if (!self->control || !self->notify) {
		return;
	}

	const uint32_t capacity = self->notify->atom.size;
	lv2_atom_forge_set_buffer (&self->forge, (uint8_t*)self->notify, capacity);
	lv2_atom_forge_sequence_head (&self->forge, &self->frame, 0);

	if (!self->initialized || self->reinit_in_progress) {
		memset (self->p_ports[AVL_PORT_OUT_L], 0, n_samples * sizeof (float));
		memset (self->p_ports[AVL_PORT_OUT_R], 0, n_samples * sizeof (float));
	} else if (self->panic) {
		fluid_synth_all_notes_off (self->synth, -1);
		fluid_synth_all_sounds_off (self->synth, -1);
		self->panic = false;
	}

	if (self->initialized && !self->reinit_in_progress) {
		// TODO clamp values to ranges
		if (self->v_ports[AVL_OUT_GAIN] != *self->p_ports[AVL_OUT_GAIN]) {
			fluid_synth_set_gain (self->synth, db_to_coeff (*self->p_ports[AVL_OUT_GAIN]));
		}
		for (uint32_t p = AVL_OUT_GAIN; p < AVL_PORT_LAST; ++p) {
			self->v_ports[p] = *self->p_ports[p];
		}
	}

	uint32_t offset = 0;

	bool hit_event = false;
	int drum_hits[DRUM_PCS];
	memset (drum_hits, 0, sizeof (drum_hits));

	LV2_ATOM_SEQUENCE_FOREACH (self->control, ev) {
		if (ev->body.type == self->uris.atom_Blank || ev->body.type == self->uris.atom_Object) {
			const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
			if (obj->body.otype == self->uris.ui_off) {
				self->ui_active = false;
			}
			else if (obj->body.otype == self->uris.ui_on) {
				self->ui_active = true;
				self->inform_ui = true;
			}
		}
		else if (ev->body.type == self->uris.midi_MidiEvent && self->initialized && !self->reinit_in_progress) {
			if (ev->body.size > 3 || ev->time.frames >= n_samples) {
				continue;
			}

			if (ev->time.frames > offset) {
				fluid_synth_write_float (
						self->synth,
						ev->time.frames - offset,
						&self->p_ports[AVL_PORT_OUT_L][offset], 0, 1,
						&self->p_ports[AVL_PORT_OUT_R][offset], 0, 1);
			}

			offset = ev->time.frames;

			const uint8_t* const data = (const uint8_t*)(ev + 1);
			fluid_midi_event_set_type (self->fmidi_event, data[0] & 0xf0);
			// TODO: consider loading kit to all channels
			fluid_midi_event_set_channel (self->fmidi_event, 0 /* data[0] & 0x0f*/);

			if (fluid_midi_event_get_type(self->fmidi_event) == 0xc0 /*PROGRAM_CHANGE*/) {
				continue;
			}
			if (ev->body.size > 1) {
				fluid_midi_event_set_key (self->fmidi_event, data[1]);
			}
			if (ev->body.size > 2) {
				fluid_midi_event_set_value (self->fmidi_event, data[2]);
			}
			fluid_synth_handle_midi_event (self->synth, self->fmidi_event);

			if (ev->body.size == 3 && fluid_midi_event_get_type(self->fmidi_event) == 0x90 /*NOTE_ON*/) {
				int dp = data[1] - 36; // base-note
				if (dp >= 0 && dp < DRUM_PCS) {
					drum_hits[dp] = data[2];
					hit_event = true;
				}
			}
		}
	}

	if (self->queue_reinit && !self->reinit_in_progress) {
		self->reinit_in_progress = true;
		int magic = 0x47110815;
		self->schedule->schedule_work (self->schedule->handle, sizeof (int), &magic);
	}

	/* inform the GUI */
	if (self->ui_active) {
		if (self->inform_ui) {
			self->inform_ui = false;
			inform_ui (self);
		}
		if (hit_event) {
			kick_ui (self, drum_hits);
		}
	}

	if (n_samples > offset && self->initialized && !self->reinit_in_progress) {
		fluid_synth_write_float (
				self->synth,
				n_samples - offset,
				&self->p_ports[AVL_PORT_OUT_L][offset], 0, 1,
				&self->p_ports[AVL_PORT_OUT_R][offset], 0, 1);
	}
}

static void cleanup (LV2_Handle instance)
{
	AVLSynth* self = (AVLSynth*)instance;
	delete_fluid_synth (self->synth);
	delete_fluid_settings (self->settings);
	delete_fluid_midi_event (self->fmidi_event);
	free (self);
}

/* *****************************************************************************
 * LV2 Extensions
 */

static LV2_Worker_Status
work (LV2_Handle                  instance,
      LV2_Worker_Respond_Function respond,
      LV2_Worker_Respond_Handle   handle,
      uint32_t                    size,
      const void*                 data)
{
	AVLSynth* self = (AVLSynth*)instance;

  if (size != sizeof (int)) {
		return LV2_WORKER_ERR_UNKNOWN;
	}
	int magic = *((const int*)data);
	if (magic != 0x47110815) {
		return LV2_WORKER_ERR_UNKNOWN;
	}

	self->initialized = load_sf2 (self, self->queue_sf2_file_path);

	if (self->initialized) {
		fluid_synth_all_notes_off (self->synth, -1);
		fluid_synth_all_sounds_off (self->synth, -1);
		self->panic = false;
		// boostrap synth engine.
		float l[1024];
		float r[1024];
		fluid_synth_write_float (self->synth, 1024, l, 0, 1, r, 0, 1);
	}

	respond (handle, 1, "");
	return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status
work_response (LV2_Handle  instance,
               uint32_t    size,
               const void* data)
{
	AVLSynth* self = (AVLSynth*)instance;

	if (self->initialized) {
		strcpy (self->current_sf2_file_path, self->queue_sf2_file_path);
	} else {
		self->current_sf2_file_path[0] = 0;
	}

	self->reinit_in_progress = false;
	self->inform_ui = true;
	self->queue_reinit = false;
	return LV2_WORKER_SUCCESS;
}

static char*
mn_file (LV2_Handle instance)
{
#include "midnam.h"
	AVLSynth* self = (AVLSynth*)instance;
	char* mn = strdup (AVL_Drumkits_midnam);
	char inst[13];
	snprintf (inst, 12, "%p", self);
	memcpy (&mn[0x142], inst, strlen(inst));
	return mn;
}

static char*
mn_model (LV2_Handle instance)
{
	AVLSynth* self = (AVLSynth*)instance;
	char inst[13];
	char* rv = malloc (13 * sizeof (char));
	sprintf (rv, "AVL-Drumkits");
	snprintf (inst, 12, "%p", self);
	memcpy (rv, inst, strlen(inst));
	return rv;
}

static void
mn_free (char* v)
{
	free (v);
}

static const void*
extension_data (const char* uri)
{
	static const LV2_Worker_Interface worker = { work, work_response, NULL };
	static const LV2_Midnam_Interface midnam = { mn_file, mn_model, mn_free };
	if (!strcmp (uri, LV2_WORKER__interface)) {
		return &worker;
	}
	if (!strcmp (uri, LV2_MIDNAM__interface)) {
		return &midnam;
	}
	return NULL;
}

#define mkdesc(ID, NAME) \
static const LV2_Descriptor descriptor ## ID = { \
	AVL_URI NAME, \
	instantiate, \
	connect_port, \
	NULL, \
	run, \
	deactivate, \
	cleanup, \
	extension_data \
};

mkdesc(0, "BlackPearl");
mkdesc(1, "RedZeppelin");

#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#    define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#    define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
#endif
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor0;
	case 1:
		return &descriptor1;
	default:
		return NULL;
	}
}
