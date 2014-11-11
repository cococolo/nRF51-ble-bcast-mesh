#include "trickle.h"
#include "rbc_mesh_common.h"

#include "nrf_soc.h"
#include "boards.h"
#include "nrf51_bitfields.h"
#include <string.h>


#define TRICKLE_RNG_POOL_SIZE   (64)
#define APP_TIMER_PRESCALER 16

#define RX_PROPAGATION_TIME         (377)


#define TRICKLE_FLAGS_T_DONE_Pos    (0)
#define TRICKLE_FLAGS_DISCARDED_Pos (1)

/*****************************************************************************
* Static Globals
*****************************************************************************/


static uint32_t g_trickle_time; /* global trickle time that all time variables are relative to */

static uint8_t rng_vals[64];
static uint8_t rng_index;

/*global parameters for trickle behavior, set in trickle_setup() */
static uint32_t g_i_min, g_i_max;
static uint8_t g_k;

/*****************************************************************************
* Static Functions
*****************************************************************************/

static void trickle_interval_begin(trickle_t* trickle)
{
    trickle->c = 0;
    
    uint32_t rand_number =  ((uint32_t) rng_vals[(rng_index++) & 0x3F])       |
                            ((uint32_t) rng_vals[(rng_index++) & 0x3F]) << 8  |
                            ((uint32_t) rng_vals[(rng_index++) & 0x3F]) << 16 |
                            ((uint32_t) rng_vals[(rng_index++) & 0x3F]) << 24;
    
    uint32_t i_half = trickle->i_relative / 2;
    trickle->t = g_trickle_time + i_half + (rand_number % i_half);
    trickle->i = g_trickle_time + trickle->i_relative;
    
    trickle->trickle_flags &= ~(1 << TRICKLE_FLAGS_T_DONE_Pos);
}

/*****************************************************************************
* Interface Functions
*****************************************************************************/


void trickle_setup(uint32_t i_min, uint32_t i_max, uint8_t k)
{
    g_i_min = i_min;
    g_i_max = i_max;
    g_k = k;
    uint32_t error_code;
    rng_index = 0;
    
    /* Fill rng pool */
    uint8_t bytes_available;
    do 
    {
        error_code = 
            sd_rand_application_bytes_available_get(&bytes_available);
        APP_ERROR_CHECK(error_code);
        if (bytes_available > 0)
        {
            uint8_t byte_count = 
                ((bytes_available > TRICKLE_RNG_POOL_SIZE - rng_index)? 
                (TRICKLE_RNG_POOL_SIZE - rng_index) : 
                (bytes_available));
            
            error_code = 
                sd_rand_application_vector_get(&rng_vals[rng_index], 
                byte_count);
            APP_ERROR_CHECK(error_code);
            
            rng_index += byte_count;
        }
    } while (rng_index < TRICKLE_RNG_POOL_SIZE);
    
}


void trickle_time_increment(void)
{
    /* step global time */
    ++g_trickle_time;
}

void trickle_time_update(uint32_t time)
{
    g_trickle_time = time;
}


void trickle_init(trickle_t* trickle)
{
    trickle->i_relative = 2 * g_i_min;
    
    trickle->trickle_flags = 0;
    
    trickle_interval_begin(trickle);
}

void trickle_rx_consistent(trickle_t* trickle)
{    
    ++trickle->c;
}

void trickle_rx_inconsistent(trickle_t* trickle)
{        
    if (trickle->i_relative > g_i_min)
    {
        trickle_timer_reset(trickle);
    }
}

void trickle_timer_reset(trickle_t* trickle)
{    
    trickle->trickle_flags &= ~(1 << TRICKLE_FLAGS_T_DONE_Pos);
    trickle->i_relative = g_i_min; 
    
        
    trickle_interval_begin(trickle);
}

void trickle_register_tx(trickle_t* trickle)
{
    trickle->trickle_flags |= (1 << TRICKLE_FLAGS_T_DONE_Pos);
}

void trickle_step(trickle_t* trickle, bool* out_do_tx)
{
    *out_do_tx = false;
    
    if (trickle->trickle_flags & (1 << TRICKLE_FLAGS_T_DONE_Pos)) /* i is next timeout for this instance */
    {
        if (trickle->i <= g_trickle_time)
        {
            /* double value of i */
            trickle->i_relative = (trickle->i_relative * 2 < g_i_max * g_i_min)?
                            trickle->i_relative * 2 : 
                            g_i_max * g_i_min;
            
            trickle_interval_begin(trickle);
        }
    }
    else /* t is next timeout for this instance */
    {
        if (trickle->t <= g_trickle_time)
        {
            if (trickle->c < g_k)
            {
                *out_do_tx = true;
            }
            else /* no tx this interval, tell trickle to prepare 
                for interval timeout*/
            {
                trickle->trickle_flags |= (1 << TRICKLE_FLAGS_T_DONE_Pos);
            }
        }
    }
}

uint32_t trickle_timestamp_get(void)
{
    return g_trickle_time;
}

uint32_t trickle_next_processing_get(trickle_t* trickle)
{
    if (trickle->trickle_flags & (1 << TRICKLE_FLAGS_T_DONE_Pos)) /* i is next timeout for this instance */
    {
        return trickle->i;
    }
    else /* t is next timeout for this instance */
    {
        return trickle->t;
    }
}