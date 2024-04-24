#include <SPI.h>
#include <MFRC522.h>
#include <hardware/flash.h>
#define SS_PIN 17
#define RST_PIN 20
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
char build_version[] = "CATALYST_485_RFID_5R_V1.1";
String default_packet = "";
char default_relay_states[]="00011"; 
char temp_state[]="00011";
char original_state[]="00011";
unsigned long timeoutarray[5] = {0,0,0,0,0};
unsigned long timeinarray[5] = {0,0,0,0,0};
unsigned long timeexp[5] = {0,0,0,0,0};

unsigned long lastPacketChange = 0;
unsigned long packetTimeout = 0;  // 15 seconds in milliseconds

MFRC522 mfrc522(SS_PIN, RST_PIN);
unsigned long rfidResetTimer = millis();
String readed_version = "";
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial1.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init(SS_PIN, RST_PIN);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);
  readed_version = read_build_version();
  if (readed_version[22] != build_version[22] | readed_version[23] != build_version[23] | readed_version[24] != build_version[24]) {
    write_build_version(build_version);
  }
  int device_id = read_device_id();
 
  if (device_id >= 97 && device_id <= 122) {

  } else {
    write_device_id('a');
  }
  switch_relays("A0"+String((char)device_id)+default_relay_states);
}

void loop() {
  // Serial.println(PICO_FLASH_SIZE_BYTES_16MB);     // 16777216 or 2097152
  // Serial.println(FLASH_SECTOR_SIZE);              // 4096
  // Serial.println(FLASH_PAGE_SIZE);                // 256
  // Serial.println(FLASH_PAGE_SIZE / sizeof(int));  // 64
  // Serial.println(FLASH_TARGET_OFFSET);            // 16773120 or 2093056
  check_timeout();
  String packet = "";
  delay(500);
  if ((millis() - lastPacketChange >= packetTimeout) && (lastPacketChange > 0) && (packetTimeout > 0)) {
    sendDefaultPacket();
  }
  if (Serial1.available()) {
    packet = Serial1.readStringUntil('\n');
    int start_index = packet.indexOf('A');
    int end_index = packet.indexOf("\r\n");
    packet = packet.substring(start_index, end_index);
    if (packet[0] == 'A'
        && packet[1] == '1'
        && (packet[2] >= 97 && packet[2] <= 122)
        && packet[3] == 'G'
        && packet[4] == 'E'
        && packet[5] == 'T'
        && packet[6] == 'V'
        && packet[7] == 'N'
        && packet[8] == '?') {
      send_build_version_toRS485();
    } else if (packet[0] == 'A'
               && packet[1] == '1'
               && (packet[2] >= 97 && packet[2] <= 122)
               && packet[3] == 'N'
               && packet[4] == '?') {
      send_device_id_toRS485();
    } else if (packet[0] == 'A' && packet[1] == 'P' && packet[2] == 'G' && packet[3] == 'M') {
      write_device_id(packet[5]);
    } else {
      uint8_t csum = 0;
      for (int i = 0; i < 10; i++) {
        csum += packet[i];
      }
      uint8_t csum2 = packet[10];
      if (csum == csum2) {
        if ((packet[8] >= 48 && packet[8] <= 57) && (packet[9] >= 48 && packet[9] <= 57)) {
          String time_s_string = packet.substring(8, 10);
          unsigned long time_s = time_s_string.toInt() * 1000;
          if (is_valid_relay_packet(packet)) {
            if (packet[8] == 48 && packet[9] == 48) {
              String pkt= packet.substring(3,8);
              for(int i=0; i<5; i++){
                if(pkt[i]!='2'){
                  original_state[i] = pkt[i];
                }
              }
              char id = read_device_id();
              String org = "A0"+String(id)+original_state;
              Serial.println(org);
              switch_relays(org);
            } else {
              String pkt= packet.substring(3,8);
              for(int i = 0; i<5; i++){
                 if(pkt[i]!= '2'){
                if(pkt[i]!= original_state[i]){
                  update_temp_state(pkt[i],i);
                  lastPacketChange = millis();
                  update_time_inout(lastPacketChange, i, time_s);
                }
              }
              }
              
            }
          }
        }
      }
    }
  }
  if (millis() - rfidResetTimer >= 5000) {
    mfrc522.PCD_Init(SS_PIN, RST_PIN);
    rfidResetTimer = millis();
  }

  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  // Select one of the cards
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  //Show UID on serial monitor
  // Serial.println();
  // Serial.print("UID tag : ");
  String content = "";
  byte letter;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    // Serial1.print(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    // Serial1.print(mfrc522.uid.uidByte[i], HEX);
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
 
  uint8_t csum = 0;
  for(int i=0; i<content.length();i++){
      csum += content[i];
  }
  content="RFID"+String((char)content.length())+content+(char)csum;
  content.toUpperCase();
  for(int i; i<content.length(); i++){
    Serial1.write(content[i]);
  }
  Serial1.println();
}

String read_build_version() {
  int *p;
  int address;
  char value[25];
  // Compute the memory-mapped address, remembering to include the offset for RAM
  address = XIP_BASE + (FLASH_TARGET_OFFSET);
  p = (int *)address;  // Place an int pointer at our memory-mapped address
  for (int i = 0; i < 25; i++) {
    value[i] = *(p + i);
    // Serial.print(value[i]);
  }
  String version = value;
  return version;
}

