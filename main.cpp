/*
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/ 
 *  
 *  
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * 
 * 	* Redistributions of source code must retain the above copyright 
 * 	  notice, this list of conditions and the following disclaimer.
 * 
 * 	* Redistributions in binary form must reproduce the above copyright
 * 	  notice, this list of conditions and the following disclaimer in the 
 * 	  documentation and/or other materials provided with the   
 * 	  distribution.
 * 
 * 	* Neither the name of Texas Instruments Incorporated nor the names of
 * 	  its contributors may be used to endorse or promote products derived
 * 	  from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <list>

#include <stdint.h>
#include <stdio.h>
//#include <string.h>

extern "C" {
#include <pru_cfg.h>
#include <pru_intc.h>
#include <rsc_types.h>
#include <sys_mailbox.h>
#include <pru_virtqueue.h>
#include <pru_rpmsg.h>
#include "resource_table_0.h"
}

#include "adc.h"
#include "hal.h"
#include "scheduled_output_action.h"
#include "timer.h"
#include "util.h"
#include "msg_definition.h"
#include "light_barrier.h"
#include "controller.h"
#include "version.h"
#include "motor.h"
#include "piston.h"
#include "hw.h"


volatile register uint32_t __R30;
volatile register uint32_t __R31;


/* PRU0 is mailbox module user 1 */
#define MB_USER						1
/* Mbox0 - mail_u1_irq (mailbox interrupt for PRU0) is Int Number 60 */
#define MB_INT_NUMBER				60

/* Host-0 Interrupt sets bit 30 in register R31 */
#define HOST_INT					0x40000000

/* The mailboxes used for RPMsg are defined in the Linux device tree
 * PRU0 uses mailboxes 2 (From ARM) and 3 (To ARM)
 * PRU1 uses mailboxes 4 (From ARM) and 5 (To ARM)
 */
#define MB_TO_ARM_HOST				3
#define MB_FROM_ARM_HOST			2

/*
 * Using the name 'rpmsg-pru' will probe the rpmsg_pru driver found
 * at linux-x.y.z/drivers/rpmsg/rpmsg_pru.c
 */
#define CHAN_NAME					"rpmsg-pru"
#define CHAN_DESC					"Channel 30"
#define CHAN_PORT					30

/* 
 * Used to make sure the Linux drivers are ready for RPMsg communication
 * Found at linux-x.y.z/include/uapi/linux/virtio_config.h
 */
#define VIRTIO_CONFIG_S_DRIVER_OK	4

uint8_t payload[RPMSG_BUF_SIZE];

static const uint32_t ADC_LIMIT_TOLERANCE = 60;
static const uint32_t ADC_NO_OBJECT_LIMIT = 0x4be;
static const uint32_t ADC_BLUE_OBJECT_LIMIT = 0x49c;
static const uint32_t ADC_RED_OBJECT_LIMIT = 0x32a;
static const uint32_t ADC_WHITE_OBJECT_LIMIT = 0x2ef;

enum Mode { MODE_NORMAL, MODE_DIAGNOSTIC };

enum Color { BLUE, RED, WHITE, UNKNOWN };
enum SortOrder { BLUE_RED_WHITE };


bool is_controller_started;
bool rpmsg_connected;
uint32_t pulsecounter_last_change;
bool is_conveyor_running;
struct pru_rpmsg_transport transport;
uint16_t src, dst, len;
uint32_t now;
uint32_t last_all_inputs_value;

uint32_t adc_last_measurement;
uint32_t adc_min_value;
uint32_t lightbarrier1_last_change;
uint32_t lightbarrier2_last_change;
uint32_t lightbarriers3_to_5_last_change;

Motor motor(MOTOR_MASK);
Piston piston[] = { Piston(VALVE1_MASK), Piston(VALVE2_MASK), Piston(VALVE3_MASK) };
LightBarrier lightBarrier[] = { LightBarrier(LIGHTBARRIER1_MASK), LightBarrier(LIGHTBARRIER2_MASK), LightBarrier(LIGHTBARRIERS3_TO_5_MASK) };


Hw hw(motor,
            piston[0],
            piston[1],
            piston[2],
            lightBarrier[0],
            lightBarrier[1],
            lightBarrier[2]);



