const int botaoPin = 4;

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Botao de Interacao ---");
  // Usa o resistor de pull-up interno do Arduino (botão deve fechar com o GND)
  pinMode(botaoPin, INPUT_PULLUP);
}

void loop() {
  int estadoBotao = digitalRead(botaoPin);

  if (estadoBotao == LOW) {
    Serial.println("BOTAO PRESSIONADO (Sinal LOW)!");
  } else {
    Serial.println("Botao Solto (Sinal HIGH)");
  }

  delay(200); // Pequeno atraso para facilitar a leitura no monitor serial
}
