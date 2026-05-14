// Interrupt-Driven Button - XIAO ESP32-S3
// Using D1 as the interrupt pin

const int BUTTON_PIN = D1;
bool statusBtn=false;
bool statusBtnBefore=false;
unsigned long timePrev = -1;
void IRAM_ATTR handleButton() {
  if(statusBtn!=statusBtnBefore)
    timePrev=millis();
  if(timePrev!=-1 && millis()-timePrev>300){
    statusBtn=!statusBtn;
    timePrev=-1;
  }
  statusBtnBefore=statusBtn;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Start");
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_PIN, handleButton, FALLING);
}

void loop() {

}