/* avldrums -- simple & robust x-platform fluidsynth LV2
 *
 * Copyright (C) 2016-2023 Robin Gareus <robin@gareus.org>
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

#ifdef HAVE_LV2_1_18_6
#include <lv2/atom/util.h>
#include <lv2/log/logger.h>
#include <lv2/worker/worker.h>
#else
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#endif

#include "midnam_lv2.h"

#include "fluidsynth.h"
#include "avldrums.h"

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

typedef struct {
	/* ports */
	const LV2_Atom_Sequence* control;
	LV2_Atom_Sequence*       notify;

	float* p_ports[AVL_PORT_LAST];

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
	bool multi_out;
	bool inform_ui;
	bool ui_active;
	int  kit_id;

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

	int chn = 0;
	fluid_preset_t* preset;
	fluid_sfont_iteration_start (sfont);
	if ((preset = fluid_sfont_iteration_next (sfont))) {
		for (chn = 0; chn < 16; ++chn) {
			fluid_synth_program_select (self->synth, chn, synth_id,
					fluid_preset_get_banknum (preset), fluid_preset_get_num (preset));
		}
	}

	if (chn == 0) {
		return false;
	}

	if (self->multi_out) {
		/* pan mono outs hard left */
		fluid_midi_event_set_value (self->fmidi_event, 0);

		for (uint8_t chn = 0; chn < 5; ++chn) {
			fluid_midi_event_set_type (self->fmidi_event, 0xb0 | chn);
			fluid_midi_event_set_key (self->fmidi_event, 0x0a);
			fluid_synth_handle_midi_event (self->synth, self->fmidi_event);

			fluid_midi_event_set_type (self->fmidi_event, 0xb0 | chn);
			fluid_midi_event_set_key (self->fmidi_event, 0x2a);
			fluid_synth_handle_midi_event (self->synth, self->fmidi_event);
		}
	}

	for (uint8_t chn = 0; chn < 6; ++chn) {
		fluid_synth_set_channel_type (self->synth, chn, CHANNEL_TYPE_DRUM);
	}

	return true;
}

static void
synth_sound (AVLSynth* self, uint32_t n_samples, uint32_t offset)
{
	if (self->multi_out) {
		while (n_samples > 0) {
			uint32_t n = n_samples > 8192 ? 8192 : n_samples;
			float* out[14];

			out[0]  = &self->p_ports[AVL_PORT_OUT_Kick][offset];
			out[1]  = NULL;
			out[2]  = &self->p_ports[AVL_PORT_OUT_Snare][offset];
			out[3]  = NULL;
			out[4]  = &self->p_ports[AVL_PORT_OUT_HiHat][offset];
			out[5]  = NULL;
			out[6]  = &self->p_ports[AVL_PORT_OUT_Tom][offset];
			out[7]  = NULL;
			out[8]  = &self->p_ports[AVL_PORT_OUT_FloorTom][offset];
			out[9]  = NULL;
			out[10] = &self->p_ports[AVL_PORT_OUT_Overheads_L][offset];
			out[11] = &self->p_ports[AVL_PORT_OUT_Overheads_R][offset];
			out[12] = &self->p_ports[AVL_PORT_OUT_Percussion_L][offset];
			out[13] = &self->p_ports[AVL_PORT_OUT_Percussion_R][offset];

			fluid_synth_process (self->synth, n, 0, NULL, 14, out);
			n_samples -= n;
			offset += n;
		}
	} else {
		fluid_synth_write_float (
				self->synth,
				n_samples,
				&self->p_ports[AVL_PORT_OUT_L][offset], 0, 1,
				&self->p_ports[AVL_PORT_OUT_R][offset], 0, 1);
	}
}

