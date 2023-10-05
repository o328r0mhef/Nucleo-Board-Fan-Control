#include "mbed.h" 
#include <cstdio>
#include "PwmOut.h"
#include "TextLCD.h"

// Define timers
Timer t;        // Tachometer timer
Timer t_b;      // Button timer
Timer t_lcd_b;  // LCD refresh timer
Timer t_lcd_d;  // LCD button state refresh timer

// Define interrupt ins
InterruptIn enc_A(PA_1);
InterruptIn enc_B(PA_4);
InterruptIn taco(PA_0);
InterruptIn button(BUTTON1);

// Define digital outputs
DigitalOut LED(PC_0);
DigitalOut LED_BI_R(PA_15);
DigitalOut LED_BI_G(PB_7);
PwmOut pwm(PB_0);

// Define LCD pins
TextLCD lcd(PB_15, PB_14, PB_10, PA_8, PB_2, PB_1);

// Define global data types
int prev_state;
int current_state;
int counter=0;
int expected_rpm;
int interrupt_change=0;
int button_state=1;
int lcd_rpm_state = 0;

bool feedback_state = 0;
bool enc_flag = 0;

float half_cycle;
float pwm_val;
float elapsed_t_b;
float elapsed_t;
float elapsed_t_lcd;
float measured_rpm;
float averaged;
float rpm_average[20]={0};
float error_;

// Define LCD Structure 
struct LCD_variables {
    bool flag_off = 0;
    bool flag = 0;   
    bool flag_2 = 0;
};

struct LCD_variables lcd_v;


//---------------- Tachometer ISRs ----------------
void tach_pulse1(){
    t.start(); 
    interrupt_change ++;
}

void tach_pulse2(){
    elapsed_t = t.elapsed_time().count();
     if (elapsed_t > 13000){
         half_cycle = elapsed_t;
         interrupt_change++;
         t.reset();
     }
    interrupt_change++;
}


//---------------- Fan control ----------------
void start_inertia(){
    pwm.write(0.5);
    wait_us(500000);
    counter = 15;
}

// Encoder ISR determines PWM to drive fan
void encoder(){
    current_state = enc_A;

    // Determine encoder direction from current and prevous state of encoder inputs
    if (prev_state != current_state){
        counter = enc_B != current_state ? counter + button_state : counter - button_state;
   
   // If fan at rest, drive with 50% duty cycle for 0.5s
    if (counter > 0 && enc_flag == 0){
        counter = 15;
        enc_flag = 1;
        lcd_v.flag_off = 1;
        start_inertia();
    }

    // PWM written to fan set in range 15-100
    if (counter < 15 && enc_flag == 1){
        counter = 0;
        enc_flag = 0;
    }

    if (counter > 100){
        counter = 100;
    }

    if (counter < 0){
        counter = 0;
    }
    
    pwm_val = float(counter)/100.0;

    // Calculate proportional RPM to maximum speed
    expected_rpm = counter * 21; 

    // Update previous state of encoder
    prev_state = current_state;
    }
}

//---------------- Feedback ----------------
void feedback_control(){
    pwm_val = float(counter)/100.0;
    if (feedback_state == 1){       // Implement proportional closed loop control if in closed loop state
        float kp = 0.01;
        error_ =  (expected_rpm - measured_rpm) / 2100;
        pwm_val = pwm_val + kp * error_;
    }
}


//---------------- Button Control ----------------
void button_press(){
    t_b.start();
}

void button_time(){
    elapsed_t_b = t_b.elapsed_time().count();

    // If the button is held for over a second, switch feedbak state
    if (elapsed_t_b > 2000000){
        if (feedback_state == 0){
            feedback_state = 1;
        }
        else if (feedback_state == 1){
            feedback_state = 0;
        }
    }
    else{
        if (button_state == 10){
        button_state = 1;
    }
        else if (button_state == 1){
            button_state = 2;
        }
        else if (button_state == 2){
        button_state = 5;
        }
        else if (button_state == 5){
        button_state = 10;
        } 
    }
    t_b.reset();
    
}

