
/*
	FM. BISON hybrid (FM) synthesis -- Self-contained JP-8000 style supersaw oscillator.
	(C) Niels J. de Wit (njdewit tech./Stockton North) & bipolaraudio.nl
	MIT license applies, please see https://en.wikipedia.org/wiki/MIT_License or LICENSE in the project root!

	- Initial ref.: https://pdfs.semanticscholar.org/1852/250068e864215dd7f12755cf00636868a251.pdf (copy in repository)
	- New (post impl.) ref.: https://youtu.be/XM_q5T7wTpQ ('From Silicon to Darude' presentation by 'The usual suspects')
	- Free running: all phases are updated by Bison::Render() if the oscillator is not being used
	- Phases are all [0..1]
	
	FIXME:
		- Not in [-1..1] range if I recall correctly, its obviously a bit like mixing voices, but FM. BISON's
		  render has ample headroom for situations like this; I can normalize (not just flatten) if I want to
		- Too much implementation lives in this header file?

	For now I'm picking the single precision version since (most of) of FM. BISON uses that internally,
	and given the final word on the real Roland implementation, well.. :-)

	Discussing the Roland / 'The usual suspects' way:
	- Changing this oscillator to more closely if not exactly reflecting the JP-8000 is possible but is not
	  simply a matter of oversampling; it would also require more coarse fixed point arithmetic as well as 100%
	  naive (non-bandlimited) oscillators. Anything inbetween would just sound wrong and isn't necessarily the
	  right call for the hardware FM. BISON is intended to run on.
	- Both implementations (the actual truth and this adaptation) offer an amount of grit because not all 
	  artifacts are eliminated in either implementation. Another important key to this particular oscillator is
	  the dissonance at the top end of the detune curve; this represents it well and true to form as far as I am
	  concerned.
	- When all is said and done this oscillator is only as good as the DSP chain it is part of and the sound
	  designer that makes the patch.
	- Nonetheless, I will have a go at it once I can justify spending the time! :-)
*/

#pragma once

#include "3rdparty/filters/Biquad.h" // Modified version

#include "synth-global.h"
#include "synth-stateless-oscillators.h"
#include "synth-one-pole-filters.h"

namespace SFM
{
	// Number of oscillators
	constexpr unsigned kNumSupersawOscillators = 7;

	// Relation between frequencies (slightly asymmetric)
	// Centre (fundamental) moved from position 4 to 1
	constexpr float kSupersawRelative[kNumSupersawOscillators] = 
	{
		// As derived by 'the usual suspects' to test Adam Szabo's measurings (and given the nature of their research: the final word)
		// You can still opt to use the other sets of course, nothing inherently wrong with those
		0.f, 
		-0.10986328125f, 
		-0.0628662109375f, 
		-0.01953125f,
		0.01953125f,
		0.062255859375f, 
		0.107421875f

/*
		// According to Adam Szabo
		 0.f, 
		-0.11002313f, 
		-0.06288439f, 
		-0.01952356f,
		 0.01991221f,
		 0.06216538f, 
		 0.10745242f
*/

/*
		// According to Alex Shore
		0.f,
		-0.11002313f,
		-0.06288439f,
		-0.03024148f, 
		0.02953130f,
		0.06216538f,
		0.10745242f
*/
	}; 

	class Supersaw
	{
		class DCBlocker
		{
		public:
			void Reset() 
			{
				m_prevSample = 0.f;
				m_feedback   = 0.f;
			}

			SFM_INLINE float Apply(float sample)
			{
				constexpr float R = 0.9925f; // What "everyone" uses in a leaky integrator is 0.995
				m_feedback = sample-m_prevSample + R*m_feedback;
				m_prevSample = sample;
				return m_feedback;
			}

		private:
			float m_prevSample = 0.f;
			float m_feedback   = 0.f;
		};
		
	public:
		static void CalculateDetuneTable();

		Supersaw() : 
			m_sampleRate(1) 
		{
			// Initialize phases with values between [0..1] with pseudo-irrational offsets
			// This is chiefly necessary for FM. BISON, not the algorithm itself, so long
			// as it is based on true free-running oscillators
			for (auto &phase : m_phase)
				phase = oscSine(0.11f + 0.1f*mt_randf()); // Good enough (TM)

				// Generating "more" irrational values can for ex. be done by accumulation, 
				// but ultimately any IEEE compliant float is rational :-)
		}

		void Initialize(float frequency, unsigned sampleRate, float detune, float mix);

		// Allows on the fly adjustment of the 2 key JP-8000 parameters (read: whilst note held)
		SFM_INLINE void SetFrequency(float frequency, float detune, float mix)
		{
			// Set JP-8000 controls
			SetDetune(detune);
			SetMix(mix);

			m_frequency = frequency;
			OnFrequencyChange(m_frequency);
		}

