/* added by lei tian, for the power consumption evaluation, 2008/11/21*/

#include "disksim_global.h"
#include "disksim_stat.h"
#include "disksim_iosim.h"
#include "disksim_orgface.h"
#include "disksim_logorg.h"
#include "disksim_ioqueue.h"

#include "disksim_power.h"
#include "disksim_disk.h"
#include "disksim_iodriver.h"

#include <stdarg.h>


static double power_get_stat_time(statgen* stats);

power_param_t *power_cfg = NULL;

power_stat_t *power_desp[MAXDISKS];

void power_initialization()
{
	int i;

	for (i = 0; i < MAXDISKS; i++){
		power_desp[i] = (power_stat_t *)DISKSIM_malloc(sizeof(power_stat_t));
		bzero(power_desp[i], sizeof(power_stat_t));	
	}
	
	for (i = 0; i < MAXDISKS; i++){
		power_desp[i]->current_state = POWER_STATE_IDLE;
	}
	
}

void power_stat_reset()
{
	int i;
	
	ASSERT(power_desp != NULL);

	for (i = 0; i < MAXDISKS; i++){
		bzero(power_desp[i], sizeof(power_stat_t));
		power_desp[i]->current_state = POWER_STATE_IDLE;
	}	

}

void power_set_idle(int devno)
{
	ASSERT(power_desp != NULL);

	power_desp[devno]->current_state = POWER_STATE_IDLE;

}

void power_set_active(int devno)
{
	ASSERT(power_desp != NULL);

	power_desp[devno]->current_state = POWER_STATE_ACTIVE;

}



/*read the configuration parameters for the power evaluation.*/
void power_read_params(char* powerfile_name)
{
  int i;
  FILE* powerfile = fopen(powerfile_name, "r");

  power_cfg = (power_param_t *)DISKSIM_malloc(sizeof(power_param_t));
  bzero(power_cfg, sizeof(power_param_t));

  getparam_double(powerfile, "\nActive power", &power_cfg->active_power, FALSE, 0, 100);
  getparam_double(powerfile, "\nIdle power", &power_cfg->idle_power, FALSE, 0, 100);
  getparam_double(powerfile, "\nStandby power", &power_cfg->standby_power, FALSE, 0, 100);

  getparam_double(powerfile, "\nSpindown threshold", &power_cfg->spindown_threshold, FALSE, 0, 100);
  getparam_double(powerfile, "\nSpindown energy", &power_cfg->spindown_energy, FALSE, 0, 100);
  getparam_double(powerfile, "\nSpindown delay", &power_cfg->spindown_delay, FALSE, 0, 100);

  getparam_double(powerfile, "\nSpinup energy", &power_cfg->spinup_energy, FALSE, 0, 100);
  getparam_double(powerfile, "\nSpinup delay", &power_cfg->spinup_delay, FALSE, 0, 100);
  getparam_double(powerfile, "\nPost spinup incr", &power_cfg->post_spinup_incr, FALSE, 0, 100);

  fprintf(outputfile, "\n\n");

  fclose(powerfile);	

}


void power_cleanup()
{
	int i;

	if (power_cfg){
		free (power_cfg);
		power_cfg = NULL;
	}

	for (i = 0; i < MAXDISKS; i++){
		if (power_desp[i]){
			free (power_desp[i]);
			power_desp[i] = NULL;
		}
	}
}

void power_stat_show()
{
	int i;
	struct disk* maindisk;
	double active_energy[MAXDISKS], idle_energy[MAXDISKS], standby_energy[MAXDISKS], spindown_energy[MAXDISKS], spinup_energy[MAXDISKS], total_energy[MAXDISKS];
	double system_energy;

	fprintf(outputfile, "\n *********************	 power consumption statistics	 ********************** \n");	

	for(i = 0; i < MAXDISKS; i++){
		active_energy[i] = 0.0;
		idle_energy[i] = 0.0;
		standby_energy[i] = 0.0;
		spindown_energy[i] = 0.0;
		spinup_energy[i] = 0.0;
		total_energy[i] = 0.0;
	}	

	for (i = 0; i < MAXDISKS; i++){

		maindisk = getdisk (i);

		if(maindisk == NULL)
			continue;
		
		/*add seek time*/
		power_desp[i]->total_time_active += power_get_stat_time(&maindisk->stat.seektimestats);

		/*add rotate time*/
		power_desp[i]->total_time_active += power_get_stat_time(&maindisk->stat.rotlatstats);

		/*add transfer time*/
		power_desp[i]->total_time_active += power_get_stat_time(&maindisk->stat.xfertimestats);
	
		active_energy[i] = power_desp[i]->total_time_active / (MILLI * 1.0) * power_cfg->active_power;
	
		idle_energy[i] = power_desp[i]->total_time_idle / (MILLI * 1.0) * power_cfg->idle_power;

		standby_energy[i] = power_desp[i]->total_time_standby / (MILLI * 1.0) * power_cfg->standby_power;

		spindown_energy[i] = power_desp[i]->num_spindowns * power_cfg->spindown_energy;

		spinup_energy[i] = power_desp[i]->num_spinups * power_cfg->spinup_energy;

		total_energy[i] = active_energy[i] + idle_energy[i] + standby_energy[i] + spindown_energy[i] + spinup_energy[i];

	}

	system_energy = 0.0;
	for (i = 0; i < MAXDISKS; i++){
		
		maindisk = getdisk (i);

		if(maindisk == NULL)
			continue;
		
		fprintf(outputfile, "disk[%d]: active: %f, idle: %f, standby: %f, spin: %f, total: %f\n",
			i, active_energy[i], idle_energy[i], standby_energy[i], spindown_energy[i] + spinup_energy[i], total_energy[i]);

		system_energy += total_energy[i];
	}

	fprintf(outputfile, "System power consumption total: %f joules\n", system_energy);	

}



