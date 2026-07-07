const int somAnalogPin = A1;

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Sensor de Som Analogico ---");
}

void loop() {
  int maxLeitura = 0;
  int minLeitura = 1023;
  
  // Realiza amostragens em uma janela de ~1.5ms (15 leituras)
  // para calcular o desvio pico-a-pico (cancela offset DC do sensor)
  for (int i = 0; i < 15; i++) {
    int leitura = analogRead(somAnalogPin);
    if (leitura > maxLeitura) maxLeitura = leitura;
    if (leitura < minLeitura) minLeitura = leitura;
  }
  
  int somMaxDev = maxLeitura - minLeitura;

  Serial.print("Diferença Pico-a-Pico de Volume: ");
  Serial.print(somMaxDev);
  
  if (somMaxDev > 70) {
    Serial.println(" -> [BARULHO ALTO / SUSTO!]");
  } else if (somMaxDev > 45) {
    Serial.println(" -> [Barulho Médio / Fala]");
  } else {
    Serial.println(" -> [Silencioso / Ruído Baixo]");
  }

  delay(250);
}
