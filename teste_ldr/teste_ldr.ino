const int ldrPin = A0;

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Sensor de Luz LDR ---");
}

void loop() {
  int ldrValor = analogRead(ldrPin);
  // Converte a leitura analogica (0 a 1023) para porcentagem (100% a 0%)
  // Conforme a logica do Robo.ino: map(ldrValor, 0, 1023, 100, 0)
  int luzPorcento = map(ldrValor, 0, 1023, 100, 0);

  Serial.print("Leitura Analógica Crua (0-1023): ");
  Serial.print(ldrValor);
  Serial.print(" | Luminosidade Calculada: ");
  Serial.print(luzPorcento);
  Serial.println("%");

  delay(500);
}
