#include <stdint.h>

// Clock and port setup
#define SYSCTL_RCGCGPIO_R      (*((volatile uint32_t *)0x400FE608))

// PORT A - IR Sensors
#define GPIO_PORTA_DIR_R       (*((volatile uint32_t *)0x40004400))
#define GPIO_PORTA_DEN_R       (*((volatile uint32_t *)0x4000451C))
#define GPIO_PORTA_DATA_R      (*((volatile uint32_t *)0x400043FC))
#define GPIO_PORTA_PUR_R       (*((volatile uint32_t *)0x40004510))
#define SENSOR_A 0x04 // PA2
#define SENSOR_B 0x08 // PA3
#define SENSOR_C 0x10 // PA4
#define SENSOR_D 0x20 // PA5

// PORT B - LCD
#define GPIO_PORTB_DIR_R       (*((volatile uint32_t *)0x40005400))
#define GPIO_PORTB_DEN_R       (*((volatile uint32_t *)0x4000551C))
#define GPIO_PORTB_DATA_R      (*((volatile uint32_t *)0x400053FC))
#define LCD_RS  0x01
#define LCD_EN  0x02
#define LCD_D4  0x04
#define LCD_D5  0x08
#define LCD_D6  0x10
#define LCD_D7  0x20

// PORT C - LEDs
#define GPIO_PORTC_DIR_R       (*((volatile uint32_t *)0x40006400))
#define GPIO_PORTC_DEN_R       (*((volatile uint32_t *)0x4000651C))
#define GPIO_PORTC_DATA_R      (*((volatile uint32_t *)0x400063FC))
#define GREEN_LED  0x10 // PC4
#define YELLOW_LED 0x20 // PC5
#define RED_LED    0x40 // PC6

// PORT D - Stepper Motor
#define GPIO_PORTD_DIR_R       (*((volatile uint32_t *)0x40007400))
#define GPIO_PORTD_DEN_R       (*((volatile uint32_t *)0x4000751C))
#define GPIO_PORTD_DATA_R      (*((volatile uint32_t *)0x40007030))
#define GPIO_PORTD_AFSEL_R     (*((volatile uint32_t *)0x40007420))
#define MOTOR_PUL  0x04  // PD2
#define MOTOR_DIR  0x08  // PD3

// PORT F - Emergency Stop Switch (SW1)
#define GPIO_PORTF_DIR_R       (*((volatile uint32_t *)0x40025400))
#define GPIO_PORTF_DEN_R       (*((volatile uint32_t *)0x4002551C))
#define GPIO_PORTF_PUR_R       (*((volatile uint32_t *)0x40025510))
#define GPIO_PORTF_DATA_R      (*((volatile uint32_t *)0x400253FC))
#define EMERGENCY_STOP_PIN     0x10 // PF4 - SW1

// Global variables
volatile uint32_t countA = 0, countB = 0, countC = 0;
volatile uint8_t motorRunning = 0;
volatile uint8_t emergencyFlag = 0;
volatile uint8_t sensorD_count = 0;
volatile uint8_t sensorD_interrupt = 0;
volatile uint8_t motorStarted = 0;

// Function prototypes
void DelayMs(uint32_t time);
void LCD_Init();
void LCD_Cmd(uint8_t cmd);
void LCD_Data(uint8_t data);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_PrintNum(uint32_t num);
void LCD_UpdateCounts();
void Stepper_Init();
void Stepper_Step();
void Motor_On();
void Motor_Off();
void HandleEmergencyStop();
void PortF_Init(void);

