#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 53
#define RST_PIN 5
MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Leitor RFID MFRC522 (SPI) ---");

  // Garante que o pino 53 (SS) do Arduino Mega esteja como saída
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);

  SPI.begin();       // Inicializa barramento SPI
  rfid.PCD_Init();   // Inicializa o leitor RFID MFRC522

  // Exibe informações de versão do chip no Monitor Serial (se retornar 0x00 ou 0xFF, há erro de fiação!)
  Serial.print("Diagnostico do Leitor RFID: ");
  rfid.PCD_DumpVersionToSerial();
  
  Serial.println("Aproxime um cartao ou tag RFID...");
}

void loop() {
  // Verifica se há novos cartões presentes
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Lê o número serial do cartão
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Exibe o UID (Código de Identificação) lido
  Serial.print("UID do Cartao Lido:");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  // Pausa para evitar múltiplas leituras consecutivas do mesmo cartão
  rfid.PICC_HaltA();
  delay(1000);
}
