/* dsp.c
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

#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //exit(0);

#include "test_osc.h"
#include "rtqueue.h"
#include "libcyperus.h"
#include "dsp.h"

int bypass[16] = {0};
int fifo_out_is_waiting = 0;
pthread_mutex_t fifo_out_is_waiting_mutex;
pthread_cond_t fifo_out_is_waiting_cond;

/* MODULE PARAMETERS -------------------- */

float (*dsp_function[16])(dsp_parameter,int,int);
dsp_parameter dsp_voice_parameters[16] = {0};
dsp_parameter dsp_voice_removals[16] = {0};

/* MODULE DEFINITIONS --------------------

   signal generators */

int
dsp_bypass(int module_no, int state) {
  bypass[module_no]=state;
  return 0;
} /* dsp_bypass */

float
dsp_sine(dsp_parameter sine_param, int jack_samplerate, int pos) {
  float outsample = 0.0;
  float outsample_new = 0.0;
  sine_param.sine.cyperus_params[0].freq = sine_param.sine.freq;
  sine_param.sine.cyperus_params[0].amp = sine_param.sine.amp;

  outsample = cyperus_sine(&(sine_param.sine.cyperus_params[0]),jack_samplerate,pos);
  
  return outsample;
} /* dsp_sine */

float
dsp_square(dsp_parameter square_param, int jack_samplerate, int pos) {
  float outsample = 0.0;
  square_param.square.cyperus_params[0].freq = square_param.square.freq;
  square_param.square.cyperus_params[0].amp = square_param.square.amp;
  
  outsample = cyperus_square(&(square_param.square.cyperus_params[0]),jack_samplerate,pos);
  
  return outsample;
} /* dsp_square */

float
dsp_pinknoise(dsp_parameter noise_param, int jack_samplerate, int pos) {
  float outsample = 0.0;
  
  outsample = cyperus_pinknoise(&(noise_param.pinknoise.cyperus_params[0]),jack_samplerate,pos);
  
  return outsample;
} /* dsp_pinknoise */

float
dsp_butterworth_biquad_lowpass(dsp_parameter filter_param, int jack_samplerate, int pos) {
  float outsample = 0.0;
  
  filter_param.butterworth_biquad_lowpass.cyperus_params[0].in = filter_param.in;
  filter_param.butterworth_biquad_lowpass.cyperus_params[0].freq = filter_param.butterworth_biquad_lowpass.cutoff;
  filter_param.butterworth_biquad_lowpass.cyperus_params[0].res = filter_param.butterworth_biquad_lowpass.res;

  outsample = cyperus_butterworth_biquad_lowpass(&(filter_param.butterworth_biquad_lowpass.cyperus_params[0]),jack_samplerate,pos);
  
  return outsample;
} /* dsp_butterworth_biquad_lowpass */

float
dsp_delay(dsp_parameter delay_param, int jack_samplerate, int pos) {
  float outsample = 0.0;
  
  delay_param.delay.cyperus_params[0].in = delay_param.in;
  delay_param.delay.cyperus_params[0].delay_amt = delay_param.delay.amt;
  delay_param.delay.cyperus_params[0].delay_time = delay_param.delay.time;
  delay_param.delay.cyperus_params[0].fb = delay_param.delay.feedback;

  outsample = cyperus_delay(&(delay_param.delay.cyperus_params[0]),jack_samplerate,pos);
  
} /* dsp_delay */

float
dsp_vocoder(dsp_parameter vocoder_param, int jack_samplerate, int pos) {
  float outsample = 0.0;
  
  vocoder_param.vocoder.cyperus_params[0].in = vocoder_param.in;
  vocoder_param.vocoder.cyperus_params[0].freq = vocoder_param.vocoder.freq;
  vocoder_param.vocoder.cyperus_params[0].amp = vocoder_param.vocoder.amp;
  
  outsample = cyperus_vocoder(&(vocoder_param.vocoder.cyperus_params[0]),jack_samplerate,pos);
  
} /* dsp_vocoder */

