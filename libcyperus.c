/* libcyperus.c
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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <fftw3.h>

#include "libcyperus.h"

#define VOCODERSIGNALSIZE 2048
#define VOCODERHOPSIZE 5
#define VOCODERFRAMESIZE 2048

float cyperus_sine(struct cyperus_parameters *sinewav, int jack_sr, int pos) {
  float outsample = 0.0;
  outsample = sin(sinewav->freq * (2 * M_PI) * pos / jack_sr);
  return outsample * sinewav->amp;
}

float cyperus_square(struct cyperus_parameters *squarewav, int jack_sr, int pos) {
  return sin(squarewav->freq * (2 * M_PI) * pos / jack_sr) >= 0 ? squarewav->amp : -1 * squarewav->amp;
}

float cyperus_triangle(struct cyperus_parameters *triwav, int jack_sr, int pos) {
  float period = jack_sr/triwav->freq;
  return 2 * triwav->amp / M_PI * asinf( sinf( (2 * M_PI / period) * pos ) );
}

float cyperus_whitenoise(struct cyperus_parameters *noiz, int jack_sr, int pos) {
  if( pos == jack_sr )
    srand(time(NULL));
  return (float)rand()/RAND_MAX;
}

const float A[] = { 0.02109238, 0.07113478, 0.68873558 }; // rescaled by (1+P)/(1-P)
const float P[] = { 0.3190, 0.7756, 0.9613 };

float cyperus_pinknoise(struct cyperus_parameters *noiz, int jack_sr, int pos) {
  /* by Evan Buswell */
  
  static const float RMI2 = 2.0 / ((float) RAND_MAX);
  static const float offset = 0.02109238 + 0.07113478 + 0.68873558;
  
  float temp = (float) random();
  noiz->state0 = P[0] * (noiz->state0 - temp) + temp;
  temp = (float) random();
  noiz->state1 = P[1] * (noiz->state1 - temp) + temp;
  temp = (float) random();
  noiz->state2 = P[2] * (noiz->state2 - temp) + temp;
  return ((A[0] * noiz->state0 + A[1] * noiz->state1 + A[2] * noiz->state2) * RMI2 - offset);
}

float cyperus_karlsen_lowpass(struct cyperus_parameters *filtr, int jack_sr, int pos) {
  /* by Ove Karlsen, 24dB 4-pole lowpass */

  /* need to scale the input cutoff to stable parameters frequencies */
  /* sweet spot for parameters->freq is up to0-0.7/8, in radians of a circle */
  
  float sampleout = 0.0;
  filtr->tempval = filtr->lastoutval3; if (filtr->tempval > 1) {filtr->tempval = 1;}
  filtr->in = (-filtr->tempval * filtr->res) + filtr->in;
  filtr->lastoutval = ((-filtr->lastoutval + filtr->in) * filtr->freq) + filtr->lastoutval;
  filtr->lastoutval1 = ((-filtr->lastoutval1 + filtr->lastoutval) * filtr->freq) + filtr->lastoutval1;
  filtr->lastoutval2 = ((-filtr->lastoutval2 + filtr->lastoutval1) * filtr->freq) + filtr->lastoutval2;
  filtr->lastoutval3 = ((-filtr->lastoutval3 + filtr->lastoutval2) * filtr->freq) + filtr->lastoutval3;
  sampleout = filtr->lastoutval3;
  
  return sampleout;
}

float cyperus_butterworth_biquad_lowpass(struct cyperus_parameters *filtr, int jack_sr, int pos) {
  /* by Patrice Tarrabia */

  /* need to scale inputs! */
  /* sweet spot for filtr->freq is 100-300(?)Hz, rez can be from
       sqrt(2) to ~0.1, filtr->freq 0Hz to samplerate/2  */
  
  float outsample = 0.0;
  float c = 1.0 / tan(M_PI * filtr->freq / jack_sr);
  float a1 = 1.0 / (1.0 + filtr->res * c + c * c);
  float a2 = 2 * a1;
  float a3 = a1;
  float b1 = 2.0 * (1.0 - c*c) * a1;
  float b2 = (1.0 - filtr->res * c + c * c) * a1;

  outsample = a1 * filtr->in + a2 * filtr->lastinval + a3 * filtr->lastinval1 - b1 * filtr->lastoutval - b2 * filtr->lastoutval1;
  
  filtr->lastoutval1 = filtr->lastoutval;
  filtr->lastoutval = outsample;
  filtr->lastinval1 = filtr->lastinval;
  filtr->lastinval = filtr->in;

  return outsample;
}

float cyperus_butterworth_biquad_hipass(struct cyperus_parameters *filtr, int jack_sr, int pos) {
  /* by Patrice Tarrabia */
  float outsample = 0.0;
  float c = tan(M_PI * filtr->freq / jack_sr);
  float a1 = 1.0 / (1.0 + filtr->res * c + c * c);
  float a2 = -2*a1;
  float a3 = a1;
  float b1 = 2.0 * ( c * c - 1.0) * a1;
  float b2 = ( 1.0 - filtr->res * c + c * c) * a1;
  
  outsample = a1 * filtr->in + a2 * filtr->lastinval + a3 * filtr->lastinval1 - b1 * filtr->lastoutval - b2 * filtr->lastoutval1;
  
  filtr->lastoutval1 = filtr->lastoutval;
  filtr->lastoutval = outsample;
  filtr->lastinval1 = filtr->lastinval;
  filtr->lastinval = filtr->in;

  return outsample;
}

