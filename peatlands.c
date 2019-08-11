/* peatlands.c
This file is a part of 'peatlands'
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

'peatlands' is a monome frontend for cyperus.
https://github.com/petrichorsystems/cyperus

Copyright 2019 murray foster */

#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //exit(0);
#include <math.h>
#include <lo/lo.h>
#include <monome.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>



/* current state of monome grid */
unsigned int grid[16][16] = { [0 ... 15][0 ... 15] = 0 };
#define MONOME_DEVICE "osc.udp://127.0.0.1:18629/monome"

/* these variables are for automagically hooking up monome */
char *monome_name;
char *monome_name_user_defined="init";
char *monome_device_type;
int monome_serialosc_port = 0;

lo_address lo_addr_send;

char *incoming_message = NULL;

/* MODULE MAX NO */
#define MAXMODULES 4

/* MODULE MAX NO OF PARAMETERS */
#define MAXMODULEPARAMS 4

/* MODULE MAX PARAMETER RESOLUTION (column slider length) */
#define MAXMODULEPARAMSRES 15

/* MODULE MAX PARAMETER RECORDING LENGTH */
#define MAXMODULEPARAMLENGTH = 150000 /* 150000 steps,10ms each, ~42min total time*/

float module_parameter[MAXMODULES][MAXMODULEPARAMS];
int module_parameter_led[2][MAXMODULES][MAXMODULEPARAMS]; /* [0][..][..] new led
							       [1][..][..] old led */
int module_bypass[MAXMODULES] = {0};
int module_bypass_led[MAXMODULES] = {0};

struct parameter_modulated
{
  int enable_record;
  int enable_envelope;

  /* envelope characteristics */
  int ceiling;
  int floor;
  int length; /* represented in 10ms steps */
  int position; /* position along 'length'  */
  float value; /* actual value of this parameter */
};

struct parameter_modulated modulated_parameters[MAXMODULES][MAXMODULEPARAMS];

/* how do we allow assignments of these in a meaningful way? */
/* DELAY MODULE */

float module_parameter_scale[MAXMODULES][MAXMODULEPARAMS][MAXMODULEPARAMSRES] = {
  { /* amount */
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0},
    /* time */
    {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.5, 2.0, 4.0, 6.0, 8.0},
    /* feedback */
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.94, 0.98},
    /* filler */
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0}
  },
  {
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0},
    {.01, .02, .03, .05, .08, .13, .21, .34, .55, .89, 1.44, 2.33, 3.77, 6.10, 9.87},
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.94, 0.98},
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0}
  },
  {
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0},
    {.01, .02, .05, .08, .12, .16, .24, .30, .47, .68, 1.2, 2.6, 5.7, 7.3, 10},
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.94, 0.98},
     {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0}
  },
  {
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0},
    {0.001, 0.005, 0.011, 0.019, 0.022, 0.031, 0.052, 0.067, 0.081, .016, .022, .043, .061, .083, .01},
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.94, 0.98},
    {0.06, 0.12, 0.19, 0.24, 0.3, 0.36, 0.43, 0.49, 0.55, 0.61, 0.77, 0.84, 0.9, 0.96, 1.0}
  }
};



void *
column_rampup(monome_t *monome, int column, int floor, int ceiling)
{
  int i=0;
  for(i=floor-1;i<=ceiling;i++)
    {
      monome_led_off(monome,column,i);
    }

  for(i=15-floor;i>=ceiling;i--)
    {
      monome_led_on(monome,column,i);
    }
}

