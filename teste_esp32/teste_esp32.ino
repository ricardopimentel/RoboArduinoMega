void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("===============================");
  Serial.println("CONEXAO COM ESP32-S3 FUNCIONANDO!");
  Serial.println("===============================");
  
  // Configura GPIO 2 e 15 como saída (LEDs comuns de placas de teste)
  pinMode(2, OUTPUT);
  pinMode(15, OUTPUT);
}

void loop() {
  Serial.println("Blink rodando... ");
  
  digitalWrite(2, HIGH);
  digitalWrite(15, HIGH);
  delay(1000);
  
  digitalWrite(2, LOW);
  digitalWrite(15, LOW);
  delay(1000);
}
