const int buzzerPin = 7;

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Buzzer ---");
  pinMode(buzzerPin, OUTPUT);
}

void loop() {
  Serial.println("Tocando som alegre (Confirmacao de acao)...");
  // Melodia alegre curta
  tone(buzzerPin, 523, 50); delay(70);
  tone(buzzerPin, 659, 50); delay(70);
  tone(buzzerPin, 784, 50); delay(70);
  tone(buzzerPin, 1047, 100);
  
  delay(3000); // Aguarda 3 segundos antes do proximo bip
  
  Serial.println("Tocando som triste (Fome/Negligenciado)...");
  // Melodia triste curta
  tone(buzzerPin, 330, 150); delay(200);
  tone(buzzerPin, 262, 250);
  
  delay(5000); // Aguarda 5 segundos antes de reiniciar o ciclo
}
