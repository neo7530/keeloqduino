/* 

433MHz Kelloq compatible transmitter

8MHz internal, BOD 2.7v, 2 or 4 Buttons. (Burn Bootloader to set the right fuses)

          ATTiny85
          ___  ___
RESET   -|   \/   |-   V+
Button  -|        |-   Button
Button  -|        |-   OOK-Transmitter Data
GND     -|________|-   Button

Buttons = GND when pressed

*/

#include <avr/sleep.h>
#include <EEPROM.h>

#define OOK_PIN 1
#define MOD_PPM 1
#define MOD_PWM 0

// Buttons
const int S1 = 4;
const int S2 = 3;
const int S3 = 2;
const int S4 = 0;

unsigned int modulation   = 0;          // PWM = 0, PPM = 1
unsigned int repeats      = 2;          // signal repeats
unsigned int bits         = 66;         // amount of bits in a packet
unsigned int pd_len       = 3000;       // pulse/distance length (in us) used for time between preamble and data
unsigned int zero_ca_len  = 800;        // length of 0 (in us)
unsigned int zero_ci_len  = 400;       // length of 0 (in us)
unsigned int one_ca_len   = 400;       // length of 1 (in us)
unsigned int one_ci_len   = 800;        // length of 0 (in us)
unsigned int pause_len    = 10000;      // pause length (in us), time between packets
unsigned int invert       = 0;          // invert the bits before transmit
unsigned int lsb          = 1;          // send LSB first
char packet_buf[80]      = {0};        // packet payload buffer
unsigned int pbuf_len     = 0;          // payload buffer length
unsigned int bit_pos      = 0;          // bit reader bit position
int carrier_mode          = 0;

//keeloq stuff
#define KeeLoq_NLF    0x3A5C742E
#define bit(x,n)    (((x)>>(n))&1)
#define g5(x,a,b,c,d,e) (bit(x,a)+bit(x,b)*2+bit(x,c)*4+bit(x,d)*8+bit(x,e)*16)

uint64_t k = 0x0123456789ABCDEF; //System-Code
uint64_t k1; //Device-Code
uint32_t serial = 0x01440001; //hexadecimal Serial-Number 32Bit
uint32_t fixed = 0; //fixed code-segment
uint32_t hopping = 0; //hopping code
uint32_t button; //buttons
uint16_t ctr; //counter

//keeloq encryption
uint32_t encrypt (const uint32_t data, const uint64_t key){
  uint32_t x = data, r;
  for (r = 0; r < 528; r++){
    x = (x>>1)^((bit(x,0)^bit(x,16)^(uint32_t)bit(key,r&63)^bit(KeeLoq_NLF,g5(x,1,9,20,26,31)))<<31);
  }
  return x;
}

//keeloq decryption
uint32_t decrypt (const uint32_t data, const uint64_t key){
  uint32_t x = data, r;
  for (r = 0; r < 528; r++){
    x = (x<<1)^bit(x,31)^bit(x,15)^(uint32_t)bit(key,(15-r)&63)^bit(KeeLoq_NLF,g5(x,0,8,19,25,30));
  }
  return x;
}

int get_bit() {
  int ret;
  int byte_pos;
  int byte_bit_pos;
if(lsb){
    byte_pos     = (bits/8)-(bit_pos / 8);
    byte_bit_pos = (bit_pos % 8);     // send lsb first
  }else{
    byte_pos     = bit_pos / 8;
    byte_bit_pos = 7 - (bit_pos % 8);     // reverse indexing to send the bits msb
  }
  bit_pos++;
  ret = (packet_buf[byte_pos] & (1<<byte_bit_pos)) ? 1 : 0;
  return ret^invert;
}