int power_waitfor_spinup(ioreq_event* curr)
{
	int devno = curr->devno;
	
	if (power_desp[devno]->current_state == POWER_STATE_SPINNING_UP){
		// re-insert at end of wakeup
		// curr->init_time = curr->time;
		curr->time = power_desp[devno]->post_spinup_time;
		power_desp[devno]->post_spinup_time += power_cfg->post_spinup_incr;
			
		addtointq((event *) curr);

		fprintf(outputfile, "\nevent(devno: %d, type: %d) Waiting for spinup until %f\n", devno, curr->type, curr->time);

		return 1;

	}else 
		return 0;

}


int power_manage_spindown(double idle_start_time, ioreq_event* curr)
{
	int devno;
	double this_idle_time, spinup_delay;
	power_event *spinup_event;

	devno = curr->devno;
	this_idle_time = curr->time - idle_start_time;
	fprintf(outputfile, "curr type: %d, devno %d, time: %f, queue idle start: %f\n", curr->type, curr->devno, curr->time, idle_start_time);
	ASSERT(this_idle_time >= 0);

	if (power_desp[devno]->current_state == POWER_STATE_IDLE){

		if(this_idle_time >= power_cfg->spindown_threshold){

			power_desp[devno]->num_spindowns++;

			if(this_idle_time - power_cfg->spindown_threshold >= power_cfg->spindown_delay){
				spinup_delay = power_cfg->spinup_delay;
				power_desp[devno]->total_time_standby += this_idle_time - power_cfg->spindown_threshold - power_cfg->spindown_delay;			
			}
			else{
				spinup_delay = power_cfg->spinup_delay + (power_cfg->spindown_delay - (this_idle_time - power_cfg->spindown_threshold));
				power_desp[devno]->total_time_standby += 0;
			}

			power_desp[devno]->total_time_idle += power_cfg->spindown_threshold;

			power_desp[devno]->num_spinups++;

			spinup_event = (power_event *) getfromextraq();
			spinup_event->time = curr->time + spinup_delay;
			spinup_event->type = POWER_SPINUP_EVENT;
			spinup_event->devno = devno;

	    	addtointq ((event *)spinup_event);
			
		    power_desp[devno]->current_state = POWER_STATE_SPINNING_UP;
		    power_desp[devno]->post_spinup_time = spinup_event->time + power_cfg->post_spinup_incr;

			fprintf(outputfile, "\nSet spinup time: %lf, devno: %d\n", spinup_event->time, devno);


		} else {
			power_desp[devno]->total_time_idle += this_idle_time;
			power_desp[devno]->current_state = POWER_STATE_ACTIVE;
		}
	}

	return 0;
}


void power_internal_event(power_event *curr)
{
	int devno = curr->devno;
	
	switch (curr->type) {
		
		case POWER_SPINDOWN_EVENT:

			power_desp[devno]->current_state = POWER_STATE_STANDBY;			

			break;
			
		case POWER_SPINUP_EVENT:
			
			power_desp[devno]->current_state = POWER_STATE_ACTIVE;

			break;

		default:
			fprintf(stderr, "Unknown event type passed to power_internal_event\n");
			addtoextraq ((event *)curr);
			exit(1);
	}
	
	addtoextraq ((event *)curr);

}


static double power_get_stat_time(statgen* stats)
{
	if (stats->count == 0) return 0.0;
	
	return stats->runval / 1000.0;
}