// Main function
int main(void) {
    SYSCTL_RCGCGPIO_R |= 0x3F; // Enable clocks for ports A-F
    DelayMs(10); // Clock stabilize

    // Port A - IR Sensors
    GPIO_PORTA_DIR_R &= ~(SENSOR_A | SENSOR_B | SENSOR_C | SENSOR_D);
    GPIO_PORTA_DEN_R |= (SENSOR_A | SENSOR_B | SENSOR_C | SENSOR_D);
    GPIO_PORTA_PUR_R |= (SENSOR_A | SENSOR_B | SENSOR_C | SENSOR_D);

    // Port B - LCD
    GPIO_PORTB_DIR_R |= (LCD_RS | LCD_EN | LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7);
    GPIO_PORTB_DEN_R |= (LCD_RS | LCD_EN | LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7);

    // Port C - LEDs
    GPIO_PORTC_DIR_R |= (GREEN_LED | YELLOW_LED | RED_LED);
    GPIO_PORTC_DEN_R |= (GREEN_LED | YELLOW_LED | RED_LED);

    // Port D - Stepper Motor
    GPIO_PORTD_DIR_R |= (MOTOR_PUL | MOTOR_DIR);
    GPIO_PORTD_DEN_R |= (MOTOR_PUL | MOTOR_DIR);
    GPIO_PORTD_AFSEL_R &= ~(MOTOR_PUL | MOTOR_DIR);
    GPIO_PORTD_DATA_R |= MOTOR_DIR;

    // Port F - SW1 (emergency stop)
    PortF_Init();

    LCD_Init();
    LCD_UpdateCounts();

    uint8_t sensorA_handled = 0, sensorB_handled = 0, sensorC_handled = 0;

    while(1) {
        if ((GPIO_PORTF_DATA_R & EMERGENCY_STOP_PIN) == 0) {
            HandleEmergencyStop();
        }

        if (emergencyFlag) continue;

        if ((GPIO_PORTA_DATA_R & SENSOR_D) == 0 && !sensorD_interrupt) {
            DelayMs(50);
            if ((GPIO_PORTA_DATA_R & SENSOR_D) == 0) {
                if (!motorRunning) {
                    Motor_On();
                    GPIO_PORTC_DATA_R &= ~YELLOW_LED;
                } else {
                    Motor_Off();
									  LCD_SetCursor(1, 0);
                    LCD_Data('O'); LCD_Data('V'); LCD_Data('E'); LCD_Data('R');
                    LCD_Data(' ');LCD_Data('L'); LCD_Data('O'); LCD_Data('A'); LCD_Data('D'); 
                    GPIO_PORTC_DATA_R |= RED_LED;
									  GPIO_PORTC_DATA_R |= YELLOW_LED;
                }
                sensorD_interrupt = 1;
            }
        } else if ((GPIO_PORTA_DATA_R & SENSOR_D) != 0) {
            sensorD_interrupt = 0;
        }

        if ((GPIO_PORTA_DATA_R & (SENSOR_A | SENSOR_B | SENSOR_C)) != (SENSOR_A | SENSOR_B | SENSOR_C)) {
            sensorD_count = 0;
            GPIO_PORTC_DATA_R &= ~RED_LED;
            if (motorRunning) Motor_Off();
            motorStarted = 0;
        }

        if (motorRunning) {
            Stepper_Step();
        }

        if ((GPIO_PORTA_DATA_R & SENSOR_A) == 0 && !sensorA_handled) {
            countA++;
            DelayMs(140);
            sensorA_handled = 1;
            LCD_UpdateCounts();
            Motor_Off();
            DelayMs(1000);
        } else if ((GPIO_PORTA_DATA_R & SENSOR_A) != 0) {
            sensorA_handled = 0;
        }

        if ((GPIO_PORTA_DATA_R & SENSOR_B) == 0 && !sensorB_handled) {
            countB++;
            DelayMs(140);
            sensorB_handled = 1;
            LCD_UpdateCounts();
            Motor_Off();
            DelayMs(1000);
        } else if ((GPIO_PORTA_DATA_R & SENSOR_B) != 0) {
            sensorB_handled = 0;
        }

        if ((GPIO_PORTA_DATA_R & SENSOR_C) == 0 && !sensorC_handled) {
            countC++;
            DelayMs(140);
            sensorC_handled = 1;
            LCD_UpdateCounts();
            Motor_Off();
            DelayMs(1000);
        } else if ((GPIO_PORTA_DATA_R & SENSOR_C) != 0) {
            sensorC_handled = 0;
        }
    }
}

// Port F Init
void PortF_Init(void) {
    GPIO_PORTF_DIR_R &= ~EMERGENCY_STOP_PIN;
    GPIO_PORTF_DEN_R |= EMERGENCY_STOP_PIN;
    GPIO_PORTF_PUR_R |= EMERGENCY_STOP_PIN;
}

// Emergency Stop
void HandleEmergencyStop() {
    DelayMs(50);
    if ((GPIO_PORTF_DATA_R & EMERGENCY_STOP_PIN) == 0 && !emergencyFlag) {  // If SW1 is pressed and emergency not yet triggered
        emergencyFlag = 1;  // Permanently set emergency flag
        Motor_Off();        // Turn off motor immediately
        GPIO_PORTC_DATA_R |= YELLOW_LED;  // Turn on RED and YELLOW LEDs
        GPIO_PORTC_DATA_R &= ~(GREEN_LED | RED_LED);              // Turn off GREEN LED
        
        LCD_SetCursor(1, 0);
        LCD_Data('E'); LCD_Data('M'); LCD_Data('E'); LCD_Data('R');
        LCD_Data('G'); LCD_Data(' '); LCD_Data('S'); LCD_Data('T');
        LCD_Data('O'); LCD_Data('P');

        while (1);  // Halt here forever; only reset button will restart system
    }
}



