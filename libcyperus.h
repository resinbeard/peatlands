/* libcyperus.h
This file is a part of 'cyperus'
This program is free software: you can redistribute it and/or modify
hit under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

'cyperus' is a JACK client for learning about software synthesis

Copyright 2015 murray foster */

#ifndef LIBCYPERUS_H
#define LIBCYPERUS_H
struct cyperus_parameters {
  float freq;
  float amp;
  float res; /* resonance */
  float fb; /* feedback */
  float delay_amt; /* delay amount, 0-1 */
  float delay_time; /* init this with
		       = seconds * sample_rate */
  int pos;
  int delay_pos;
  int vocoder_pos;
  float *signal_buffer; /* init this before using with 
			= (float *)malloc(2 * seconds * sample_rate) */
  float *signal_buffer1;
  
  float in;
  float state0;
  float state1;
  float state2;
  float tempval;
  float lastinval;
  float lastinval1;
  float lastinval2;
  float lastinval3;
  float lastoutval;
  float lastoutval1;
  float lastoutval2;
  float lastoutval3;
};
#endif

/* signal generators */
float cyperus_sine(struct cyperus_parameters *sinewav, int jack_sr, int pos);
float cyperus_square(struct cyperus_parameters *squarewav, int jack_sr, int pos);
float cyperus_triangle(struct cyperus_parameters *triwav, int jack_sr, int pos);
float cyperus_whitenoise(struct cyperus_parameters *noiz, int jack_sr, int pos);
float cyperus_pinknoise(struct cyperus_parameters *noiz, int jack_sr, int pos);

float cyperus_karlsen_lowpass(struct cyperus_parameters *filtr, int jack_sr, int pos);
float cyperus_butterworth_biquad_lowpass(struct cyperus_parameters *filtr, int jack_sr, int pos);

float cyperus_butterworth_biquad_hipass(struct cyperus_parameters *filtr, int jack_sr, int pos);
float cyperus_moog_vcf(struct cyperus_parameters *filtr, int jack_sr, int pos);

float cyperus_delay(struct cyperus_parameters *effect, int jack_sr, int pos);

float cyperus_vocoder(struct cyperus_parameters *vocoder, int jack_ser, int pos);
