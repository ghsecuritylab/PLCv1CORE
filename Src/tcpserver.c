/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */


#include "lwip/opt.h"

#if LWIP_NETCONN

#include "lwip/sys.h"
#include "lwip/api.h"

#include "ethip.h"
#include "cmsis_os.h"

#define TCPSERVER_THREAD_PRIO  2

//uint16_t reset_tmr;
static uint8_t nums = 0x00;



/*-----------------------------------------------------------------------------------*/
static void ethipserver_thread(void *arg)
{
	struct netconn   *newconn;
    struct netbuf   *buf;
    uint16_t   len;
    uint8_t close_flag = 0;
    static uint8_t out_buf[1024*3];
    uint8_t num=0;
    newconn = (struct netconn*)arg;
    void *data;
    packet	inp_data_pckt;
    packet out_data_pckt;
    uint8_t bool_result = 0;
    encaps_packet enc_pckt;
    //uint8_t*   DataPtr;

    if((nums&(1<<0))==0) num=1;
    else if((nums&(1<<1))==0) num=2;
    else if((nums&(1<<2))==0) num=3;
    if(num==0 || num>3) {
    	netconn_close(newconn);
		netconn_delete(newconn);
		vTaskDelete(NULL);
    }
    nums|=(1<<(num-1));

    close_flag = 0;
	netconn_set_recvtimeout(newconn,30000);
	while (netconn_recv(newconn, &buf) == ERR_OK)
	{
			do
			{
			  netbuf_data(buf, &data, &len);
			  inp_data_pckt.data = (uint8_t*)data;
			  inp_data_pckt.length = len;
			  get_encaps_packet(&inp_data_pckt,&enc_pckt,&bool_result);
			  if(bool_result) {
				  //reset_tmr = 0;
				  out_data_pckt.data = &out_buf[1024*(num-1)];
				  out_data_pckt.length = 0;
				  get_answer(&enc_pckt, &out_data_pckt);
				  close_flag = out_data_pckt.close_tcp;
				  if(out_data_pckt.length){
					netconn_write(newconn, out_data_pckt.data, out_data_pckt.length, NETCONN_NOCOPY);
					//HAL_GPIO_TogglePin(LED2_GREEN_GPIO_Port,LED2_GREEN_Pin);
				  }
			  }
			}
			while (netbuf_next(buf) >= 0);

		netbuf_delete(buf);
		if(close_flag) break;
	}

	/* Close connection and discard connection identifier. */
	nums &= ~((uint8_t)(1<<(num-1)));
	netconn_close(newconn);
	netconn_delete(newconn);
	vTaskDelete(NULL);

}

/*-----------------------------------------------------------------------------------*/
static void tcp_server_thread(void *arg)
{
  struct netconn *conn, *newconn;
  err_t err, accept_err;
      
  LWIP_UNUSED_ARG(arg);

  /* Create a new connection identifier. */
  conn = netconn_new(NETCONN_TCP);
  
  if (conn!=NULL)
  {  
    /* Bind connection to well known port number 7. */
    err = netconn_bind(conn, NULL, 0xAF12);
    
    if (err == ERR_OK)
    {
      /* Tell connection to go into listening mode. */
      netconn_listen(conn);
    
      while (1) 
      {
        /* Grab new connection. */
         accept_err = netconn_accept(conn, &newconn);
    
        /* Process the new connection. */
        if (accept_err == ERR_OK) 
        {
        	if(nums!=0x07) sys_thread_new("ethipserver", ethipserver_thread, newconn, DEFAULT_THREAD_STACKSIZE, TCPSERVER_THREAD_PRIO);
        	else {
        		netconn_close(newconn);
				netconn_delete(newconn);
        	}
        }
        osDelay(1);
      }
    }
  }
}
/*-----------------------------------------------------------------------------------*/

void tcp_server_init(void)
{
  sys_thread_new("tcpecho_thread", tcp_server_thread, NULL, DEFAULT_THREAD_STACKSIZE, TCPSERVER_THREAD_PRIO);
}
/*-----------------------------------------------------------------------------------*/

#endif /* LWIP_NETCONN */
