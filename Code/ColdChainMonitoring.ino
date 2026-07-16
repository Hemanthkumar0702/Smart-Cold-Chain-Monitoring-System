#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <SPI.h>
#include <MFRC522.h>
#include <TinyGPS++.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <DHT.h>


// ---------------- WIFI ----------------
const char* ssid = "STARFETCHINNOVATIONS";
const char* password = "Star@fetch1440";

// ---------------- EMAIL ----------------
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "saichaitanyaavugaddi27@gmail.com"
#define AUTHOR_PASSWORD "nyilgdrzuxsnzane"
#define RECIPIENT_EMAIL "saichaitanyaavugaddi27@gmail.com"

// ---------------- SMTP ----------------
SMTPSession smtp;

// ---------------- RFID ----------------
#define SS_PIN 5
#define RST_PIN 27
MFRC522 rfid(SS_PIN, RST_PIN);
// ---------------- DHT11 ----------------
#define DHTPIN 2
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// ---------------- IR SENSOR ----------------
#define IR_PIN 15

// ---------------- GPS ----------------
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] =
{
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {13,12,14,26};
byte colPins[COLS] = {25,33,32,4};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
LiquidCrystal_I2C lcd(0x27, 16, 2);
// ---------------- AUTH ----------------
String authorizedUID = "23BEC906";
String uid = "";

// ---------------- STATE ----------------
int state = 0;
// 0 = WAIT CARD
// 1 = MEDICINE SELECT
// 2 = JOURNEY ACTIVE

// ---------------- MEDICINE ----------------
int medicine = 0;
String medName;
float currentTemp = 0;

float minAllowedTemp = 0;
float maxAllowedTemp = 0;

bool tempBreachSent = false;
 

// ---------------- LOCATION ----------------
String lat = "NO FIX";
String lng = "NO FIX";

// ---------------- TIMER ----------------
unsigned long lastCheck = 0;
unsigned long lastMailTime = 0;

// ---------------- IR FLAG ----------------
bool irBreachSent = false;
int defaultIR = HIGH;
// ---------------- TRIP REPORT ----------------
String startTime = "";
String endTime = "";

float minTemp = 999;
float maxTemp = -999;
float tempSum = 0;
int tempCount = 0;

int breachCount = 0;
int tamperCount = 0;
String breachDetails = "";
String tamperDetails = "";

String startLat = "";
String startLng = "";

String endLat = "";
String endLng = "";

// ---------------- TIME ----------------
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;

String getTimeNow()
{
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
    return "TIME ERROR";

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);

  return String(buffer);
}
void setMedicine(int m)
{
  switch(m)
  {
    case 1:
      medName = "Insulin";
      minAllowedTemp = 2;
      maxAllowedTemp = 8;
      break;

    case 2:
      medName = "Polio Vaccine";
      minAllowedTemp = 2;
      maxAllowedTemp = 8;
      break;

    case 3:
      medName = "COVID Vaccine";
      minAllowedTemp = -25;
      maxAllowedTemp = -15;
      break;

    case 4:
      medName = "Antibiotics";
      minAllowedTemp = 15;
      maxAllowedTemp = 25;
      break;

    case 5:
      medName = "Blood Sample";
      minAllowedTemp = 2;
      maxAllowedTemp = 6;
      break;
  }
}
// ---------------- MEDICINE SET ----------------
  

// ---------------- EMAIL FUNCTION ----------------
void sendMail(String subject, String body)
{
  ESP_Mail_Session session;

  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;

  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;

  SMTP_Message message;

  message.sender.name = "Cold Chain Monitoring System";
  message.sender.email = AUTHOR_EMAIL;

  message.subject = subject;

  message.addRecipient("User", RECIPIENT_EMAIL);

  message.text.content = body.c_str();

  smtp.connect(&session);

  if (MailClient.sendMail(&smtp, &message))
  {
    Serial.println("EMAIL SENT");
  }
  else
  {
    Serial.println("EMAIL FAILED");
    Serial.println(smtp.errorReason());
  }

  smtp.closeSession();
}

