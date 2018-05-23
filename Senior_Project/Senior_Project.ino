///GRANNY SAFE SENIOR PROJECT CODE
///
///Authored by:  Josh Andrews
///              Riley McKay
///
///UMO ECE 402 Fall 2017 Semester
///Last update 12/13/17


//enable arduino support for PID control
#include <PID_v1.h>

//enable support for i2c to 16x2 LCD module
#include <FaBoLCD_PCF8574.h>

//enable support for register access and interrupts
#include <avr/io.h>
#include <avr/interrupt.h>

//enable arduino library for the keypad
#include <Key.h>
#include <Keypad.h>

#define ALARM_PIN     12    //pin 18 on chip, used to control alarm speaker and led
#define SETPOINT_PIN  13    //pin 19 on chip, used control setpoint led
#define ZERO_SENSE    2     //pin 4 on chip, used as external interrupt pin to capture zero crossing event
#define GATE_PIN      3     //pin 5 on chip, used to control the triggering of the triac (pretty sure want PWM here, so chose PWM PIN)
#define HOT_LED_PIN   4     //probably need to change pin (6 on chip currently)
#define KP            40.0     // PID proportional factor
#define KI            1    // PID integral factor
#define KD            10.0    // PID derivative factor  
#define ALR_DELAY     600000   //time in ms until alarm activates
#define OFF_DELAY     60000    //hoow long the alarm sounds until turns off

//used to convert adc reading into degree F                            
#define TEMP_OFFSET   -470.0    
#define TEMP_SLOPE    1.42  //.4545454545

const byte ROWS = 4; // Four rows
const byte COLS = 3; // Three columns

// Define the Keymap
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
// Connect keypad ROW0, ROW1, ROW2 and ROW3 to these Arduino Uno pins
byte rowPins[ROWS] = { 6, 11, 10, 8 };    

// Connect keypad COL0, COL1 and COL2 to these Arduino pins.
byte colPins[COLS] = { 7, 5, 9 }; 

//define the keypad using info above
Keypad kpd = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );
FaBoLCD_PCF8574 lcd;

double temperature = 0;                 // variable to store temperature
double setpoint = 0;                    // variable to store setpoint
double pid_output;                      // variable to store pid calculation result
boolean set_state = false;              // used as flag for setpoint entered or not
boolean hot_led_state = false;          // used as flag for temp > 120F 
unsigned long alarm_timer = 0xFFFFFFFF; // used to store alarm time
const long alarm_period = 1;            //1ms on/off time, 500Hz, closest can get to 1kHz using millis, Blink Without Delay Arduino
unsigned int print_cnt = 50000;                 //used to slow down writing to lcd
int key_temp = 0;                       //key press counter
boolean zero_state = false;             //flag to signal if we are in state where we care about zero-crossing (pid and hotplate control)
//boolean on_state = false;             //if we are a state to care about alarms
int windowsize = 510;                   //500 - 2, practically turns triac off (see ISRs below), if 500 get full wave again 

//define the PID using coeffiecients above, comparing the hotplate temperature to setpoint
//PID out will influence a time delay and reverse mode is need to get proper operation
PID myPID(&temperature, &pid_output, &setpoint, KP, KI, KD, REVERSE);

//the use of serial.print will severly slow down this code
void setup() {

  pinMode(ALARM_PIN, OUTPUT);         //controls alarm speaker and led bjt
  pinMode(SETPOINT_PIN, OUTPUT);      // enables setpoint entered led
  pinMode(ZERO_SENSE, INPUT);         // reads the zero sense signal
  pinMode(GATE_PIN, OUTPUT);          //controls triac gate pin
  pinMode(HOT_LED_PIN, OUTPUT);       //used to control led to signal >120F

  // set up Timer1 
  //(see ATMEGA 328 data sheet pg 134 for more details)
  OCR1A = 2;       //initialize the comparator (5=roughly full sine wave)
  TIMSK1 = 0x03;    //enable comparator A and overflow interrupts
  TCCR1A = 0x00;    //timer 1 control registers set for
  TCCR1B = 0x00;    //normal operation, timer disabled

  //Creates an interrupt triggered on rising edge of zero-crossing circuit output
  //INT0, pin 4 on chip
  attachInterrupt(0, zero_crossing, RISING);  //currently working
  
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  
  // Print labels to LCD.
  lcd.print("Temp = ");
  lcd.setCursor(0,1);
  lcd.print("SETp = ");

  //setup PID limits and mode.  
  myPID.SetOutputLimits(2, windowsize);
  myPID.SetMode(AUTOMATIC);
}