//---------------- LED Control ----------------
void LED_control(){
    if (feedback_state == 0){
            LED_BI_R = 0;
            LED_BI_G = 1;
        }
    if (feedback_state == 1){
        LED_BI_R = 1;
        LED_BI_G = 0;
    }
    if (counter >= 15){
        LED = 1;
    }
    if (counter < 15){
        LED = 0;
    }
}

//---------------- LCD Control ----------------
void LCD_control(){

    if (t_lcd_b.elapsed_time().count() > 100000){
        lcd.printf("M_RPM:%d\n", int(measured_rpm));
        lcd.printf("D_RPM:%d", expected_rpm);
        lcd.locate(12, 1);
        lcd.printf("B:%d\n", button_state);
        t_lcd_b.reset();
    }
    // Refresh button state on LCD
    if (button_state == 10){
        lcd_v.flag = 1;
    }

    // Refresh PWM on LCD
    if (counter < 100 && lcd_v.flag_2 == 1){
        lcd.cls();
        lcd_v.flag_2 = 0;
    }
    if (counter == 100 && lcd_v.flag_2 == 0){
        lcd_v.flag_2 = 1;
    }

    // Refresh RPM on LCD
    if (t_lcd_d.elapsed_time().count() > 500000){ // Cap refresh rate
        t_lcd_d.reset();
        if (button_state == 1 && lcd_v.flag == 1){
            lcd.cls();
            lcd_v.flag = 0;
        }

        if (measured_rpm < 100 && measured_rpm > 9 && lcd_rpm_state != 0){ // double digit
            lcd_rpm_state = 0;
            lcd.cls();
        }
        if (measured_rpm < 1000 && measured_rpm > 99 && lcd_rpm_state != 1){ // triple digit
            lcd_rpm_state = 1;
            lcd.cls();
        }
        if (measured_rpm < 10000 && measured_rpm > 999 && lcd_rpm_state != 2){ // quadruple digit
            lcd_rpm_state = 2;
            lcd.cls();
        }
    }
}


int main()
{
    // Trigger encoder and button ISRs 
    enc_A.rise(&encoder);
    enc_B.rise(&encoder);
    enc_A.fall(&encoder);
    enc_B.fall(&encoder);
    button.fall(&button_press);
    button.rise(&button_time);

    // Write to fan initally
    pwm.write(pwm_val);
    pwm.period(0.05f);
    
    while(true)
    {      
        // Alternate between tachometer ISRs
        if (interrupt_change % 2 == 0 ){
            taco.rise(&tach_pulse1);
        }
        else if (interrupt_change % 2 != 0 ){
            taco.rise(&tach_pulse2);
        }

        // Set RPM to 0 if fan driven below a duty cycle of 15%
        if (counter < 15){
            measured_rpm = 0;
            }
        else{
            // Calculate measured RPM from tachometer ISRs
            measured_rpm = 60/(2*(half_cycle/1000000));

        // Filter
        for (int k = 0; k < 20; k++){
            rpm_average[k+1] = rpm_average[k];  // Shift average right
            }
        
        rpm_average[0] = measured_rpm;          // Update 0th term
        
        for (int k = 0; k < 20; k++){
            measured_rpm = measured_rpm + rpm_average[k];
            }
        measured_rpm = measured_rpm / 20;       // Average times

        }
        
        // Write to fan
        pwm.write(pwm_val); 

        // Refresh LCD if fan driven bellow a duty cycle of 15%
        if (counter < 15 && lcd_v.flag_off == 1){
            lcd.cls();
            lcd_v.flag_off = 0;
        }

        LED_control();
        feedback_control();
        LCD_control();

        t_lcd_b.start();
        t_lcd_d.start();
    }
}