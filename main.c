/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include <project.h>
#include "stdio.h"
#include "stdlib.h"

#define true 1
#define false 0
/* Define variables for the UBS device */
#define DEVICE_ID 0
#define IN_ENDPOINT 0X01
#define OUT_ENDPOINT 0x02
#define MAX_NUM_BYTES 32  // how big to make the IN and OUT ENDPOINT BUFFERS
#define MAX_DATA_BUFFER 16 // make this MAX_NUM_BYTES / 2
/* define how big to make the arrays for the lut for dac and how big
 * to make the adc data array*/ 
#define MAX_LUT_SIZE 4000
#define ADC_channels 4

/* External variable of the device address located in USBFS.h */
extern uint8 USB_deviceAdress;

/* Make a structure to hold the information needed to make a triangle waveform
for the cyclic voltametry protocol */
struct wv {
    uint16 low_amplitude;
    uint16 high_amplitude;
    uint16 divider;
    uint16 length_side;
    uint16 length_total;
};
struct wv waveform;  // create a global of the waveform struct

/* make buffers for the USB ENDPOINTS */
uint8 IN_Data_Buffer[MAX_NUM_BYTES];// = {1,2,3,4,5,0,0,0,0,0};
uint8 OUT_Data_Buffer[MAX_NUM_BYTES];
char LCD_str[32];  // buffer for LCD screen, make it extra big to avoid overflow
int IN_EP_count = 0;  // for debug
int OUT_EP_count = 0;  // for debug

int16 ADC_array[ADC_channels][MAX_LUT_SIZE];  // allocate space to put adc measurements
uint8 Input_Flag = false;  // if there is an input, set this flag to process it

/* Make global variables needed for the DAC interrupt service routine */
uint8 msb_waveform_lut[MAX_LUT_SIZE];  // look up table for waveform values to put into msb DAC
uint8 lsb_waveform_lut[MAX_LUT_SIZE];  // same as above for lsb
int16 lut_index;  // look up table index
uint8 msb_lut_current_value;  // value need to load into msb PIDAC
uint8 lsb_lut_current_value;  // value to load into lsb PIDAC

/* function prototypes */
void send_message(char* message, uint8 n);
void Export_Data(int16 array[], int size);
uint16 Convert2Dec(uint8 array[], uint8 len);
void MakeTriangleWave(void);
void ArrayCheck(void);
void CheckInput(void);
void debug_LCD(uint8 row, uint var1, uint var2, uint test_num);
void button_wait(void);


CY_ISR(dacInterrupt)
{
    //ADC_array[0][lut_index] = PWM_isr_ReadCounter();  // for debug
    IDAC_msb_SetValue(msb_lut_current_value);
    IDAC_lsb_SetValue(lsb_lut_current_value);
    lut_index++;
    if (lut_index > waveform.length_total)  // all the data points have been given
    {
        isr_dac_Disable();
        isr_adc_Disable();
        lut_index = 0;         
        send_message("Done", 32);  // calls a function in an isr but only after the isr has been disabled
        LCD_Position(1,0);
        LCD_PrintString("Done running");
    }
    msb_lut_current_value = msb_waveform_lut[lut_index];
    lsb_lut_current_value = lsb_waveform_lut[lut_index];

}
CY_ISR(adcInterrupt)
{
    //ADC_array[0][lut_index] = PWM_isr_ReadCounter();
    ADC_StartConvert();
    ADC_IsEndConversion(ADC_WAIT_FOR_RESULT);
    ADC_array[0][lut_index] = ADC_GetResult16();
    //ADC_array[0][lut_index] = read_adc
}