void Motor_On() {
    motorRunning = 1;
    GPIO_PORTC_DATA_R |= GREEN_LED;
    GPIO_PORTC_DATA_R &= ~RED_LED;
    LCD_SetCursor(1, 0);
    LCD_Data('M'); LCD_Data('o'); LCD_Data('t'); LCD_Data('o'); LCD_Data('r');
    LCD_Data(' '); LCD_Data('O'); LCD_Data('N'); LCD_Data(' ');
}

void Motor_Off() {
    motorRunning = 0;
    GPIO_PORTD_DATA_R &= ~MOTOR_PUL;
    GPIO_PORTC_DATA_R |= RED_LED;
    GPIO_PORTC_DATA_R &= ~GREEN_LED;
    LCD_SetCursor(1, 0);
    LCD_Data('M'); LCD_Data('o'); LCD_Data('t'); LCD_Data('o'); LCD_Data('r');
    LCD_Data(' '); LCD_Data('O'); LCD_Data('F'); LCD_Data('F');
}

void Stepper_Step() {
    GPIO_PORTD_DATA_R |= MOTOR_PUL;
    DelayMs(1);  // Shorter delay than 1ms (~0.2ms or faster)
    GPIO_PORTD_DATA_R &= ~MOTOR_PUL;
    DelayMs(1);  // Tune this number to control speed further
}
void LCD_EnablePulse() {
    GPIO_PORTB_DATA_R |= LCD_EN;
    DelayMs(1);
    GPIO_PORTB_DATA_R &= ~LCD_EN;
    DelayMs(1);
}

void LCD_Send4Bit(uint8_t data) {
    GPIO_PORTB_DATA_R &= ~(LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7);
    if (data & 0x01) GPIO_PORTB_DATA_R |= LCD_D4;
    if (data & 0x02) GPIO_PORTB_DATA_R |= LCD_D5;
    if (data & 0x04) GPIO_PORTB_DATA_R |= LCD_D6;
    if (data & 0x08) GPIO_PORTB_DATA_R |= LCD_D7;
    LCD_EnablePulse();
}

void LCD_Cmd(uint8_t cmd) {
    GPIO_PORTB_DATA_R &= ~LCD_RS;
    LCD_Send4Bit(cmd >> 4);
    LCD_Send4Bit(cmd & 0x0F);
    if (cmd == 0x01 || cmd == 0x02) DelayMs(2);
}

void LCD_Data(uint8_t data) {
    GPIO_PORTB_DATA_R |= LCD_RS;
    LCD_Send4Bit(data >> 4);
    LCD_Send4Bit(data & 0x0F);
    DelayMs(1);
}

void LCD_Init() {
    DelayMs(50);
    LCD_Cmd(0x33);
    LCD_Cmd(0x32);
    LCD_Cmd(0x28);
    LCD_Cmd(0x0C);
    LCD_Cmd(0x06);
    LCD_Cmd(0x01);
    DelayMs(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t address = (row == 0) ? (0x80 + col) : (0xC0 + col);
    LCD_Cmd(address);
}

void LCD_PrintNum(uint32_t num) {
    if (num == 0) {
        LCD_Data('0');
        return;
    }

    char buffer[10];
    uint8_t i = 0;

    while (num > 0) {
        buffer[i++] = (num % 10) + '0';
        num /= 10;
    }

    while (i > 0) {
        LCD_Data(buffer[--i]);
    }
}

void LCD_UpdateCounts() {
    LCD_SetCursor(0, 0);
    LCD_Data('A'); LCD_Data(':'); LCD_PrintNum(countA);
    LCD_Data(' '); LCD_Data('B'); LCD_Data(':'); LCD_PrintNum(countB);
    LCD_Data(' '); LCD_Data('C'); LCD_Data(':'); LCD_PrintNum(countC);
}

void DelayMs(uint32_t time) {
    volatile uint32_t i, j;
    for (i = 0; i < time; i++)
        for (j = 0; j < 1600; j++);
}

