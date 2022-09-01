
#include <Int64String.h>

//BT24藍芽模組
int ble_rx = D12;
int ble_tx = D11; 
int ble_disconnect = D10; //>輸出200ms的低電壓脈衝給BLE: 連線中會斷開連線，未連線時則會退出低功耗模式
int ble_setting_mode = D10; //同上
int ble_state = GPIO_NUM_34; //BT24 STATE 連線:1；未連線:0
gpio_num_t ble_state_gpio = GPIO_NUM_34;

//電池與供電
int battery_voltage = A1; //讀取電池電壓(經過分壓)
int vout_gate = D3; //連接mosfet的gate，控制輸出供電與否，0斷電；1通電
RTC_DATA_ATTR uint64_t  work_period = 5; //每隔多久喚醒供電(秒)
RTC_DATA_ATTR uint64_t  work_time = 5; //每次供電多久(秒);

//外接用電模組
int fwd_rx=D7;
int fwd_tx=D6;

//其他
unsigned long runTime;  //工作計時器
#define LED_pin   D9
#define uS_TO_S_FACTOR 1000000ULL 

//void IRAM_ATTR toggleLED()
//{
//  Serial.println("IRAM_ATTR");
//  //digitalWrite(LED_pin, !digitalRead(LED_pin));
//}
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason){
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}
void enter_sleep(){
  //gpio_hold_en(GPIO_NUM_11);
  //中斷供電
  Serial.println("中斷供電");
  digitalWrite(vout_gate, LOW);
  delay(1000);
  //斷開連線
  bt24_disconnect();
  delay(500);
  Serial1.write(String("AT+RESET\r\n").c_str()); //以防意外退出低功耗模式
  //設定喚醒腳位
  //esp_sleep_enable_ext1_wakeup(ble_hex_state, ESP_EXT1_WAKEUP_ANY_HIGH); //0x11即gpio 4
  if(work_period>0){
    esp_sleep_enable_timer_wakeup(work_period * uS_TO_S_FACTOR);  
    esp_sleep_enable_ext0_wakeup(ble_state_gpio, 1); //1 = High, 0 = Low
    esp_deep_sleep_start(); 
    
  }  
  Serial.println("工作週期設為0不會進入睡眠");
}
void bt24_disconnect(){
  //給BT24 key pin 一個低壓脈衝，隨後懸空
  pinMode(ble_disconnect, OUTPUT);
  digitalWrite(ble_disconnect, HIGH);
  delay(250);
  digitalWrite(ble_setting_mode, LOW);
  delay(250);
  digitalWrite(ble_disconnect, HIGH);
  pinMode(ble_disconnect, INPUT);
}
void bt24_enter_setting_mode(){
  bt24_disconnect();
  delay(500);
  bt24_disconnect();
  delay(500);
}
void setup() {
//  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1 );
  
  pinMode(ble_disconnect, INPUT);
  pinMode(ble_setting_mode, INPUT);
  pinMode(battery_voltage, INPUT);
  pinMode(vout_gate, OUTPUT);
  pinMode(LED_pin, OUTPUT); //工作狀態指示燈
  digitalWrite(LED_pin, HIGH);
  digitalWrite(vout_gate, HIGH);
//  pinMode(interruptPin, INPUT_PULLUP);
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, ble_rx, ble_tx);
  Serial2.begin(9600, SERIAL_8N1, fwd_rx, fwd_tx);
  Serial.println("Serial Txd is on pin: "+String(ble_tx));
  Serial.println("Serial Rxd is on pin: "+String(ble_rx));
//  attachInterrupt(digitalPinToInterrupt(interruptPin), toggleLED, RISING);
  runTime = millis();
  print_wakeup_reason();
}