int transmit() {
  int i,j;
  int bit;
  int pwm_bl;

  // repeats
  for (j=0 ; j<repeats ; j++) {

  for(i=0;i<12;i++){ //preamble 
      digitalWrite(OOK_PIN, HIGH);
      delayMicroseconds(one_ca_len);
      digitalWrite(OOK_PIN, LOW);
      delayMicroseconds(one_ca_len);
    }

  delayMicroseconds(pd_len); //delay after preamble
    
    // reset bit reader
    bit_pos = 0;

    // send bits
    for (i=0 ; i<bits ; i++) {
      bit = get_bit();
      if ((modulation==MOD_PPM) || (modulation==MOD_PWM)) {
        digitalWrite(OOK_PIN, HIGH);
        if (bit) {
          delayMicroseconds(one_ca_len);
          digitalWrite(OOK_PIN, LOW);
          delayMicroseconds(one_ci_len);
        } else {
          delayMicroseconds(zero_ca_len);
          digitalWrite(OOK_PIN, LOW);
          delayMicroseconds(zero_ci_len);
        }
      } else {
        return -1; 
      }
    }
    
    // Send ending PPM pulse
    if (modulation == MOD_PPM) {
        digitalWrite(OOK_PIN, HIGH);
//        digitalWrite(LED_PIN, HIGH);
        delayMicroseconds(pd_len);
        digitalWrite(OOK_PIN, LOW);
//        digitalWrite(LED_PIN, LOW);    
    }
    // delay between packets
    delayMicroseconds(pause_len);
  }

  return 0;
}


void update_tx(void){
  button = 0;
  if(!digitalRead(3))button |= 2;
  if(!digitalRead(4))button |= 4;
  if(!digitalRead(0))button |= 8;
  if(!digitalRead(2))button |= 1;
  fixed = button << 28 | serial & 0x0fffffff;
  hopping = button << 28 | ((serial & 0x00000fff) << 16) | ctr;
  hopping = encrypt(hopping,k1);
  
  pbuf_len = 9;
  packet_buf[0] = 0x02;
  packet_buf[1] = fixed >> 24 & 0xff;
  packet_buf[2] = fixed >> 16 & 0xff;
  packet_buf[3] = fixed >> 8 & 0xff;
  packet_buf[4] = fixed & 0xff; 
  packet_buf[5] = hopping >> 24 & 0xff;
  packet_buf[6] = hopping >> 16 & 0xff;
  packet_buf[7] = hopping >> 8 & 0xff;
  packet_buf[8] = hopping & 0xff;
}

// Setup **********************************************

void setup() {
  pinMode(S1, INPUT_PULLUP);
  pinMode(S2, INPUT_PULLUP);
  pinMode(S3, INPUT_PULLUP);
  pinMode(S4, INPUT_PULLUP);
  pinMode(OOK_PIN, OUTPUT);  // ook transmitter
  
  //calculate device-key
  k1 = decrypt((0x60000000|serial),k); 
  k1 <<= 32; 
  k1 |= decrypt((0x20000000|serial),k);

  EEPROM.get(0,ctr);
    if(isnan(ctr)){
      ctr = 1;
      EEPROM.put(0,ctr);
    }; 


  update_tx();

  // Configure pin change interrupts to wake on button presses
  PCMSK = 1<<S1 | 1<<S2 | 1<<S3 | 1<<S4;
  GIMSK = 1<<PCIE;                  // Enable interrupts
  GIFR = 1<<PCIF;                   // Clear interrupt flag
  // Disable what we don't need to save power
  ADCSRA &= ~(1<<ADEN);             // Disable ADC
  PRR = 1<<PRUSI | 1<<PRADC;        // Turn off clocks to unused peripherals
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
}

// Pin change interrupt service routine
ISR (PCINT0_vect) {
  //just for wakeup
}

// Stay asleep and just respond to interrupts
void loop() {
  sleep_enable();
  sleep_cpu();
 delay(200); //debounce keys
do{
  update_tx(); //code buttons, generate keeloq-frame
  delay(100);
  transmit(); //blast it out
}while (!digitalRead(0) || !digitalRead(2) || !digitalRead(3)|| !digitalRead(4)); //as long a button is pressed

ctr++; //increment counter
if(ctr % 5 == 0){  //to enhace eeprom lifetime by 5
  EEPROM.update(0,ctr); //write counter to eeprom, will be restored after battery change
}
}