Controller ctrl(hw);


std::list<Color> detected_colors;

bool verbose;

int16_t post_event(char const *event, uint16_t length)
{
  if (!rpmsg_connected) {
    return -4; //RPMSG_NOT_CONNECTED
  }
  return pru_rpmsg_send(&transport, dst, src, (void*)event, length);
}

int16_t post_info(char info)
{
  if (!rpmsg_connected) {
    return -4; //RPMSG_NOT_CONNECTED
  }
  return pru_rpmsg_send(&transport, dst, src, (void*)&info, 1);
}

std::list<ScheduledOutputAction> pusher_actions;

void schedule_pusher_action(uint32_t timestamp, uint32_t bitmask, bool value)
{
  ScheduledOutputAction action(timestamp, bitmask, value);
  pusher_actions.push_back(action);
}

void check_scheduled_pusher_actions()
{
  if (pusher_actions.size() == 0) {
    return;
  }
  ScheduledOutputAction &next_action = pusher_actions.front();
  if (next_action.timestamp > now) {
    return;
  }
  char buffer[29] = "valve";
  uint32_t length = 5;
  buffer[length] = 'X';
  if (next_action.bitmask == VALVE1_MASK) {
    buffer[length] = '1';
  }
  else if (next_action.bitmask == VALVE2_MASK) {
    buffer[length] = '2';
  }
  else if (next_action.bitmask == VALVE3_MASK) {
    buffer[length] = '3';
  }
  length += 1;
  buffer[length] = '=';
  length += 1;
  if (next_action.value) {
    //strcpy(&buffer[length], "on");
    length += 2;
  }
  else {
    //strcpy(&buffer[length], "off");
    length += 3;
  }
  buffer[length] = '\n';
  length += 1;
  //post_event(buffer, length);
  if (next_action.value) {
    __R30 |= next_action.bitmask;
  }
  else {
    __R30 &= ~next_action.bitmask;
  }
  pusher_actions.pop_front();
}

std::list<uint32_t> adc_actions;

void schedule_adc_action(uint32_t timestamp)
{
  adc_actions.push_back(timestamp);
}

void check_scheduled_adc_actions()
{
  if (adc_actions.size() == 0) {
    return;
  }
  uint32_t &next_action = adc_actions.front();
  if (now < next_action - 22) {
    return;
  }
  if (now == adc_last_measurement) {
    return;
  }
  uint16_t value = adc_read();
  adc_last_measurement = now;
  if (value < adc_min_value) {
    adc_min_value = value;
  }
  if (now < next_action + 22) {
    return;
  }
  adc_actions.pop_front();
  if (verbose) {
    char buffer[3];//= "log: adc_min_value=0xXXXXXXXX\n";
    buffer[0] = INFO_COLOR_DETECT;
    buffer[1] = adc_min_value & 0xff;
    buffer[2] = (adc_min_value >> 8 ) & 0xff;
    //appendNumber(&buffer[21], adc_min_value);
    post_event(buffer, 3);
  }
  Color color;
  char *color_string;
  uint32_t color_length;
  if (adc_min_value >= ADC_NO_OBJECT_LIMIT) {
    color = UNKNOWN;
    color_string = "?";
    color_length = 1;
  }
  else if ((adc_min_value <= ADC_BLUE_OBJECT_LIMIT + ADC_LIMIT_TOLERANCE) &&
           (adc_min_value >= ADC_BLUE_OBJECT_LIMIT)) {
    color = BLUE;
    color_string = "blue";
    color_length = 4;
  }
  else if ((adc_min_value <= ADC_RED_OBJECT_LIMIT + ADC_LIMIT_TOLERANCE) &&
           (adc_min_value >= ADC_RED_OBJECT_LIMIT)) {
    color = RED;
    color_string = "red";
    color_length = 3;
  }
  else if ((adc_min_value <= ADC_WHITE_OBJECT_LIMIT + ADC_LIMIT_TOLERANCE) &&
           (adc_min_value >= ADC_WHITE_OBJECT_LIMIT)) {
    color = WHITE;
    color_string = "white";
    color_length = 5;
  }
  else {
    color = UNKNOWN;
    color_string = "?";
    color_length = 1;
  }
  char buffer[12] = "color=";
  //strcpy(&buffer[6], color_string);
  buffer[6 + color_length] = '\n';
  //post_event(buffer, 6 + color_length + 1);
  if (is_controller_started && (color != UNKNOWN)) {
    detected_colors.push_back(color);
  }
  adc_min_value = 0xFFFF;
}