int main()
{
    /* Place your initialization/startup code here */
    /* Initialize all the interrupts */
    isr_dac_StartEx(dacInterrupt);
    isr_dac_Disable();  // disable interrupt until a voltage signal needs to be given
    isr_adc_StartEx(adcInterrupt);
    isr_adc_Disable();
    
    /* Initialize all the hardware components */
    ADC_Start();
    TIA_Start();
    VDAC_TIA_Start();
    Timer_Start();
    IDAC_msb_Start();
    IDAC_lsb_Start();
    PWM_isr_Start();
    Opamp_Aux_Start();
    
    CyGlobalIntEnable; 
    LED_Write(0);  // for debug
    LED4_Write(0);  // for debug
    
    LCD_Start();  // for debug
    LCD_Position(0,0);  // for debug
    LCD_PrintString("Amp rebuild");  // for debug
    
    USBFS_Start(0, USBFS_5V_OPERATION);  // initialize the USB
    
    while(!USBFS_bGetConfiguration());  //Wait till it the usb gets its configuration from the PC ??
    USBFS_EnableOutEP(OUT_ENDPOINT);  // changed
    
    for (int i=0; i<MAX_LUT_SIZE; i++)
    {
        ADC_array[1][i] = i;
    }
    
    for(;;)
    {
        /* check if there is a response from the computer */
        CheckInput();

        if (Input_Flag == true)
        {
            switch (OUT_Data_Buffer[0]) 
            {  // OUT is in reference from out of the computer,
            // to the PSoC, the OUT data is actually input
            case 'E': ; // User wants to export the data, the user can choose what ADC array to export
                //LED_Write(1);  // for debug
                uint8 user_ch = OUT_Data_Buffer[1]-'0';
                if (user_ch <= ADC_channels)  // check for buffer overflow
                {
                    LED_Write(0);
                    Export_Data(ADC_array[user_ch], waveform.length_total);
                    LED_Write(1);
                    LCD_Position(0,0);
                    LCD_PrintString("Done Export");
                }
                else
                {
                    LCD_Position(0,0);
                    LCD_PrintString("Buff Ovrflw req");
                }
                break;
            case 'C': ;  // change the compare value of the PWM to start the adc isr
                uint16 CMP = Convert2Dec(&OUT_Data_Buffer[12], 5);
                PWM_isr_WriteCompare(CMP);
                break;
            case 'A': ; // input a 0-7 string into the device
                uint8 TIA_resistor_value = OUT_Data_Buffer[1]-'0';
                TIA_SetResFB(TIA_resistor_value);  // see TIA.h for how the values work, basically 0 - 20k, 1 -30k, etc.
                break;
            case 'R': ;
                LCD_Position(1,0);
                LCD_PrintString("Running");
                if (!isr_dac_GetState())  // enable the dac isr if it isnt already enabled
                {
                    isr_dac_Enable();
                    isr_adc_Enable();
                }
                break;
            case 'X': ; // reset the device by disabbleing isrs
                isr_dac_Disable();
                isr_adc_Disable();
                lut_index = 0;     
            case 'S': ; // getting the starting bit of the dacs, use ; after : to make an empty statement to prevent error
                /*Get the string of data that contains the start and end voltges of the
                triangle wave and produce the memory */
                
                waveform.low_amplitude = Convert2Dec(&OUT_Data_Buffer[2], 4);
                waveform.high_amplitude = Convert2Dec(&OUT_Data_Buffer[7], 4);
                waveform.divider = Convert2Dec(&OUT_Data_Buffer[12], 5);
                PWM_isr_WritePeriod(waveform.divider);
                
                // put the data in a string to be put on the LCD screen
                sprintf(LCD_str, "s:%i |", waveform.divider);
                LCD_Position(1,0);
                LCD_PrintString(LCD_str);
                MakeTriangleWave();
                break; 
            }  // end of switch statment
            OUT_Data_Buffer[0] = '0';  // clear data buffer cause it has been processed
            Input_Flag = false;  // turn off input flag because it has been processed
        }
    }  // end of for loop in main
}  // end of main


void send_message(char* message, uint8 n)
{
    strncpy(IN_Data_Buffer, message, n);
    while(USBFS_GetEPState(IN_ENDPOINT) != USBFS_IN_BUFFER_EMPTY)
    {
        
    }
    LED4_Write(1);  // for debug  
    if(USBFS_GetEPState(IN_ENDPOINT) == USBFS_IN_BUFFER_EMPTY)
    {
        USBFS_LoadEP(IN_ENDPOINT, IN_Data_Buffer, MAX_NUM_BYTES);
        USBFS_EnableOutEP(OUT_ENDPOINT);
    }
}

void CheckInput(void)
{
    if(USBFS_GetEPState(OUT_ENDPOINT) == USBFS_OUT_BUFFER_FULL)
    {
        /* There is data coming in, get the number of bytes*/
        uint8 OUT_COUNT = USBFS_GetEPCount(OUT_ENDPOINT);
        /* Read the OUT endpoint and store data in OUT_Data_buffer */
        USBFS_ReadOutEP(OUT_ENDPOINT, OUT_Data_Buffer, OUT_COUNT);
        /* Re-enable OUT endpoint */
        USBFS_EnableOutEP(OUT_ENDPOINT);
        /* Signal that the LCD display should be updated */
        OUT_EP_count++;  // for debug
        Input_Flag = true;
    }
}

uint16 Convert2Dec(uint8 array[], uint8 len)
{
    uint16 num = 0;
    for (int i = 0; i < len; i++)
    {
        num = num * 10 + (array[i] - '0');
    }
    return num;
}

