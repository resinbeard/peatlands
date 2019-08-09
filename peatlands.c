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
#include <signal.h>

/* current state of monome grid */
unsigned int grid[16][16] = { [0 ... 15][0 ... 15] = 0 };
#define MONOME_DEVICE "osc.udp://127.0.0.1:18629/monome"

/* these variables are for automagically hooking up monome */
char *monome_name;
char *monome_name_user_defined="init";
char *monome_device_type;
int monome_serialosc_port = 0;

lo_address lo_addr_send;

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

void *
state_manager(void *arg)
{
  monome_add_delay_handler();
  monome_add_delay_handler();
  monome_add_delay_handler();
  monome_add_delay_handler();
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
    dsp_bypass(0,module_bypass[0]);
  } else if(x>3&&x<8&&y==15) {
    module_bypass[1]=!module_bypass[1];
    dsp_bypass(1,module_bypass[1]);
  } else if(x>7&&x<12&&y==15) {
    module_bypass[2]=!module_bypass[2];
    dsp_bypass(2,module_bypass[2]);
  } else if(x>11&&x<16&&y==15) {
    module_bypass[3]=!module_bypass[3];
    dsp_bypass(3,module_bypass[3]);
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

int osc_add_sine_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  float freq;
  float amp;
  
  fprintf(stdout, "path: <%s>\n", path);
  freq=argv[0]->f;
  amp=argv[1]->f;

  fprintf(stderr, "creating sine wave at freq %f and amp %f..\n",freq,amp);
  
  /* add sine() function from libcyperus onto correct voice/signal chain */
  dsp_create_sine(freq,amp);
  
  return 0;
} /* osc_create_sine_handler */

int osc_edit_sine_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  int module_no;
  float freq;
  float amp;
  
  fprintf(stdout, "path: <%s>\n", path);
  module_no=argv[0]->i;
  freq=argv[1]->f;
  amp=argv[2]->f;

  fprintf(stderr, "module_no %d, editing sine wave to freq %f and amp %f..\n",module_no,freq,amp);
  
  /* add sine() function from libcyperus onto correct voice/signal chain */
  dsp_edit_sine(module_no,freq,amp);
  
  return 0;
} /* osc_edit_sine_handler */

int osc_add_square_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  float freq;
  float amp;
  
  fprintf(stdout, "path: <%s>\n", path);
  freq=argv[0]->f;
  amp=argv[1]->f;

  fprintf(stderr, "creating square wave at freq %f and amp %f..\n",freq,amp);
  
  dsp_create_square(freq,amp);
  
  return 0;
} /* osc_create_square_handler */

int osc_edit_square_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  int module_no;
  float freq;
  float amp;
  
  fprintf(stdout, "path: <%s>\n", path);
  module_no=argv[0]->i;
  freq=argv[1]->f;
  amp=argv[2]->f;

  fprintf(stderr, "module_no %d, editing square wave to freq %f and amp %f..\n",module_no,freq,amp);
  
  dsp_edit_square(module_no,freq,amp);
  
  return 0;
} /* osc_edit_square_handler */

int osc_add_pinknoise_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  
  fprintf(stdout, "path: <%s>\n", path);

  fprintf(stderr, "creating pink noise..\n");
  
  /* add pinknoise() function from libcyperus onto correct voice/signal chain */
  dsp_create_pinknoise();
  
  return 0;
} /* osc_add_pinknoise_handler */


int osc_add_butterworth_biquad_lowpass_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  float cutoff;
  float res;
  
  fprintf(stdout, "path: <%s>\n", path);
  cutoff=argv[0]->f;
  res=argv[1]->f;

  fprintf(stderr, "creating butterworth biquad lowpass filter at freq cutoff %f and resonance %f..\n",cutoff,res);
  
  dsp_create_butterworth_biquad_lowpass(cutoff,res);
  
  return 0;
} /* osc_create_butterworth_biquad_lowpass_handler */

int osc_edit_butterworth_biquad_lowpass_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  int module_no;
  float cutoff;
  float res;
  
  fprintf(stdout, "path: <%s>\n", path);
  module_no=argv[0]->i;
  cutoff=argv[1]->f;
  res=argv[2]->f;

  fprintf(stderr, "module_no %d, editing butterworth biquad lowpass filter at cutoff freq %f and resonance %f..\n",module_no,cutoff,res);
  
  dsp_edit_butterworth_biquad_lowpass(module_no,cutoff,res);
  
  return 0;
} /* osc_edit_butterworth_biquad_lowpass_handler */

int osc_add_delay_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  float amt;
  float time;
  float feedback;
  
  fprintf(stdout, "path: <%s>\n", path);
  amt=argv[0]->f;
  time=argv[1]->f;
  feedback=argv[2]->f;
  
  fprintf(stderr, "creating delay with amount %f, time %f seconds, and feedback %f..\n",amt,time,feedback);
  
  dsp_create_delay(amt,time,feedback);
  
  return 0;
} /* osc_add_delay_handler */