void *
initialize_state_variables() {
  int i,c;

  for(i=0;i<MAXMODULES;i++)
    {
      module_bypass[i] = 0;
      module_bypass_led[i] = 0;

      for(c=0;c<MAXMODULEPARAMS;c++)
	{
	  modulated_parameters[i][c].enable_record = 0;
	  modulated_parameters[i][c].enable_envelope = 0;
	  modulated_parameters[i][c].ceiling = 0;
	  modulated_parameters[i][c].floor = 0;
	  modulated_parameters[i][c].length = 0.0;
	  modulated_parameters[i][c].position = 0;
	  modulated_parameters[i][c].value = module_parameter_scale[i][c][15 - modulated_parameters[i][c].floor - 1];

	  module_parameter_led[0][i][c] = 0;
	  module_parameter_led[1][i][c] = 0;
	  module_parameter[i][c] = 0.0;
	}
    }

  module_parameter[0][0] = 0.5;//module_parameter_delay_scale_amount[0];
  module_parameter[0][1] = 0.5;//module_parameter_delay_scale_time[0];
  module_parameter[0][2] = 0.5;//module_parameter_delay_scale_feedback[0];
  module_parameter[0][3] = 0.5;//module_parameter_delay_scale_filler[0];

  module_parameter[1][0] = 0.5;//module_parameter_delay_scale_amount[0];
  module_parameter[1][1] = 0.5;//module_parameter_delay_scale_time[0];
  module_parameter[1][2] = 0.5;//module_parameter_delay_scale_feedback[0];
  module_parameter[1][3] = 0.5;//module_parameter_delay_scale_filler[0];

  module_parameter[2][0] = 0.5;//module_parameter_delay_scale_amount[0];
  module_parameter[2][1] = 0.5;//module_parameter_delay_scale_time[0];
  module_parameter[2][2] = 0.5;//module_parameter_delay_scale_feedback[0];
  module_parameter[2][3] = 0.5;//module_parameter_delay_scale_filler[0];

  module_parameter[3][0] = 0.5;//module_parameter_delay_scale_amount[0];
  module_parameter[3][1] = 0.5;//module_parameter_delay_scale_time[0];
  module_parameter[3][2] = 0.5;//module_parameter_delay_scale_feedback[0];
  module_parameter[3][3] = 0.5;//module_parameter_delay_scale_filler[0];
}


int
monome_edit_delay_handler(int module_no)
{
  float amt=module_parameter[module_no][0];
  float time=module_parameter[module_no][1];
  float feedback=module_parameter[module_no][2];
  float filler=module_parameter[module_no][3];

  // printf("sending /cyperus/edit/module/delay %s %s ... \n", bus_port_out_path, main_out_0);
  // lo_send(lo_addr_send, "/cyperus/add/connection", "ss", bus_port_out_path, main_out_0);
  printf("sent.\n");

  /* dsp_edit_delay(module_no,amt,time,feedback); */

  return 0;
} /* monome_edit_delay_handler */



void *
clock_manager(void *arg) {
  int i,c,k;
  int msdelay = 10000;
  int envelope_peak = 0;
  int scale_difference;
  int current_led_position = 0;
  float step_length = 0.0;


  float step_time_scaled = 0.0;
  float step_value = 0.0;
  int current_step = 0;

  do {
    for(i=0; i<MAXMODULES; i++)
      {
	for(c=0; c<MAXMODULEPARAMS; c++)
	  {
	    if( modulated_parameters[i][c].enable_record ) {
	      modulated_parameters[i][c].length = modulated_parameters[i][c].length + 1;;
	    }
	
	    if( modulated_parameters[i][c].enable_envelope )
	      {
		/* reset counter */
		if( modulated_parameters[i][c].position > modulated_parameters[i][c].length ) {
		  modulated_parameters[i][c].position = 1;
		  modulated_parameters[i][c].value = module_parameter_scale[i][c][15 - modulated_parameters[i][c].floor - 1];
		}
		
		envelope_peak = modulated_parameters[i][c].length / 2;
		scale_difference = modulated_parameters[i][c].ceiling - modulated_parameters[i][c].floor;
		step_length = fabs((float)envelope_peak / (float)scale_difference);

		/* find current LED position based on parameter location
		   first, if we're on the attack section of the attack/decay env */
		if( modulated_parameters[i][c].position < envelope_peak )
		  {
		    current_led_position = modulated_parameters[i][c].floor -
		      (modulated_parameters[i][c].position / step_length) + 1;
		  }
		else
		  {
		    current_led_position = modulated_parameters[i][c].floor -
		      ((modulated_parameters[i][c].length - modulated_parameters[i][c].position) / step_length);
		  }

		/* led position is relative to the coordinates, so increasing a column's height is actually
		   subtracting from the led position and vice versa */
		if( module_parameter_led[0][i][c] < abs(modulated_parameters[i][c].ceiling) )
		  module_parameter_led[0][i][c] = modulated_parameters[i][c].ceiling;
		
		module_parameter_led[0][i][c] = abs(current_led_position);

		/* smoothly transitions between parameters whose values are defined in module_parameter_scale,
		 keep in mind if the position is over the envelope_peak, we're reversing the direction of
		 travel/smoothing between a parameter's value */
		if( modulated_parameters[i][c].position > envelope_peak )
		  {
		    if( (15 - module_parameter_led[0][i][c] - 1) < 0)
		      step_value = module_parameter_scale[i][c][0];
		    else
		      {
			step_value = module_parameter_scale[i][c][15 - module_parameter_led[0][i][c] - 1];
			step_value -= (module_parameter_scale[i][c][15 - module_parameter_led[0][i][c] - 1] -
				       module_parameter_scale[i][c][15 - module_parameter_led[0][i][c] - 2])  *
			  ((modulated_parameters[i][c].length - modulated_parameters[i][c].position) / envelope_peak);
		      }
		  }
		else
		  {
		    if( (15 - module_parameter_led[0][i][c] - 1) > 14)
		      step_value = module_parameter_scale[i][c][14];
		    else
		      {
			step_value = module_parameter_scale[i][c][15 - module_parameter_led[0][i][c] - 1];
			step_value += (module_parameter_scale[i][c][15 - module_parameter_led[0][i][c]] -
				       module_parameter_scale[i][c][15 - module_parameter_led[0][i][c] - 1])  *
			  (modulated_parameters[i][c].position / envelope_peak);
		      }
		  }

		/* 1. set value of the modulating parameter for this iteration,
		   2. set actual value of dsp module's parameter,
		   3. tell dsp engine to edit parameter,
		   4. tentatively update our modulation's position for this parameter */
		modulated_parameters[i][c].value = step_value;
		module_parameter[i][c] = modulated_parameters[i][c].value;
		monome_edit_delay_handler(i);
		modulated_parameters[i][c].position += 1;
	      }
	  }
      }
    usleep(msdelay);
  } while(1);
} /* clock_manager */

