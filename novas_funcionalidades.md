# Novas Funcionalidades Propostas para o Robô (ESP32-S3)

Com a migração para o **ESP32-S3**, agora temos à disposição um processador dual-core de 240MHz, memória Flash abundante e conectividade **Wi-Fi/Bluetooth**. 

Aqui estão as propostas de novas funções divididas em categorias. Você pode escolher quais deseja que eu implemente!

---

## 1. Conectividade Wi-Fi: Bichinho Virtual na Rede (Sem Hardware Extra)

* **O que faz**: O robô se conecta ao Wi-Fi da sua casa e roda um **servidor web interno**. Ao digitar o IP do robô no navegador do celular ou computador, uma página web moderna e interativa se abre.
* **Recursos do Painel Web**:
  * Visualização em tempo real de todas as estatísticas do pet (Fome, Diversão, Saúde, Vínculo, Temperatura, Umidade, Luminosidade).
  * **Botões Interativos**: Alimentar, Brincar ou Curar o pet diretamente pelo celular (que atualiza na hora os status e roda as animações na tela OLED física do robô).
  * Configuração do nome do Pet via página web.

---

## 2. Sensor de Toque Capacitivo na Cabeça (Sem Módulos Extras)
* **O que faz**: O ESP32-S3 tem pinos de leitura capacitiva integrados. Podemos conectar um fio simples a um parafuso, fita de cobre ou pedaço de metal no topo da cabeça do robô.
* **Como funciona**: Quando você fizer carinho encostando o dedo na cabeça do robô, o ESP32 detecta o toque capacitivo (`touchRead`) e aciona a animação de carinho (olhos felizes e corações subindo), aumentando os pontos de vínculo sem precisar apertar o botão físico.

---

## 3. Relógio de Tempo Real (RTC) e Sincronização Wi-Fi (NTP)
* **O que faz**: O robô sincroniza a hora exata da internet usando servidores NTP via Wi-Fi.
* **Novas Telas/Comportamentos**:
  * Adiciona uma tela de Relógio Digital elegante com data e hora.
  * **Ciclo Circadiano Real**: O robô dorme e acorda baseado no horário do mundo real (ex: fecha os olhos automaticamente a partir das 22:00 e acorda às 08:00), a menos que você acenda a luz ou brinque com ele.

---

## 4. Animações e Partículas com Física Suave
* **O que faz**: Aproveita a alta velocidade de processamento para calcular efeitos visuais mais fluidos rodando a 60 FPS estáveis na tela OLED.
* **Melhorias Visuais**:
  * **Física de Partículas**: Corações de carinho, gotas de suor e letras "zZz" do sono agora flutuam, giram e se dispersam com gravidade simulada e vento virtual em tempo real, em vez de subir em linhas retas.
  * **Transições de Tela Suaves**: Deslizamentos laterais (slide transitions) ao alternar entre as telas de informações (temperatura, umidade, relógio).

---

## 5. Reprodutor de Melodias RTTTL (Retro Nokia)
* **O que faz**: Substitui os bipes simples do buzzer por melodias completas de jogos e filmes clássicos codificados em formato RTTTL (como o tema de Star Wars, Super Mario Bros, Zelda, Missão Impossível, etc.).
* **Como funciona**: O robô tocará melodias temáticas em momentos marcantes (como ao chocar o ovo, ao subir de nível de vínculo ou ao ficar doente).
