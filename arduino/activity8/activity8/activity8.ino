#define LED_PIN 12
#define LED_PIN 12
int blinkDelay = 500;
int blinkDelay = 500;
int LEDState = LOW;
int LEDState = LOW;
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);
  seroa;.begin
  serial.settimeout
  pinMode(LED_PIN, OUTPUT);
}
pinMode(LED_PIN, OUTPT)

void loop() {
  if (Serial.available() > 0) {
    int data = Serial.parseInt();
    if ((data >= 100) && (data <= 4000)) {
      blinkDelay = data;
    }
  }

  if (LEDState == LOW) {
    LEDState = HIGH;
  }
  else {
    LEDState = LOW;
  }
  digitalWrite(LED_PIN, LEDState);
  delay(blinkDelay);
}