void
setup_cyperus_modules_delay() {
  char *mains_str;
  char *main_in_0, *main_out_0;
  char *bus_id;
  char *bus_ports;
  char *bus_port_in, *bus_port_out;
  char *bus_port_in_path, *bus_port_out_path;
  char *bus_path = NULL;
  char *delay_id = NULL;
  char *module_path = NULL;
  char *module_ports = NULL;
  char *module_port_in, *module_port_out;
  char *module_port_in_path, *module_port_out_path;

  float amt = 1.0;
  float time = 0.75;
  float feedback = 0.5;

  int count;

  printf("sending /cyperus/list/main ...\n");
  lo_send(lo_addr_send, "/cyperus/list/main", NULL);
  printf("sent /cyperus/list/main\n");
  while(incoming_message == NULL)
    usleep(100);
  printf("about to strlen()\n");
  printf("strlen(incoming_message): %d\n", strlen(incoming_message));
  printf("post-sleep\n");
  mains_str = malloc(sizeof(char) * (strlen(incoming_message) + 1));
  printf("allocating for mains_str\n");
  strcpy(mains_str, incoming_message);
  printf("strcpy(mains_str, incoming_message)\n");
  free(incoming_message);
  printf("free() incoming_message\n");
  incoming_message = NULL;

  printf("process list/main done.\n");
  
  int out_pos;
  char *subptr = malloc(sizeof(char) * (strlen(mains_str) + 1));

  main_in_0 = malloc(sizeof(char) * 44);
  for(count=4; count<47; count++) {
    main_in_0[count - 4] = mains_str[count];
  }

  main_out_0 = malloc(sizeof(char) * 44);
  subptr = strstr(mains_str, "out:");
  out_pos = subptr - mains_str;
  for(count=out_pos + 5; count<out_pos + 44 + 4; count++) {
    main_out_0[count - 5 - out_pos] = mains_str[count];
  }
  
  printf("sending /cyperus/add/bus / main0 in out ... \n");
  lo_send(lo_addr_send, "/cyperus/add/bus", "ssss", "/", "main0", "in", "out");
  printf("sent.\n");

  printf("sending /cyperus/list/bus / 1 ... \n");
  lo_send(lo_addr_send, "/cyperus/list/bus", "si", "/", 1);
  printf("sent.\n");
  usleep(500);
  bus_id = malloc(sizeof(char) * (strlen(incoming_message) + 1));
  strcpy(bus_id, incoming_message);
  free(incoming_message);
  incoming_message = NULL;

  bus_path = malloc(sizeof(char) * 38);
  bus_path[0] = '/';
  for(count=0; count < 37; count++)
    bus_path[count+1] = bus_id[count];
  bus_path[count] = '\0';

  printf("sending /cyperus/list/bus_port %s ... \n", bus_path);
  lo_send(lo_addr_send, "/cyperus/list/bus_port", "s", bus_path);
  printf("sent.\n");
  usleep(500);
  bus_ports = malloc(sizeof(char) * (strlen(incoming_message) + 1));
  strcpy(bus_ports, incoming_message);
  free(incoming_message);
  incoming_message = NULL;
  
  bus_port_in = malloc(sizeof(char) * 37);
  for(count=4; count<40; count++) {
    bus_port_in[count - 4] = bus_ports[count];
  }

  subptr = malloc(sizeof(char) * (strlen(bus_ports) + 1));
  subptr = strstr(bus_ports, "out:");
  out_pos = subptr - bus_ports;
  bus_port_out = malloc(sizeof(char) * 37);
  for(count=out_pos+5; count<out_pos+36+5; count++) {
    bus_port_out[count - 5 - out_pos] = bus_ports[count];
  }

  bus_port_in_path = malloc(sizeof(char) * (36 * 2 + 2));
  bus_port_out_path = malloc(sizeof(char) * (36 * 2 + 2));

  strcpy(bus_port_in_path, bus_path);
  strcat(bus_port_in_path, ":");
  strcat(bus_port_in_path, bus_port_in);
  strcpy(bus_port_out_path, bus_path);
  strcat(bus_port_out_path, ":");
  strcat(bus_port_out_path, bus_port_out);

  printf("sending /cyperus/add/module/delay %s %f %f %f ... \n", bus_path, amt, time, feedback);
  lo_send(lo_addr_send, "/cyperus/add/module/delay", "sfff", bus_path, amt, time, feedback);
  printf("sent.\n");
  sleep(1);
  delay_id = malloc(sizeof(char) * (strlen(incoming_message) + 1));
  strcpy(delay_id, incoming_message);
  free(incoming_message);
  incoming_message = NULL;
  
  printf("delay_id: %s\n", delay_id);

  module_path = malloc(sizeof(char) * (strlen(bus_path) + 38));
  strcpy(module_path, bus_path);
  strcat(module_path, "?");
  strcat(module_path, delay_id);

  printf("sending /cyperus/list/module_port %s ... \n", module_path);
  lo_send(lo_addr_send, "/cyperus/list/module_port", "s", module_path);
  printf("sent.\n");
  while( incoming_message == NULL )
    usleep(500);
  printf("got module_ports\n");
  printf("incoming_message: %s\n", incoming_message);
  module_ports = malloc(sizeof(char) * (strlen(incoming_message) + 1));
  strcpy(module_ports, incoming_message);

  printf("module_ports: %s\n", module_ports);
  /* add delay and associated ports */

  module_port_in = malloc(sizeof(char) * 37);
  for(count=4; count<40; count++) {
    module_port_in[count - 4] = module_ports[count];
  }

  subptr = malloc(sizeof(char) * (strlen(module_ports) + 1));
  subptr = strstr(module_ports, "out:");
  out_pos = subptr - module_ports;
  module_port_out = malloc(sizeof(char) * 37);
  for(count=out_pos+5; count<out_pos+36+5; count++) {
    module_port_out[count - 5 - out_pos] = module_ports[count];
  }

  module_port_in_path = malloc(sizeof(char) * (36 * 3 + 3));
  module_port_out_path = malloc(sizeof(char) * (36 * 3 + 3));

  strcpy(module_port_in_path, module_path);
  strcat(module_port_in_path, "<");
  strcat(module_port_in_path, module_port_in);
  strcpy(module_port_out_path, module_path);
  strcat(module_port_out_path, ">");
  strcat(module_port_out_path, module_port_out);

  printf("module_port_in_path: %s\n", module_port_in_path);
  printf("module_port_out_path: %s\n", module_port_out_path);


  printf("sending /cyperus/add/connection %s %s ... \n", main_in_0, bus_port_in_path);
  lo_send(lo_addr_send, "/cyperus/add/connection", "ss", main_in_0, bus_port_in_path);
  printf("sent.\n");

  printf("sending /cyperus/add/connection %s %s ... \n", bus_port_in_path, module_port_in_path);
  lo_send(lo_addr_send, "/cyperus/add/connection", "ss", bus_port_in_path, module_port_in_path);
  printf("sent.\n");

  printf("sending /cyperus/add/connection %s %s ... \n", module_port_out_path, bus_port_out_path);
  lo_send(lo_addr_send, "/cyperus/add/connection", "ss", module_port_out_path, bus_port_out_path);
  printf("sent.\n");

  printf("sending /cyperus/add/connection %s %s ... \n", bus_port_out_path, main_out_0);
  lo_send(lo_addr_send, "/cyperus/add/connection", "ss", bus_port_out_path, main_out_0);
  printf("sent.\n");

  free(bus_id);
  free(bus_path);
  free(bus_ports);
  free(bus_port_in);
  free(bus_port_out);
  free(bus_port_in_path);
  free(bus_port_out_path);
  free(main_in_0);
  free(main_out_0);
  free(mains_str);

} /* setup_modules_delay */

