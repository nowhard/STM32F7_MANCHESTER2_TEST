/*
    FreeRTOS V7.3.0 - Copyright (C) 2012 Real Time Engineers Ltd.


    ***************************************************************************
     *                                                                       *
     *    FreeRTOS tutorial books are available in pdf and paperback.        *
     *    Complete, revised, and edited pdf reference manuals are also       *
     *    available.                                                         *
     *                                                                       *
     *    Purchasing FreeRTOS documentation will not only help you, by       *
     *    ensuring you get running as quickly as possible and with an        *
     *    in-depth knowledge of how to use FreeRTOS, it will also help       *
     *    the FreeRTOS project to continue with its mission of providing     *
     *    professional grade, cross platform, de facto standard solutions    *
     *    for microcontrollers - completely free of charge!                  *
     *                                                                       *
     *    >>> See http://www.FreeRTOS.org/Documentation for details. <<<     *
     *                                                                       *
     *    Thank you for using FreeRTOS, and thank you for your support!      *
     *                                                                       *
    ***************************************************************************


    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation AND MODIFIED BY the FreeRTOS exception.
    >>>NOTE<<< The modification to the GPL is included to allow you to
    distribute a combined work that includes FreeRTOS without being obliged to
    provide the source code for proprietary components outside of the FreeRTOS
    kernel.  FreeRTOS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
    more details. You should have received a copy of the GNU General Public
    License and the FreeRTOS license exception along with FreeRTOS; if not it
    can be viewed here: http://www.freertos.org/a00114.html and also obtained
    by writing to Richard Barry, contact details for whom are available on the
    FreeRTOS WEB site.

    1 tab == 4 spaces!

    http://www.FreeRTOS.org - Documentation, latest information, license and
    contact details.

    http://www.SafeRTOS.com - A version that is certified for use in safety
    critical systems.

    http://www.OpenRTOS.com - Commercial support, development, porting,
    licensing and training services.
*/

/* Standard includes. */
#include "string.h"

#include "stm32F7xx_hal.h"

extern UART_HandleTypeDef huart3;
#include "cmsis_os.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+IO includes. */
//#include "FreeRTOS_IO.h"

/* Example includes. */
#include "FreeRTOS_CLI.h"
#include "UART-interrupt-driven-command-console.h"
#include "fifo.h"

/* Dimensions the buffer into which input characters are placed. */
#define cmdMAX_INPUT_SIZE			1000

/* Place holder for calls to ioctl that don't use the value parameter. */
#define cmdPARAMTER_NOT_USED		( ( void * ) 0 )

/* Block times of 50 and 500milliseconds, specified in ticks. */
#define cmd50ms						( ( void * ) ( 50UL / portTICK_RATE_MS ) )
#define cmd500ms					( ( void * ) ( 500UL / portTICK_RATE_MS ) )
/*-----------------------------------------------------------*/



#define UART_FIFO_SIZE	256
FIFO(UART_FIFO_SIZE) uart_rx_fifo;

extern UART_HandleTypeDef huart3;
/*
 * The task that implements the command console processing.
 */
static void prvUARTCommandConsoleTask(void const * pvParameters );

/*-----------------------------------------------------------*/

/* Holds the handle of the task that implements the UART command console. */
static xTaskHandle xCommandConsoleTask = NULL;

static const int8_t * const pcWelcomeMessage = ( int8_t * ) "FreeRTOS command server.\r\nType Help to view a list of registered commands.\r\n\r\n>";
static const int8_t * const pcNewLine = ( int8_t * ) "\r\n";
static const int8_t * const pcEndOfCommandOutputString = ( int8_t * ) "\r\n[Press ENTER to execute the previous command again]\r\n>";

/* The peripheral used by the command interpreter (and the vWriteString()
function). */
//static Peripheral_Descriptor_t xConsoleUART = NULL;

/*-----------------------------------------------------------*/
osThreadId UARTCmdTaskHandle;

