/* main.c
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
#include <jack/jack.h> 
#include <math.h>
#include "rtqueue.h"
#include "libcyperus.h"

static jack_default_audio_sample_t ** outs ;
static jack_default_audio_sample_t ** ins ;
int samples_can_process = 0;

rtqueue_t *fifo_in;
rtqueue_t *fifo_out;

jack_client_t *client=NULL;
pthread_t dspthreadid;
int jack_sr;
const size_t sample_size = sizeof (jack_default_audio_sample_t) ;

jack_port_t **output_port ;
static jack_port_t **input_port ;

static void*
dspthread(void *arg)
{
  int c;
  float outsample;

  struct cyperus_parameters sinwav;
  sinwav.freq = 440;
  sinwav.amp = 1.75;
  
  struct cyperus_parameters squarewav;
  squarewav.freq = 440;
  squarewav.amp = 1.0;

  struct cyperus_parameters triwav;
  triwav.freq = 440;
  triwav.amp = 1;

  struct cyperus_parameters noiz;
  noiz.amp=1;
  noiz.state0=0.0;
  noiz.state1=0.0;
  noiz.state2=0.0;

  struct cyperus_parameters filtr;
  filtr.freq = 100;
  filtr.res = 0.0;
  filtr.tempval = 0.0;
  filtr.lastinval = 0.0;
  filtr.lastinval1 = 0.0;
  filtr.lastinval2 = 0.0;
  filtr.lastinval3 = 0.0;
  filtr.lastoutval = 0.0;
  filtr.lastoutval1 = 0.0;
  filtr.lastoutval2 = 0.0;
  filtr.lastoutval3 = 0.0;
  
  fprintf(stderr,"starting test in 3 seconds...\n");
  sleep(3);
  
  srand(time(NULL));
  while(1) {
    fprintf(stderr,"sine wave..\n");
    for(c=0; c<jack_sr;  c++) {
      outsample = cyperus_sine(&sinwav,jack_sr,c);
      rtqueue_enq(fifo_out,outsample);
    }
    fprintf(stderr,"square wave..\n");
    for(c=0; c<jack_sr;  c++) {
      outsample = cyperus_square(&squarewav,jack_sr,c);
      rtqueue_enq(fifo_out,outsample);
    }
    fprintf(stderr,"triangle wave..\n");
    for(c=0; c<jack_sr;  c++) {
      outsample = cyperus_triangle(&triwav,jack_sr,c);
      rtqueue_enq(fifo_out,outsample);
    }
    fprintf(stderr,"white noise..\n");
    for(c=0; c<jack_sr;  c++) {
      outsample = cyperus_whitenoise(&noiz,jack_sr,c);
      rtqueue_enq(fifo_out,outsample);
    }
    fprintf(stderr,"pink noise..\n");
    for(c=0; c<jack_sr;  c++) {
      outsample = cyperus_pinknoise(&noiz,jack_sr,c);
      rtqueue_enq(fifo_out,outsample);
    }

    sinwav.freq = 100;
    fprintf(stderr,"butterworth biquad lowpass filter of a sine wave....\n");
    for(c=0; c<jack_sr*5;  c++) {
      filtr.in = cyperus_triangle(&sinwav,jack_sr,c);
      outsample = cyperus_butterworth_biquad_lowpass(&filtr,jack_sr,c);
      rtqueue_enq(fifo_out,outsample);

      if( c % (jack_sr / 2) == 0)
	sinwav.freq += 150;
      else if( c % (jack_sr) == 0)
	sinwav.freq = 150;
      
      if( c % (jack_sr / 8) == 0) {
	filtr.freq += 50;
	fprintf(stderr, "%f\n", filtr.freq);
	filtr.res += .01;
	filtr.tempval = 0.0;
	filtr.lastinval = 0.0;
	filtr.lastinval1 = 0.0;
	filtr.lastinval2 = 0.0;
	filtr.lastinval3 = 0.0;
	filtr.lastoutval = 0.0;
	filtr.lastoutval1 = 0.0;
	filtr.lastoutval2 = 0.0;
	filtr.lastoutval3 = 0.0;
      }
      
    }
    break;
  }
}
 
static int
process(jack_nframes_t nframes, void *arg)
{
  float sample = 0; 
  unsigned i, n, x; 
  int sample_count = 0;

  /* allocate all output buffers */
  for(i = 0; i < 1; i++)
    {
      outs [i] = jack_port_get_buffer (output_port[i], nframes);
      memset(outs[i], 0, nframes * sample_size);;
    }

  for ( i = 0; i < nframes; i++) {
    if( !rtqueue_isempty(fifo_out) ) {
      outs[0][i] = rtqueue_deq(fifo_out);
    }
    else {
      outs[0][i]=0;
    }
  }
  return 0 ;
} /* process */

static void
jack_shutdown (void *arg)
{
  (void) arg ;
  exit (1) ;
} /* jack_shutdown */

void
allocate_ports(int channels, int channels_in)
{
  int i = 0;
  char name [16];
  /* allocate output ports */
  output_port = calloc (channels, sizeof (jack_port_t *)) ;
  outs = calloc (channels, sizeof (jack_default_audio_sample_t *)) ;
  for (i = 0 ; i < channels; i++)
    {     
      snprintf (name, sizeof (name), "out_%d", i + 1) ;
      output_port [i] = jack_port_register (client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0) ;
    }
  
  /* allocate input ports */
  size_t in_size = channels_in * sizeof (jack_default_audio_sample_t*);
  input_port = (jack_port_t **) malloc (sizeof (jack_port_t *) * channels_in);
  ins = (jack_default_audio_sample_t **) malloc (in_size);
  memset(ins, 0, in_size);
  
  for( i = 0; i < channels_in; i++)
    {      
      snprintf( name, sizeof(name), "in_%d", i + 1);
      input_port[i] = jack_port_register(client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    }
} /* allocate_ports */

int
set_callbacks()
{
a  /* Set up callbacks. */
  jack_set_process_callback (client, process, NULL) ;
  jack_on_shutdown (client, jack_shutdown, 0) ;
  return 0;
} /* set_callbacks */

int
jack_setup(char *client_name)
{
  /* create jack client */
  if ((client = jack_client_open(client_name, JackNullOption,NULL)) == 0)
    {
      fprintf (stderr, "Jack server not running?\n") ;
      return 1 ;
    } ;

  /* store jack server's samplerate */
  jack_sr = jack_get_sample_rate (client) ;

  fifo_in = rtqueue_init(9999999);
  fifo_out = rtqueue_init(9999999);

  return 0;
} /* jack_setup */

int
activate_client()
{
  /* Activate client. */
  if (jack_activate (client))
    {	
      fprintf (stderr, "Cannot activate client.\n") ;
      return 1 ;
    }
  return 0;
} /* activate_client */

int main(void)
{
  jack_setup("cyperus");
  set_callbacks();
  if (activate_client() == 1)
    return 1;
  allocate_ports(1, 1);
  pthread_create(&dspthreadid, NULL, dspthread, 0);

  while(1) {sleep(1);};

  return 0;
}
