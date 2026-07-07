#include <Wire.h>
#include <U8g2lib.h>

// Construtor do display (mesmo do RoboESP32.ino)
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== TESTE DE DISPLAY E VARREDURA I2C ===");
  
  // Inicializa I2C nos pinos GPIO 4 (SDA) e GPIO 5 (SCL)
  Wire.begin(4, 5);
  
  // --- VARREDURA I2C (I2C SCANNER) ---
  Serial.println("Escaneando barramento I2C...");
  byte error, address;
  int nDevices = 0;
  
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("Dispositivo I2C encontrado no endereco 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("!");
      nDevices++;
    }
    else if (error == 4) {
      Serial.print("Erro desconhecido no endereco 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  
  if (nDevices == 0) {
    Serial.println("NENHUM dispositivo I2C encontrado! Verifique a fiacao.");
  } else {
    Serial.print("Varredura concluida. Dispositivos encontrados: ");
    Serial.println(nDevices);
  }
  
  // --- TESTE DE DISPLAY ---
  Serial.println("Inicializando Display OLED...");
  u8g2.begin();
  
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(10, 20, "ESP32-S3 OK!");
    u8g2.drawStr(10, 40, "I2C SDA: GPIO 4");
    u8g2.drawStr(10, 55, "I2C SCL: GPIO 5");
  } while (u8g2.nextPage());
  
  Serial.println("Desenho na tela enviado.");
}

void loop() {
  // Nada aqui
}