void loop() {
  // put your main code here, to run repeatedly:
  //藍芽訊息轉發
  while (Serial1.available()||Serial2.available()) {
    HardwareSerial *MySerial = Serial1.available()? &Serial1:&Serial2;
    String msg = Serial1.available()? Serial1.readString():Serial2.readString();
    msg.trim();
    Serial.println(msg);
    //設定藍芽模組
    if(msg.substring(0,2)=="AT"){
      Serial.println("藍芽退出低功耗，進入設定模式...");
      bt24_enter_setting_mode();
      Serial.println("轉發AT指令:"+(msg+"\r\n"));
      MySerial->write((msg+"\r\n").c_str());
    }
    //進入深度睡眠
    if(msg=="esp_sleep"){
      Serial.println("進入深度睡眠...");
      enter_sleep();
    }
    //斷開藍芽連線
    if(msg=="ble_disconnection"){
      Serial.println("斷開藍芽連線...");
      bt24_disconnect();
    }
    //取得3.7v電池電壓電量
    if(msg=="batt"){
      Serial.println("檢測剩餘電量...");
//      3.294/ 2.6
      int read_a = analogRead(battery_voltage);
      Serial.print("analogRead:");;Serial.println(read_a);
      float voltage = map(analogRead(battery_voltage), 0, 4095, 0, 3294);
      voltage = map(voltage, 2510, 3294, 320, 420);
      float life = map(voltage, 320, 420, 0, 100);
      Serial.print("電壓:");Serial.println(voltage);
      Serial.print("電量:");Serial.println(life);
      MySerial->write(("batt_vol="+String(float(voltage)/100, 3)+'\n').c_str()); //voltage
      MySerial->write(("batt_life="+String(float(life), 3)+'\n').c_str()); //mah
    }

    //供電
    if(msg=="power_on"){
      digitalWrite(vout_gate, HIGH);
      MySerial->write("Success\n");
    }
    //斷電
    if(msg=="power_off"){
      digitalWrite(vout_gate, LOW);
      MySerial->write("Success\n");
    }
    //目前設定:工作週期(斷電多久供電一次)
    if(msg=="period"){
      MySerial->write(String("period="+int64String(work_period)+"\n").c_str());
    }
    //設定供電週期 0表示不進入睡眠
    else if(msg.substring(0,7)=="period="){
      work_period = msg.substring(7).toInt();
      MySerial->write("Success\n");
    }
    if(msg=="worktime"){
      MySerial->write(String("work_time="+int64String(work_time)+"\n").c_str());
    }
    //最大清醒時間，中間有輸入則自動延長，超過自動進入睡眠 0表示無限
    if(msg.substring(0,9)=="worktime="){
      work_time = msg.substring(9).toInt();
      MySerial->write("Success\n");
    }
    //藍芽轉送指令給外接模組
    if(msg.substring(0,4)=="fwd="){
      Serial2.write(msg.substring(4).c_str());
    }
    
  }
  //Serial轉發給藍芽
  while (Serial.available()) {
    String msg = Serial.readString();
    msg.trim();
    Serial1.write(msg.c_str());
    Serial2.write(msg.c_str());
    
    if(msg=="esp_sleep"){
      Serial.println("進入深度睡眠...");
      enter_sleep();
    }
    if(msg=="ble_disconnection"){
      Serial.println("斷開藍芽連線...");
      bt24_disconnect();
    }
    //取得3.7v電池電壓電量
    if(msg=="batt"){
      Serial.println("檢測剩餘電量...");
//      3.294/ 2.6
      int read_a = analogRead(battery_voltage);
      Serial.print("analogRead:");;Serial.println(read_a);
      float voltage = map(analogRead(battery_voltage), 0, 4095, 0, 3294);
      voltage = map(voltage, 2510, 3294, 320, 420);
      float life = map(voltage, 320, 420, 0, 100);
      Serial.print("電壓:");Serial.println(voltage);
      Serial.print("電量:");Serial.println(life);
      Serial.print(("batt_vol="+String(float(voltage)/100, 3)+'\n').c_str()); //voltage
      Serial.print(("batt_life="+String(float(life), 3)+'\n').c_str()); //mah
    }
  }
  //超時進入睡眠
  if(((millis()-runTime) > work_time*1000) and work_time!=0){
    //連線中不進入睡眠
    if(digitalRead(ble_state)==0){
      enter_sleep();  
    }
    
    //runTime = millis();
  }
}
