#include <iSdio.h>
#include <SDExt.h>
#include <dht.h>

#include <avr/sleep.h>

#define DHT11_PIN 3
dht DHT;

#define CHIP_SELECT_PIN 4

#define LOG_INTERVAL 60000

#define ID_FILE "/id.txt"

uint8_t buffer[512];

char SSID[] = "Please set SSID";
char NETWORKKEY[] = "Please set PASSWORD";
char API_HOST[] = "Please set host IP address";
char API_PATH[] = "Please set host API path";

int ID = 0;
uint32_t nextSequenceId = 0;

boolean iSDIO_waitResponse(uint32_t sequenceId) {
  Serial.println(F("Waiting response "));
  uint8_t prev = 0xFF;
  for (int i = 0; i < 20; ++i) {
    memset(buffer, 0, 0x14);

    // Read command response status.
    if (!SD.card.readExtMemory(1, 1, 0x440, 0x14, buffer)) {
      return false;
    }

    uint8_t resp = get_u8(buffer + 8);
    if (sequenceId == get_u32(buffer + 4)) {
     if (prev != resp) {
        switch (resp) {
          case 0x00:
            Serial.println(F("  Initial"));
            break;
          case 0x01:
            Serial.println(F("  Command Processing"));
            break;
          case 0x02:
            Serial.println(F("  Command Rejected"));
            return false;
          case 0x03:
            Serial.println(F("  Process Succeeded"));
            return true;
          case 0x04:
            Serial.println(F("  Process Terminated"));
            return false;
          default:
            Serial.println(F("  Process Failed "));
            Serial.println(resp, HEX);
            return false;
        }
        prev = resp;
      }
    }
    Serial.print(F("."));
    delay(1000);
  }
  Serial.print(F("\n"));
  return false;
}

boolean iSDIO_disconnect(uint32_t sequenceId) {
  Serial.println(F("Disconnect command: "));
  memset(buffer, 0, 512);
  uint8_t* p = buffer;
  p = put_command_header(p, 1, 0);
  p = put_command_info_header(p, 0x07, sequenceId, 0);
  put_command_header(buffer, 1, (p - buffer));
  return SD.card.writeExtDataPort(1, 1, 0x000, buffer) ? true : false;
}

boolean iSDIO_connect(uint32_t sequenceId, const char* ssid, const char* networkKey) {
  Serial.println(F("Connect command: "));
  Serial.println(ssid);
  memset(buffer, 0, 512);
  uint8_t* p = buffer;
  p = put_command_header(p, 1, 0);
  p = put_command_info_header(p, 0x02, sequenceId, 2);
  p = put_str_arg(p, ssid);
  p = put_str_arg(p, networkKey);
  put_command_header(buffer, 1, (p - buffer));
  return SD.card.writeExtDataPort(1, 1, 0x000, buffer) ? true : false;
}


boolean iSDIO_http(uint32_t sequenceId, char* host, char* path, char* param) {
  Serial.println(F("http command: "));
  memset(buffer, 0, 512);
  uint8_t* p = buffer;
  p = put_command_header(p, 1, 0);
  p = put_command_info_header(p, 0x21, sequenceId, 2);
  p = put_str_arg(p, host);  // Argument #1.
  char getParam[128];
  sprintf(getParam,
    "GET %s?%s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "User-Agent: FlashAir\r\n"
    "\r\n", path, param, host);
  Serial.println(getParam);
  p = put_str_arg(p, getParam);
  put_command_header(buffer, 1, (p - buffer));
  return SD.card.writeExtDataPort(1, 1, 0x000, buffer) ? true : false;
}

boolean iSDIO_httpResponse(char* buf, size_t s) {
  // Read header and data.
  if (!SD.card.readExtDataPort(1, 1, 0x200, buffer)) {
    return false;
  }
  uint32_t totalSize = get_u32(buffer + 20);
  uint32_t availableSize = totalSize > 488 ? 488 : totalSize;
  uint32_t pos = 24;
  String response = "";
  for (;;) {
    for (uint32_t i = 0; i < availableSize; ++i) {
      response += (char)buffer[pos + i];
    }
    totalSize -= availableSize;
    
    // Have we read all data?
    if (totalSize == 0) break;
    
    // Read next data.
    if (!SD.card.readExtDataPort(1, 1, 0x200, buffer)) {
      return false;
    }
    availableSize = totalSize > 512 ? 512 : totalSize;
    pos = 0;
  }

  response = response.substring(response.indexOf("\r\n\r\n")+sizeof("\r\n\r\n")-1);
  response.toCharArray(buf,s);

  return true;
}