void *
state_manager(void *arg) {

  setup_cyperus_modules_delay();

  initialize_state_variables();

  struct monome_t *monome = arg;
  int i,c;
  do {
    for(i=0;i<MAXMODULES;i++) {
      if(module_bypass[i]!=module_bypass_led[i]) {
	for(c=0;c<4;c++)
	  if(module_bypass[i])
	    monome_led_on(monome,i*4+c,15);
	  else
	    monome_led_off(monome,i*4+c,15);
	module_bypass_led[i]=module_bypass[i];
      }
      for(c=0;c<MAXMODULEPARAMS;c++)
	if(module_parameter_led[0][i][c] &&
	   (module_parameter_led[0][i][c]!=module_parameter_led[1][i][c])) {
	  module_parameter_led[1][i][c]=module_parameter_led[0][i][c];
	  column_rampup(monome,i*4+c,1,module_parameter_led[0][i][c]-1);
	}
    }
    usleep(9999);
  } while(1);
}

void *
monome_thread(void *arg)
{
  struct monome_t *monome = arg;
  monome_event_loop(monome);
} /* monome_thread */

void
update_modulation_state(int module_no, int param_no, int y)
{

  fprintf(stderr,"update modulation state module_no %d param_no %d y %d\n",module_no, param_no, y);

  if (!modulated_parameters[module_no][param_no].enable_record &&
      !modulated_parameters[module_no][param_no].enable_envelope)
    {
      fprintf(stderr,"enable record\n");
      modulated_parameters[module_no][param_no].position = 0;
      modulated_parameters[module_no][param_no].length = 0;
      modulated_parameters[module_no][param_no].floor = 15-y-1;
      modulated_parameters[module_no][param_no].enable_record = 1;
    }
  else if (modulated_parameters[module_no][param_no].enable_envelope)
    {
      fprintf(stderr, "stop envelope\n");
      modulated_parameters[module_no][param_no].enable_envelope = 0;
    }
  else if (modulated_parameters[module_no][param_no].enable_record)
    {
      fprintf(stderr,"stop record, begin envelope\n");
      modulated_parameters[module_no][param_no].enable_record = 0;
      modulated_parameters[module_no][param_no].ceiling = 15-y-1;
      modulated_parameters[module_no][param_no].position = 1;
      modulated_parameters[module_no][param_no].enable_envelope = 1;
    }
} /* update_modulation_state */

