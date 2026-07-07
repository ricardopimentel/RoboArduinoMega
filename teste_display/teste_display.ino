#include <U8g2lib.h>
#include <Wire.h>

// Inicializa o display OLED SSD1306 (128x64) via I2C por Hardware
// SDA = Pino 20, SCL = Pino 21 no Arduino Mega
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitário do Display OLED ---");

  Wire.begin();
  u8g2.begin();
}

void loop() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(0, 24, "Display OK!");
    u8g2.setFont(u8g2_font_profont12_tf);
    u8g2.drawStr(0, 45, "SDA: Pino 20");
    u8g2.drawStr(0, 60, "SCL: Pino 21");
  } while ( u8g2.nextPage() );

  Serial.println("Desenhando na tela...");
  delay(1000);
}
