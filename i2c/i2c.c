/*
 * Copyright (c) 2007 by Jochen Roessner <jochen@lugrot.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * For more information on the GPL, please go to:
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "../net/i2c_state.h"
#include "../uip/uip.h"
#include "../config.h"
#include "i2c.h"

#ifdef I2C_SUPPORT

#define STATS (uip_udp_conn->appstate.i2c)

/*
 * direkter zugriff zum packet buffer
 */
#define BUF ((struct uip_udpip_hdr *)&uip_appdata[-UIP_IPUDPH_LEN])

static struct i2c_tx tx;

void 
i2c_wait_int()
{
	while( (TWCR & _BV(TWINT)) == 0);
}

uint8_t i2c_send ( uint8_t sendbyte )
{
	TWDR = sendbyte;
	TWCR |= _BV(TWINT);
	i2c_wait_int();
	return TWSR;
}

static void
i2c_port_init(void)
{
  TWCR = 0;
  TWSR &= ~(_BV(TWPS0) | _BV(TWPS1));//prescaler for twi
  TWBR = 16; //16;//max speed for twi, ca 400khz by 20Mhz Crystal
  TWCR |= _BV(TWEN);
}

void 
i2c_core_init(struct uip_udp_conn *i2c_conn)
{
  i2c_port_init();

  i2c_conn->appstate.i2c.tx = &tx;
  i2c_conn->appstate.i2c.tx->connstate = I2C_INIT;
}

void 
i2c_core_periodic(void)
{
  if(STATS.timeout > 1)
    STATS.timeout--;
  if(STATS.timeout == 1){
    uip_ipaddr_t ip;
    uip_ipaddr(&ip, 255,255,255,255);
    uip_ipaddr_copy(uip_udp_conn->ripaddr, &ip);
    uip_udp_conn->rport = 0;
    STATS.timeout = 0;
    STATS.tx->seqnum = 0;
    STATS.tx->connstate = I2C_INIT;
    TWCR |= _BV(TWINT) | _BV(TWSTO);

    /* error detection on i2c bus */
    if(TWSR == 0x00)
      i2c_port_init();
/* FIXME:   PORTC &= ~_BV(PC2); */
  }
}

