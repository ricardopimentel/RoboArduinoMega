#include <Wire.h>

const int MPU_ADDR = 0x68; 
int16_t ax, ay, az; 

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Giroscopio/Acelerometro MPU-6050 (I2C) ---");
  
  Wire.begin();
  
  // Inicializa o MPU-6050 acordando-o do modo sleep
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // Registrador PWR_MGMT_1
  Wire.write(0);    // Define 0 para acordar o sensor
  byte status = Wire.endTransmission(true);

  if (status == 0) {
    Serial.println("MPU-6050 encontrado e configurado com sucesso!");
  } else {
    Serial.print("Falha ao comunicar com o MPU-6050. Status do erro: ");
    Serial.println(status);
    Serial.println("Verifique se as conexões SDA (Pino 20) e SCL (Pino 21) estão corretas.");
  }
}

void loop() {
  // Solicita as leituras de aceleração (registradores 0x3B ao 0x40 para os eixos X, Y, Z)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // Registrador inicial do acelerometro
  Wire.endTransmission(false);
  
  // Solicita 6 bytes (2 bytes para cada eixo: X, Y, Z)
  int bytesRead = Wire.requestFrom(MPU_ADDR, 6, true);
  
  if (bytesRead == 6) {
    ax = Wire.read() << 8 | Wire.read();
    ay = Wire.read() << 8 | Wire.read();
    az = Wire.read() << 8 | Wire.read();

    Serial.print("Acelerações - X: ");
    Serial.print(ax);
    Serial.print(" | Y: ");
    Serial.print(ay);
    Serial.print(" | Z: ");
    Serial.println(az);
  } else {
    Serial.println("Erro ao ler dados do MPU-6050! O barramento I2C pode estar desligado.");
  }

  delay(500);
}