void Cmd_UART_Rx(UART_HandleTypeDef *huart)
{
	  char ch;

		ch = huart->Instance->RDR;
		if( !FIFO_IS_FULL( uart_rx_fifo ) ) 
		{
			FIFO_PUSH( uart_rx_fifo, ch );
		}
}

void Cmd_UART_Tx(UART_HandleTypeDef *huart,  char *str, uint16_t len)
{
		uint16_t cnt=0;
		for(cnt = 0; cnt < len; cnt++)
		{
				huart->Instance->TDR = str[cnt];	
				while(!(__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC)));
		}

}

void vUARTCommandConsoleStart( void )
{
	vRegisterCLICommands();

  osThreadDef(UARTCmdTask, prvUARTCommandConsoleTask, osPriorityHigh, 0, 512);
  UARTCmdTaskHandle = osThreadCreate(osThread(UARTCmdTask), NULL);
		__HAL_UART_CLEAR_OREFLAG(&huart3);
		__HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
  
//	xTaskCreate( 	prvUARTCommandConsoleTask,				/* The task that implements the command console. */
//					( const int8_t * const ) "UARTCmd",		/* Text name assigned to the task.  This is just to assist debugging.  The kernel does not use this name itself. */
//					configUART_COMMAND_CONSOLE_STACK_SIZE,	/* The size of the stack allocated to the task. */
//					NULL,									/* The parameter is not used, so NULL is passed. */
//					configUART_COMMAND_CONSOLE_TASK_PRIORITY,/* The priority allocated to the task. */
//					&xCommandConsoleTask );					/* Used to store the handle to the created task. */
}
/*-----------------------------------------------------------*/
static int8_t cInputString[ cmdMAX_INPUT_SIZE ], cLastInputString[ cmdMAX_INPUT_SIZE ];
static void prvUARTCommandConsoleTask(void const * pvParameters )
{
int8_t cRxedChar, cInputIndex = 0, *pcOutputString;

portBASE_TYPE xReturned;

	( void ) pvParameters;

	/* Obtain the address of the output buffer.  Note there is no mutual
	exclusion on this buffer as it is assumed only one command console
	interface will be used at any one time. */
	pcOutputString = FreeRTOS_CLIGetOutputBuffer();

	/* Open the UART port used for console input.  The second parameter
	(ulFlags) is not used in this case.  The default board rate is set by the
	boardDEFAULT_UART_BAUD parameter.  The baud rate can be changed using a
	FreeRTOS_ioctl() call with the ioctlSET_SPEED command. */
//	xConsoleUART = FreeRTOS_open( boardCOMMAND_CONSOLE_UART, ( uint32_t ) cmdPARAMTER_NOT_USED );
//	configASSERT( xConsoleUART );

	/* Change the Tx usage model from straight polled mode to use zero copy
	buffers with interrupts.  In this mode, the UART will transmit characters
	directly from the buffer passed to the FreeRTOS_write()	function. */
//	xReturned = FreeRTOS_ioctl( xConsoleUART, ioctlUSE_ZERO_COPY_TX, cmdPARAMTER_NOT_USED );
//	configASSERT( xReturned );

	/* Change the Rx usage model from straight polled mode to use a character
	queue.  Character queue reception is appropriate in this case as characters
	can only be received as quickly as they can be typed, and need to be parsed
	character by character. */
//	xReturned = FreeRTOS_ioctl( xConsoleUART, ioctlUSE_CHARACTER_QUEUE_RX, ( void * ) cmdMAX_INPUT_SIZE );
//	configASSERT( xReturned );

	/* By default, the UART interrupt priority will have been set to the lowest
	possible.  It must be kept at or below configMAX_LIBRARY_INTERRUPT_PRIORITY,
	but	can be raised above its default priority using a FreeRTOS_ioctl() call
	with the ioctlSET_INTERRUPT_PRIORITY command. */
//	xReturned = FreeRTOS_ioctl( xConsoleUART, ioctlSET_INTERRUPT_PRIORITY, ( void * ) ( configMIN_LIBRARY_INTERRUPT_PRIORITY - 1 ) );
//	configASSERT( xReturned );

	/* Send the welcome message. */
//	if( FreeRTOS_ioctl( xConsoleUART, ioctlOBTAIN_WRITE_MUTEX, cmd50ms ) == pdPASS )
//	{
//		FreeRTOS_write( xConsoleUART, pcWelcomeMessage, strlen( ( char * ) pcWelcomeMessage ) );
//	}
//        if(UART_CheckIdleState(&huart3) == HAL_OK)
//        {
//          HAL_UART_Transmit(&huart3, (uint8_t *) pcWelcomeMessage, strlen( ( char * ) pcWelcomeMessage ) ,strlen( ( char * ) pcWelcomeMessage ) );

//        }
					Cmd_UART_Tx(&huart3, (uint8_t *) pcWelcomeMessage, strlen( ( char * ) pcWelcomeMessage ));
        
	for( ;; )
	{
		/* Only interested in reading one character at a time. */
//		FreeRTOS_read( xConsoleUART, &cRxedChar, sizeof( cRxedChar ) );
            //    HAL_UART_Receive(&huart3, (uint8_t *) &cRxedChar, sizeof( cRxedChar ), 0xFFFFFFFF );
		
		while(FIFO_IS_EMPTY( uart_rx_fifo ))
		{
				vTaskDelay(5);
		}

				cRxedChar = FIFO_FRONT( uart_rx_fifo );
				FIFO_POP( uart_rx_fifo );


		/* Echo the character back. */
//		if( FreeRTOS_ioctl( xConsoleUART, ioctlOBTAIN_WRITE_MUTEX, cmd50ms ) == pdPASS )
//		{
//			FreeRTOS_write( xConsoleUART, &cRxedChar, sizeof( cRxedChar ) );
//		}
//                if(UART_CheckIdleState(&huart3) == HAL_OK)
//                {
//                  HAL_UART_Transmit(&huart3, (uint8_t *) &cRxedChar, sizeof( cRxedChar ), sizeof( cRxedChar ) );
//                }
								
								Cmd_UART_Tx(&huart3,  &cRxedChar, 1 );
                
		if( cRxedChar == '\r' )
		{
			/* The input command string is complete.  Ensure the previous
			UART transmission has finished before sending any more data.
			This task will be held in the Blocked state while the Tx completes,
			if it has not already done so, so no CPU time will be wasted by
			polling. */
//			if( FreeRTOS_ioctl( xConsoleUART, ioctlOBTAIN_WRITE_MUTEX, cmd50ms ) == pdPASS )
//			{
//				/* Start to transmit a line separator, just to make the output
//				easier to read. */
//				FreeRTOS_write( xConsoleUART, pcNewLine, strlen( ( char * ) pcNewLine ) );
//			}
//                      if(UART_CheckIdleState(&huart3) == HAL_OK)
//                      {
//                        HAL_UART_Transmit(&huart3, (uint8_t *) pcNewLine, strlen( ( char * ) pcNewLine ), strlen( ( char * ) pcNewLine ) );

//                      }
			
			 Cmd_UART_Tx(&huart3, (uint8_t *) pcNewLine, strlen( ( char * ) pcNewLine ));
			/* See if the command is empty, indicating that the last command is
			to be executed again. */
			if( cInputIndex == 0 )
			{
				strncpy( ( char * ) cInputString, ( char * ) cLastInputString, cmdMAX_INPUT_SIZE );
			}

			/* Pass the received command to the command interpreter.  The
			command interpreter is called repeatedly until it returns
			pdFALSE as it might generate more than one string. */
			do
			{
				/* Once again, just check to ensure the UART has completed
				sending whatever it was sending last.  This task will be held
				in the Blocked state while the Tx completes, if it has not
				already done so, so no CPU time	is wasted polling. */
//				xReturned = FreeRTOS_ioctl( xConsoleUART, ioctlOBTAIN_WRITE_MUTEX, cmd50ms );
          //                      if(UART_CheckIdleState(&huart3) == HAL_OK)
                                  xReturned = pdPASS;
                                
				if( xReturned == pdPASS )
				{
					/* Get the string to write to the UART from the command
					interpreter. */
					xReturned = FreeRTOS_CLIProcessCommand( cInputString, pcOutputString, configCOMMAND_INT_MAX_OUTPUT_SIZE );

					/* Write the generated string to the UART. */
//					FreeRTOS_write( xConsoleUART, pcOutputString, strlen( ( char * ) pcOutputString ) );
           //                             HAL_UART_Transmit(&huart3, (uint8_t *) pcOutputString, strlen( ( char * ) pcOutputString ), strlen( ( char * ) pcOutputString ) );
								Cmd_UART_Tx(&huart3, (uint8_t *) pcOutputString, strlen( ( char * ) pcOutputString ));
				}

			} while( xReturned != pdFALSE );

			/* All the strings generated by the input command have been sent.
			Clear the input	string ready to receive the next command.  Remember
			the command that was just processed first in case it is to be
			processed again. */
			strncpy( ( char * ) cLastInputString, ( char * ) cInputString, cmdMAX_INPUT_SIZE );
			cInputIndex = 0;
			memset( cInputString, 0x00, cmdMAX_INPUT_SIZE );

			/* Ensure the last string to be transmitted has completed. */
//			if( FreeRTOS_ioctl( xConsoleUART, ioctlOBTAIN_WRITE_MUTEX, cmd50ms ) == pdPASS )
//			{
//				/* Start to transmit a line separator, just to make the output
//				easier to read. */
//				FreeRTOS_write( xConsoleUART, pcEndOfCommandOutputString, strlen( ( char * ) pcEndOfCommandOutputString ) );
//			}
//                      if(UART_CheckIdleState(&huart3) == HAL_OK)
//                      {
//                        HAL_UART_Transmit(&huart3, (uint8_t *) pcEndOfCommandOutputString, strlen( ( char * ) pcEndOfCommandOutputString ), strlen( ( char * ) pcEndOfCommandOutputString ) );

//                      }
											
											Cmd_UART_Tx(&huart3, (uint8_t *) pcEndOfCommandOutputString, strlen( ( char * ) pcEndOfCommandOutputString ));
		}
		else
		{
			if( cRxedChar == '\r' )
			{
				/* Ignore the character. */
			}
			else if( cRxedChar == '\b' )
			{
				/* Backspace was pressed.  Erase the last character in the
				string - if any. */
				if( cInputIndex > 0 )
				{
					cInputIndex--;
					cInputString[ cInputIndex ] = '\0';
				}
			}
			else
			{
				/* A character was entered.  Add it to the string
				entered so far.  When a \n is entered the complete
				string will be passed to the command interpreter. */
				if( ( cRxedChar >= ' ' ) && ( cRxedChar <= '~' ) )
				{
					if( cInputIndex < cmdMAX_INPUT_SIZE )
					{
						cInputString[ cInputIndex ] = cRxedChar;
						cInputIndex++;
					}
				}
			}
		}
	}
}
/*-----------------------------------------------------------*/

void vOutputString( const uint8_t * const pucMessage )
{
	/* Obtaining the write mutex prevents strings output using this function
	from corrupting strings output by the command interpreter task (and visa
	versa).  It does not, however, prevent complete strings output using this
	function intermingling with complete strings output from the command
	interpreter as the command interpreter only holds the mutex on an output
	string by output string basis. */
//	if( xConsoleUART != NULL )
//	{
//		if( FreeRTOS_ioctl( xConsoleUART, ioctlOBTAIN_WRITE_MUTEX, cmd500ms ) == pdPASS )
//		{
//			FreeRTOS_write( xConsoleUART, pucMessage, strlen( ( char * ) pucMessage ) );
//		}
//	}
  

      Cmd_UART_Tx(&huart3, (uint8_t *) pucMessage, strlen( ( char * ) pucMessage ));


}
/*-----------------------------------------------------------*/