/* MODULE OPERATIONS  -------------------- */
int
dsp_remove_module(int voice, int module_index) {
  int i;

  dsp_voice_removals[module_index] = dsp_voice_parameters[module_index];
  
  return 0;
  
} /* dsp_remove_module */

int
dsp_add_module(float(*dsp_module)(dsp_parameter,int,int), dsp_parameter dsp_param) {
  int i;
  for(i=0; i<16; i++)
    if( dsp_function[i] == 0 ) {
      dsp_function[i] = dsp_module;
      dsp_voice_parameters[i] = dsp_param;
      return 0;
    }
  return 1;
} /* dsp_add_module */

char*
param_to_module_name(dsp_parameter module) {
  switch( module.type ) {
  case DSP_NULL_PARAMETER_ID:
    module.null.name = "null";
    return module.null.name;
  case DSP_SINE_PARAMETER_ID:
    return module.sine.name;
  case DSP_SQUARE_PARAMETER_ID:
    return module.square.name;
  case DSP_PINKNOISE_PARAMETER_ID:
    return module.pinknoise.name;
  case DSP_BUTTERWORTH_BIQUAD_LOWPASS_PARAMETER_ID:
    return module.butterworth_biquad_lowpass.name;
  case DSP_DELAY_PARAMETER_ID:
    return module.delay.name;
  case DSP_VOCODER_PARAMETER_ID:
    return module.vocoder.name;
  default:
    module.null.name="null";
    return module.null.name;
  }
} /* param_to_module_name */

char*
dsp_list_modules(int voice_no) {
  char *module_list[16] = {NULL};
  char *current_module_name;
  int i, new_len;
  char *new_str = malloc(1);
  char *old_str = malloc(1);

  new_str[0] = '\0';
  old_str[0] = '\0';
  
  new_len = 0;
  for( i=0; i<16; i++) {
    if( strcmp((current_module_name=param_to_module_name(dsp_voice_parameters[i])),"null") ) {
      if( !strlen(new_str) ) {
	new_len = strlen(current_module_name) + 2; 
	new_str = realloc(new_str, new_len);
	new_str[0] = '\0';
	strcat(new_str,current_module_name);
	if( strcmp((current_module_name=param_to_module_name(dsp_voice_parameters[i+1])),"null") )
	  strcat(new_str,"&");
      } else {
	new_len += strlen(current_module_name) + 1;
	old_str = realloc(old_str,strlen(new_str));
	old_str[0] = '\0';
	strcpy(old_str,new_str);
	new_str = realloc(new_str,new_len);
	new_str[0] = '\0';
	strcpy(new_str,old_str);
	strcat(new_str,current_module_name);
	if( i<15 )
	  if( strcmp((current_module_name=param_to_module_name(dsp_voice_parameters[i+1])),"null") ) 
	    strcat(new_str,"&");
      }
    } else
      break;
  }
  free(old_str);
  return new_str;
} /* dsp_list_modules */  
  
int
dsp_create_sine(float freq, float amp) {
  dsp_parameter sine_param;
  sine_param.type = DSP_SINE_PARAMETER_ID;
  sine_param.pos = 0;
  sine_param.sine.name = "sine";
  sine_param.sine.cyperus_params = malloc(sizeof(struct cyperus_parameters));
  sine_param.sine.freq = freq;
  sine_param.sine.amp = amp;
  dsp_add_module(dsp_sine,sine_param);
  return 0;
} /* dsp_create_sine */

int
dsp_edit_sine(int module_no, float freq, float amp) {
  dsp_voice_parameters[module_no].sine.freq = freq;
  dsp_voice_parameters[module_no].sine.amp = amp;
  return 0;
} /* dsp_edit_sine */