// ---------------- SETUP ----------------
void setup()
{
  Serial.begin(115200);
  lcd.init();
lcd.backlight();

lcd.clear();
lcd.setCursor(0,0);
lcd.print("Cold Chain");
lcd.setCursor(0,1);
lcd.print("System Ready");
delay(2000);

  SPI.begin();
  rfid.PCD_Init();

  pinMode(IR_PIN, INPUT);
  dht.begin();
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  

  Serial.println();
  Serial.println("SCAN THE CARD");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Scan RFID Card");
}

// ---------------- LOOP ----------------
void loop()
{
  // GPS Update
  while (gpsSerial.available())
  {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isValid())
  {
    lat = String(gps.location.lat(), 6);
    lng = String(gps.location.lng(), 6);
  }

  // RFID Read
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
  {
    uid = "";

    for (byte i = 0; i < rfid.uid.size; i++)
    {
      if (rfid.uid.uidByte[i] < 0x10)
        uid += "0";

      uid += String(rfid.uid.uidByte[i], HEX);
    }

    uid.toUpperCase();

    Serial.println();
    Serial.println("RFID UID : " + uid);

    // JOURNEY START
    if (state == 0 && uid == authorizedUID)
    {
      state = 1;
      startTime = getTimeNow();
      startLat = lat;
      startLng = lng;

      breachDetails = "";
      tamperDetails = "";
      minTemp = 999;
      maxTemp = -999;
      tempSum = 0;
      tempCount = 0;

      breachCount = 0;
      tamperCount = 0;
      defaultIR = digitalRead(IR_PIN);

      Serial.println("ACCESS GRANTED");
      Serial.println("JOURNEY STARTED");
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Access Granted");
      lcd.setCursor(0,1);
      lcd.print("Select Med");

      String msg = "";
      msg += "JOURNEY STARTED\n\n";
      msg += "Latitude : " + lat + "\n";
      msg += "Longitude : " + lng + "\n";
      msg += "Google Maps : https://maps.google.com/?q=" + lat + "," + lng;

      sendMail("Journey Started", msg);

      Serial.println();
      Serial.println("SELECT MEDICINE");
      Serial.println("1. Insulin");
      Serial.println("2. Polio Vaccine");
      Serial.println("3. COVID Vaccine");
      Serial.println("4. Antibiotics");
      Serial.println("5. Blood Sample");
    }

    // JOURNEY END
    else if (state == 2 && uid == authorizedUID)
    {
      state = 0;
      endTime = getTimeNow();
      endLat = lat;
      endLng = lng;
      float avgTemp = 0;

      if(tempCount > 0)
      {
        avgTemp = tempSum / tempCount;
      }

      Serial.println("JOURNEY ENDED");
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Journey End");
      lcd.setCursor(0,1);
      lcd.print("Report Sent");

      String report = "";
      report += "JOURNEY END REPORT\n\n";
      report += "Medicine : " + medName + "\n";
      report += "Start Time : " + startTime + "\n";
      report += "End Time : " + endTime + "\n";

      report += "Min Temp : " + String(minTemp) + " C\n";
      report += "Max Temp : " + String(maxTemp) + " C\n";
      report += "Avg Temp : " + String(avgTemp) + " C\n";
      report += "Start GPS : " + startLat + "," + startLng + "\n";
      report += "End GPS : " + endLat + "," + endLng + "\n\n";
      report += "Breaches : " + String(breachCount) + "\n";
      report += "Tamper : " + String(tamperCount) + "\n";
      if(breachCount > 0)
      {
        report += "\nBREACH DETAILS\n";
        report += breachDetails;
      }

      if(tamperCount > 0)
      {
        report += "\nTAMPER DETAILS\n";
        report += tamperDetails;
      }
      if(breachCount == 0 && tamperCount == 0)
      {
        report += "Result : COMPLIANT\n";
      }
      else
      {
        report += "Result : NON-COMPLIANT\n";
      }
      report += "Latitude : " + lat + "\n";
      report += "Longitude : " + lng + "\n";
      report += "Google Maps : https://maps.google.com/?q=" + lat + "," + lng;

      sendMail("Journey End Report", report);
  
      irBreachSent = false;
    }

    rfid.PICC_HaltA();
  }

  // MEDICINE SELECT
   // MEDICINE SELECT USING KEYPAD
char key = keypad.getKey();

if (state == 1 && key)
{
  switch(key)
  {
    case '1':
      medicine = 1;
      break;

    case '2':
      medicine = 2;
      break;

    case '3':
      medicine = 3;
      break;

    case '4':
      medicine = 4;
      break;

    case '5':
      medicine = 5;
      break;

    default:
      return;
  }

  setMedicine(medicine);

  Serial.println();
  Serial.println("MEDICINE SELECTED : " + medName);
  Serial.println("MONITORING STARTED");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Medicine:");
  lcd.setCursor(0,1);
  lcd.print(medName);
  delay(2000);

 

      state = 2;
    
  }

  // LIVE MONITOR
  if (state == 2 && millis() - lastCheck > 2000)
  {
    currentTemp = dht.readTemperature();

    if (isnan(currentTemp))
    {
      Serial.println("DHT11 Error");
      return;
    }
    Serial.println("Medicine : " + medName);
    Serial.println("Temperature : " + String(currentTemp) + " C");
    Serial.println("Latitude : " + lat);
    Serial.println("Longitude : " + lng);
    if (millis() - lastMailTime >= 60000)
{
    lastMailTime = millis();

    String statusMail = "";

    statusMail += "LIVE STATUS\n\n";
    statusMail += "Medicine : " + medName + "\n";
    statusMail += "Temperature : " + String(currentTemp) + " C\n";
    statusMail += "Latitude : " + lat + "\n";
    statusMail += "Longitude : " + lng + "\n";
    statusMail += "Google Maps : https://maps.google.com/?q=" + lat + "," + lng;

    sendMail("LIVE STATUS UPDATE", statusMail);

    Serial.println("1 Minute Mail Sent");
}
    lastCheck = millis();
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(medName);

    lcd.setCursor(0,1);
    lcd.print(currentTemp);
    lcd.print(" C");

    Serial.println();
    Serial.println("Medicine : " + medName);
    tempSum += currentTemp;
    tempCount++;

    if (currentTemp < minTemp)
      minTemp = currentTemp;

    if (currentTemp > maxTemp)
      maxTemp = currentTemp;
    Serial.println("Temperature : " + String(currentTemp) + " C");
    Serial.println("Latitude : " + lat);
    Serial.println("Longitude : " + lng);

    int currentIR = digitalRead(IR_PIN);
    if(currentIR != defaultIR)
    {
      if(!irBreachSent)
      {
        tamperCount++;

        tamperDetails += "Tamper " + String(tamperCount) + "\n";
        tamperDetails += "Time : " + getTimeNow() + "\n";
        tamperDetails += "GPS : " + lat + "," + lng + "\n\n";

        sendMail("TAMPER ALERT",
                "Unauthorized box opening detected");

        irBreachSent = true;
      }
    }
    else
    {
      irBreachSent = false;
    }
     // TEMPERATURE BREACH
 // TEMPERATURE BREACH
if(currentTemp < minAllowedTemp ||
   currentTemp > maxAllowedTemp)
{
  if(!tempBreachSent)
  {
    breachCount++;

    breachDetails += "Breach " + String(breachCount) + "\n";
    breachDetails += "Time : " + getTimeNow() + "\n";
    breachDetails += "Temp : " + String(currentTemp) + " C\n";
    breachDetails += "GPS : " + lat + "," + lng + "\n\n";

    String alert = "";
    alert += "TEMPERATURE BREACH ALERT\n\n";
    alert += "Medicine : " + medName + "\n";
    alert += "Temperature : " + String(currentTemp) + " C\n";
    alert += "GPS : " + lat + "," + lng + "\n";

    sendMail("TEMPERATURE BREACH ALERT", alert);

    tempBreachSent = true;
  }
}
else
{
  tempBreachSent = false;
}

  }   // state == 2 monitor block end

}     // loop() end