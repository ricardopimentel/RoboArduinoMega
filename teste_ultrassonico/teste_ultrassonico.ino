const int trigPin = 9;
const int echoPin = 8;

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Sensor Ultrassonico HC-SR04 ---");
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

void loop() {
  long duracao, distancia;
  
  // Limpa o trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Ativa o trigPin por 10 microssegundos
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Le o echoPin, retorna o tempo de viagem da onda em microssegundos
  duracao = pulseIn(echoPin, HIGH, 30000); // timeout de 30ms (~5 metros max)
  
  // Calcula a distancia
  distancia = duracao * 0.034 / 2;
  
  if (duracao == 0) {
    Serial.println("Sem leitura/Fora de alcance. Verifique a conexao do HC-SR04 (Trig=9, Echo=8).");
  } else {
    Serial.print("Distancia medida: ");
    Serial.print(distancia);
    Serial.println(" cm");
  }
  
  delay(500);
}
