/*
	analog_sensors_board.ino
	
	Connect LM35 temperature sensors to J2, J3, J5 and J6 and battery lanes to J4.
	This program will read analog values, apply data conversion and send back
	detected levels using the serial connection on the Arduino Leonardo board.

	Copyright (c) 2012-2013 Valerio De Carolis, http://decabyte.it

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#include <avr/wdt.h>
#include <Wire.h>

#define LED_PIN 13

#define DELAY_SETUP 250
#define DELAY_MAIN 1000
#define DELTA_IQ 2000

// Reference voltages (fine tuned):
//	measured using AREF when the shield is in place and 
//	the Arduino is powered by external power with vehicle

#define ADC_INTERNAL_V 	0.002483f	// manual calibration (was 0.002502f)
#define ADC_INTERNAL_MV 2.483f		// manual calibration (was 2.502f)
#define ADC_DEFAULT_V 0.004887f		// uncalibrated
#define ADC_DEFAULT_MV 4.887f		// uncalibrated

#define BAT_R1 24000.0f 		// R1 (ohm)
#define BAT_R2 2200.0f 			// R2 (ohm)

#define LM35_MVC 10.0f			// mV/C
#define ACS714_MVA 185.0f 		// mv/A

#define BMP085_ADDRESS 0x77		// I2C address
#define BMP085_OSS 0 			// Oversampling Setting
#define BMP_MAX 1000 			// Loop protection (number of loops)

// ** LM35 Analog Temperature Sensor
// 	[1]: http://learn.adafruit.com/tmp36-temperature-sensor
//
// ** HIH-4043 Humidity Sensor
// 	[2]: https://www.sparkfun.com/products/9569
// 	[3]: http://bildr.org/2012/11/hih4030-arduino/
// 	[4]: http://forum.arduino.cc/index.php/topic,19961.0.html
//
// ** BMP085 Barometric Pressure Sensor
// 	[5]: https://www.sparkfun.com/products/11282
// 	[6]: https://www.sparkfun.com/tutorials/253
//
//  [7]: http://linux.die.net/man/8/picocom
//  [8]: http://forum.arduino.cc/index.php?topic=128717.0
//  [9]: http://www.nongnu.org/avr-libc/user-manual/group__avr__watchdog.html

const int ACS_ZERO = (int) 2500 / ADC_INTERNAL_MV;		// ADC reading for 2.5V (~ 1007)
const float ACS714_CONV = ADC_INTERNAL_MV / ACS714_MVA;	// Amps per ADC level
const float BAT_RK = (BAT_R1 + BAT_R2) / BAT_R2;

unsigned long time_iq;
unsigned long delta;

// adc readings
int raw_bat0, raw_bat1, raw_bat2, raw_bat3;
int raw_tm0, raw_tm1, raw_tm2, raw_tm3;
int raw_hih;

// output values
float bat0, bat1, bat2, bat3;
float tm0, tm1, tm2, tm3;
float hih, hm, hb;


// BMP085 raw readings
long raw_ut, raw_up;

// BMP085 output values
short temperature;
long pressure;

// BMP085 loop control
short bmp_cnt;
byte bmp_dirty;
 
// BMP085 calibration values
int ac1, ac2, ac3;
unsigned int ac4, ac5, ac6;
int b1, b2, mb, mc, md; 

// b5 is calculated in bmp085GetTemperature(...), this variable is also used in
// bmp085GetPressure(...) so bmp085GetTemperature(...) must be called before
// bmp085GetPressure(...) to work correctly.
long b5;


// ACS714 current sensor 
//	working in reverse current flow to keep output signal within 0-2.5V range
//	2.5V output is the steady 0A initial reading
float acs0;
int raw_acs0;


// the setup routine runs once when you press reset:
void setup() {
	// disable watchdog, because of [8]
	wdt_disable();

	// not ready
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW); 

	// while the serial stream is not open, flash the on-board led:
	while(!Serial) {
		digitalWrite(LED_PIN, HIGH);
		delay(DELAY_SETUP);
		digitalWrite(LED_PIN, LOW);
		delay(DELAY_SETUP);
	}

	// switch to precise reference (2.56V)
	analogReference(INTERNAL);
	delay(5);

	// enable watchdog
	wdt_enable(WDTO_8S);

	// bmp085 init
	Wire.begin();
	bmp085Calibration();			// read data from BMP085 registers
	report_bmp_calibration();		// send calibration data for reference

	// ready to go
	digitalWrite(LED_PIN, HIGH);
}

// the loop routine runs over and over again forever:
void loop() {

	// start acquisition (with double readings to avoid interferences [1])
	digitalWrite(LED_PIN, HIGH);

	// current sensor
	raw_acs0 = analogRead(A11);		// J5 (pin 2)
	raw_acs0 = analogRead(A11);		// J5 (pin 2)

	// calculate output is Amps
	acs0 = (float) (ACS_ZERO - raw_acs0) * ACS714_CONV;

	// send current report
	report_current();


	// acquisition loop delta
	delta = millis() - time_iq;

	// limit data rate
	if( delta > DELTA_IQ ) {
		
		// analog inputs (battery)
		raw_bat0 = analogRead(A0);		// J4 (pin 1)
		raw_bat0 = analogRead(A0);		// J4 (pin 1)

		raw_bat1 = analogRead(A1);		// J4 (pin 2)
		raw_bat1 = analogRead(A1);		// J4 (pin 2)

		raw_bat2 = analogRead(A2);		// J4 (pin 3)
		raw_bat2 = analogRead(A2);		// J4 (pin 3)

		raw_bat3 = analogRead(A3);		// J4 (pin 4)
		raw_bat3 = analogRead(A3);		// J4 (pin 4)

		// voltage level conversion
		bat0 = BAT_RK * (float)raw_bat0 * ADC_INTERNAL_V;
		bat1 = BAT_RK * (float)raw_bat1 * ADC_INTERNAL_V;
		bat2 = BAT_RK * (float)raw_bat2 * ADC_INTERNAL_V;
		bat3 = BAT_RK * (float)raw_bat3 * ADC_INTERNAL_V;

		// send battery report
		report_battery();


		// analog inputs (temperature)
		raw_tm0 = analogRead(A4);		// J2 (pin 2)
		raw_tm0 = analogRead(A4);		// J2 (pin 2)

		raw_tm1 = analogRead(A5);		// J3 (pin 2)
		raw_tm1 = analogRead(A5);		// J3 (pin 2)
		
		raw_tm2 = analogRead(A8);		// J6 (pin 2)
		raw_tm2 = analogRead(A8);		// J6 (pin 2)

		//raw_tm3 = analogRead(A11);		// J5 (pin 2)
		//raw_tm3 = analogRead(A11);		// J5 (pin 2)

		// lm35 temperature conversion
		tm0 = (float(raw_tm0) * ADC_INTERNAL_MV) / LM35_MVC;
		tm1 = (float(raw_tm1) * ADC_INTERNAL_MV) / LM35_MVC;
		tm2 = (float(raw_tm2) * ADC_INTERNAL_MV) / LM35_MVC;
		//tm3 = (float(raw_tm3) * ADC_INTERNAL_MV) / LM35_MVC;

		// send temperature report
		report_temperature();


		// pressure sensor
		raw_ut = bmp085ReadUT();						// raw_ut = 27898;
		temperature = bmp085GetTemperature(raw_ut);		// compensated temperature

		raw_up = bmp085ReadUP(); 						// raw_up = 23843;
		pressure = bmp085GetPressure(raw_up);			// compesated pressure

		// send pressure report
		report_pressure();

		
		// humidity sensor
		raw_hih = analogRead(A6);		// HIH-4030 (pin 2)
		raw_hih = analogRead(A6);		// HIH-4030 (pin 2)

		// relative humidity with temperature correction (see hih_4030_fitting.m)
		hm = (0.0002f * temperature) + 0.0763f;			// x-coeffient fitting
		hb = (-0.0612f * temperature) - 24.4265f;		// b-term fitting
		hih = hm * float(raw_hih) + hb;

		// send humidity report
		report_humidity();

		// send timestamp
		Serial.print("$TIME,");
		Serial.println(time_iq, DEC);

		// update timestamp
		time_iq = millis();

		// reset dirty bits
		bmp_dirty = 0;
	}

	// signal end of acquisition
	digitalWrite(LED_PIN, LOW);

	// pause between data acquisition
	delay(DELAY_MAIN);

	// reset watchdog
	wdt_reset();
}

void report_current() {
	Serial.print("$ACS,");
	Serial.print(acs0, 4);
	Serial.print(',');
	Serial.println(raw_acs0, DEC);
}

void report_battery() {
	Serial.print("$BAT,");
	Serial.print(bat0, 4);
	Serial.print(',');
	Serial.print(bat1, 4);
	Serial.print(',');
	Serial.print(bat2, 4);
	Serial.print(',');
	Serial.print(bat3, 4);
	Serial.print(',');
	Serial.print(raw_bat0, DEC);
	Serial.print(',');
	Serial.print(raw_bat1, DEC);
	Serial.print(',');
	Serial.print(raw_bat2, DEC);
	Serial.print(',');
	Serial.println(raw_bat3, DEC);
}

void report_temperature() {
	Serial.print("$TEMP,");
	Serial.print(tm0, 2);
	Serial.print(',');
	Serial.print(tm1, 2);
	Serial.print(',');
	Serial.print(tm2, 2);
	Serial.print(',');
	Serial.print(tm3, 2);
	Serial.print(',');
	Serial.print(raw_tm0, DEC);
	Serial.print(',');
	Serial.print(raw_tm1, DEC);
	Serial.print(',');
	Serial.print(raw_tm2, DEC);
	Serial.print(',');
	Serial.println(raw_tm3, DEC);
}

void report_pressure() {
	Serial.print("$BMP,");
	Serial.print(temperature, DEC);
	Serial.print(',');
	Serial.print(pressure, DEC);
	Serial.print(',');
	Serial.print(raw_ut, DEC);
	Serial.print(',');
	Serial.print(raw_up, DEC);
	Serial.print(',');
	Serial.println(bmp_dirty, DEC);
}

void report_humidity() {
	Serial.print("$HIH,");
	Serial.print(hih, 2);
	Serial.print(',');
	Serial.println(raw_hih, DEC);
}

void report_bmp_calibration() {
	Serial.print("$BMPCAL,");
	Serial.print(ac1, DEC);
	Serial.print(',');
	Serial.print(ac2, DEC);
	Serial.print(',');
	Serial.print(ac3, DEC);
	Serial.print(',');
	Serial.print(ac4, DEC);
	Serial.print(',');
	Serial.print(ac5, DEC);
	Serial.print(',');
	Serial.print(ac6, DEC);
	Serial.print(',');
	Serial.print(b1 , DEC);
	Serial.print(',');
	Serial.print(b2 , DEC);
	Serial.print(',');
	Serial.print(mb , DEC);
	Serial.print(',');
	Serial.print(mc , DEC);
	Serial.print(',');
	Serial.print(md , DEC);
	Serial.print(',');
	Serial.println(bmp_dirty, DEC);
}


// *** BMP085 functions *** 

// Stores all of the bmp085's calibration values into global variables
// Calibration values are required to calculate temp and pressure
// This function should be called at the beginning of the program
void bmp085Calibration() {
	ac1 = bmp085ReadInt(0xAA);
	ac2 = bmp085ReadInt(0xAC);
	ac3 = bmp085ReadInt(0xAE);
	ac4 = bmp085ReadInt(0xB0);
	ac5 = bmp085ReadInt(0xB2);
	ac6 = bmp085ReadInt(0xB4);
	b1 = bmp085ReadInt(0xB6);
	b2 = bmp085ReadInt(0xB8);
	mb = bmp085ReadInt(0xBA);
	mc = bmp085ReadInt(0xBC);
	md = bmp085ReadInt(0xBE);

	// datasheet values (debug only)
	// ac1 = 408;
	// ac2 = -72;
	// ac3 = -14383;
	// ac4 = 32741;
	// ac5 = 32757;
	// ac6 = 23153;
	// b1 = 6190;
	// b2 = 4;
	// mb = -32768;
	// mc = -8711;
	// md = 2868;
}

// Calculate temperature given ut.
//
// Value returned will be in units of 0.1 deg C
short bmp085GetTemperature(unsigned int ut) {
  long x1, x2;
  
  x1 = ( ((long)ut - (long)ac6) * (long)ac5 ) >> 15;
  x2 = ((long)mc << 11)/(x1 + md);
  b5 = x1 + x2;

  return ((b5 + 8) >> 4);  
}

// Calculate pressure given up - calibration values must be known
// b5 is also required so bmp085GetTemperature(...) must be called first.
//
// Value returned will be pressure in units of Pa.
long bmp085GetPressure(unsigned long up) {
  long x1, x2, x3, b3, b6, p;
  unsigned long b4, b7;
  
  b6 = b5 - 4000;
  // Calculate B3
  x1 = (b2 * (b6 * b6) >> 12) >> 11;
  x2 = (ac2 * b6) >> 11;
  x3 = x1 + x2;
  b3 = (((((long)ac1)*4 + x3) << BMP085_OSS) + 2) >> 2;
  
  // Calculate B4
  x1 = (ac3 * b6)>>13;
  x2 = (b1 * ((b6 * b6) >> 12)) >> 16;
  x3 = ((x1 + x2) + 2) >> 2;
  b4 = (ac4 * (unsigned long)(x3 + 32768)) >> 15;
  
  b7 = ((unsigned long)(up - b3) * (50000 >> BMP085_OSS));

  if (b7 < 0x80000000)
    p = (b7<<1)/b4;
  else
    p = (b7/b4) << 1;
    
  x1 = (p >> 8) * (p >> 8);
  x1 = (x1 * 3038) >> 16;
  x2 = (-7357 * p) >> 16;
  p += (x1 + x2 + 3791) >> 4;
  
  return p;
}

// // Read 1 byte from the BMP085 at 'address'
// char bmp085Read(unsigned char address)
// {
//   unsigned char data;
//  
//   Wire.beginTransmission(BMP085_ADDRESS);
//   Wire.write(address);
//   Wire.endTransmission();
//  
//   Wire.requestFrom(BMP085_ADDRESS, 1);
//   while(!Wire.available()) {
//
//   }
//
//   data = Wire.read();
//   return data;
// }

// Read 2 bytes from the BMP085
//
// First byte will be from 'address'
// Second byte will be from 'address'+1
int bmp085ReadInt(unsigned char address) {
  unsigned char msb, lsb;
  
  Wire.beginTransmission(BMP085_ADDRESS);
  Wire.write(address);
  Wire.endTransmission();
  
  Wire.requestFrom(BMP085_ADDRESS, 2);

  // loop check
  bmp_cnt = 0;

  // wait for data to become available
  while(Wire.available() < 2) {
  	bmp_cnt += 1;

  	if(bmp_cnt > BMP_MAX) {
  		bmp_dirty = 1;
  		return -1;
  	}
  }
 
  msb = Wire.read();
  lsb = Wire.read();
  
  return (int) msb<<8 | lsb;
}

// Read the uncompensated temperature value
unsigned int bmp085ReadUT() {
  unsigned int ut;
  
  // Write 0x2E into Register 0xF4
  // This requests a temperature reading
  Wire.beginTransmission(BMP085_ADDRESS);
  Wire.write(0xF4);
  Wire.write(0x2E);
  Wire.endTransmission();
  
  // Wait at least 4.5ms
  delay(5);
  
  // Read two bytes from registers 0xF6 and 0xF7
  ut = bmp085ReadInt(0xF6);
  return ut;
}

// Read the uncompensated pressure value
unsigned long bmp085ReadUP() {
  unsigned char msb, lsb, xlsb;
  unsigned long up = 0;
  
  // Write 0x34+(BMP085_OSS << 6) into register 0xF4
  // Request a pressure reading w/ oversampling setting
  Wire.beginTransmission(BMP085_ADDRESS);
  Wire.write(0xF4);
  Wire.write(0x34 + (BMP085_OSS << 6));
  Wire.endTransmission();
  
  // Wait for conversion, delay time dependent on BMP085_OSS
  delay(2 + (3 << BMP085_OSS));
  
  // Read register 0xF6 (MSB), 0xF7 (LSB), and 0xF8 (XLSB)
  Wire.beginTransmission(BMP085_ADDRESS);
  Wire.write(0xF6);
  Wire.endTransmission();
  Wire.requestFrom(BMP085_ADDRESS, 3);
  
  // loop check
  bmp_cnt = 0;

  // wait for data to become available
  while(Wire.available() < 3) {
  	bmp_cnt += 1;

  	if(bmp_cnt > BMP_MAX) {
  		bmp_dirty = 1;
  		return -1;
  	}
  }

  msb = Wire.read();
  lsb = Wire.read();
  xlsb = Wire.read();
  
  up = (((unsigned long) msb << 16) | ((unsigned long) lsb << 8) | (unsigned long) xlsb) >> (8 - BMP085_OSS);
  return up;
}