void send_build_version_toRS485() {
  String version = read_build_version();
  Serial1.write("N[");
  for (int i = 0; i < 25; i++) {
    Serial1.write(version[i]);
  }
  Serial1.write("]");
  Serial1.write("\n");
}

void write_build_version(char *id) {
  int buf[FLASH_PAGE_SIZE / sizeof(int)];
  int mydata[25];  // One page worth of 32-bit ints
  for (int i = 0; i < 25; i++) {
    mydata[i] = id[i];
    buf[i] = mydata[i];  // Put the data into the first four bytes of buf[]
  }
  uint32_t ints = save_and_disable_interrupts();
  // Erase the last sector of the flash
  flash_range_erase((PICO_FLASH_SIZE_BYTES - (FLASH_SECTOR_SIZE)), FLASH_SECTOR_SIZE);
  // Program buf[] into the first page of this sector
  flash_range_program((PICO_FLASH_SIZE_BYTES - (FLASH_SECTOR_SIZE)), (uint8_t *)buf, FLASH_PAGE_SIZE);
  restore_interrupts(ints);
  // The data I want to store
}

char read_device_id() {
  int *p;
  int addr;
  char value;
  // Compute the memory-mapped address, remembering to include the offset for RAM
  addr = XIP_BASE + FLASH_TARGET_OFFSET - FLASH_SECTOR_SIZE;
  p = (int *)addr;  // Place an int pointer at our memory-mapped address
  value = *p;
  return value;
}

void send_device_id_toRS485() {
  char value = read_device_id();
  Serial1.write("Current id = ");
  Serial1.write(value);
  Serial1.write("\n");
}

void write_device_id(char id) {
  int buf[FLASH_PAGE_SIZE / sizeof(int)];  // One page worth of 32-bit ints
  int mydata = id;                         // The data I want to store
  if (mydata >= 97 && mydata <= 122) {
    buf[0] = mydata;  // Put the data into the first four bytes of buf[]
    uint32_t ints = save_and_disable_interrupts();
    // Erase the last sector of the flash
    flash_range_erase(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    // Program buf[] into the first page of this sector
    flash_range_program(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - FLASH_SECTOR_SIZE, (uint8_t *)buf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
  }
}

void switch_relays(String packet) {
  char device_id = read_device_id();
  if (device_id == packet[2]) {
    packet = packet.substring(3, 8);
    char relay_switch[packet.length() + 1];
    packet.toCharArray(relay_switch, packet.length() + 1);
    if (relay_switch[0] != '2') {
      digitalWrite(10, ((String(relay_switch[0])).toInt()));
    }
    if (relay_switch[1] != '2') {
      digitalWrite(9, ((String(relay_switch[1])).toInt()));
    }
    if (relay_switch[2] != '2') {
      digitalWrite(8, ((String(relay_switch[2])).toInt()));
    }
    if (relay_switch[3] != '2') {
      digitalWrite(7, (!((String(relay_switch[3])).toInt())));
    }
    if (relay_switch[4] != '2') {
      digitalWrite(6, (!((String(relay_switch[4])).toInt())));
    }
  }
}

bool is_valid_relay_packet(String Data) {
  if (Data[0] == 'A' && (Data[1] == '0' | Data[1] == '1') && (Data[2] >= 97 && Data[2] <= 122)
      && (Data[3] == '0' || Data[3] == '1' || Data[3] == '2')
      && (Data[4] == '0' || Data[4] == '1' || Data[4] == '2')
      && (Data[5] == '0' || Data[5] == '1' || Data[5] == '2')
      && (Data[6] == '0' || Data[6] == '1' || Data[6] == '2')
      && (Data[7] == '0' || Data[7] == '1' || Data[7] == '2')) {
    return true;
  }
  return false;
}

void sendDefaultPacket() {
  switch_relays(default_packet);
  packetTimeout = 0;
  lastPacketChange = 0;
}
void update_temp_state(char sw,int index){
  char pkt[] = "22222";
  pkt[index]= sw;
  char id  = read_device_id();
  String st = "A0"+String(id)+pkt;
  switch_relays(st);
}

void update_time_inout(unsigned long lastPacketChange, int index, unsigned long packetTimeout){
  timeinarray[index] = lastPacketChange;
  timeoutarray[index] = lastPacketChange + packetTimeout;
  timeexp[index]=packetTimeout;
  Serial.print("Time in Array [");
  for(int i=0; i<5; i++){
    Serial.print(timeinarray[i]);
    Serial.print(" ");
  }
  Serial.print("]");
  Serial.println();
  Serial.print("Time out Array [");
  for(int i=0; i<5; i++){
    Serial.print(timeoutarray[i]);
    Serial.print(" ");
  }
  Serial.print("]");
  Serial.println();
  Serial.print("Time Exp Array [");
  for(int i=0; i<5; i++){
    Serial.print(timeexp[i]);
    Serial.print(" ");
  }
  Serial.print("]");
  Serial.println();
}

void check_timeout(){
  for(int i=0; i<5; i++){
    if((millis()>= timeoutarray[i]) && (timeoutarray[i]!=0) && (timeinarray[i]!=0) && timeexp[i]!=0){
      update_original_state(i);
      timeinarray[i] = 0;
      timeoutarray[i] = 0;
      timeexp[i]=0;
      Serial.print(i);
      Serial.println( "succussfuly reset");
    }

  }
}

void update_original_state(int i){
   char pkt[] = "22222";
  pkt[i]= original_state[i];
  char id  = read_device_id();
  String st = "A0"+String(id)+pkt;
  switch_relays(st);
}