float cyperus_moog_vcf(struct cyperus_parameters *filtr, int jack_sr, int pos) {
  /* by Stilson/Smith CCRMA paper, Timo Tossavainen */

  double f = filtr->freq * 1.16;
  double fb = filtr->res * (1.0 - 0.15 * f * f);
  
  filtr->in -= filtr->lastoutval3 * fb;
  filtr->in *= 0.35013 * (f*f) * (f*f);
  filtr->lastoutval = filtr->in + 0.3 * filtr->lastinval + (1 - f) * filtr->lastoutval;
  filtr->lastinval = filtr->in;
  filtr->lastoutval1 = filtr->lastoutval + 0.3 * filtr->lastinval1 + (1 - f) * filtr->lastoutval1;
  filtr->lastinval1 = filtr->lastoutval;
  filtr->lastoutval2 = filtr->lastoutval1 + 0.3 * filtr->lastinval2 + (1 - f) * filtr->lastoutval2;
  filtr->lastinval2 = filtr->lastoutval1;
  filtr->lastoutval3 = filtr->lastoutval2 + 0.3 * filtr->lastinval3 + (1 - f) * filtr->lastoutval3;
  filtr->lastinval3 = filtr->lastoutval2;
  return filtr->lastoutval3;
}

float cyperus_delay(struct cyperus_parameters *delay, int jack_sr, int pos) {
  float outsample = 0.0;

  if( delay->pos >= delay->delay_time )
    delay->pos = 0;

  delay->delay_pos = delay->pos - delay->delay_time;

  if( delay->delay_pos < 0 )
    delay->delay_pos += delay->delay_time;

  outsample = delay->signal_buffer[delay->pos] = delay->in + (delay->signal_buffer[delay->delay_pos] * delay->fb);
  delay->pos += 1;
  
  return outsample * delay->delay_amt;
}

float cyperus_vocoder(struct cyperus_parameters *vocoder, int jack_sr, int pos) {
  /* thanks to Jack Schaedler for fftw3 tutorial,
         http://ofdsp.blogspot.com/2011/08/short-time-fourier-transform-with-fftw3.html
     and special thanks to Kyung Ae Lim for paper and research on an open source phase vocoder
         http://music.informatics.indiana.edu/media/students/kyung/kyung_paper.pdf */

  float timestretch_ratio = 2.0;
  
  float outsample = 0.0;
  outsample = vocoder->in;
  
  vocoder->signal_buffer[vocoder->pos] = vocoder->in;
  
  fftw_init_threads();
  fftw_plan_with_nthreads(1);
  
  /* if we've filled the buffer, perform dsp calculations */
  if(vocoder->pos == VOCODERSIGNALSIZE-1) {    
    int fft_size = VOCODERSIGNALSIZE;
    int hop_size = VOCODERHOPSIZE;
    int frame_size = VOCODERFRAMESIZE;
    int total_frames = VOCODERSIGNALSIZE/VOCODERHOPSIZE;

    fftw_complex *transform_data,*inverse_data,*transform_result,*inverse_result,*data_out;
    fftw_plan plan_forward, plan_backward;

    int h,i;
    int fft_index = 0;
    int read_index = 0;
    int stop_windowing = 0;

    float magnitude[fft_size];
    float phase[fft_size];
    float old_phase[fft_size];
    float rotated_phase[fft_size];
    float new_phase[fft_size];
    float unwrapped_phase[fft_size];
    float temp_phase;
    int output_index = 0.0;

    /* create Hanning window */
    float window[frame_size];
    for(i=0; i<frame_size; i++)
      window[i] = 0.54-(0.46*cosf((2*M_PI*i)/frame_size));
    
    transform_data = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*fft_size);
    inverse_data = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*(fft_size));
    transform_result = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*(fft_size));
    inverse_result = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*(fft_size));

    for(i=0; i<fft_size; i++) {
      transform_data[i][0] = vocoder->signal_buffer[i];
      transform_data[i][1] = 0.0;
    }
    
    plan_forward = fftw_plan_dft_1d(fft_size,transform_data,transform_result,FFTW_FORWARD,FFTW_ESTIMATE);
    plan_backward = fftw_plan_dft_1d(fft_size,inverse_data,inverse_result,FFTW_BACKWARD,FFTW_ESTIMATE);
    
    fftw_execute(plan_forward);
    
    for(i=0; i<fft_size;i++)
      {
      magnitude[i] = sqrtf((transform_result[i][0]*transform_result[i][0])+(transform_result[i][1]*transform_result[i][1]));
      phase[i] = atan2f(transform_result[i][1],transform_result[i][0]);
    }
    
    for(i=0; i<fft_size; i++)
      {

      /* I think calculations/transformations go here? */
      new_phase[i] = phase[i];

      inverse_data[i][0] = magnitude[i]*cosf(new_phase[i]);
      inverse_data[i][1] = magnitude[i]*sinf(new_phase[i]);
    }
    
    fftw_execute(plan_backward);

    for(i=0; i<fft_size; i++)
      vocoder->signal_buffer1[i] = inverse_result[i][0]/fft_size;
    
    fftw_cleanup_threads();
    
    /* reset vocoder's sample counter */
    vocoder->pos = 0;
  } else {
    vocoder->pos++;
  }
  return vocoder->signal_buffer1[vocoder->pos];
}
