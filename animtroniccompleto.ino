#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
#include <Servo.h>

const char* ssid = "pitbull baliza";
const char* password = "19991010";
const char* mqtt_server = "test.mosquitto.org";

#define pinRx D2
#define pinTx D3
#define SERVO_PIN D5

Servo motorServo;
SoftwareSerial playerMP3Serial(pinRx, pinTx);
DFRobotDFPlayerMini playerMP3;

WiFiClient espClient;
PubSubClient client(espClient);

const char* topicPlay = "dfplayer/music/play";
const char* topicVolume = "dfplayer/volume/set";
const char* topicStop = "dfplayer/music/stop";
const char* topicLoop = "dfplayer/music/loop";
const char* topicAuto = "dfplayer/music/auto";

const char* topicServoOn = "servo/on";
const char* topicServoOff = "servo/off";
const char* topicVigilancia = "servo/vigilancia";

const char* topicPreset = "dfplayer/preset";
const char* topicPresetStop = "dfplayer/preset/stop";


bool servoAtivo = false;
unsigned long ultimoMovimento = 0;
int estadoServo = 0;

int pasta = 1;
int musica = 1;
int estadoAnterior = -1;

bool repetirMusica = false;
bool modoAutomatico = false;

bool presetExecutando = false;
bool interromperPreset = false;

bool modoVigilancia = false;

String clientId;

void moverServo(int anguloInicial, int anguloFinal, int tempoMS, int repeticoes) {
  for (int r = 0; r < repeticoes && !interromperPreset; r++) {
    for (int ang = anguloInicial; ang <= anguloFinal && !interromperPreset; ang++) {
      motorServo.write(ang);
      delay(tempoMS);
    }
    for (int ang = anguloFinal; ang >= anguloInicial && !interromperPreset; ang--) {
      motorServo.write(ang);
      delay(tempoMS);
    }
  }
}

void moverServoDuranteMusica(int anguloInicial, int anguloFinal, int tempoMS) {
  if (!motorServo.attached()) motorServo.attach(SERVO_PIN);
  
  bool subindo = true;
  int angulo = anguloInicial;
  
  while (playerMP3.readState() != 512 && !interromperPreset) {
    motorServo.write(angulo);
    delay(tempoMS);
    
    if (subindo) {
      angulo++;
      if (angulo >= anguloFinal) subindo = false;
    } else {
      angulo--;
      if (angulo <= anguloInicial) subindo = true;
    }
  }

  motorServo.detach();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT] Tópico recebido: ");
  Serial.print(topic);
  Serial.print(" | Mensagem: ");
  Serial.println(msg);

  if (String(topic) == topicPlay) {
    int idx = msg.indexOf(',');
    if (idx > 0) {
      pasta = msg.substring(0, idx).toInt();
      musica = msg.substring(idx + 1).toInt();
      playerMP3.playFolder(pasta, musica);
    }
  } 
  else if (String(topic) == topicVolume) {
    playerMP3.volume(msg.toInt());
  } 
  else if (String(topic) == topicStop) {
    playerMP3.stop();
  }
  else if (String(topic) == topicLoop) {
    repetirMusica = msg == "on";
    Serial.print("Loop está ");
    Serial.println(repetirMusica ? "ativado" : "desativado");
  }
  else if (String(topic) == topicAuto) {
    modoAutomatico = msg == "on";
    Serial.print("Modo automático está ");
    Serial.println(modoAutomatico ? "ativado" : "desativado");
  }
  else if (String(topic) == topicServoOn) {
    servoAtivo = true;
    Serial.println("Servo iniciado (modo girar)");
  } 
  else if (String(topic) == topicServoOff) {
    servoAtivo = false;
    motorServo.detach();
    Serial.println("Servo parado");
  }
  else if (msg == "stop") {
    interromperPreset = true;
    playerMP3.stop();
    motorServo.detach();
    presetExecutando = false;
    Serial.println("Preset interrompido.");
  }
  else if (String(topic) == topicVigilancia) {
    if (msg == "on") {
      modoVigilancia = true;
      servoAtivo = false;
      if (!motorServo.attached()) motorServo.attach(SERVO_PIN);
      Serial.println("Modo vigilância ativado");
    } else {
      modoVigilancia = false;
      motorServo.detach();
      Serial.println("Modo vigilância desativado");
    }
  }


}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (client.connect(clientId.c_str())) {
      Serial.println("conectado");
      playerMP3.playFolder(3, 1);
      client.subscribe(topicPlay);
      client.subscribe(topicVolume);
      client.subscribe(topicStop);
      client.subscribe(topicLoop);
      client.subscribe(topicAuto);
      client.subscribe(topicServoOn);
      client.subscribe(topicServoOff);
      client.subscribe(topicPreset);
      client.subscribe(topicPresetStop);
      client.subscribe(topicVigilancia);
    } else {
      Serial.print("Falha, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  playerMP3Serial.begin(9600);
  
  if (!playerMP3.begin(playerMP3Serial)) {
    Serial.println("Erro ao iniciar DFPlayer!");
    while (true) delay(0);
  }
  playerMP3.volume(35);
  playerMP3.playFolder(3, 2);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
  playerMP3.playFolder(3, 3);


  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  clientId = "ESP8266Client-" + String(ESP.getChipId());

  motorServo.attach(SERVO_PIN);
  motorServo.write(90);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  static unsigned long ultimoCheck = 0;
if (millis() - ultimoCheck > 1000) {
  ultimoCheck = millis();
  
  int estadoAtual = playerMP3.readState();

  if (estadoAnterior != 512 && estadoAtual == 512) {
    // Transição de tocando para parado
    if (repetirMusica) {
      playerMP3.playFolder(pasta, musica);
    } else if (modoAutomatico) {
      musica++;
      if (musica > 30) musica = 1; // ajustar de acordo com seu limite
      playerMP3.playFolder(pasta, musica);
      Serial.print("Tocando próxima música automática: ");
      Serial.println(musica);
    }
  }

  estadoAnterior = estadoAtual;
}

  if (servoAtivo) {
    unsigned long agora = millis();
    if (agora - ultimoMovimento >= 500) { // alterna a cada 0.5s
      if (!motorServo.attached()) {
        motorServo.attach(SERVO_PIN);
      }

      if (estadoServo == 0) {
        motorServo.write(1);
        estadoServo = 1;
      } else {
        motorServo.write(0);
        estadoServo = 0;
      }
      ultimoMovimento = agora;
    }
  }

  static unsigned long tempoAnterior = 0;
  static float anguloAtualF = 60;
  static bool indoDireita = true;

  if (modoVigilancia) {
    unsigned long agora = millis();
    if (!motorServo.attached()) motorServo.attach(SERVO_PIN);

    float tempoTotal = random(1000, 5001);
    float passos = 60;
    float passoTempo = tempoTotal / passos;
    float passoAngulo = 1.0;

    if (agora - tempoAnterior >= passoTempo) {
      tempoAnterior = agora;
      if (indoDireita) {
        anguloAtualF += passoAngulo;
        if (anguloAtualF >= 120) {
          anguloAtualF = 120;
          indoDireita = false;
        }
      } else {
        anguloAtualF -= passoAngulo;
        if (anguloAtualF <= 60) {
          anguloAtualF = 60;
          indoDireita = true;
        }
      }
      motorServo.write((int)anguloAtualF);
    }
  }
}
