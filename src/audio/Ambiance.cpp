/*
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code'). 

Arx Fatalis Source Code is free software: you can redistribute key and/or modify key under the terms of the GNU General Public 
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that key will be useful, but WITHOUT ANY WARRANTY; without even the implied 
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see 
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these 
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx 
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o 
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#include "audio/Ambiance.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "audio/AudioGlobal.h"
#include "audio/Sample.h"
#include "audio/AudioBackend.h"
#include "audio/AudioSource.h"
#include "audio/Mixer.h"

#include "io/PakReader.h"
#include "io/FilePath.h"

#include "platform/String.h"
#include "platform/Flags.h"
#include "platform/Random.h"

using std::string;

namespace {

using namespace audio;

struct KeySetting {
	
	// Key settings flags
	enum KeyFlag {
		FLAG_RANDOM      = 1,
		FLAG_INTERPOLATE = 2
	};
	DECLARE_FLAGS(KeyFlag, KeyFlags)
	
	KeyFlags flags; // A set of KeySettingFlag
	float min, max; // Min and max setting values
	float from, to, cur; // Current min and max values
	unsigned interval; // Interval between updates (On Start = 0)
	int tupdate; // Last update time
	
	KeySetting() : flags(0), min(0), max(0), from(0), to(0), cur(0), interval(0), tupdate(0) { }
	
	bool load(PakFileHandle * file) {
		
		f32 _min, _max;
		u32 _interval, _flags;
		if(!file->read(&_min, 4) ||
		   !file->read(&_max, 4) ||
		   !file->read(&_interval, 4) ||
		   !file->read(&_flags, 4)) {
			return false;
		}
		min = _min, max = _max, interval = _interval;
		flags = KeyFlags::load(_flags); // TODO save/load flags
		
		return true;
	}
	
	void reset() {
		tupdate = 0;
		if(min != max && flags & FLAG_RANDOM) {
			cur = min + Random::getf() * (max - min);
		} else {
			cur = min;
		}
		from = min;
		to = max;
	}
	
	float update(signed timez = 0) {
		
		if(min == max) {
			return cur;
		}
		
		signed elapsed = timez - tupdate;
		if(elapsed >= (signed)interval) {
			elapsed = 0;
			tupdate += interval;
			if(flags & FLAG_RANDOM) {
				from = to;
				to = min + Random::getf() * (max - min);
			} else {
				if(from == min) {
					from = max, to = min;
				} else {
					from = min, to = max;
				}
			}
			cur = from;
		}
		
		if(flags & FLAG_INTERPOLATE) {
			cur = from + float(elapsed) / interval * (to - from);
		}
		
		return cur;
	};
	
};

struct TrackKey {
	
	size_t start; // Start time (after last key)
	size_t n_start; // Next time to play sample (when delayed)
	size_t loop, loopc; // Loop count
	unsigned delay_min, delay_max; // Min and max delay before each sample loop
	unsigned delay; // Current delay
	KeySetting volume; // Volume settings
	KeySetting pitch; // Pitch settings
	KeySetting pan; // Pan settings
	KeySetting x, y, z; // Positon settings
	
	TrackKey() : start(0), n_start(0), loop(0), loopc(0),
	             delay_min(0), delay_max(0), delay(0) {
	}
	
	bool load(PakFileHandle * file) {
		
		u32 _start, _loop, _delay_min, _delay_max, _flags;
		
		if(!file->read(&_flags, 4) ||
		   !file->read(&_start, 4) ||
		   !file->read(&_loop, 4) ||
		   !file->read(&_delay_min, 4) ||
		   !file->read(&_delay_max, 4) ||
		   !volume.load(file) ||
		   !pitch.load(file) ||
		   !pan.load(file) ||
		   !x.load(file) ||
		   !y.load(file) ||
		   !z.load(file)) {
			return false;
		}
		start = _start, loop = _loop, delay_min = _delay_min, delay_max = _delay_max;
		
		return true;
	}
	
	void updateSynch() {
		if(delay_min != delay_max) {
			delay = delay_max - delay;
			delay += delay_min + Random::get() % (delay_max - delay_min);
		} else {
			delay = delay_min;
		}
	}
	
};

} // anonymous namespace

namespace audio {

enum aalAmbianceFlag {
	IS_PLAYING    = 0x00000001,
	IS_PAUSED     = 0x00000002,
	IS_LOOPED     = 0x00000004,
	IS_FADED_UP   = 0x00000008,
	IS_FADED_DOWN = 0x00000010
};
DECLARE_FLAGS(aalAmbianceFlag, aalAmbianceFlags)
DECLARE_FLAGS_OPERATORS(aalAmbianceFlags)

static const size_t KEY_CONTINUE = (size_t)-1;

struct Ambiance::Track {
	
	enum Flag {
		POSITION   = 0x00000001,
		REVERB     = 0x00000002,
		MASTER     = 0x00000004,
		MUTED      = 0x00000008,
		PAUSED     = 0x00000010,
		PREFETCHED = 0x00000020
	};
	DECLARE_FLAGS(Flag, TrackFlags)
	
	SourceId s_id; // Sample id
	AmbianceId a_id; // Ambiance id
	
	string name; // Track name
	
	TrackFlags flags;

	typedef std::vector<TrackKey> KeyVector;
	KeyVector			key_l; // Key list
	KeyVector::iterator key_i;
	
	Track() : s_id(INVALID_ID), a_id(INVALID_ID), flags(0) {
	}
	
	~Track() {
		Source * source = backend->getSource(s_id);
		if(source) {
			source->stop();
		}
		SampleId sid = Backend::getSampleId(s_id);
		arx_assert(_sample.isValid(sid));
		_sample[sid]->dereference();
	}
	
	void keyPlay(TrackKey & key) {
		
		Source * source = backend->getSource(s_id);
		if(!source) {
			
			Ambiance * ambiance = _amb[a_id];
			Channel channel;
			
			channel.mixer = ambiance->channel.mixer;
			channel.flags = FLAG_CALLBACK | FLAG_VOLUME | FLAG_PITCH | FLAG_RELATIVE;
			channel.flags |= ambiance->channel.flags;
			channel.volume = key.volume.cur;
			
			if(ambiance->channel.flags & FLAG_VOLUME) {
				channel.volume *= ambiance->channel.volume;
			}
			
			channel.pitch = key.pitch.cur;
			
			if(flags & POSITION) {
				channel.flags |= FLAG_POSITION;
				channel.position.x = key.x.cur;
				channel.position.y = key.y.cur;
				channel.position.z = key.z.cur;
				if(ambiance->channel.flags & FLAG_POSITION) {
					channel.position += ambiance->channel.position;
				}
				channel.flags |= ambiance->channel.flags & FLAG_REVERBERATION;
			} else {
				channel.flags |= FLAG_PAN;
				channel.pan = key.pan.cur;
			}
			
			source = backend->createSource(s_id, channel);
			if(!source) {
				s_id = Backend::clearSource(s_id);
				return;
			}
			
			s_id = source->getId();
		}
		
		if(!key.delay_min && !key.delay_max) {
			source->play(key.loopc + 1);
		} else {
			source->play();
		}
		
		key.n_start = KEY_CONTINUE;
	}
	
};
DECLARE_FLAGS_OPERATORS(Ambiance::Track::TrackFlags)

static const u32 AMBIANCE_FILE_SIGNATURE = 0x424d4147; //'GAMB'
static const u32 AMBIANCE_FILE_VERSION_1001 = 0x01000001; // y
static const u32 AMBIANCE_FILE_VERSION_1002 = 0x01000002; // y
static const u32 AMBIANCE_FILE_VERSION_1003 = 0x01000003; // y
static const u32 AMBIANCE_FILE_VERSION = AMBIANCE_FILE_VERSION_1003;

static void OnAmbianceSampleStart(void * inst, const SourceId &, void * data);

Ambiance::Ambiance(const fs::path & _name) :
	status(Idle), loop(false), fade(None), start(0), time(0),
	track_c(0), track_l(NULL), name(_name),
	data(NULL) {
	channel.flags = 0;
}

Ambiance::~Ambiance() {
	LogDebug << "deleting ambiance " << name;
	if(track_l) {
		delete[] track_l;
	}
}

static SampleId _loadSample(PakFileHandle * file) {
	
	char text[256];
	size_t k = 0;
	do {
		if(!file->read(&text[k], 1)) {
			return AAL_ERROR_FILEIO;
		}
	} while (text[k++]);
	
	Sample * sample = new Sample(fs::path::load(text));
	SampleId id = INVALID_ID;
	if(sample->load() || (id = _sample.add(sample)) == INVALID_ID) {
		delete sample;
	} else {
		sample->reference();
	}
	return id;
}

static aalError _LoadAmbiance_1001(PakFileHandle * file, size_t track_c, Ambiance::Track * track_l) {
	
	for(size_t i = 0; i < track_c; ++i) {
		Ambiance::Track * track = &track_l[i];
		
		// Get track sample name
		if((track->s_id = _loadSample(file)) == INVALID_ID) {
			return AAL_ERROR_FILEIO;
		}
		
		// Read flags and key count
		u32 flags;
		u32 key_c;
		if(!file->read(&flags, 4) || !file->read(&key_c, 4)) {
				return AAL_ERROR_FILEIO;
		}

		track->flags = Ambiance::Track::TrackFlags::load(flags); // TODO save/load flags
		track->flags &= ~(Ambiance::Track::MUTED | Ambiance::Track::PAUSED | Ambiance::Track::PREFETCHED);
		
		track->key_l.resize(key_c);
		
		//Read settings for each key
		Ambiance::Track::KeyVector::iterator key;
		for(key = track->key_l.begin(); key != track->key_l.end(); ++key) {
			if(!key->load(file)) {
				return AAL_ERROR_FILEIO;
			}
		}
	}
	
	return AAL_OK;
}

static aalError _LoadAmbiance_1002(PakFileHandle * file, size_t track_c, Ambiance::Track * track_l) {
	
	for(size_t i = 0; i < track_c; i++) {
		Ambiance::Track * track = &track_l[i];
		
		//Get track sample name
		if((track->s_id = _loadSample(file)) == INVALID_ID) {
			return AAL_ERROR_FILEIO;
		}
		
		//Get track name (!= sample name)
		track->name.clear();
		for(;;) {
			char c;
			if(!file->read(&c, 1)) {
				return AAL_ERROR_FILEIO;
			}
			if(c) {
				track->name.push_back(c);
			} else {
				break;
			}
		}
		makeLowercase(track->name);
		
		// Read flags and key count
		u32 flags;
		u32 key_c;
		if(!file->read(&flags, 4) || !file->read(&key_c, 4)) {
				return AAL_ERROR_FILEIO;
		}

		track->flags = Ambiance::Track::TrackFlags::load(flags); // TODO save/load flags
		track->flags &= ~(Ambiance::Track::MUTED | Ambiance::Track::PAUSED | Ambiance::Track::PREFETCHED);
		
		track->key_l.resize(key_c);
		
		
		//Read settings for each key
		Ambiance::Track::KeyVector::iterator key;
		for(key = track->key_l.begin(); key != track->key_l.end(); ++key) {
			if(!key->load(file)) {
				return AAL_ERROR_FILEIO;
			}
		}
	}
	
	return AAL_OK;
}

static aalError _LoadAmbiance_1003(PakFileHandle * file, size_t track_c, Ambiance::Track * track_l) {
	
	Ambiance::Track * track = &track_l[track_c];
	
	while(track > track_l) {
		--track;
		
		//Get track sample name
		if((track->s_id = _loadSample(file)) == INVALID_ID) {
			return AAL_ERROR_FILEIO;
		}
		
		//Get track name (!= sample name)
		track->name.clear();
		for(;;) {
			char c;
			if(!file->read(&c, 1)) {
				return AAL_ERROR_FILEIO;
			}
			if(c) {
				track->name.push_back(c);
			} else {
				break;
			}
		}
		makeLowercase(track->name);
		
		// Read flags and key count
		u32 flags;
		u32 key_c;
		if(!file->read(&flags, 4) || !file->read(&key_c, 4)) {
				return AAL_ERROR_FILEIO;
		}

		track->flags = Ambiance::Track::TrackFlags::load(flags); // TODO save/load flags
		track->flags &= ~(Ambiance::Track::MUTED | Ambiance::Track::PAUSED | Ambiance::Track::PREFETCHED);
		
		track->key_l.resize(key_c);
		
		//Read settings for each key
		Ambiance::Track::KeyVector::reverse_iterator key;
		for(key = track->key_l.rbegin(); key != track->key_l.rend(); ++key) {
			if(!key->load(file)) {
				return AAL_ERROR_FILEIO;
			}
		}
	}
	
	return AAL_OK;
}

aalError Ambiance::load() {
	
	if(track_c) {
		return AAL_ERROR_INIT;
	}
	arx_assert(!track_l);
	
	PakFileHandle * file = OpenResource(name, ambiance_path);
	if(!file) {
		return AAL_ERROR_FILEIO;
	}
	
	// Read file signature and version
	u32 sign, version;
	if(!file->read(&sign, 4) || !file->read(&version, 4)) {
		delete file;
		return AAL_ERROR_FILEIO;
	}
	
	// Check file signature
	if(sign != AMBIANCE_FILE_SIGNATURE || version > AMBIANCE_FILE_VERSION) {
		delete file;
		return AAL_ERROR_FORMAT;
	}
	
	// Read track count and initialize track structures
	file->read(&track_c, 4);
	track_l = new Track[track_c];
	if(!track_l) {
		delete file;
		return AAL_ERROR_MEMORY;
	}
	
	aalError error;
	switch(version) {
		case AMBIANCE_FILE_VERSION_1001 :
			error = _LoadAmbiance_1001(file, track_c, track_l);
			break;
		case AMBIANCE_FILE_VERSION_1002 :
			error = _LoadAmbiance_1002(file, track_c, track_l);
			break;
		case AMBIANCE_FILE_VERSION_1003 :
			error = _LoadAmbiance_1003(file, track_c, track_l);
			break;
		default :
			error = AAL_ERROR;
	}
	
	delete file;
	
	return error;
}

aalError Ambiance::setVolume(float _volume) {
	
	if(!(channel.flags & FLAG_VOLUME)) {
		return AAL_ERROR_INIT;
	}
	
	channel.volume = clamp(_volume, 0.f, 1.f);
	
	if(!isPlaying()) {
		return AAL_OK;
	}
	
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		Source * source = backend->getSource(track->s_id);
		if(source) {
			source->setVolume(track->key_i->volume.cur * channel.volume);
		}
	}
	
	return AAL_OK;
}

aalError Ambiance::muteTrack(const string & name, bool mute) {
	
	if(!track_c) {
		return AAL_OK;
	}
	
	bool found = false;
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		if(track->name == name || _sample[Backend::getSampleId(track->s_id)]->getName() == name) {
			found = true;
			break;
		}
	}
	
	if(!found) {
		return AAL_OK;
	}
	
	if(mute) {
		track->flags |= Track::MUTED;
		if(isPlaying()) {
			Source * source = backend->getSource(track->s_id);
			if(source) {
				source->stop();
			}
		}
	} else {
		track->flags &= ~Track::MUTED;
		if(isPlaying()) {
			track->key_i = track->key_l.begin();
			track->keyPlay(*track->key_i);
		}
	}
	
	return AAL_OK;
}

aalError Ambiance::play(const Channel & _channel, bool _loop, size_t _fade_interval) {
	
	channel = _channel;
	
	if(isPlaying() || isPaused()) {
		stop();
	}
	
	loop = _loop;
	
	fade_interval = (float)_fade_interval;
	if(fade_interval) {
		fade = FadeUp;
		fade_max = channel.volume;
		channel.volume = 0.0F;
		fade_time = 0.0F;
	} else {
		fade = None;
	}
	
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		
		SampleId s_id = Backend::getSampleId(track->s_id);
		if(_sample.isValid(s_id)) {
			Sample * sample = _sample[s_id];
			if(!sample->getCallbackCount()) {
				sample->setCallback(OnAmbianceSampleStart, track, 0, UNIT_BYTES);
				sample->setCallback(OnAmbianceSampleEnd, track, sample->getLength(), UNIT_BYTES);
			}
		}
		
		//Init track keys
		Ambiance::Track::KeyVector::reverse_iterator key;
		for(key = track->key_l.rbegin(); key != track->key_l.rend(); ++key) {
			
			key->delay = key->delay_max;
			key->updateSynch();
			key->n_start = key->start + key->delay;
			key->loopc = key->loop;
			
			key->volume.reset();
			key->pitch.reset();
			key->pan.reset();
			key->x.reset();
			key->y.reset();
			key->z.reset();
		}
		
		track->key_i = track->key_l.begin();
	}
	
	status = Playing;
	start = session_time;
	
	const Mixer * mixer = _mixer[channel.mixer];
	if(mixer && mixer->isPaused()) {
		status = Paused;
	}
	
	return AAL_OK;
}

aalError Ambiance::stop(size_t _fade_interval) {
	
	if(!isIdle()) {
		return AAL_OK;
	}
	
	fade_interval = (float)_fade_interval;
	if(fade_interval) {
		fade = FadeDown;
		fade_time = 0;
		return AAL_OK;
	}
	
	status = Idle;
	time = 0;
	
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		Source * source = backend->getSource(track->s_id);
		if(source) {
			source->stop();
		}
		track->s_id = Backend::clearSource(track->s_id);
	}
	
	return AAL_OK;
}

aalError Ambiance::pause() {
	
	if(!isPlaying()) {
		return AAL_ERROR;
	}
	
	status = Paused;
	time = session_time;
	
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		Source * source = backend->getSource(track->s_id);
		if(source) {
			source->pause();
			track->flags |= Track::PAUSED;
		}
	}
	
	return AAL_OK;
}

aalError Ambiance::resume() {
	
	if(!isPaused()) {
		return AAL_ERROR;
	}
	
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		if(track->flags & Track::PAUSED) {
			Source * source = backend->getSource(track->s_id);
			if(source) {
				source->resume();
			}
			track->flags &= ~Track::PAUSED;
		}
	}
	
	status = Playing;
	start += session_time - time;
	time = session_time - start;
	
	return AAL_OK;
}

aalError Ambiance::update() {
	
	if(!isPlaying()) {
		return AAL_OK;
	}
	
	size_t interval = session_time - start - time;
	time += interval;
	
	// Fading
	if(fade_interval && fade != None) {
		fade_time += interval;
		if(fade == FadeUp) {
			channel.volume = fade_max * fade_time / fade_interval;
			if(channel.volume >= fade_max) {
				channel.volume = fade_max;
				fade_interval = 0.f;
			}
		} else {
			channel.volume = fade_max - fade_max * fade_time / fade_interval;
			if(channel.volume <= 0.f) {
				stop();
				return AAL_OK;
			}
		}
		channel.volume = LinearToLogVolume(channel.volume);
	}
	
	// Update tracks
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		
		SampleId s_id = Backend::getSampleId(track->s_id);
		if(!_sample.isValid(s_id) || track->flags & Track::MUTED) {
			continue;
		}
		
		if(track->key_i == track->key_l.end()) {
			continue;
		}
		
		Ambiance::Track::KeyVector::iterator key = track->key_i;
		
		//Run / update keys
		if(key->n_start <= interval) {
			track->keyPlay(*key);
			continue;
		}
		
		key->n_start -= interval;
		Source * source = backend->getSource(track->s_id);
		if(!source) {
			continue;
		}
		
		if(key->volume.interval) {
			float value = key->volume.update(time);
			if (channel.flags & FLAG_VOLUME) value *= channel.volume;
			source->setVolume(value);
		} else {
			source->setVolume(key->volume.cur * channel.volume);
		}
		if(key->pitch.interval) {
			source->setPitch(key->pitch.update(time));
		}
		if(track->flags & Track::POSITION) {
			Vec3f position;
			position.x = key->x.interval ? key->x.update(time) : key->x.cur;
			position.y = key->y.interval ? key->y.update(time) : key->y.cur;
			position.z = key->z.interval ? key->z.update(time) : key->z.cur;
			if(channel.flags & FLAG_POSITION) {
				position += channel.position;
			}
			source->setPosition(position);
		} else {
			source->setPan(key->pan.update(time));
		}
	}
	
	return AAL_OK;
}

static void OnAmbianceSampleStart(void * inst, const SourceId &, void * data) {
	
	Source * instance = (Source *)inst;
	Ambiance::Track * track = (Ambiance::Track*)data;
	arx_assert(_amb.isValid(track->a_id));
	Ambiance::Track::KeyVector::iterator key = track->key_i;
	
	if(track->flags & Ambiance::Track::PREFETCHED) {
		track->flags &= ~Ambiance::Track::PREFETCHED;
		if(track->key_i == track->key_l.end()) {
			track->key_i = track->key_l.begin();
			key = track->key_i;
		}
		track->key_i->n_start = KEY_CONTINUE;
		track->key_i->loopc = track->key_i->loop;
	}
	
	// Prefetch
	if(!key->loopc && _amb[track->a_id]->isLooped()) {
		Ambiance::Track::KeyVector::iterator keyPrefetch = track->key_i + 1;
		if(keyPrefetch == track->key_l.end()) {
			keyPrefetch = track->key_l.begin();
		}
		if(!keyPrefetch->start && !keyPrefetch->delay_min && !keyPrefetch->delay_max) {
			instance->play(keyPrefetch->loop + 1);
			track->flags |= Ambiance::Track::PREFETCHED;
		}
	}
	
	float value = key->volume.update();
	if(_amb[track->a_id]->getChannel().flags & FLAG_VOLUME) {
		value *= _amb[track->a_id]->getChannel().volume;
	}
	instance->setVolume(value);
	
	instance->setPitch(key->pitch.update());
	
	if(instance->getChannel().flags & FLAG_POSITION) {
		Vec3f position;
		position.x = key->x.update();
		position.y = key->y.update();
		position.z = key->z.update();
		if(_amb[track->a_id]->getChannel().flags & FLAG_POSITION) {
			position += _amb[track->a_id]->getChannel().position;
		}
		instance->setPosition(position);
	} else {
		instance->setPan(key->pan.update());
	}
}

void Ambiance::OnAmbianceSampleEnd(void *, const SourceId &, void * data) {
	
	Ambiance::Track * track = (Ambiance::Track*)data;
	arx_assert(_amb.isValid(track->a_id));
	Ambiance * ambiance = _amb[track->a_id];

	Ambiance::Track::KeyVector::iterator key = track->key_i;
	if(!key->loopc--) {
		
		//Key end
		key->delay = key->delay_max;
		key->updateSynch();
		key->n_start = key->start + key->delay;
		key->loopc = key->loop;
		key->pitch.tupdate -= ambiance->time;
		
		if(++track->key_i == track->key_l.end()) {
			//Track end
			
			if(track->flags & Ambiance::Track::MASTER) {
				//Ambiance end
				ambiance->time = 0;
				
				if(ambiance->isLooped()) {
					Ambiance::Track * track2 = &ambiance->track_l[ambiance->track_c];
					while (track2 > ambiance->track_l) {
						--track2;
						if(!(track2->flags & Ambiance::Track::PREFETCHED)) {
							track2->key_i = track2->key_l.begin();
						}
					}
					ambiance->start = session_time;
				} else {
					ambiance->status = Ambiance::Idle;
				}
			}
		}
		
	} else if(key->delay_min || key->delay_max) {
		key->updateSynch();
		key->n_start = key->delay;
	}
}

void Ambiance::setId(AmbianceId id) {
	
	arx_assert(_amb.isValid(id) && _amb[id] == this);
	
	Track * track = &track_l[track_c];
	while(track > track_l) {
		--track;
		track->a_id = id;
	}
	
}

} // namespace audio