void
handle_press(const monome_event_t *e, void *data)
{
  unsigned int x, y, i, x2, y2, button, c, edit_module;

  x = e->grid.x;
  y = e->grid.y;

  /* store monome state change */
  grid[x][y]=1;

  edit_module = 0;

  if(x>=0&&x<4&&y==15) {
    module_bypass[0]=!module_bypass[0];
    // dsp_bypass(0,module_bypass[0]);
  } else if(x>3&&x<8&&y==15) {
    module_bypass[1]=!module_bypass[1];
    // dsp_bypass(1,module_bypass[1]);
  } else if(x>7&&x<12&&y==15) {
    module_bypass[2]=!module_bypass[2];
    // dsp_bypass(2,module_bypass[2]);
  } else if(x>11&&x<16&&y==15) {
    module_bypass[3]=!module_bypass[3];
    // dsp_bypass(3,module_bypass[3]);
  }

  if(x>=0&&x<4&&y<15) {
    if(module_parameter_led[0][0][x]!=y+1) { /* offset y by 1 so zero-indexes don't
					 logically translate to false */
      module_parameter_led[0][0][x]=y+1;
      edit_module=1; /* flag module to-edit */
    }

    switch(x) {
    case 0:
      /* invert column's led position,offset by 1, translate value */
      update_modulation_state(0,0,15-y-1);
      if(edit_module)
        module_parameter[0][0]=module_parameter_scale[0][0][15-y-1];
      break;
    case 1:
      update_modulation_state(0,1,15-y-1);
      if(edit_module)
        module_parameter[0][1]=module_parameter_scale[0][1][15-y-1];
      break;
    case 2:
      update_modulation_state(0,2,15-y-1);
      if(edit_module)
        module_parameter[0][2]=module_parameter_scale[0][2][15-y-1];
      break;
    case 3:
      update_modulation_state(0,3,15-y-1);
      if(edit_module)
        module_parameter[0][3]=module_parameter_scale[0][3][15-y-1];
      break;
    default:
      break;
    }
    if(edit_module)
      monome_edit_delay_handler(0);

  } else if(x>3&&x<8&&y<15) {
    if(module_parameter_led[0][1][x-4]!=y+1) {
      module_parameter_led[0][1][x-4]=y+1;
      edit_module=1;
    }

    x -= 4;

    /* delay module, dsp module no 1 */
    switch(x) {
    case 0:
      /* invert column's led position,offset by 1, translate value */
      update_modulation_state(1,0,15-y-1);
      if(edit_module)
	module_parameter[1][0]=module_parameter_scale[1][0][15-y-1];
      break;
    case 1:
      update_modulation_state(1,1,15-y-1);
      if(edit_module)
	module_parameter[1][1]=module_parameter_scale[1][1][15-y-1];
      break;
    case 2:
      update_modulation_state(1,2,15-y-1);
      if(edit_module)
	module_parameter[1][2]=module_parameter_scale[1][2][15-y-1];
      break;
    case 3:
      update_modulation_state(1,3,15-y-1);
      if(edit_module)
	module_parameter[1][3]=module_parameter_scale[1][3][15-y-1];
      break;
    default:
      break;
    }
    if(edit_module)
      monome_edit_delay_handler(1);
  } else if(x>7&&x<12&&y<15) {
    if(module_parameter_led[0][2][x-8]!=y+1) {
      module_parameter_led[0][2][x-8]=y+1;
      edit_module=1;
    }

    x -= 8;

    /* delay module, dsp module no 2 */
    switch(x) {
    case 0:
      /* invert column's led position,offset by 1, translate value */
      update_modulation_state(2,0,15-y-1);
      if(edit_module)
	module_parameter[2][0]=module_parameter_scale[2][0][15-y-1];
      break;
    case 1:
      update_modulation_state(2,1,15-y-1);
      if(edit_module)
	module_parameter[2][1]=module_parameter_scale[2][1][15-y-1];
      break;
    case 2:
      update_modulation_state(2,2,15-y-1);
      if(edit_module)
	module_parameter[2][2]=module_parameter_scale[2][2][15-y-1];
      break;
    case 3:
      update_modulation_state(2,3,15-y-1);
      if(edit_module)
	module_parameter[2][3]=module_parameter_scale[2][3][15-y-1];
      break;
    default:
      break;
    }
    if(edit_module)
      monome_edit_delay_handler(2);
  } else if(x>11&&x<16&&y<15) {
    if(module_parameter_led[0][3][x-12]!=y+1) {
      module_parameter_led[0][3][x-12]=y+1;
      edit_module=1;
    }

    x -= 12;

    /* delay module, dsp module no 3 */
    switch(x) {
    case 0:
      /* invert column's led position,offset by 1, translate value */
      update_modulation_state(3,0,15-y-1);
      if(edit_module)
	module_parameter[3][0]=module_parameter_scale[3][0][15-y-1];
      break;
    case 1:
      update_modulation_state(3,1,15-y-1);
      if(edit_module)
	module_parameter[3][1]=module_parameter_scale[3][1][15-y-1];
      break;
    case 2:
      update_modulation_state(3,2,15-y-1);
      if(edit_module)
	module_parameter[3][2]=module_parameter_scale[3][2][15-y-1];
      break;
    case 3:
      update_modulation_state(3,3,15-y-1);
      if(edit_module)
	module_parameter[3][3]=module_parameter_scale[3][3][15-y-1];
      break;
    default:
      break;
    }
    if(edit_module)
      monome_edit_delay_handler(3);
  }

} /* handle_press */