void on_input_change(uint32_t mask, bool value)
{
  if (mask == LIGHTBARRIERS3_TO_5_MASK) {
    if (now - lightbarriers3_to_5_last_change < 5) {
      return;
    }
    lightbarriers3_to_5_last_change = now;
    if (value) {
      post_info(INFO_EMERGENCY_STOP_ON);
      if (is_controller_started) {
        __R30 &= ~MOTOR_MASK;
        post_info(INFO_MOTOR_STOP);
        is_controller_started = false;
        post_info(INFO_CTRL_STOP);
      }
    }
    else {
      post_info(INFO_EMERGENCY_STOP_OFF);
    }
  }
  else if (mask == PULSECOUNTER_MASK) {
    pulsecounter_last_change = timer_get_ticks();
    if (!is_conveyor_running) {
      is_conveyor_running = true;
      post_info(INFO_CONVEYER_RUNNING);
    }
  }
  else if (mask == LIGHTBARRIER1_MASK) {
    if (now - lightbarrier1_last_change < 5) {
      return;
    }
    lightbarrier1_last_change = now;
    if (value) {
      post_info(INFO_LIGHT_BARRIER_1_ON);
    }
    else {
      post_info(INFO_LIGHT_BARRIER_1_OFF);
      schedule_adc_action(now + 111);
    }
    if (verbose) {
      char buffer[] = "now=0xXXXXXXXX\n";
      appendNumber(&buffer[6], now);
      //post_event(buffer, 15);
    }
  }
  else if (mask == LIGHTBARRIER2_MASK) {
    if (now - lightbarrier2_last_change < 5) {
      return;
    }
    lightbarrier2_last_change = now;
    if (value) {
      post_info(INFO_LIGHT_BARRIER_2_ON);
      if (is_controller_started) {
        if (detected_colors.size() == 0) {
          //post_event("debug: No colored object detected. Letting it pass...\n", 54);
        }
        else {
          Color color = detected_colors.front();
          detected_colors.pop_front();
          switch (color) {
            case BLUE:
              schedule_pusher_action(now + 69, VALVE1_MASK, true);
              schedule_pusher_action(now + 99, VALVE1_MASK, false);
              break;
            case RED:
              schedule_pusher_action(now + 166, VALVE2_MASK, true);
              schedule_pusher_action(now + 196, VALVE2_MASK, false);
              break;
            case WHITE:
              schedule_pusher_action(now + 264, VALVE3_MASK, true);
              schedule_pusher_action(now + 294, VALVE3_MASK, false);
              break;
            case UNKNOWN:
            default:
              break;
          }
        }
      }
    }
    else {
      post_info(INFO_LIGHT_BARRIER_2_OFF);
    }
    if (verbose) {
      char buffer[] = "now=0xXXXXXXXX\n";
      appendNumber(&buffer[6], now);
      //post_event(buffer, 15);
    }
  }

  if (value) {
    last_all_inputs_value |= mask;
  }
  else {
    last_all_inputs_value &= ~mask;
  }
}

void process_input(uint32_t all_inputs_value, uint32_t mask)
{
  bool value = all_inputs_value & mask;
  bool last_value = last_all_inputs_value & mask;
  if (value != last_value) {
    on_input_change(mask, value);
  }
}

void process_inputs(uint32_t all_inputs_value)
{
  process_input(all_inputs_value, LIGHTBARRIERS3_TO_5_MASK);
  process_input(all_inputs_value, PULSECOUNTER_MASK);
  process_input(all_inputs_value, LIGHTBARRIER1_MASK);
  process_input(all_inputs_value, LIGHTBARRIER2_MASK);

  if (is_conveyor_running) {
    int32_t ticks_since_last_change = now - pulsecounter_last_change;
    if (ticks_since_last_change > 100) {
      is_conveyor_running = false;
      post_info(INFO_CONVEYER_STOPPED);
    }
  }
}

