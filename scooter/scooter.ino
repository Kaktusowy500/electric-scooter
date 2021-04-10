#include <Wire.h>
#include <DS3231.h>//http://www.rinkydinkelectronics.com/library.php?id=73
#include <LiquidCrystal_I2C.h>//https://github.com/fmalpartida/New-LiquidCrystal
#include <EEPROM.h>


LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); //sets lcd address to 0x27
DS3231 rtc(SDA, SCL);
float totalDist = 0;

//custom lcd chars
byte battery[] = {//battery icon
	B01110,
	B11111,
	B10001,
	B10001,
	B10001,
	B11111,
	B11111,
	B11111 };
byte perH[] = {// per hour char(to fit it on one lcd segment
	B00010,
	B00100,
	B01000,
	B11001,
	B01001,
	B01111,
	B01001,
	B01001 };

byte degree[] = {//degree symbol
  B11000,
  B11011,
  B00100,
  B01000,
  B01000,
  B01000,
  B00100,
  B00011
};



// prints info on lcd, depends on current page
void printLCD(const int& page, const int& power, const float& realV, const float& mVolt, const float& lVolt, const float& tempDist, const float& totalDist, const float& tempB);

//gets main battery, and logic battery voltages
void getVoltages(float& mainV, float& logicV);

//controls motor power, braking and relay(for energy retrieving)
void motorCtrl(const int speed);

//makes speed changes smooth
void optSpeed(const int& decS, int& actS);

//calculates current motor power, and prepares to show it on lcd
int calcPower(int speed);

//reads battery temperature
float kty(unsigned int port);

// reads control buttons states - switches page displayed on lcd, changes lcd backlight state  
void switchP(int& page, const unsigned long& nowtime);

//speedometer, based on hall sensor
float getRealV(unsigned long nowtime);

//pin configuration, constants
#define POTP 2 //speed regulator
#define MOTORP 5 //motor(pwm controler)
#define RELAYP 4 //relay pin
#define HALLP 0 //hall sensor pin - for speed meter
#define RBUTTON 6 //right button
#define MBUTTON 7 //middle button
#define LBUTTON 8 //left button
#define BRAKEP 9 //brake button
#define REFRESH 300 //lcd refresh time in milisec
#define MTIME 5000 //real speed refresh time in milisec
#define S_MARGIN 10//speed change margin, for smooth changing
//#define DEBUG

void setup()
{
  //pin setup
	pinMode(RBUTTON, INPUT);
	pinMode(MBUTTON, INPUT);
	pinMode(LBUTTON, INPUT);
	pinMode(BRAKEP, INPUT);
	pinMode(MOTORP, OUTPUT); // motor pwm
	digitalWrite(MOTORP, HIGH);
	pinMode(RELAYP, OUTPUT);
	digitalWrite(RELAYP, LOW); // no connection
 
	Serial.begin(9600);

  //i2c devices initialization
	rtc.begin(); // Initialize the rtc object
	//rtc.setTime(20, 51, 20);     // sets time
	lcd.begin(16, 2);
	lcd.createChar(0, battery);
	lcd.createChar(1, perH);
	lcd.createChar(2, degree);
	lcd.backlight(); // backlight on
	lcd.setCursor(2, 0);
	lcd.print("CACTUSOFT");//non-existing company - looks more profesional :) 
	lcd.setCursor(2, 1);
	lcd.print("ENTERTAINMENT");
 //gets total distance from EEPROM
	EEPROM.get(0, totalDist);
	Serial.println(totalDist);
	delay(2000);
}


//main loop
void loop()
{
	static int page = 1;
	unsigned long nowtime = millis();
	static int decSpeed = 0;
	static int speed = 255; //
	static int power = 0;   //0-100% of motor power (to display)
	static int sspeed = 0;
	static int countspeed = 0;
	static float tempDist = 0;
	static float mainVolt = 0;
	static float logicVolt = 0;
	static unsigned long refreshT = 0;

	int val = analogRead(POTP);          // read the value from potentiometer - desired speed
	//Serial.println(val);
  
  //desired speed optimization to remove some read errors 
	if (val < 680)
		  val = 680;

	sspeed += map(val, 680, 1023, 0, 255); // 255- stay, sum of declared speeds
	countspeed++;

  //some speed optimatimizations to provide speed smooth changes and soft start 
	if (countspeed > 100)
	{                                   
		decSpeed = sspeed / countspeed; //average
		optSpeed(decSpeed, speed);
		power = calcPower(speed);
		sspeed = 0; //sum
		countspeed = 0;
	}

	motorCtrl(speed);
	switchP(page, nowtime);

	float realV = getRealV(nowtime);
	getVoltages(mainVolt, logicVolt);

  //lcd refresh - few times per second is enough(it takes quite a lot power) 
	if (nowtime - refreshT >= REFRESH)
	{
		float bTemp = kty(1);
		float distance = realV * REFRESH / 1000000 / 3.6;
		tempDist += distance; //current distance
		totalDist += distance;
		printLCD(page, power, realV, mainVolt, logicVolt, tempDist, totalDist, bTemp);
		refreshT = nowtime;
	}
 
  //saves total distance to eeprom
	static unsigned long lastTime = 0;
	if (nowtime - lastTime > 5000)
	{
		EEPROM.put(0, totalDist);
		lastTime = nowtime;
		//Serial.print("saved ");
	}
	

#ifdef DEBUG
	static unsigned long lastTimeD = 0;
	static int count = 0;
	count++;
	if (nowtime - lastTimeD > 1000)
	{
		Serial.print("LOOPS PER SEC: ");
		Serial.println(count);
		// EEPROM.put(0, totalDist);//przeniesc
		lastTimeD = nowtime;
		count = 0;
	}
#endif
}