static uint8_t
assign_channel (uint8_t note)
{
	switch (note) {
		case 36:
			return 0; // Kick
		case 37:
		case 38:
		case 40:
			return 1; // Snare
		case 42:
		case 44:
		case 46:
		case 48:
			return 2; // HiHat
		case 45:
		case 47:
			return 3; // Tom
		case 41:
		case 43:
			return 4; // Floor Tom
		case 35: // Stick click (BB only)
		case 39: // Hand Clap
		case 54: // Tambourine
		case 56: // Cowbell
		case 61: // Maracas
			return 6; // Percussions etc
		default:
			break;
	}
	return 5; // Cymbals
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
		"Black_Pearl_4_LV2.sf2",
		"Red_Zeppelin_4_LV2.sf2",
		"Blonde_Bop_LV2.sf2",
		"Blonde_Bop_HR_LV2.sf2"
	};

	int kit = -1;
	bool multi_out = false;

	if (!strcmp (descriptor->URI, AVL_URI "BlackPearl")) {
		kit = 0;
	} else if (!strcmp (descriptor->URI, AVL_URI "BlackPearlMulti")) {
		kit = 0; multi_out = true;
	} else if (!strcmp (descriptor->URI, AVL_URI "RedZeppelin")) {
		kit = 1;
	} else if (!strcmp (descriptor->URI, AVL_URI "RedZeppelinMulti")) {
		kit = 1; multi_out = true;
	} else if (!strcmp (descriptor->URI, AVL_URI "BlondeBop")) {
		kit = 2;
	} else if (!strcmp (descriptor->URI, AVL_URI "BlondeBopMulti")) {
		kit = 2; multi_out = true;
	} else if (!strcmp (descriptor->URI, AVL_URI "BlondeBopHR")) {
		kit = 3;
	} else if (!strcmp (descriptor->URI, AVL_URI "BlondeBopHRMulti")) {
		kit = 3; multi_out = true;
	}

	AVLSynth* self = (AVLSynth*)calloc (1, sizeof (AVLSynth));

	if (!self || !bundle_path || kit < 0) {
		return NULL;
	}

	self->kit_id = kit;

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
	fluid_settings_setint (self->settings, "synth.threadsafe-api", 0);

	if (multi_out) {
		self->multi_out = true;
		fluid_settings_setint (self->settings, "synth.audio-channels", 7);
		fluid_settings_setint (self->settings, "synth.audio-groups", 7);
	} else {
		self->multi_out = false;
		fluid_settings_setint (self->settings, "synth.audio-channels", 1); // stereo pairs
	}

	self->synth = new_fluid_synth (self->settings);

	if (!self->synth) {
		lv2_log_error (&self->logger, "avldrums.lv2: cannot allocate Fluid Synth\n");
		delete_fluid_settings (self->settings);
		free (self);
		return NULL;
	}

	fluid_synth_set_gain (self->synth, 1.0f);
	fluid_synth_set_polyphony (self->synth, DRUM_PCS * 16);
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

	if (self->multi_out || !self->initialized || self->reinit_in_progress) {
		memset (self->p_ports[AVL_PORT_OUT_L], 0, n_samples * sizeof (float));
		memset (self->p_ports[AVL_PORT_OUT_R], 0, n_samples * sizeof (float));
		if (self->multi_out) {
			for (uint32_t cc = AVL_PORT_OUT_HiHat; cc < AVL_PORT_LAST; ++cc) {
				memset (self->p_ports[cc], 0, n_samples * sizeof (float));
			}
		}
	} else if (self->panic) {
		fluid_synth_all_notes_off (self->synth, -1);
		fluid_synth_all_sounds_off (self->synth, -1);
		self->panic = false;
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
			} else if (obj->body.otype == self->uris.ui_on) {
				self->ui_active = true;
				self->inform_ui = true;
			}
		} else if (ev->body.type == self->uris.midi_MidiEvent &&
				   self->initialized && !self->reinit_in_progress) {
			if (ev->body.size != 3 || ev->time.frames > n_samples) { // XXX should be >= n_samples
				continue;
			}
			// work-around jalv sending GUI -> DSP messages @ n_samples
			if (ev->time.frames == n_samples) {
				ev->time.frames = n_samples -1;
			}

			if (ev->time.frames > offset) {
				synth_sound (self, ev->time.frames - offset, offset);
			}

			offset = ev->time.frames;

			const uint8_t* const data = (const uint8_t*)(ev + 1);
			fluid_midi_event_set_type (self->fmidi_event, data[0] & 0xf0);

			if (fluid_midi_event_get_type(self->fmidi_event) == 0xc0 /*PROGRAM_CHANGE*/) {
				continue;
			}
			if (fluid_midi_event_get_type(self->fmidi_event) == 0xa0 /*KEY_PRESSURE*/) {
				continue;
			}
			if (fluid_midi_event_get_type(self->fmidi_event) == 0xd0 /*CHANNEL_PRESSURE*/) {
				continue;
			}
			if (fluid_midi_event_get_type(self->fmidi_event) == 0xe0 /*PITCH_BEND*/) {
				continue;
			}
			if (fluid_midi_event_get_type(self->fmidi_event) == 0xf0 /*MIDI_SYSEX*/) {
				continue;
			}

			if (fluid_midi_event_get_type(self->fmidi_event) == 0xb0 /*CC*/) {
				switch (data[1]) {
					case 0x0a: // pan
					case 0x2a: // pan
					case 0x40: // sustain
					case 0x42: // sostenuto
					case 0x44: // legato
						/* ignore */
						continue;
					default:
						break;
				}
			}

			if ((fluid_midi_event_get_type(self->fmidi_event) & 0xe0) == 0x80 /*NOTE*/) {
				fluid_midi_event_set_channel (self->fmidi_event, assign_channel (data[1]));
			}

			fluid_midi_event_set_key (self->fmidi_event, data[1]);
			fluid_midi_event_set_value (self->fmidi_event, data[2]);
			fluid_synth_handle_midi_event (self->synth, self->fmidi_event);

			if (fluid_midi_event_get_type(self->fmidi_event) == 0x90 /*NOTE_ON*/) {
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
		synth_sound (self, n_samples - offset, offset);
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
		float b[1024];
		fluid_synth_write_float (self->synth, 1024, b, 0, 1, b, 0, 1);
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
	AVLSynth* self = (AVLSynth*)instance;
	char* mn = (char*) calloc (4096, sizeof (char));
	snprintf (mn, 4095,
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<!DOCTYPE MIDINameDocument PUBLIC \"-//MIDI Manufacturers Association//DTD MIDINameDocument 1.0//EN\" \"http://www.midi.org/dtds/MIDINameDocument10.dtd\">\n"
"<MIDINameDocument>\n"
"  <Author>Glen MacArthur</Author>\n"
"  <MasterDeviceNames>\n"
"    <Manufacturer>AVL-Drumkits</Manufacturer>\n"
"    <Model>AVL-Drumkits-LV2:%p</Model>\n"
"    <CustomDeviceMode Name=\"Drumkit Keymap\">\n"
"      <ChannelNameSetAssignments>\n"
"        <ChannelNameSetAssign Channel=\"1\" NameSet=\"Names\"/>\n"
"        <ChannelNameSetAssign Channel=\"10\" NameSet=\"Names\"/>\n"
"      </ChannelNameSetAssignments>\n"
"    </CustomDeviceMode>\n"
"    <ChannelNameSet Name=\"Names\">\n"
"      <AvailableForChannels>\n"
"        <AvailableChannel Channel=\"1\" Available=\"true\"/>\n"
"        <AvailableChannel Channel=\"10\" Available=\"true\"/>\n"
"      </AvailableForChannels>\n"
"      <UsesNoteNameList Name=\"Notes\"/>\n"
"    </ChannelNameSet>\n"
"    <NoteNameList Name=\"Notes\">\n"
"%s"
"      <Note Number=\"36\" Name=\"KickDrum\"/>\n"
"      <Note Number=\"37\" Name=\"SnareSidestk\"/>\n"
"      <Note Number=\"38\" Name=\"SnareCtr\"/>\n"
"      <Note Number=\"39\" Name=\"HandClap\"/>\n"
"      <Note Number=\"40\" Name=\"SnareEdge\"/>\n"
"      <Note Number=\"41\" Name=\"FloorTomCtr\"/>\n"
"      <Note Number=\"42\" Name=\"ClosedHat\"/>\n"
"      <Note Number=\"43\" Name=\"FloorTomEdge/Alt\"/>\n"
"      <Note Number=\"44\" Name=\"PedalHat\"/>\n"
"      <Note Number=\"45\" Name=\"TomCtr\"/>\n"
"      <Note Number=\"46\" Name=\"SemiHat\"/>\n"
"      <Note Number=\"47\" Name=\"TomEdge/Alt\"/>\n"
"      <Note Number=\"48\" Name=\"SwishHat\"/>\n"
"      <Note Number=\"49\" Name=\"Crash1\"/>\n"
"      <Note Number=\"50\" Name=\"Crash1Chk\"/>\n"
"      <Note Number=\"51\" Name=\"RideTip\"/>\n"
"      <Note Number=\"52\" Name=\"RideChk\"/>\n"
"      <Note Number=\"53\" Name=\"RideBell\"/>\n"
"      <Note Number=\"54\" Name=\"Tambourine\"/>\n"
"      <Note Number=\"55\" Name=\"Splash\"/>\n"
"      <Note Number=\"56\" Name=\"Cowbell\"/>\n"
"      <Note Number=\"57\" Name=\"Crash2\"/>\n"
"      <Note Number=\"58\" Name=\"Crash2Chk\"/>\n"
"      <Note Number=\"59\" Name=\"RideShank\"/>\n"
"      <Note Number=\"60\" Name=\"Crash3\"/>\n"
"      <Note Number=\"61\" Name=\"%s\"/>\n"
"    </NoteNameList>\n"
"   </MasterDeviceNames>\n"
"</MIDINameDocument>\n"
	, self
	, self->kit_id > 2 ? "      <Note Number=\"35\" Name=\"Stick Click\"/>\n" : ""
	, self->kit_id > 2 ? "Shaker" : "Maracas"
);
	return mn;
}

static char*
mn_model (LV2_Handle instance)
{
	AVLSynth* self = (AVLSynth*)instance;
	char* rv = (char*) calloc (64, sizeof (char));
	snprintf (rv, 63, "AVL-Drumkits-LV2:%p", self);
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
mkdesc(1, "BlackPearlMulti");
mkdesc(2, "RedZeppelin");
mkdesc(3, "RedZeppelinMulti");
mkdesc(4, "BlondeBop");
mkdesc(5, "BlondeBopMulti");
mkdesc(6, "BlondeBopHR");
mkdesc(7, "BlondeBopHRMulti");

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
	case 0: return &descriptor0;
	case 1: return &descriptor1;
	case 2: return &descriptor2;
	case 3: return &descriptor3;
	case 4: return &descriptor4;
	case 5: return &descriptor5;
	case 6: return &descriptor6;
	case 7: return &descriptor7;
	default:
		return NULL;
	}
}
