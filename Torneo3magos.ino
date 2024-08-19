const char *version = "1.01"; // Versión del programa 
// Adaptación para la Trivia de los 3 Magos

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2SNoDAC.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESP32Servo.h>

bool OTAhabilitado = false;

// Configuración de la red WiFi
const char *ssid = "";
const char *password = "";

// Pines del Bus SPI para la conexión de la Tarjeta SD
#define SCK 18
#define MISO 19
#define MOSI 23
#define CS 5

// Pines para los pulsadores (ahora activos en bajo)
#define PIN_BOTON_1 4
#define PIN_BOTON_2 15
#define PIN_BOTON_3 34
#define PIN_BOTON_4 35

// Pin para el servomotor
#define PIN_SERVO_TAPA 13

// Pines para LED RGB
#define PIN_LED_ROJO 25
#define PIN_LED_VERDE 26
#define PIN_LED_AZUL 27

#define CANAL_LEDC_0 0
#define CANAL_LEDC_1 1
#define CANAL_LEDC_2 2

#define LEDC_TIMER_8_BIT 8
#define FRECUENCIA_BASE_LEDC 5000

// Objeto Servo
Servo servoTapa;

bool pulsadorPresionado = false;

// Variables para el estado de los pulsadores y manejo del debounce
bool pulsadoresPresionados[4] = {false, false, false, false};
unsigned long ultimosTiemposPulsadores[4] = {0, 0, 0, 0};
const unsigned long debounceDelay = 120;

// Variables para el manejo del audio
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *fuente;
AudioOutputI2SNoDAC *salida;
bool yaReprodujo = false;

// Variables para el manejo de la lógica del cuestionario
int categoriaSeleccionada = -1;
const int totalCategorias = 4;
const int preguntasPorCategoria = 30;
const int preguntasPorJuego = 5;
int preguntasSeleccionadas[preguntasPorJuego];
int preguntaActual = 0;
int respuestasCorrectas = 0;
int ordenOpciones[4];

// Variable para el tiempo límite de respuesta
const unsigned long tiempoLimiteRespuesta = 30000; // 30 segundos, configurable
unsigned long tiempoInicioPregunta;

// Variables para el control del servomotor
unsigned long ultimoMovimientoTapa = 0;
const int intervaloMovimientoTapa = 500;

void setup() {
  Serial.begin(115200);
  Serial.println(version);

  if (!SD.begin(CS)) {
    Serial.println("Tarjeta SD no encontrada");
    return;
  }

  randomSeed(analogRead(0));

  pinMode(PIN_BOTON_1, INPUT_PULLUP);
  pinMode(PIN_BOTON_2, INPUT_PULLUP);
  pinMode(PIN_BOTON_3, INPUT_PULLUP);
  pinMode(PIN_BOTON_4, INPUT_PULLUP);
  
  ESP32PWM::allocateTimer(0);
  servoTapa.setPeriodHertz(50);
  servoTapa.attach(PIN_SERVO_TAPA, 500, 2400);

  configurarLED();

  mp3 = new AudioGeneratorMP3();
  salida = new AudioOutputI2SNoDAC();
  fuente = new AudioFileSourceSD();
  salida->SetOutputModeMono(true);

  if (OTAhabilitado)
    iniciarOTA();
  
  reproducirIntroduccion();
}

void loop() {
  OTAhabilitado ? ArduinoOTA.handle() : yield();
  
  unsigned long tiempoActual = millis();

  if (mp3->isRunning()) {
    if (!mp3->loop() && yaReprodujo) {
      yaReprodujo = false;
      mp3->stop();
      fuente->close();
      Serial.println("Audio Stop");
      Serial.println("Archivo Cerrado");
      servoTapa.write(90);
      yield();
    } else {
      moverServoTapa();
    }
  } else {
    if (categoriaSeleccionada == -1) {
      verificarSeleccionCategoria();
    } else if (preguntaActual < preguntasPorJuego) {
      if (!yaReprodujo) {
        reproducirPregunta(preguntaActual);
      } else {
        verificarRespuestaPregunta();
      }
    } else {
      finalizarJuego();
    }
  }
}

void verificarSeleccionCategoria() {
  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTON_1 + i);
    unsigned long tiempoActual = millis();
    
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true;
      ultimosTiemposPulsadores[i] = tiempoActual;
      categoriaSeleccionada = i;
      Serial.print("Categoría seleccionada: ");
      Serial.println(categoriaSeleccionada);
      seleccionarPreguntasAleatorias();
      reproducirPregunta(preguntaActual);
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false;
    }
  }
}

void seleccionarPreguntasAleatorias() {
  for (int i = 0; i < preguntasPorJuego; i++) {
    bool preguntaUnica;
    int nuevaPregunta;
    do {
      nuevaPregunta = random(1, preguntasPorCategoria + 1);
      preguntaUnica = true;
      for (int j = 0; j < i; j++) {
        if (preguntasSeleccionadas[j] == nuevaPregunta) {
          preguntaUnica = false;
          break;
        }
      }
    } while (!preguntaUnica);
    preguntasSeleccionadas[i] = nuevaPregunta;
  }
}