float kty(unsigned int port) {
	float temp = 82;
	ADCSRA = 0x00;
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
	ADMUX = 0x00;
	ADMUX = (1 << REFS0);
	ADMUX |= port;

	for (int i = 0; i <= 64; i++)
	{
		ADCSRA |= (1 << ADSC);
		while (ADCSRA & (1 << ADSC));
		temp += (ADCL + ADCH * 256);
	}

	temp /= 101;
	temp -= 156;
	return (temp);
}

void optSpeed(const int& decS, int& actS)
{
	if (decS - actS > S_MARGIN) //increase
		actS += 5;
	else if (decS - actS < -S_MARGIN) //decrease
		actS -= 5;
	else
		actS = decS;
}


void motorCtrl(const int speed)
{
	bool brake = digitalRead(BRAKEP);
	static bool braking = false;
	static int state = 0; //0- motor off 1 -on
	if (state &&(speed>235||brake))
	{
		if (brake)
			braking = true;
		Serial.println("Motor OFF");
		analogWrite(MOTORP, 255);
		delay(50);
		state = 0;
		digitalWrite(RELAYP, LOW);
		delay(100);
	}
	else if (speed < 230 && !state && !brake&& !braking)
	{
		Serial.println("Motor ON");
		digitalWrite(RELAYP, HIGH);
		delay(200);
		state = 1;
	}
	
	if (braking && speed > 235)
		braking = false;


	if (state == 1)
		analogWrite(MOTORP, speed);
	else
		analogWrite(MOTORP, 255); //stay
	//Serial.println(state);
}

int calcPower(int speed) 
{
	static bool toClear1 = true;
	static bool toClear2 = true;
	int power = map(speed, 0, 255, 100, 0);
	if (power < 10)
	{
		power = 0;
		if (toClear2)
		{
			lcd.clear();
			toClear2 = false;
		}
	}
	if (power == 100)
		toClear1 = true;

	else if (power >= 10 && power < 100)
	{
		toClear2 = true;

		if (toClear1)
		{
			lcd.clear();
			toClear1 = false;
		}
	}
	return power;
}

void getVoltages(float& mainV, float& logicV)
{
	float vmain = (float)analogRead(3) * 5.0 / 1024.0 / 0.0472; //main battery voltage
	float vlogic = (float)analogRead(6)/1024 * 5.0;           // logic battery voltage
	static float s1 = 0;
	static float s2 = 0;
	static int count = 0;
	s2 += vlogic;
	s1 += vmain;
	count++;

	if (count > 1000)
	{
		mainV = s1 / count;  //main
		logicV = s2 / count; //logic
		count = 0;
		s1 = 0;
		s2 = 0;
	}
}

float getRealV(unsigned long nowtime)
{
	static float Speed = 0;
	static unsigned int counthall = 0;
	static int lasthall = 0;
	static bool hallflag = false;
	static unsigned long lasttime = 0;
	int hall = analogRead(HALLP); //read hall value
								  //Serial.println(hall);
	if (lasthall < 5 && hall < 5 && hallflag)
	{
		counthall++;
		hallflag = false;
	}
	else if (hall > 50 && lasthall > 50)
	{
		hallflag = true; //ready to count/
	}

	lasthall = hall;
	if (nowtime - lasttime > MTIME)
	{
		Speed = (counthall * 2 * 3.14 * 0.08) / 5 * 3.6; // distance in ()
		counthall = 0;
		lasttime = nowtime;
	}
	return Speed;
}

void printLCD(const int& page, const int& power, const float& realV, const float& mVolt, const float& lVolt, const float& tempDist, const float& totalDist, const float & tempB)
{
	lcd.setCursor(0, 0);
	lcd.print(power);
	lcd.print("%");
	lcd.setCursor(0, 1);
	lcd.print(realV);
	lcd.print("KM");
	lcd.write(byte(1));
	switch (page)
	{
	case 1:
		lcd.setCursor(7, 0);
		lcd.print(rtc.getTimeStr());
		lcd.setCursor(9, 1);
		lcd.write(byte(0));
		lcd.print(mVolt);
		lcd.print("V");
		break;
	case 2:
		lcd.setCursor(6, 0);
		lcd.print("D:");
		lcd.print(tempDist); //distance
		lcd.print("KM");
		lcd.setCursor(9, 1);
		lcd.write(byte(0));//battery
		lcd.print(lVolt);
		lcd.print("V");
		break;
	case 3:
		lcd.setCursor(5, 0);
		lcd.print("TD:");
		lcd.print(totalDist, 1); //distance
		lcd.print("KM");
		lcd.setCursor(9, 1);
		lcd.print("T");
		lcd.write(byte(0));
		lcd.print(tempB,1);
		lcd.write(byte(2));
		break;
	}
}

void switchP(int& page, const unsigned long& nowtime)
{
	static unsigned long lasttime = 0;
	static bool backlight = false;
	if (nowtime - lasttime > 300)
	{
		if (digitalRead(LBUTTON))
		{
			page--;
			if (page < 1)
				page = 3;
			lcd.clear();
			lasttime = nowtime;
		}
		if (digitalRead(MBUTTON))
		{
			backlight = !backlight;
			if (backlight)
				lcd.backlight();
			else
				lcd.noBacklight();
			lasttime = nowtime;
		}
		if (digitalRead(RBUTTON))
		{
			page++;
			if (page > 3)
				page = 1;
			lcd.clear();
			lasttime = nowtime;
		}
	}
}

/*
TODO
modes
charging hardware
warnings
case
lights
phisical brake
drive improvment
*/
