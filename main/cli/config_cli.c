#include "config_cli.h"
#include "spiffs_util.h"
#include "ble/ble_util.h"
#include "wifi_util.h"
#include "simple_ota_example.h"
#include "ble/ble_parse.h"
void myptonx( const char *str, uint8_t *data, int len )
{
	int		pos = 0;
	int		i = 0;
	uint8_t	val = 0;

	memset(data, 0x00, len);

	while(*str)
	{
		if ('0' <= *str && *str <= '9')
			val = *str - '0';
		else if ('a' <= *str && *str <= 'f')
			val = *str - 'a' + 10;
		else if ('A' <= *str && *str <= 'F')
			val = *str - 'A' + 10;
		else if (':' == *str || *str == '-') {
			str++;
			continue;
		}

		if(++i % 2)
			data[pos] = (val << 4);
		else
			data[pos++] |= (val & 0x0f);
		
		if(pos >= len)
			break;
		str++;
	}
}


BaseType_t prvConfigInformationCommand( char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString )
{
    const char *pcParameter;
    BaseType_t xParameterStringLength, xReturn;
    static BaseType_t lParameterNumber = 0;
    char ag[6][40];
    char buf[64];
    int  offset=0;
    int j;
    int	val32 = 0;
	
	/* Remove compile time warnings about unused parameters, and check the
	write buffer is not NULL.  NOTE - for simplicity, this example assumes the
	write buffer length is adequate, so does not check for buffer overflows. */
	( void ) pcCommandString;
	( void ) xWriteBufferLen;
	configASSERT( pcWriteBuffer );
	
	if( lParameterNumber == 0 )
	{
		/* The first time the function is called after the command has been
		entered just a header string is returned. */
//		sprintf( pcWriteBuffer, "show/get/set parameters were:\r\n" );
		memset( &ag[0][0],0,sizeof(ag));
		memset( pcWriteBuffer, 0x00, xWriteBufferLen );
		/* Next time the function is called the first parameter will be echoed
		back. */
		lParameterNumber = 1L;

		/* There is more data to be returned as no parameters have been echoed
		back yet. */
		xReturn = pdPASS;
	}
	else
	{
		/* Obtain the parameter string. */
		pcParameter = FreeRTOS_CLIGetParameter
						(
							pcCommandString,		/* The command string itself. */
							lParameterNumber,		/* Return the next parameter. */
							&xParameterStringLength	/* Store the parameter string length. */
						);

		/* Sanity check something was returned. */
//		configASSERT( pcParameter );

		if (pcParameter != NULL)
		{
			memset( pcWriteBuffer, 0x00, xWriteBufferLen );
//			sprintf (pcWriteBuffer, "+=%d+=%s=+\r\n", lParameterNumber, pcParameter);
			/* Return the parameter string. */
			strcpy (ag[lParameterNumber], pcParameter);
			/* If this is the last of the three parameters then there are no more
			strings to return after this one. */
			ag[lParameterNumber][xParameterStringLength] = '\0';
			xReturn = pdTRUE;
			lParameterNumber++;
		}
		else
		{
			memset( pcWriteBuffer, 0x00, xWriteBufferLen );
			if (!strncmp(ag[1], "help", 4))
			{
				offset += sprintf (&pcWriteBuffer[offset], "\r\nconfig [show | save | facto]\r\nconfig save ethmac\r\nconfig route show\r\nconfig route [static | delete] {param}\r\n");
			}
			else if (!strncmp(ag[1], "show", 4))
			{

			}
			else if (!strncmp(ag[1], "mem", 3))
			{
                if (!strncmp(ag[2], "show", 4))
                {
                    spiffs_info();
					ble_info_print();
					wifi_info_print();
                }
				else if (!strncmp(ag[2], "facto", 5))
				{
					spiffs_facto();
				}
                else if (!strncmp(ag[2], "format", 6))
                {
                    spiffs_format();   
                }
				else if (!strncmp(ag[2], "save", 4))
				{
					if ((lParameterNumber == 5) && (!strncmp(ag[3], "ethmac", 6))) {
						if (strlen(ag[4]) != (MAC_ADDRESS_LEN*2)) {
							offset += sprintf (&pcWriteBuffer[offset], "Configuration Change Fail...invalid Ethernet MAC address length %d\r\n", strlen(ag[3]));
							lParameterNumber = 0L;
							return pdFALSE;
						}
						myptonx( ag[4], (uint8_t*)buf, MAC_ADDRESS_LEN );
						offset += sprintf (&pcWriteBuffer[offset], "ETH-MAC Address Save ...\r\n");
						ble_mac_write((uint8_t*)buf);
					}
					else if ((lParameterNumber == 5) && (!strncmp(ag[3], "used", 4))) {
						uint32_t used = atoi(ag[4]);
						offset += sprintf (&pcWriteBuffer[offset], "used save %ld\r\n",used);
						wifi_info_set_used(used);
					}					
					else if ((lParameterNumber == 5) && (!strncmp(ag[3], "ssid", 4))) {
						offset += sprintf (&pcWriteBuffer[offset], "ssid save %s\r\n",ag[4]);
						wifi_info_set_ssid((uint8_t*)ag[4]);
					}
					else if ((lParameterNumber == 5) && (!strncmp(ag[3], "pass", 4))) {
						offset += sprintf (&pcWriteBuffer[offset], "pass save %s\r\n",ag[4]);
						wifi_info_set_passward((uint8_t*)ag[4]);
					}     
					else if ((lParameterNumber == 5) && (!strncmp(ag[3], "addr", 4))) {
						offset += sprintf (&pcWriteBuffer[offset], "addr save %s\r\n",ag[4]);
						wifi_info_set_hostip((uint8_t*)ag[4]);
					}  
					else if ((lParameterNumber == 5) && (!strncmp(ag[3], "port", 4))) {
						unsigned short port = atoi(ag[4]);
						offset += sprintf (&pcWriteBuffer[offset], "port save %d\r\n",port);
						wifi_info_set_hostport(port);
					}                
				}
				else if (!strncmp(ag[2], "del", 3))
				{
					if(!strncmp(ag[3], "ethmac", 6)) {
						ble_mac_clear();
					}
				}
				else if (!strncmp(ag[2], "fw", 2))
				{
					if(!strncmp(ag[3], "URL", 3)) {
						APP_String_printf("URL = %s",ag[4]);
						ota_main(ag[4]);
					}
				}
            }
			/* There are more parameters to return after this one. */
//			pcWriteBuffer[ 0 ] = 0x00;
			xReturn = pdFALSE;
			lParameterNumber = 0L;
		}
	}

	return xReturn;
}