void
handle_lift(const monome_event_t *e, void *data)
{

  unsigned int x, y, x2;

  x = e->grid.x;
  y = e->grid.y;

  x2 = x - 8;

  /* store monome state change */
  grid[x][y]=0;


  /* disable record state for parameter modulation */
  if(x>=0&&x<4&&y<15)
    {
      fprintf(stderr,"handle lift on module_no %d param_no %d\n",0,x);
      fprintf(stderr,"record off\n");
      modulated_parameters[0][x].enable_record = 0;
    }
  else if(x>3&&x<8&&y<15)
    {
      x -= 4;
      fprintf(stderr,"handle lift on module_no %d param_no %d\n",1,x);
      fprintf(stderr,"record off\n");
      modulated_parameters[1][x].enable_record = 0;
    }
  else if(x>7&&x<12&&y<15)
    {
      x -= 8;
      fprintf(stderr,"handle lift on module_no %d param_no %d\n",2,x);
      fprintf(stderr,"record off\n");
      modulated_parameters[2][x].enable_record = 0;
    }
  else if(x>11&&x<16&&y<15)
    {
      x -= 12;
      fprintf(stderr,"handle lift on module_no %d param_no %d\n",3,x);
      fprintf(stderr,"record off\n");
      modulated_parameters[3][x].enable_record = 0;
    }

} /* handle_lift */