int
dsp_create_square(float freq, float amp) {
  dsp_parameter square_param;
  square_param.type = DSP_SQUARE_PARAMETER_ID;
  square_param.pos = 0;
  square_param.square.name = "square";
  square_param.square.cyperus_params = malloc(sizeof(struct cyperus_parameters));
  square_param.square.freq = freq;
  square_param.square.amp = amp;
  dsp_add_module(dsp_square,square_param);
  return 0;
} /* dsp_create_square */

int
dsp_edit_square(int module_no, float freq, float amp) {
  dsp_voice_parameters[module_no].square.freq = freq;
  dsp_voice_parameters[module_no].square.amp = amp;
  return 0;
} /* dsp_edit_square */

int
dsp_create_pinknoise(void) {
  dsp_parameter pinknoise_param;
  pinknoise_param.type = DSP_PINKNOISE_PARAMETER_ID;
  pinknoise_param.pos = 0;
  pinknoise_param.pinknoise.name = "pinknoise";
  pinknoise_param.pinknoise.cyperus_params = malloc(sizeof(struct cyperus_parameters));
  dsp_add_module(dsp_pinknoise,pinknoise_param);
  return 0;
} /* dsp_create_pinknoise */

int
dsp_create_butterworth_biquad_lowpass(float cutoff, float res) {
  dsp_parameter filtr_param;
  filtr_param.type = DSP_BUTTERWORTH_BIQUAD_LOWPASS_PARAMETER_ID;
  filtr_param.pos = 0;
  filtr_param.butterworth_biquad_lowpass.name = "butterworth_biquad_lowpass";
  filtr_param.butterworth_biquad_lowpass.cyperus_params = malloc(sizeof(struct cyperus_parameters));
  filtr_param.butterworth_biquad_lowpass.cutoff = cutoff;
  filtr_param.butterworth_biquad_lowpass.res = res;
  dsp_add_module(dsp_butterworth_biquad_lowpass,filtr_param);
  return 0;
} /* dsp_create_butterworth_biquad_lowpass */

int
dsp_edit_butterworth_biquad_lowpass(int module_no, float cutoff, float res) {
  dsp_voice_parameters[module_no].butterworth_biquad_lowpass.cutoff = cutoff;
  dsp_voice_parameters[module_no].butterworth_biquad_lowpass.res = res;
  return 0;
} /* dsp_edit_butterworth_biquad_lowpass */

int
dsp_create_delay(float amt, float time, float feedback) {
  dsp_parameter delay_param;
  delay_param.type = DSP_DELAY_PARAMETER_ID;
  delay_param.pos = 0;
  delay_param.delay.name = "delay";
  delay_param.delay.cyperus_params = malloc(sizeof(struct cyperus_parameters));
  delay_param.delay.amt = amt;
  delay_param.delay.time = time * jack_sr;
  delay_param.delay.feedback = feedback;
  delay_param.delay.cyperus_params[0].signal_buffer = (float *)calloc(time * jack_sr * 30, sizeof(float));

  delay_param.delay.cyperus_params[0].pos = 0;
  delay_param.delay.cyperus_params[0].delay_pos = 0;
  
  dsp_add_module(dsp_delay,delay_param);
  return 0;
} /* dsp_create_delay*/

int
dsp_edit_delay(int module_no, float amt, float time, float feedback) {
  int i = 0;

  dsp_voice_parameters[module_no].delay.amt = amt;
  dsp_voice_parameters[module_no].delay.time = time * jack_sr;
  dsp_voice_parameters[module_no].delay.feedback = feedback;
  /*
  dsp_voice_parameters[module_no].delay.cyperus_params[0].pos = 0;
  dsp_voice_parameters[module_no].delay.cyperus_params[0].delay_pos = 0;
  */
  return 0;
} /* dsp_edit_delay */