void i2c_core_newdata(void)
{
	
		struct i2c_request_t *REQ = uip_appdata;
		/*
		* ueberschreiben der connection info. 
		* port und adresse auf den remotehost begrenzen
		*/
		if(STATS.tx->connstate == I2C_INIT && STATS.tx->seqnum == 0){
			uip_ipaddr_copy(uip_udp_conn->ripaddr, BUF->srcipaddr);
			uip_udp_conn->rport = BUF->srcport;
			STATS.tx->seqnum = REQ->seqnum -1;
		}
		
		if(REQ->seqnum == STATS.tx->seqnum + 1){
			STATS.tx->seqnum = REQ->seqnum;
			STATS.timeout = 10;
/* FIXME:			PORTC |= _BV(PC2); */
			/* read init des i2c bus */
			if (REQ->type == I2C_READ || (REQ->type == I2C_READON && STATS.tx->connstate != I2C_READON)){
				STATS.tx->connstate = REQ->type;
				TWCR |= _BV(TWINT) | _BV(TWSTA);
				i2c_wait_int();
				STATS.tx->i2cstate = TWSR;
				if(TWSR == 0x08 || TWSR == 0x10)
				{
					uint8_t TWSRtmp = 0;
					TWSRtmp = i2c_send ( REQ->i2c_addr<<1 | 0x01 );
					TWCR &= ~_BV(TWSTA);
					STATS.tx->i2cstate = TWSRtmp;
					if(TWSRtmp != 0x40)
					{
						TWCR = _BV(TWEN) | _BV(TWSTO);
						STATS.tx->connstate = I2C_ERROR;
					}
				}
				else
				{
					TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
					STATS.tx->i2cstate = TWSR;
					STATS.tx->connstate = I2C_ERROR;
				}
				
				if(REQ->datalen > 0)
					TWCR |= _BV(TWEA);
			}
			
			if ((REQ->type == I2C_READ || REQ->type == I2C_READON ) && STATS.tx->connstate != I2C_ERROR){
				STATS.tx->datalen = 0;
				while (STATS.tx->datalen < REQ->datalen){ // && STATS.tx->datalen <= I2C_TXBUFMAX){
					{
						TWCR |= _BV(TWINT);
						
						i2c_wait_int();
						
						uint8_t TWSRtmp = TWSR;
						if(STATS.tx->datalen <= REQ->datalen && TWSRtmp == 0x50){
							STATS.tx->buf[STATS.tx->datalen++] = TWDR;
							if(STATS.tx->datalen == REQ->datalen && (REQ->type == I2C_READ || REQ->datalen < 254))
							{
								TWCR = (TWCR | _BV(TWINT)) & ~_BV(TWEA);
							}
						}
						else{
							STATS.tx->buf[STATS.tx->datalen++] = TWSRtmp;
						}
					}
				}
			}
			
			
			/* write init des i2c bus */
			if (uip_datalen() >= I2C_DATAOFFSET && ( REQ->type == I2C_WRITE || (REQ->type == I2C_WRITEON && STATS.tx->connstate != I2C_WRITEON))){
				STATS.tx->connstate = REQ->type;
					/* sende startcondition */
				TWCR |= _BV(TWINT) | _BV(TWSTA);
				i2c_wait_int();
				STATS.tx->i2cstate = TWSR;
				if(TWSR == 0x08 || TWSR == 0x10)
				{
						/* loesche startcondition und sende adresse */
					TWCR &= ~(_BV(TWSTA) | _BV(TWINT));
					uint8_t TWSRtmp = 0;
					TWSRtmp = i2c_send ( REQ->i2c_addr<<1 & 0xFE );
					STATS.tx->i2cstate = TWSRtmp;
					if(TWSRtmp != 0x18)
					{
							/* adresse nicht erreichbar */
						TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
						STATS.tx->connstate = I2C_ERROR;
					}
				}
				else
				{
						/* startcondition fehlgeschlagen */
					TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
					STATS.tx->i2cstate = TWSR;
					STATS.tx->connstate = I2C_ERROR;
				}
			}
			/* sende daten an den slave */
			if (uip_datalen() > I2C_DATAOFFSET && ( REQ->type == I2C_WRITE || REQ->type == I2C_WRITEON ) && STATS.tx->connstate != I2C_ERROR){
				uint8_t TWSRtmp;
				STATS.tx->datalen = 0;
				/* sende bis paketdaten ende oder bis datalen erreicht ist */
				while ((STATS.tx->datalen + I2C_DATAOFFSET) < uip_datalen() && STATS.tx->datalen < REQ->datalen){
					TWSRtmp = i2c_send ( REQ->data[STATS.tx->datalen] );
					STATS.tx->buf[STATS.tx->datalen] = REQ->data[STATS.tx->datalen];
						/* fehler protokollieren */
					if(TWSRtmp != 0x28){
						STATS.tx->buf[STATS.tx->datalen] = TWSR;
						if(STATS.tx->datalen >= I2C_TXBUFMAX){
							break;
						}
						TWCR |= _BV(TWINT) | _BV(TWSTO);
					}
					STATS.tx->datalen++;
				}
			}
			if(STATS.tx->connstate != I2C_ERROR){
				STATS.timeout = 1;
			}
			
			if(REQ->type == I2C_INIT){
				uip_ipaddr_t ip;
				uip_ipaddr(&ip, 255,255,255,255);
				uip_ipaddr_copy(uip_udp_conn->ripaddr, &ip);
				uip_udp_conn->rport = 0;
				STATS.timeout = 0;
				STATS.tx->seqnum = 0;
				STATS.tx->connstate = I2C_INIT;
				/* FIXME: PORTC &= ~_BV(PC2); */
				TWCR |= _BV(TWINT) | _BV(TWSTO);
			}
			else{
				uip_send(&tx, STATS.tx->datalen+I2C_DATAOFFSET);
			}
		}
		else if(REQ->seqnum == STATS.tx->seqnum){
			uip_send(&tx, STATS.tx->datalen+I2C_DATAOFFSET);
		}
}

#endif