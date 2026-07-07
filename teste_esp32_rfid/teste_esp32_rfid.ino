#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 10
#define RST_PIN 14
MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== TESTE DE LEITOR RFID MFRC522 NO ESP32-S3 ===");
  
  // Inicializa barramento SPI com pinos customizados
  SPI.begin(12, 13, 11, 10); // SCK=12, MISO=13, MOSI=11, SS=10
  rfid.PCD_Init();
  
  Serial.println("Inicializacao concluida. Lendo versao do chip...");
  rfid.PCD_DumpVersionToSerial();
  
  Serial.println("\nAproxime uma tag RFID/NFC para testar a leitura...");
}

void loop() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    Serial.print("Tag lida com sucesso! UID:");
    for (byte i = 0; i < rfid.uid.size; i++) {
      Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(rfid.uid.uidByte[i], HEX);
    }
    Serial.println();
    
    rfid.PICC_HaltA();
  }
  delay(100);
}
