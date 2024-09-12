/* Versión del programa
const char *version = "1.04";

// Incluimos las bibliotecas necesarias
#include "AudioFileSourceSD.h"      // Para manejar archivos de audio desde la tarjeta SD
#include "AudioGeneratorMP3.h"      // Para generar audio en formato MP3
#include "AudioOutputI2SNoDAC.h"    // Para la salida de audio I2S sin DAC
#include "FS.h"                     // Para el sistema de archivos
#include "SD.h"                     // Para manejar la tarjeta SD
#include "SPI.h"                    // Para la comunicación SPI
#include <WiFi.h>                   // Para la conectividad WiFi
#include <ArduinoOTA.h>             // Para actualizaciones Over-The-Air
#include <esp_system.h>             // Para funciones del sistema ESP32

// Variable para habilitar o deshabilitar actualizaciones OTA
bool OTAhabilitado = false;

// Configuración de la red WiFi (dejar en blanco si no se usa)
const char *ssid = "";
const char *password = "";

// Definimos los pines para la conexión de la Tarjeta SD
#define SCK  18
#define MISO 19
#define MOSI 23
#define CS   5

// Definimos los pines para los pulsadores (activos en bajo)
#define PIN_BOTON_1 4
#define PIN_BOTON_2 15
#define PIN_BOTON_3 34
#define PIN_BOTON_4 35

// Definimos el pin y el canal para el motor Ventilador
#define PIN_FUEGO 13
#define CANAL_LEDC_3 3

// Definimos los pines para el LED RGB
#define PIN_LED_ROJO 33
#define PIN_LED_VERDE 14
#define PIN_LED_AZUL 27

// Definimos los canales LEDC para los LEDs
#define CANAL_LEDC_0 0
#define CANAL_LEDC_1 1
#define CANAL_LEDC_2 2

// Configuración del temporizador LEDC
#define LEDC_TIMER_8_BIT  8
#define FRECUENCIA_BASE_LEDC 5000

// Variable para verificar si un pulsador ha sido presionado
bool pulsadorPresionado = false;

// Arreglos para manejar el estado de los pulsadores y el debounce
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
int ordenOpciones[totalCategorias];

// Variable para el tiempo límite de respuesta
const unsigned long tiempoLimiteRespuesta = 30000;  // 30 segundos
unsigned long tiempoInicioPregunta = 0;

// Variables para el control del motor del ventilador que simula el fuego
unsigned long ultimoMovimientoFuego = 0;
const int intervaloMovimientoFuego = 200;
const int velocidadMaximaFuego = 144;
const int velocidadMinimaFuego = 254;

// Definimos los estados del juego
enum class EstadoJuego {
  INICIO,
  SELECCION_CATEGORIA,
  REPRODUCCION_PREGUNTA,
  ESPERA_RESPUESTA,
  VERIFICACION_RESPUESTA,
  FIN_JUEGO
};

// Variable para mantener el estado actual del juego
EstadoJuego estadoActual = EstadoJuego::INICIO;

// Función de configuración inicial
void setup() {
  // Iniciamos la comunicación serial
  Serial.begin(115200);
  Serial.println(version);

  // Intentamos inicializar la tarjeta SD
  if (!SD.begin(CS)) {
    Serial.println("No se encontró la tarjeta SD");
    return;
  }

  // Inicializamos la semilla para la generación de números aleatorios
  randomSeed(esp_random());

  // Configuramos los pines de los botones como entradas con resistencia pull-up
  pinMode(PIN_BOTON_1, INPUT_PULLUP);
  pinMode(PIN_BOTON_2, INPUT_PULLUP);
  pinMode(PIN_BOTON_3, INPUT_PULLUP);
  pinMode(PIN_BOTON_4, INPUT_PULLUP);
  
  // Llamamos a las funciones de configuración
  configurarLED();
  configurarFuego();

  // Inicializamos los componentes de audio
  mp3 = new AudioGeneratorMP3();
  salida = new AudioOutputI2SNoDAC();
  fuente = new AudioFileSourceSD();
  salida->SetOutputModeMono(true);

  // Si OTA está habilitado, iniciamos la actualización OTA
  if (OTAhabilitado)
    iniciarOTA();
  
  // Reproducimos la introducción
  reproducirIntroduccion();
}

// Función principal que se ejecuta continuamente
void loop() {
  // Manejamos la actualización OTA si está habilitada
  if (OTAhabilitado) {
    ArduinoOTA.handle();
  }
  
  // Obtenemos el tiempo actual
  unsigned long tiempoActual = millis();
  
  // Movemos el "fuego"
  moverFuego();

  // Si el reproductor MP3 está funcionando
  if (mp3->isRunning()) {
    // Si el MP3 ha terminado de reproducirse
    if (!mp3->loop()) {
      mp3->stop();
      fuente->close();
      Serial.println("Audio detenido");
      Serial.println("Archivo cerrado");
      manejarFinReproduccion();
    }
  } else {
    // Si no hay audio reproduciéndose, manejamos el estado actual del juego
    switch (estadoActual) {
      case EstadoJuego::INICIO:
        manejarEstadoInicio();
        break;
      case EstadoJuego::SELECCION_CATEGORIA:
        manejarSeleccionCategoria();
        break;
      case EstadoJuego::REPRODUCCION_PREGUNTA:
        manejarReproduccionPregunta();
        break;
      case EstadoJuego::ESPERA_RESPUESTA:
        manejarEsperaRespuesta(tiempoActual);
        break;
      case EstadoJuego::VERIFICACION_RESPUESTA:
        manejarVerificacionRespuesta();
        break;
      case EstadoJuego::FIN_JUEGO:
        manejarFinJuego();
        break;
    }
  }
}

// Función para manejar el fin de la reproducción de audio
void manejarFinReproduccion() {
  switch (estadoActual) {
    case EstadoJuego::INICIO:
      // Pasamos a la selección de categoría después de la introducción
      estadoActual = EstadoJuego::SELECCION_CATEGORIA;
      break;
    case EstadoJuego::REPRODUCCION_PREGUNTA:
      // Pasamos a esperar la respuesta después de reproducir la pregunta
      estadoActual = EstadoJuego::ESPERA_RESPUESTA;
      tiempoInicioPregunta = millis();
      break;
    case EstadoJuego::VERIFICACION_RESPUESTA:
      // Pasamos a la siguiente pregunta o al fin del juego
      if (preguntaActual < preguntasPorJuego) {
        estadoActual = EstadoJuego::REPRODUCCION_PREGUNTA;
      } else {
        estadoActual = EstadoJuego::FIN_JUEGO;
      }
      break;
    case EstadoJuego::FIN_JUEGO:
      // Volvemos al inicio después de finalizar el juego
      estadoActual = EstadoJuego::INICIO;
      break;
    default:
      break;
  }
}

// Función para manejar el estado de inicio
void manejarEstadoInicio() {
  reproducirIntroduccion();
  estadoActual = EstadoJuego::SELECCION_CATEGORIA;
}

// Función para manejar la selección de categoría
void manejarSeleccionCategoria() {
  int categoriaSeleccionada = verificarSeleccionCategoria();
  if (categoriaSeleccionada != -1) {
    this->categoriaSeleccionada = categoriaSeleccionada;
    seleccionarPreguntasAleatorias();
    estadoActual = EstadoJuego::REPRODUCCION_PREGUNTA;
  }
}

// Función para manejar la reproducción de la pregunta
void manejarReproduccionPregunta() {
  reproducirPregunta(preguntaActual);
  reproducirOpciones();
  estadoActual = EstadoJuego::ESPERA_RESPUESTA;
}

// Función para manejar la espera de la respuesta
void manejarEsperaRespuesta(unsigned long tiempoActual) {
  if (tiempoActual - tiempoInicioPregunta >= tiempoLimiteRespuesta) {
    // Si se agotó el tiempo
    LedPWM(255, 0, 0); // Rojo para tiempo agotado
    reproducirAudio("/incorrecta.mp3");
    estadoActual = EstadoJuego::VERIFICACION_RESPUESTA;
  } else {
    int respuesta = verificarRespuestaPregunta();
    if (respuesta != -1) {
      // Si se dio una respuesta
      if (ordenOpciones[respuesta] == 0) {
        // Si la respuesta es correcta
        respuestasCorrectas++;
        LedPWM(0, 255, 0); // Verde para respuesta correcta
        reproducirAudio("/correcta.mp3");
      } else {
        // Si la respuesta es incorrecta
        LedPWM(255, 0, 0); // Rojo para respuesta incorrecta
        reproducirAudio("/incorrecta.mp3");
      }
      estadoActual = EstadoJuego::VERIFICACION_RESPUESTA;
    }
  }
}

// Función para manejar la verificación de la respuesta
void manejarVerificacionRespuesta() {
  preguntaActual++;
  if (preguntaActual < preguntasPorJuego) {
    estadoActual = EstadoJuego::REPRODUCCION_PREGUNTA;
  } else {
    estadoActual = EstadoJuego::FIN_JUEGO;
  }
}

// Función para manejar el fin del juego
void manejarFinJuego() {
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
  
  // Reiniciamos las variables para un nuevo juego
  categoriaSeleccionada = -1;
  preguntaActual = 0;
  respuestasCorrectas = 0;
  estadoActual = EstadoJuego::INICIO;
}

// Función para verificar la selección de categoría
int verificarSeleccionCategoria() {
  int PIN_BOTONES[4] = {PIN_BOTON_1, PIN_BOTON_2, PIN_BOTON_3, PIN_BOTON_4};
  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTONES[i]);
    unsigned long tiempoActual = millis();
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true;
      ultimosTiemposPulsadores[i] = tiempoActual;
      return i;
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false;
    }
  }
  return -1;
}

// Función optimizada para seleccionar preguntas aleatorias
void seleccionarPreguntasAleatorias() {
  // Creamos un array con todas las preguntas posibles
  int todasLasPreguntas[preguntasPorCategoria];
  for (int i = 0; i < preguntasPorCategoria; i++) {
    todasLasPreguntas[i] = i + 1;
  }
  
  // Mezclamos el array usando el algoritmo de Fisher-Yates
  for (int i = preguntasPorCategoria - 1; i > 0; i--) {
    int j = aleatorioTRNG(0, i + 1);
    int temp = todasLasPreguntas[i];
    todasLasPreguntas[i] = todasLasPreguntas[j];
    todasLasPreguntas[j] = temp;
  }
  
  // Seleccionamos las primeras 'preguntasPorJuego' preguntas
  for (int i = 0; i < preguntasPorJuego; i++) {
    preguntasSeleccionadas[i] = todasLasPreguntas[i];
  }
}

//Función para verificar la respuesta a una pregunta
int verificarRespuestaPregunta() {
  int PIN_BOTONES[4] = {PIN_BOTON_1, PIN_BOTON_2, PIN_BOTON_3, PIN_BOTON_4};
  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTONES[i]);
    unsigned long tiempoActual = millis();
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true;
      ultimosTiemposPulsadores[i] = tiempoActual;
      return i;
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false;
    }
  }
  return -1;
}

// Función para reproducir una pregunta
void reproducirPregunta(int numeroPregunta) {
  char rutaPregunta[50];
  snprintf(rutaPregunta, sizeof(rutaPregunta), "/categoria%d/pregunta%d.mp3", categoriaSeleccionada + 1, preguntasSeleccionadas[numeroPregunta]);
  reproducirAudio(rutaPregunta);
  yaReprodujo = false;
  tiempoInicioPregunta = millis();
}

// Función para reproducir las opciones de respuesta
void reproducirOpciones() {
  // Generamos un orden aleatorio de opciones
  for (int i = 0; i < 4; i++) {
    ordenOpciones[i] = i;
  }
  // Barajamos las opciones
  for (int i = 3; i > 0; i--) {
    int j = aleatorioTRNG(0, i + 1);
    int temp = ordenOpciones[i];
    ordenOpciones[i] = ordenOpciones[j];
    ordenOpciones[j] = temp;
  }

  // Reproducimos las opciones en orden aleatorio
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
      moverFuego();
    }
    delay(500);
    moverFuego();
  }
  yaReprodujo = true;
}

// Función para reproducir la introducción
void reproducirIntroduccion() {
  const char *archivoIntroduccion = "/intro.mp3";
  reproducirAudio(archivoIntroduccion);
}

// Función para reproducir un archivo de audio
void reproducirAudio(const char *ruta) {
  if (!SD.exists(ruta)) {
    Serial.print("Archivo no encontrado: ");
    Serial.println(ruta);
    return;
  }

  if (!fuente->open(ruta)) {
    Serial.print("Error al abrir el archivo: ");
    Serial.println(ruta);
    return;
  }

  yield();
  mp3->begin(fuente, salida);
  yaReprodujo = true;
}

// Función para configurar los LEDs
void configurarLED() {
  ledcSetup(CANAL_LEDC_0, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_1, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_2, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  
  ledcAttachPin(PIN_LED_ROJO, CANAL_LEDC_0);
  ledcAttachPin(PIN_LED_VERDE, CANAL_LEDC_1);
  ledcAttachPin(PIN_LED_AZUL, CANAL_LEDC_2);
}

// Función para controlar los LEDs mediante PWM
void LedPWM(int rojo, int verde, int azul) {
  ledcWrite(CANAL_LEDC_0, rojo);
  ledcWrite(CANAL_LEDC_1, verde);
  ledcWrite(CANAL_LEDC_2, azul);
}

// Función para iniciar la actualización OTA
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

// Función para configurar el "fuego"
void configurarFuego() {
  ledcSetup(CANAL_LEDC_3, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcAttachPin(PIN_FUEGO, CANAL_LEDC_3);
}

// Función para simular el movimiento del fuego
void moverFuego() {
  unsigned long tiempoActual = millis();
  int mapeoIni = 144;
  int mapeofin = 244;
  if (tiempoActual - ultimoMovimientoFuego >= intervaloMovimientoFuego) {
    int velocidad = aleatorioTRNG(mapeoIni, mapeofin);
    int velocidadMap = map(velocidad, mapeoIni, mapeofin, velocidadMaximaFuego, velocidadMinimaFuego);
    ledcWrite(CANAL_LEDC_3, velocidadMap);
    ultimoMovimientoFuego = tiempoActual;
  }
}

// Función para generar un número aleatorio de alta entropía
long aleatorioTRNG(long minimo, long maximo) {
  if (minimo >= maximo) {
    return minimo;
  }
  unsigned long rango = maximo - minimo;
  uint32_t numeroAleatorio = esp_random();
  return (numeroAleatorio % rango) + minimo;
}*/