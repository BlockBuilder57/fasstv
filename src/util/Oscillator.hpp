// Created by block on 5/25/24.

// This file was taken almost entirely from BespokeSynth.
// BespokeSynth operates under the GPL-3 license, a copy of which
// can be found at https://www.gnu.org/licenses/gpl-3.0.html.

#pragma once

#include <algorithm>

#ifndef M_PI
	#define M_PI PI
#endif

#define FPI 3.14159265358979323846f
#define FTWO_PI 6.28318530717958647693f

namespace fasstv {

	enum OscillatorType { kOsc_Sin, kOsc_Square, kOsc_Tri, kOsc_Saw, kOsc_NegSaw, kOsc_Random, kOsc_Drunk, kOsc_Perlin };

	class Oscillator {
	   public:
		Oscillator(OscillatorType type) : mType(type) {}

		OscillatorType GetType() const { return mType; }
		void SetType(OscillatorType type) { mType = type; }

		float Value(float phase) const;

		float GetPulseWidth() const { return mPulseWidth; }
		void SetPulseWidth(float width) { mPulseWidth = width; }

		float GetShuffle() const { return mShuffle; }
		void SetShuffle(float shuffle) { mShuffle = std::min(shuffle, .999f); }

		float GetSoften() const { return mSoften; }
		void SetSoften(float soften) { mSoften = std::clamp((double)soften, 0.0, 1.0); }

		OscillatorType mType { OscillatorType::kOsc_Sin };

		static double GetPhaseInc(float freq, int samplerate);

	   private:
		float SawSample(float phase) const;

		float mPulseWidth { .5 };
		float mShuffle { 0 };
		float mSoften { 0 };
	};

} // namespace fasstv