		SFM_INLINE void PitchBend(float bend)
		{
			// An unnecessary OnFrequencyChange() isn't cheap enough to just disregard
			if (1.f == bend)
				return;

			const float frequency = m_frequency*bend;
			OnFrequencyChange(frequency);
		}

		SFM_INLINE float Sample()
		{
			// Fundamental
			const float main = Oscillate(0, false);

			// Side bands
			float sides = 0.f;
			for (unsigned iOsc = 1; iOsc < kNumSupersawOscillators; ++iOsc)
				sides += Oscillate(iOsc, true);

			float signal = main*m_mainMix + sides*m_sideMix;

//			m_HPF.processMono(signal);
//			signal = m_blocker.Apply(signal);

			return signal;
		}
		
		// FIXME: used by Bison::Render() to emulate (pseudo) free running oscillators
		SFM_INLINE void Skip(unsigned numSamples)
		{
			for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
			{
				float &phase = m_phase[iOsc];
				phase = fmodf(phase + numSamples*m_pitch[iOsc], 1.f);
			}
		}

		SFM_INLINE float GetFrequency() const
		{	
			// Fundamental freq.
			return m_frequency;
		}

		SFM_INLINE float GetPhase() const
		{
			// Return main oscillator's phase
			return m_phase[0];
		}

	private:
		unsigned m_sampleRate;
		float m_frequency = 0.f;

		float m_curDetune = 0.f;
		float m_mainMix   = 0.f; 
		float m_sideMix   = 0.f;

		float m_phase[kNumSupersawOscillators] = { 0.f };
		float m_pitch[kNumSupersawOscillators] = { 0.f };

//		Biquad m_HPF;
//		DCBlocker m_blocker;

		SFM_INLINE float Tick(unsigned iOsc)
		{
			SFM_ASSERT(iOsc < kNumSupersawOscillators);

			float &phase = m_phase[iOsc];
			float  pitch = m_pitch[iOsc];

			const float oscPhase = phase;
			SFM_ASSERT(oscPhase >= 0.f && oscPhase <= 1.f);

			phase += pitch;

			if (phase > 1.f)
				phase -= 1.f;

			return oscPhase;
		}

		SFM_INLINE float Oscillate(unsigned iOsc, bool subtractPureSine)
		{
			const float phase = Tick(iOsc);
			
			float polySaw = oscPolySaw(phase, m_pitch[iOsc]);

			// Thought I'd explain this: by subtracting the pure sine from the pseudo-bandlimited saw
			// from each of the side bands retains their higher order harmonics and thus the grit of
			// the oscillator remains uncompromised; also as opposed to a high pass filter, especially
			// without oversampling like Roland does, more effectively stops lower frequencies of the
			// saws muddying each other
			if (subtractPureSine)
				polySaw -= oscSine(phase);
			
			return polySaw;
		}

		SFM_INLINE void OnFrequencyChange(float frequency)
		{
			// Calc. pitch for each oscillator
			for (unsigned iOsc = 0; iOsc < kNumSupersawOscillators; ++iOsc)
			{
				const float offset   = m_curDetune*kSupersawRelative[iOsc];
				const float freqOffs = frequency * offset;
				const float detuned  = frequency + freqOffs;
				const float pitch    = CalculatePitch<float>(detuned, m_sampleRate);
				m_pitch[iOsc] = pitch;
			}

			// Cut lower end (as proposed by Szabo and done by Roland)
			// constexpr float Q = kNormalGainAtCutoff; // 0.707
			// m_HPF.setBiquad(bq_type_highpass, frequency/m_sampleRate, Q, 0.f);
		}
		
		// Key parameters (detune & mix)
		SFM_INLINE void SetDetune(float detune /* [0..1] */)
		{
//			m_curDetune = (float) SampleDetuneCurve(detune);
			m_curDetune = SampleDetuneTable(detune);
			SFM_ASSERT(m_curDetune >= 0.f && m_curDetune <= 1.f);
		}
	
		SFM_INLINE void SetMix(float mix /* [0..1] */)
		{
			// JP-8000 ratios (according to Adam's thesis)
			// FIXME (potential): I'd have to look a the *entire* Roland implementation if I were to simplify this
			SFM_ASSERT_NORM(mix);
			m_mainMix = -0.55366f*mix + 0.99785f;
			m_sideMix = -0.73764f*mix*mix + 1.2841f*mix + 0.044372f;
		}
		
		// See impl.
		static double SampleDetuneCurve(double detune);
		static float SampleDetuneTable(float detune);
	};
}

