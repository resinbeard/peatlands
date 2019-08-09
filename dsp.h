/* dsp.h
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

#include "rtqueue.h"
#include "libcyperus.h"

#ifndef DSP_H
#define DSP_H
   

typedef struct dsp_module_parameter {
  int type;
  float in;
  int pos;
  union {
    struct {
      char *name;
    } null;
    struct {
      char *name;
      struct cyperus_parameters *cyperus_params;
      float freq;
      float amp;
    } sine;
    struct {
      char *name;
      struct cyperus_parameters *cyperus_params;
      float freq;
      float amp;
    } square;
    struct {
      char *name;
      struct cyperus_parameters *cyperus_params;
    } pinknoise;
    struct {
      char *name;
      float cutoff;
      float res;
      struct cyperus_parameters *cyperus_params;
    } butterworth_biquad_lowpass;
    struct {
      char *name;
      float amt; /* 0-1 */
      float time; /* seconds */
      float feedback;
      struct cyperus_parameters *cyperus_params;
    } delay;
    struct {
      char *name;
      float amp; /* 0-1 */
      float freq; /* hertz */
      struct cyperus_parameters *cyperus_params;
    } vocoder;
  };
}dsp_parameter;

/* all possible identifiers for module parameters */
typedef enum dsp_parameter_identifiers{
  DSP_NULL_PARAMETER_ID=0,
  DSP_SINE_PARAMETER_ID=1,
  DSP_SQUARE_PARAMETER_ID=2,
  DSP_PINKNOISE_PARAMETER_ID=5,
  DSP_BUTTERWORTH_BIQUAD_LOWPASS_PARAMETER_ID=7,
  DSP_DELAY_PARAMETER_ID=8,
  DSP_VOCODER_PARAMETER_ID=9
}dsp_param_identifiers;  


int dsp_remove_module(int voice, int module_index);
char *dsp_list_modules(int voice_no);
int dsp_add_module(float (*dsp_function)(dsp_parameter,int,int), dsp_parameter dsp_param);

int dsp_create_sine(float freq, float amp);
int dsp_edit_sine(int module_no, float freq, float amp);
int dsp_create_square(float freq, float amp);
int dsp_edit_square(int module_no, float freq, float amp);

int dsp_create_pinknoise();

int dsp_create_butterworth_biquad_lowpass(float freq, float res);
int dsp_edit_butterworth_biquad_lowpass(int module_no, float freq, float res);

int dsp_create_delay(float amt, float time, float feedback);
int dsp_edit_delay(int module_no, float amt, float time, float feedback);

int dsp_create_vocoder(float freq, float amp);
int dsp_edit_vocoder(int module_no, float freq, float amp);

void *dsp_thread(void *arg);

extern float (*dsp_function[16])(dsp_parameter,int,int);
extern dsp_parameter dsp_voice_parameters[16];

#endif