void error(int num, const char *msg, const char *path)
{
  printf("liblo server error %d in path %s: %s\n", num, path, msg);
  fflush(stdout);
}

/* catch any incoming messages and display them. returning 1 means that the
 * message has not been fully handled and the server should try other methods */
int generic_handler(const char *path, const char *types, lo_arg ** argv,
                    int argc, void *data, void *user_data)
{
  int i;

  fprintf(stdout,"path: <%s>\n", path);
  for (i = 0; i < argc; i++) {
    fprintf(stderr,"arg %d '%c' ", i, types[i]);
    lo_arg_pp((lo_type)types[i], argv[i]);
    fprintf(stdout,"\n");
  }
  return 0;
}

int osc_list_main_handler(const char *path, const char *types, lo_arg **argv,
			   int argc, void *data, void *user_data)
{
  char *msg_str = argv[0];
  incoming_message = malloc(sizeof(char) * (strlen(msg_str) + 1));
  strcpy(incoming_message, msg_str);
  return 0;
} /* osc_list_main_handler */


int osc_list_single_bus_handler(const char *path, const char *types, lo_arg **argv,
			 int argc, void *data, void *user_data)
{
  char *bus_path = argv[0];
  int list_type = argv[1]->i;
  int more = argv[2]->i;
  char *result_str = argv[3];
  incoming_message = malloc(sizeof(char) * (strlen(result_str) + 1));
  strcpy(incoming_message, result_str);
  return 0;
} /* osc_list_single_bus_handler */


int osc_list_bus_port_handler(const char *path, const char *types, lo_arg **argv,
				     int argc, void *data, void *user_data)
{
  char *bus_port_path_str = argv[0];
  char *result_str = argv[1];
  incoming_message = malloc(sizeof(char) * (strlen(result_str) + 1));
  strcpy(incoming_message, result_str);
  return 0;
} /* osc_list_bus_port_handler */


int osc_add_module_delay_handler(const char *path, const char *types, lo_arg **argv,
				    int argc, void *data, void *user_data)
{
  char *delay_id = argv[0];
  incoming_message = malloc(sizeof(char) * (strlen(delay_id) + 1));
  strcpy(incoming_message, delay_id);
  return 0;
} /* osc_add_module_delay_handler */


int osc_list_module_port_handler(const char *path, const char *types, lo_arg **argv,
				 int argc, void *data, void *user_data)
{
  char *module_path_str = argv[0];
  char *result_str = argv[1];

  printf("osc_lust_module_port_handler(): path(): %s\n", path);
  
  incoming_message = malloc(sizeof(char) * (strlen(result_str) + 1));
  strcpy(incoming_message, result_str);
  return 0;
} /* osc_list_module_port_handler */

int osc_edit_module_delay_handler(const char *path, const char *types, lo_arg **argv,
				    int argc, void *data, void *user_data)
{
  char *delay_id = argv[0];
  incoming_message = malloc(sizeof(char) * (strlen(delay_id) + 1));
  strcpy(incoming_message, delay_id);
  return 0;
} /* osc_edit_module_delay_handler */

void print_usage() {
  printf("Usage: peatlands [options] [arg]\n\n");
  printf("Options:\n"
	 " -h,  --help          displays this menu\n"
	 " -i,  --input         input channels.  default: 8\n"
	 " -o,  --output        output channels. default: 8\n"
	 " -b,  --bitdepth      set bitdepth of capture to 8, 16, 24, 32, 64, or 128. default: 24\n"
	 " -p,  --port          set osc interface receiving port. default: 97211\n"
	 " -sp, --send-port     set osc interface sending port. default: 97217\n"
	 " -f,  --file          set path of session file to load preexisting sounds.\n"
	 " -fi, --fifo-size     set fifo buffer size for each channel. default: 2048\n\n"
	 "documentation available soon\n\n");
} /* print_usage */