bool get_last_input(uint32_t mask)
{
  return last_all_inputs_value & mask;
}


static void checkArmToPruMsg()
{
    /* Check bit 30 of register R31 to see if the mailbox interrupt has occurred */
    if (__R31 & HOST_INT) {
      /* Clear the mailbox interrupt */
      CT_MBX.IRQ[MB_USER].STATUS_CLR |= 1 << (MB_FROM_ARM_HOST * 2);

      /* Clear the event status, event MB_INT_NUMBER corresponds to the mailbox interrupt */
      CT_INTC.SICR_bit.STS_CLR_IDX = MB_INT_NUMBER;

      /* Use a while loop to read all of the current messages in the mailbox */
      while (CT_MBX.MSGSTATUS_bit[MB_FROM_ARM_HOST].NBOFMSG > 0) {
        /* Check to see if the message corresponds to a receive event for the PRU */
        if (CT_MBX.MESSAGE[MB_FROM_ARM_HOST] == 1) {
          /* Receive the message */
          if (pru_rpmsg_receive(&transport, &src, &dst, payload, &len) == PRU_RPMSG_SUCCESS) {

            rpmsg_connected = true;
            bool skip_echo = false;

            ctrl.processCmd((payload[0]));


            if (!verbose) {
              if(payload[0] == CMD_VERBOSE_ON) {
                verbose = true;
              }
            }
            if (verbose) {
              if (payload[0] == CMD_VERBOSE_OFF) {
                verbose = false;
              }
            }

            if (!skip_echo) {
              /* Echo the message back to the same address from which we just received */
              pru_rpmsg_send(&transport, dst, src, payload, len);
            }
          }
        }
      }
    }
}

void main() {
  is_controller_started = false;
  rpmsg_connected = false;
  pulsecounter_last_change = 0;
  is_conveyor_running = false;
  src=0xFFFF;
  dst=0xFFFF;
  len=0xFFFF;
  now = 0;
  last_all_inputs_value = get_all_inputs();
  __R30 = 0;
  adc_last_measurement = 0;
  adc_min_value = 0xFFFF;
  lightbarrier1_last_change = 0;
  lightbarrier2_last_change = 0;
  lightbarriers3_to_5_last_change = 0;
  verbose = false;

  volatile uint8_t *status;

    /* allow OCP master port access by the PRU so the PRU can read external memories */
  CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

  /* clear the status of event MB_INT_NUMBER (the mailbox event) and enable the mailbox event */
  CT_INTC.SICR_bit.STS_CLR_IDX = MB_INT_NUMBER;
  CT_MBX.IRQ[MB_USER].ENABLE_SET |= 1 << (MB_FROM_ARM_HOST * 2);

  /* Make sure the Linux drivers are ready for RPMsg communication */
  status = &resourceTable.rpmsg_vdev.status;
  while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK));

  /* Initialize pru_virtqueue corresponding to vring0 (PRU to ARM Host direction) */
  pru_virtqueue_init(&transport.virtqueue0, &resourceTable.rpmsg_vring0, &CT_MBX.MESSAGE[MB_TO_ARM_HOST], &CT_MBX.MESSAGE[MB_FROM_ARM_HOST]);

  /* Initialize pru_virtqueue corresponding to vring1 (ARM Host to PRU direction) */
  pru_virtqueue_init(&transport.virtqueue1, &resourceTable.rpmsg_vring1, &CT_MBX.MESSAGE[MB_TO_ARM_HOST], &CT_MBX.MESSAGE[MB_FROM_ARM_HOST]);

  /* Create the RPMsg channel between the PRU and ARM user space using the transport structure. */
  while (pru_rpmsg_channel(RPMSG_NS_CREATE, &transport, CHAN_NAME, CHAN_DESC, CHAN_PORT) != PRU_RPMSG_SUCCESS);

  timer_start();

  while (1) {
    timer_poll();
    now = timer_get_ticks();

    checkArmToPruMsg();
    process_inputs(get_all_inputs());
    check_scheduled_pusher_actions();
    check_scheduled_adc_actions();
  }
}