void verificarRespuestaPregunta() {
  unsigned long tiempoActual = millis();
  
  if (tiempoActual - tiempoInicioPregunta >= tiempoLimiteRespuesta) {
    // Tiempo agotado, marcar como incorrecta y avanzar
    LedPWM(255, 0, 0); // Rojo para respuesta incorrecta
    reproducirAudio("/incorrecta.mp3");
    avanzarSiguientePregunta();
    return;
  }

  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTON_1 + i);
    
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true;
      ultimosTiemposPulsadores[i] = tiempoActual;
      
      // Verificar si la respuesta es correcta
      if (ordenOpciones[i] == 0) { // La opción 1 (índice 0) siempre es la correcta
        respuestasCorrectas++;
        LedPWM(0, 255, 0); // Verde para respuesta correcta
        reproducirAudio("/correcta.mp3");
      } else {
        LedPWM(255, 0, 0); // Rojo para respuesta incorrecta
        reproducirAudio("/incorrecta.mp3");
      }
      
      avanzarSiguientePregunta();
      return;
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false;
    }
  }
}

void avanzarSiguientePregunta() {
  preguntaActual++;
  if (preguntaActual < preguntasPorJuego) {
    yaReprodujo = false; // Preparar para reproducir la siguiente pregunta y opciones
  }
}

void reproducirPregunta(int numeroPregunta) {
  char rutaPregunta[50];
  snprintf(rutaPregunta, sizeof(rutaPregunta), "/categoria%d/pregunta%d.mp3", categoriaSeleccionada + 1, preguntasSeleccionadas[numeroPregunta]);
  reproducirAudio(rutaPregunta);
  yaReprodujo = false; // Preparar para reproducir las opciones
  tiempoInicioPregunta = millis(); // Iniciar el temporizador para la pregunta
}

void reproducirOpciones() {
  // Generar orden aleatorio de opciones
  for (int i = 0; i < 4; i++) {
    ordenOpciones[i] = i;
  }
  for (int i = 3; i > 0; i--) {
    int j = random(0, i + 1);
    int temp = ordenOpciones[i];
    ordenOpciones[i] = ordenOpciones[j];
    ordenOpciones[j] = temp;
  }

  // Reproducir opciones en orden aleatorio
  for (int i = 0; i < 4; i++) {
    char rutaOpcion[50];
    snprintf(rutaOpcion, sizeof(rutaOpcion), "/categoria%d/pregunta%d_opcion%d.mp3", 
             categoriaSeleccionada + 1, preguntasSeleccionadas[preguntaActual], ordenOpciones[i] + 1);
    reproducirAudio(rutaOpcion);
    while (mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        fuente->close();
        break;
      }
      moverServoTapa();
    }
    delay(500); // Pequeña pausa entre opciones
  }
  yaReprodujo = true;
}

void finalizarJuego() {
  const char* archivoResultado;
  if (respuestasCorrectas == preguntasPorJuego) {
    archivoResultado = "/campeon.mp3";
    LedPWM(255, 255, 255); // Blanco para campeón
  } else if (respuestasCorrectas > preguntasPorJuego * 0.7) {
    archivoResultado = "/ganador.mp3";
    LedPWM(255, 165, 0); // Naranja para ganador
  } else {
    archivoResultado = "/perdedor.mp3";
    LedPWM(0, 0, 255); // Azul para perdedor
  }
  reproducirAudio(archivoResultado);
  
  // Reiniciar variables para un nuevo juego
  categoriaSeleccionada = -1;
  preguntaActual = 0;
  respuestasCorrectas = 0;
}

void reproducirIntroduccion() {
  const char *archivoIntroduccion = "/intro.mp3";
  reproducirAudio(archivoIntroduccion);
}

void reproducirAudio(const char *ruta) {
  if (!SD.exists(ruta)) {
    Serial.println("Archivo no encontrado");
    return;
  }

  if (!fuente->open(ruta)) {
    Serial.println("Error al abrir el archivo");
    return;
  }

  yield();
  mp3->begin(fuente, salida);
  yaReprodujo = true;
  
  moverServoTapa();
}

void configurarLED() {
  ledcSetup(CANAL_LEDC_0, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_1, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_2, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  
  ledcAttachPin(PIN_LED_ROJO, CANAL_LEDC_0);
  ledcAttachPin(PIN_LED_VERDE, CANAL_LEDC_1);
  ledcAttachPin(PIN_LED_AZUL, CANAL_LEDC_2);
}

void LedPWM(int rojo, int verde, int azul) {
  ledcWrite(CANAL_LEDC_0, rojo);
  ledcWrite(CANAL_LEDC_1, verde);
  ledcWrite(CANAL_LEDC_2, azul);
}

void iniciarOTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Conexión fallida! Reiniciando...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("ESP32-TriviaMagos");
  ArduinoOTA.begin();
}

void moverServoTapa() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimoMovimientoTapa >= intervaloMovimientoTapa) {
    int posicion = random(70, 111);
    servoTapa.write(posicion);
    ultimoMovimientoTapa = tiempoActual;
  }
}