char* SD_read( char* path, char* buf, size_t s ) {

  delay(1000);
  File file = SD.open(path,FILE_READ);
  if ( !file ) { return ""; }

  String str = "";
  while (file.available()) { str += char(file.read()); }
  file.close();

  str.toCharArray(buf, s);

  return buf;
}

void setup_SD() {
  // Initialize SD card.
  Serial.println(F("Initializing SD card..."));  

  pinMode(SS, OUTPUT);
  if (!SD.begin(CHIP_SELECT_PIN)) {
    Serial.println(F("Card failed, or not present"));
    abort();
  }
 
  // Read the previous sequence ID.
  if (SD.card.readExtMemory(1, 1, 0x420, 0x34, buffer)) {
    if (buffer[0x20] == 0x01) {
      nextSequenceId = get_u32(buffer + 0x24);
      iSDIO_waitResponse(nextSequenceId);
      nextSequenceId++;
    } else {
      nextSequenceId = 0; 
    }
  } else {
    Serial.println(F("Failed to read status."));
    nextSequenceId = 0; 
  }

  char buf[32];
  ID = atoi(SD_read(ID_FILE,buf,32));
  Serial.print(F("ID="));
  Serial.println(ID);
}


void setup() {
  // Initialize UART for message print.
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  setup_SD();
}

void loop() {

  unsigned long start = millis();
  digitalWrite(13,HIGH);

  //### Make Parameter ###
  int chk = DHT.read11(DHT11_PIN);
  if ( chk != 0 ){
    Serial.println(F("Fail to read humidity."));
    delay(1000);
    return;
  }
  char humid[8];
  char temp[8];
  char param[32];
  sprintf(param,"ID=%d&H=%s&T=%s",
    ID,dtostrf(DHT.humidity,5,2,humid),dtostrf(DHT.temperature,5,2,temp));
  Serial.print(F("PARAM:"));
  Serial.println(param);

  //### Connect ###
  for ( int retry = 3; retry > 0; retry -- ) {
    delay(1000);
    if( iSDIO_connect(nextSequenceId, SSID, NETWORKKEY) &&
      iSDIO_waitResponse(nextSequenceId) ) {
      Serial.println(F("Connected"));
      retry = 0;
    } else {
      Serial.print(F("Failed or waiting. errorCode="));
      Serial.println(SD.card.errorCode(), HEX);
    }
    nextSequenceId++;
  }

  //### Send Parameter ###
  for ( int retry = 3; retry > 0; retry -- ) {
    delay(1000);
    if ( iSDIO_http(nextSequenceId,API_HOST,API_PATH,param) &&
      iSDIO_waitResponse(nextSequenceId)) {
        Serial.println(F("OK"));
        retry = 0;
    } else {
      Serial.print(F("Failed or waiting. errorCode="));
      Serial.println(SD.card.errorCode(), HEX);
    }
    nextSequenceId++;
  }

  //### Disconnect ###
  if (iSDIO_disconnect(nextSequenceId) &&
      iSDIO_waitResponse(nextSequenceId)) {
    Serial.println(F("Success."));
  } else {
    Serial.print(F("Failed or waiting. errorCode="));
    Serial.println(SD.card.errorCode(), HEX);
  }
  nextSequenceId++;

  digitalWrite(13,LOW);

  //### sleep ###
  unsigned long span = millis() - start;
  if ( span > LOG_INTERVAL ) { return; }
  unsigned long sleep_time = LOG_INTERVAL - span;
  Serial.print(F("SLEEP => "));
  Serial.println(sleep_time,DEC);
  //delay(sleep_time);
  delaySleep(sleep_time);
  //sleep(sleep_time);
}


void delaySleep(unsigned long t) {
  //
  // 注意：millis関数を使っているので50日以上の連続動作は出来ない。
  //
  unsigned long t0;
  if( t <= 16 ) {                       // 16ms以下なら普通のdelayで処理
    delay(t);
  }
  else{                                 // 17ms以上ならスリープ入れたdelayで実行
    t0 = millis();                      // 開始時のmillisの値を記録しておき
    set_sleep_mode (SLEEP_MODE_IDLE);   // アイドルのモード指定
    while( millis() - t0 < t ) {        // 設定値になるまでループ
      sleep_mode();                     // スリープに入れる（自動復帰するので何度も指定）
    }
  }
}