int
monome_add_delay_handler()
{
  float amt;
  float time;
  float feedback;
  
  amt=0.5;
  time=0.5;
  feedback=0.5;
  
  dsp_create_delay(amt,time,feedback);
  
  return 0;
} /* monome_add_delay_handler */

int
osc_edit_delay_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  int module_no;
  float amt;
  float time;
  float feedback;
  
  fprintf(stdout, "path: <%s>\n", path);
  module_no=argv[0]->i;
  amt=argv[1]->f;
  time=argv[2]->f;
  feedback=argv[3]->f;
  
  fprintf(stderr, "module_no %d, editing delay of amount %f, time %f seconds, and feedback %f..\n",module_no,amt,time,feedback);
  
  dsp_edit_delay(module_no,amt,time,feedback);
  
  return 0;
} /* osc_edit_delay_handler */

int
monome_edit_delay_handler(int module_no)
{
  float amt=module_parameter[module_no][0];
  float time=module_parameter[module_no][1];
  float feedback=module_parameter[module_no][2];
  float filler=module_parameter[module_no][3];
  
  dsp_edit_delay(module_no,amt,time,feedback);

  return 0;
} /* monome_edit_delay_handler */

int
osc_add_vocoder_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  float freq;
  float amp;

  freq=argv[0]->f;
  amp=argv[1]->f;
  
  fprintf(stdout, "path: <%s>\n", path);  
  fprintf(stderr, "creating vocoder..\n");

  dsp_create_vocoder(freq,amp);
  
  return 0;
} /* osc_add_vocoder_handler */

int
osc_edit_vocoder_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  int module_no;
  float freq;
  float amp;
  
  fprintf(stdout, "path: <%s>\n", path);
  module_no=argv[0]->i;
  freq=argv[1]->f;
  amp=argv[2]->f;
  
  fprintf(stderr, "module_no %d, editing vocoder freq %f amp %f..\n",module_no,freq,amp);
  
  dsp_edit_vocoder(module_no,freq,amp);
  
  return 0;
} /* osc_edit_vocoder_handler */

int osc_remove_module_handler(const char *path, const char *types, lo_arg ** argv,
			      int argc, void *data, void *user_data)
{
  int voice;
  int module_no;
  
  fprintf(stdout, "path: <%s>\n", path);
  module_no=argv[0]->i;

  fprintf(stderr, "removing module #%d..\n",module_no);
  
  dsp_remove_module(0,module_no);
  
  return 0;
} /* osc_remove_module_handler */

int osc_list_modules_handler(const char *path, const char *types, lo_arg ** argv,
			     int argc, void *data, void *user_data)
{
  int voice_no;
  char *module_list;
  char return_string[100];
  
  fprintf(stdout, "path: <%s>\n", path);
  voice_no=argv[0]->i;

  module_list = dsp_list_modules(voice_no);
  
  fprintf(stderr, "listing modules for voice #%d..\n",voice_no);

  lo_send(lo_addr_send,"/cyperus/list","s",module_list);

  free(module_list);
  return 0;
  
} /* osc_list_modules_handler */

int main(void)
{
  struct monome_t *monome = NULL;

  char *osc_port_in = "97217";
  char *osc_port_out = "97211";

  lo_addr_send = lo_address_new("127.0.0.1",osc_port_out);
  
  lo_server_thread st = lo_server_thread_new(osc_port_in, error);
  /* add method that will match any path and args */
  /* lo_server_thread_add_method(st, NULL, NULL, generic_handler, NULL); */
  
  /* non-generic methods */
  lo_server_thread_add_method(st, "/cyperus/remove", "i", osc_remove_module_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/list", "i", osc_list_modules_handler, NULL);
  
  lo_server_thread_add_method(st, "/cyperus/add/sine", "ff", osc_add_sine_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/edit/sine", "iff", osc_edit_sine_handler, NULL);

  lo_server_thread_add_method(st, "/cyperus/add/square", "ff", osc_add_square_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/edit/square", "iff", osc_edit_square_handler, NULL);
  
  lo_server_thread_add_method(st, "/cyperus/add/pinknoise", NULL, osc_add_pinknoise_handler, NULL);

  lo_server_thread_add_method(st, "/cyperus/add/butterworth_biquad_lowpass", "ff", osc_add_butterworth_biquad_lowpass_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/edit/butterworth_biquad_lowpass", "iff", osc_edit_butterworth_biquad_lowpass_handler, NULL);

  lo_server_thread_add_method(st, "/cyperus/add/delay", "fff", osc_add_delay_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/edit/delay", "ifff", osc_edit_delay_handler, NULL);

  lo_server_thread_add_method(st, "/cyperus/add/vocoder", "ff", osc_add_vocoder_handler, NULL);
  lo_server_thread_add_method(st, "/cyperus/edit/vocoder", "iff", osc_edit_vocoder_handler, NULL);

  lo_server_thread_start(st);
  
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