int
dsp_create_vocoder(float freq, float amp) {
  dsp_parameter vocoder_param;
  vocoder_param.type = DSP_VOCODER_PARAMETER_ID;
  vocoder_param.pos = 0;
  vocoder_param.vocoder.name = "phase vocoder";
  vocoder_param.vocoder.cyperus_params = malloc(sizeof(struct cyperus_parameters));

  vocoder_param.vocoder.freq = freq;
  vocoder_param.vocoder.amp = amp;
  vocoder_param.vocoder.cyperus_params[0].freq = freq;
  vocoder_param.vocoder.cyperus_params[0].amp = amp;
  
  /* buffer size for fft should be 2x signal buffer size */
  vocoder_param.vocoder.cyperus_params[0].signal_buffer = (float *)calloc(4096, sizeof(float));
  vocoder_param.vocoder.cyperus_params[0].signal_buffer1 = (float *)calloc(20*4096, sizeof(float));

  memset(vocoder_param.vocoder.cyperus_params[0].signal_buffer1,0,sizeof(float)*20*4096);
  
  vocoder_param.vocoder.cyperus_params[0].pos = 0;
  vocoder_param.vocoder.cyperus_params[0].vocoder_pos = 0;
  
  dsp_add_module(dsp_vocoder,vocoder_param);
  return 0;
} /* dsp_create_vocoder */

int
dsp_edit_vocoder(int module_no, float freq, float amp) {
  int i = 0;

  /*
  dsp_voice_parameters[module_no].vocoder.cyperus_params[0].pos = 0;
  dsp_voice_parameters[module_no].vocoder.cyperus_params[0].vocoder_pos = 0;
  */

  dsp_voice_parameters[module_no].vocoder.freq = freq;
  dsp_voice_parameters[module_no].vocoder.amp = amp;
  dsp_voice_parameters[module_no].vocoder.cyperus_params[0].freq = freq;
  dsp_voice_parameters[module_no].vocoder.cyperus_params[0].amp = amp;
  
  return 0;
} /* dsp_edit_vocoder */

/* EXECUTION THREAD  -------------------- */

void *dsp_thread(void *arg) {
  int i,n,x,pos;
  float outsample = 0;

  /* first initialize module parameter array for each voice */
  dsp_parameter null_param;
  null_param.type = DSP_NULL_PARAMETER_ID;
  null_param.null.name = "null";
  for( i=0; i<16; i++) {
    dsp_voice_parameters[i] = null_param;
    dsp_voice_removals[i] = null_param;
  }
  while(1) {
    for(pos=0; pos< (jack_sr); pos++) {
      
      /* always provide external input to first sample in dsp function traversal
	   (in case we're starting signal chain w/ filter/effect instead of signal generator) */
      outsample = rtqueue_deq(fifo_in);

      /* as long as we don't have a null/assigned voice in front of the chain.. */
      if( dsp_voice_parameters[0].type != DSP_NULL_PARAMETER_ID ) {
	for(i=0; i<16; i++)
	  if(!bypass[i])
	    if( dsp_voice_parameters[i].type != DSP_NULL_PARAMETER_ID ) {
	      dsp_voice_parameters[i].in = outsample;
	      outsample = dsp_function[i](dsp_voice_parameters[i],jack_sr,pos);
	    }
	/* only enqueue new samples once we've iterated through the whole signal chain */
	rtqueue_enq(fifo_out,outsample);  
	
	/* deal with voices scheduled to remove */
	for(i=0; i<16; i++) {
	  if( dsp_voice_removals[i].type != DSP_NULL_PARAMETER_ID ) {
	    dsp_voice_parameters[i] = null_param;
	    dsp_voice_removals[i] = null_param;
	    
	    /* after removal, reorder remaining modules */
	    for(n=i; n<15; n++) {
	      dsp_function[n] = dsp_function[n+1];
	      dsp_voice_parameters[n] = dsp_voice_parameters[n+1];
	    }
	  }
	}
      }
    }
  }
} /* dsp_thread */