void print_header(void) {
  printf("\n\n"
	 "welcome to the\n"
         "██▓███  ▓█████ ▄▄▄     ▄▄▄█████▓ ██▓    ▄▄▄       ███▄    █ ▓█████▄   ██████  \n"
         "▓██░  ██▒▓█   ▀▒████▄   ▓  ██▒ ▓▒▓██▒   ▒████▄     ██ ▀█   █ ▒██▀ ██▌▒██    ▒ \n"
         "▓██░ ██▓▒▒███  ▒██  ▀█▄ ▒ ▓██░ ▒░▒██░   ▒██  ▀█▄  ▓██  ▀█ ██▒░██   █▌░ ▓██▄   \n"
         "▒██▄█▓▒ ▒▒▓█  ▄░██▄▄▄▄██░ ▓██▓ ░ ▒██░   ░██▄▄▄▄██ ▓██▒  ▐▌██▒░▓█▄   ▌  ▒   ██▒\n"
         "▒██▒ ░  ░░▒████▒▓█   ▓██▒ ▒██▒ ░ ░██████▒▓█   ▓██▒▒██░   ▓██░░▒████▓ ▒██████▒▒\n"
         "▒▓▒░ ░  ░░░ ▒░ ░▒▒   ▓▒█░ ▒ ░░   ░ ▒░▓  ░▒▒   ▓▒█░░ ▒░   ▒ ▒  ▒▒▓  ▒ ▒ ▒▓▒ ▒ ░\n"
         "░▒ ░      ░ ░  ░ ▒   ▒▒ ░   ░    ░ ░ ▒  ░ ▒   ▒▒ ░░ ░░   ░ ▒░ ░ ▒  ▒ ░ ░▒  ░ ░\n"
         "░░          ░    ░   ▒    ░        ░ ░    ░   ▒      ░   ░ ░  ░ ░  ░ ░  ░  ░  \n"
         "            ░  ░     ░  ░            ░  ░     ░  ░         ░    ░          ░  \n"
          "\t\ta monome frontend for audio processing via cyperus\n\n\n");
} /* print_header */


int main(int argc, char *argv[])
{
  struct monome_t *monome = NULL;

  char *osc_port_in = "97217";
  char *osc_port_out = "97211";

  char *store_flag = NULL;
  char *store_input = NULL;  
  char *cyperus_cmd = NULL;

  lo_server_thread st = lo_server_thread_new(osc_port_in, error);

  int c;
  int exit_key;
  
  lo_addr_send = lo_address_new("127.0.0.1",osc_port_out);

  if( argc > 1 )
    if( !strcmp(argv[1], "-h") ||
	!strcmp(argv[1], "--help") ) {
      printf("welcome to the cyperus realtime music system\n\n");
      print_usage();
      exit(0);
    }

  print_header();
  
  /* process command-line input */
  for(c=1; c<argc; c++)
    {
      store_flag = argv[c];
      if( store_flag != NULL )
	{
	  if( !strcmp(store_flag,"-cy") ||
	      !strcmp(store_flag,"--cyperus-path")) {
	    store_input=argv[c+1];
	    cyperus_cmd=store_input;
	  }
          
	  /* reset temporarily stored flag&input */
	  store_input=NULL;
	  store_flag=NULL;
	}
    }

  if( cyperus_cmd == NULL )
    cyperus_cmd="cyperus -i 4 -o 4 &";
  
  if( osc_port_in == NULL )
    osc_port_in="97211";

  if( osc_port_out == NULL )
    osc_port_out="97217";

  printf("cyperus-cmd: '%s'\n\n\n", cyperus_cmd);

  system(cyperus_cmd);
  usleep(9999);
  
  printf("launched\n");
  
  /* non-generic methods */
  lo_server_thread_add_method(st, "/cyperus/list/main", "s", osc_list_main_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/list/bus", "siis", osc_list_single_bus_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/list/bus_port", "ss", osc_list_bus_port_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/list/module_port", "ss", osc_list_module_port_handler, NULL);

  lo_server_thread_add_method(st, "/cyperus/add/module/delay", "sfff", osc_add_module_delay_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/edit/module/delay", "sfff", osc_edit_module_delay_handler, NULL);

  lo_server_thread_start(st);

  printf("osc started\n");
  
  char monome_device_addr[128] = MONOME_DEVICE;

  monome = monome_open(MONOME_DEVICE, "8002");

  /* clear monome LEDs */
  monome_led_all(monome, 0);

  /* register our button presses callback for triggering events
   and maintaining state */
  monome_register_handler(monome, MONOME_BUTTON_DOWN, handle_press, NULL);
  monome_register_handler(monome, MONOME_BUTTON_UP, handle_lift, NULL);

  pthread_t monome_thread_id;
  pthread_create(&monome_thread_id, NULL, monome_thread, monome);
  pthread_detach(monome_thread_id);

  /* begin 'state manager' thread */
  pthread_t state_thread_id;
  pthread_t clock_thread_id;
  pthread_create(&state_thread_id, NULL, state_manager, monome);
  pthread_create(&clock_thread_id, NULL, clock_manager, NULL);
  pthread_detach(state_thread_id);

  while(1){sleep(1);};

  return 0;
}