void loop() {  

  print_temp();

  //will use to start alarm timer and turn on and off the hot led, just keep in mind that millis overflows after 50 days (unsigned long should correct it) 
  if ((temperature>=120) && !(hot_led_state) && set_state) {       //setpoint entered, hotplate temp >=120 and led not already on, start alarm timer
    alarm_timer = millis();                                        //Need to have setpoint to work
    digitalWrite(HOT_LED_PIN, HIGH);                               //Turn on HOT/timer activated LED
    hot_led_state = !hot_led_state;                                //toggle state            
  }

  //if >120 check to see if timer has expired, 
  if (((millis() - alarm_timer) >= ALR_DELAY) && hot_led_state) {     //alarm after delay once 120F reached
    digitalWrite(ALARM_PIN, HIGH);                                    //turn on alarm

    //if OFF DELAY time has passed, turn off the hotplate and alarm
    //clear the setpoint too and any keys that may have been entered
    if ((millis() - alarm_timer) >= OFF_DELAY + ALR_DELAY) {          
      hot_led_state = false;                                          
      set_state = false;
      zero_state = false;
      setpoint = 0;
      key_temp = 0;
      lcd_clear(7,1);
      key_temp = 0;
      lcd.print("ALARM");
      digitalWrite(HOT_LED_PIN, LOW);
      digitalWrite(SETPOINT_PIN, LOW);
      digitalWrite(ALARM_PIN, LOW);
            
    }
  }
//for pid stuff, only need when hotplate on

 pid_calc();

//keypad read, in test setup now
  char key = kpd.getKey();
  if(key){  // Check for a valid key.
    switch (key){

      //alarm timer reset key, will reset timer if pressed
      case '*':
        alarm_timer = millis();         //set new alarm timer
        digitalWrite(ALARM_PIN, LOW);   //turn off alarm
        break;

      //turn off key
      case '#':
        //reset flags to off state condition
        hot_led_state = false;
        set_state = false;
        zero_state = false;

        //clear setpoint and any keys
        setpoint = 0;
        key_temp = 0;

        //clear setpoint off lcd
        lcd_clear(7,1);

        //turn off leds/alarms
        digitalWrite(HOT_LED_PIN, LOW);
        digitalWrite(SETPOINT_PIN, LOW);
        digitalWrite(ALARM_PIN, LOW);
        break;

      //how we get our setpoint, reads keys until 3 #'s have been entered.  
      //then either makes it setpoint of prints invaild if out of range  
      default:
        if (key_temp == 0) {          //first key
          setpoint = 100*(key-'0');   //convert from ascii to integer, *100 puts in 100 spot
          key_temp++;                 //update # of keys entered
          set_state = false;          //if setpoint before, clear now
          hot_led_state = false;      //no setpoint = no timer
          digitalWrite(HOT_LED_PIN, LOW);   //turn of timer led
          digitalWrite(SETPOINT_PIN, LOW);   //turn off setpoint led
          lcd_clear(7,1);                   //clear current setpoint off screen
          lcd.print((int)setpoint/100);     //print first digit of setpoint
          break;
        }
        else if (key_temp == 1) {                 //next key
          setpoint = setpoint + 10*(key-'0');     //add next integer to setpoint in 10s place
          key_temp++;                             //add 1 to # of keys pressed

          //keep in off state
          set_state = false;
          hot_led_state = false;
          digitalWrite(HOT_LED_PIN, LOW);
          digitalWrite(SETPOINT_PIN, LOW);
          
          lcd_clear(7,1);                     //clear lcd and print current entry
          lcd.print((int)setpoint/10);
          break;
        }
          
        else if (key_temp == 2) {       //last key
          
          setpoint = setpoint + (key-'0');    //add final integer to setpoint in 1s place
          key_temp = 0;                       //reset # of keys pressed
          
          //check if setpoint valid
          if (setpoint >=150 && setpoint <=450) {
            set_state = true;                 //if valid, we have a setpoint to reach
            lcd_clear(7,1);                   //clear lcd then print setpoint
            lcd.print((int)setpoint);         
            digitalWrite(SETPOINT_PIN, HIGH); //turn on setpoint led 
          }
          //if not valid keep/turn everything off and print invalid to screen
          else {      
            setpoint=0;
            set_state = false;
            hot_led_state = false;
            digitalWrite(SETPOINT_PIN, LOW);
            digitalWrite(HOT_LED_PIN, LOW);
            lcd_clear(7,1);
            lcd.print("INVALID");
          }
          break; 
        }
        break;
      
    }
  }
}

//ISR for zero crossing signal
//Code modified from arduino ac phase control tutorial

void zero_crossing() {
  if (zero_state) { //zero sense enabled and hotplate not in full off condition  
    TCCR1B=0x04;                  //start timer 1 with divide by 256 input
    TCNT1 = 0;                    //reset timer 1 - count from zero
  }   
}

ISR(TIMER1_COMPA_vect){                 //comparator match interrupt
  if (zero_state && OCR1A < windowsize) {                     //zero sense is enable
    digitalWrite(GATE_PIN, HIGH);       //set TRIAC gate to high
    TCNT1 = 65536 + (-(windowsize+2) + OCR1A);      //trigger pulse width, will be full half wave - delay
  }                                     //bigger than 500 and starts skipping some half waves
                                        //given the rise/fall of zero-cross, 500 ~ half wave
}

ISR(TIMER1_OVF_vect){                   //timer 1 overflow interrupt
  if (zero_state) {                     //zero sense is enable
    digitalWrite(GATE_PIN,LOW);         //turn off TRIAC gate
    TCCR1B = 0x00;                      //disable timer, stop unintended triggers
  }
}

void pid_calc(void) {

  //only do if setpoint is entered
   if (set_state) {
    zero_state = true;    //start caring about zero crossing
    myPID.Compute();      //calculate pid control of hotplate
    OCR1A = pid_output;   //set output to comparator register, ISRs will use this
  }
  else {                    //if no setpoint keep hotplate off and disregard zero sense
    zero_state = false;           
    digitalWrite(GATE_PIN,LOW);
  }
}

//calculate and print the temperature
void print_temp(void) {
  
  //calculate the temperature from the adc reading and res/temp coefficients
  temperature = (TEMP_SLOPE * analogRead(A0)) + TEMP_OFFSET;  //Seems to be pretty close, will need to double check again

  //print the temperature to the screen
  if (print_cnt>=10000) {
    lcd_clear(7,0);
    lcd.print((int)temperature);  //display temp
    print_cnt = 0;
  }
  print_cnt++;
}

//clear 7 spaces starting at row and col entered, also reset cursor back to col/row
void lcd_clear(int col, int row) {
  lcd.setCursor(col,row);
  lcd.print("       ");
  lcd.setCursor(col,row);
}