void MakeTriangleWave(void)
{
    /* Make a look up table (LUT) for the two parrallel current dacs */
  //FIX THIS  //isr_dac_Disable();  // disable the isr incase a waveform is currently running
    lut_index = 0;  // start at the beginning of the lut
    // number of points on the side of the triangle waveform 
    waveform.length_side = waveform.high_amplitude - waveform.low_amplitude;  
    waveform.length_total = 2*(waveform.length_side+1);

    for (int dacValue = waveform.low_amplitude; dacValue <= waveform.high_amplitude; dacValue++)
    {
        /* split the values into 2 bytes, msb and lsb to use the 8 bit dacs */
        msb_waveform_lut[lut_index] = (uint8)(dacValue >> 3);
        lsb_waveform_lut[lut_index] = (uint8)((dacValue <<0) & 0x07);
        ADC_array[1][lut_index] = (uint16) dacValue;  // TOCHECK
        lut_index++;
    }
    for (int dacValue = waveform.high_amplitude; dacValue >= waveform.low_amplitude; dacValue--)
    {
        msb_waveform_lut[lut_index] = (uint8)(dacValue >> 3);
        lsb_waveform_lut[lut_index] = (uint8)((dacValue <<0) & 0x07);
        ADC_array[1][lut_index] = (uint16) dacValue;  // TOCHECK
        lut_index++;
    }
    lut_index = 0;
    msb_lut_current_value = msb_waveform_lut[lut_index];
    lsb_lut_current_value = lsb_waveform_lut[lut_index];
}

void ArrayCheck(void)  // for debug
{
    //LED3_Write(1);
    CyDelay(1000);
    //LED3_Write(0);
}

void Export_Data(int16 array[], int size)
{
    LCD_Position(0,0);
    sprintf(LCD_str, "Exp Sz:%i |  ", size);
    LCD_PrintString(LCD_str);
    int pre_buffer_index = 0;
    uint8 pre_buffer[MAX_NUM_BYTES];
    for(int i = 0; i <= size; i++)  // have to go through all the data and send it out the usb
    { // check if the old buffer was filled and sent and now needs to be cleared
        
        if (i % MAX_DATA_BUFFER == 0)   
        {
            pre_buffer_index = 0;  //clear the buffer_index
            for (int j=0; j < MAX_NUM_BYTES; j++)
            {
                pre_buffer[j] = 0;  // clear the pre_buffer
            }
        }
        // convert the uint16 data to two uint8 bytes to send
        pre_buffer[2*pre_buffer_index] = (array[i] >> 8);
        pre_buffer[2*pre_buffer_index + 1] = array[i] & 0xFF;
        pre_buffer_index++;
//        LCD_Position(0,0);
//        sprintf(LCD_str, "pre_buf_ind: %i  |", pre_buffer_index);
//        LCD_PrintString(LCD_str);
        //CyDelay(500);
        
        if ((i % MAX_DATA_BUFFER) == (MAX_DATA_BUFFER-1))  // the pre buffer is full
        {  // load the pre buffer into the usb hub to send it
            LED4_Write(0);  // for debug  
            while(USBFS_GetEPState(IN_ENDPOINT) != USBFS_IN_BUFFER_EMPTY)
            LED4_Write(1);  // for debug  
            if(USBFS_GetEPState(IN_ENDPOINT) == USBFS_IN_BUFFER_EMPTY)
            {
                USBFS_LoadEP(IN_ENDPOINT, pre_buffer, MAX_NUM_BYTES);
                USBFS_EnableOutEP(OUT_ENDPOINT);
            }
            LCD_Position(1,0);
            sprintf(LCD_str, "ExpC:%i|%i|  ", pre_buffer_index, i);
            LCD_PrintString(LCD_str);
        }
        
        //button_wait();
    }
    // send last chunk of data out
    LCD_Position(1,0);
    sprintf(LCD_str, "ExpC:%i |  ", size);
    //LED4_Write(0);  // for debug
    while(USBFS_GetEPState(IN_ENDPOINT) != USBFS_IN_BUFFER_EMPTY)
    {
        
    }
    //LED4_Write(1);  // for debug
    if(USBFS_GetEPState(IN_ENDPOINT) == USBFS_IN_BUFFER_EMPTY)
    {
        USBFS_LoadEP(IN_ENDPOINT, pre_buffer, MAX_NUM_BYTES);
        USBFS_EnableOutEP(OUT_ENDPOINT);
    }
    //LCD_Position(1,0);
    //sprintf(LCD_str, "ExpC:%i |  ", size);
    //LCD_PrintString(LCD_str);
    debug_LCD(1, size, 0, 1);
}

void debug_LCD(uint8 row, uint var1, uint var2, uint test_num)
{
    LCD_Position(row,0);
    sprintf(LCD_str, "t%i:%i|%i ", test_num, var1, var2);
    LCD_PrintString(LCD_str);
}

void button_wait(void)
{
    //LCD_Position(1,0);
    //LCD_PrintString("Waiting for SW3");
    for(;;)
    {
        if(SW3_Read()==0)
        {
            //LCD_Position(1,0);
            //LCD_PrintString("Read SW3");
            CyDelay(200);
            return;
        }
    }
}

/* [] END OF FILE */
