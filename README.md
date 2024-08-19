# Documentación: Torneo de los 3 Magos

## Índice
1. [Descripción General](#descripción-general)
2. [Requisitos de Hardware](#requisitos-de-hardware)
3. [Diagrama de Conexiones](#diagrama-de-conexiones)
4. [Configuración del Proyecto](#configuración-del-proyecto)
5. [Funciones Principales](#funciones-principales)
6. [Flujo del Programa](#flujo-del-programa)
7. [Configuración OTA](#configuración-ota)

## Descripción General

Este proyecto implementa un juego de trivia interactivo llamado "Torneo de los 3 Magos" utilizando un ESP32. El juego reproduce preguntas de audio, permite a los jugadores seleccionar respuestas mediante botones, y proporciona retroalimentación visual y auditiva.

## Requisitos de Hardware

- ESP32
- Módulo de tarjeta SD
- 4 botones pulsadores
- LED RGB
- Servomotor
- Altavoz (salida de audio I2S)

## Diagrama de Conexiones

# Trivia de los 3 Magos

## Diagrama de Conexiones

graph TD
    ESP32[ESP32] --> |PIN 18| SD_SCK[SD Card SCK]
    ESP32 --> |PIN 19| SD_MISO[SD Card MISO]
    ESP32 --> |PIN 23| SD_MOSI[SD Card MOSI]
    ESP32 --> |PIN 5| SD_CS[SD Card CS]
    
    ESP32 --> |PIN 4| BOTON1[Botón 1]
    ESP32 --> |PIN 15| BOTON2[Botón 2]
    ESP32 --> |PIN 34| BOTON3[Botón 3]
    ESP32 --> |PIN 35| BOTON4[Botón 4]
    
    ESP32 --> |PIN 13| SERVO[Servo Tapa]
    
    ESP32 --> |PIN 25| LED_R[LED RGB - Rojo]
    ESP32 --> |PIN 26| LED_G[LED RGB - Verde]
    ESP32 --> |PIN 27| LED_B[LED RGB - Azul]
    
    ESP32 --> |I2S| DAC[DAC para Audio]

## Configuración del Proyecto

1. Asegúrese de tener instaladas las siguientes bibliotecas:
   - AudioFileSourceSD
   - AudioGeneratorMP3
   - AudioOutputI2SNoDAC
   - ESP32Servo
   - ArduinoOTA (si se habilita la funcionalidad OTA)

2. Configure los pines según el diagrama de conexiones.

3. Prepare una tarjeta SD con la siguiente estructura de archivos:
   ```
   /intro.mp3
   /correcta.mp3
   /incorrecta.mp3
   /campeon.mp3
   /ganador.mp3
   /perdedor.mp3
   /categoria1/pregunta1.mp3
   /categoria1/pregunta1_opcion1.mp3
   /categoria1/pregunta1_opcion2.mp3
   /categoria1/pregunta1_opcion3.mp3
   /categoria1/pregunta1_opcion4.mp3
   ... (repetir para todas las categorías y preguntas)
   ```

4. Si desea habilitar OTA, configure las credenciales WiFi en las constantes `ssid` y `password`.

## Funciones Principales

### `setup()`
Inicializa los componentes del sistema, incluyendo la comunicación serie, la tarjeta SD, los pines de entrada/salida, el servomotor, el LED RGB y el sistema de audio. También inicia la configuración OTA si está habilitada.

### `loop()`
Función principal que maneja el flujo del juego. Controla la reproducción de audio, la selección de categorías, la presentación de preguntas y la verificación de respuestas.

### `verificarSeleccionCategoria()`
Detecta la selección de categoría por parte del jugador mediante los botones pulsadores.

### `seleccionarPreguntasAleatorias()`
Selecciona aleatoriamente un conjunto de preguntas para el juego actual.

### `verificarRespuestaPregunta()`
Comprueba si el jugador ha respondido a la pregunta actual y si la respuesta es correcta.

### `avanzarSiguientePregunta()`
Prepara el sistema para la siguiente pregunta del juego.

### `reproducirPregunta(int numeroPregunta)`
Reproduce el archivo de audio correspondiente a la pregunta actual.

### `reproducirOpciones()`
Reproduce las opciones de respuesta en un orden aleatorio.

### `finalizarJuego()`
Concluye el juego, reproduce el audio correspondiente al resultado y reinicia las variables para un nuevo juego.

### `reproducirIntroduccion()`
Reproduce el audio de introducción al inicio del juego.

### `reproducirAudio(const char *ruta)`
Función genérica para reproducir archivos de audio desde la tarjeta SD.

### `configurarLED()`
Configura los canales PWM para controlar el LED RGB.

### `LedPWM(int rojo, int verde, int azul)`
Controla el color del LED RGB mediante PWM.

### `iniciarOTA()`
Configura y inicia la funcionalidad de actualización Over-The-Air (OTA).

### `moverServoTapa()`
Controla el movimiento aleatorio del servomotor durante la reproducción de audio.

## Flujo del Programa

1. Inicialización del sistema
2. Reproducción de la introducción
3. Espera de selección de categoría
4. Selección aleatoria de preguntas
5. Para cada pregunta:
   - Reproducción de la pregunta
   - Reproducción de las opciones
   - Espera de respuesta del jugador
   - Verificación de la respuesta
   - Retroalimentación visual y auditiva
6. Finalización del juego y presentación del resultado
7. Reinicio para un nuevo juego

## Configuración OTA

Para habilitar las actualizaciones Over-The-Air:

1. Asegúrese de que `OTAhabilitado` esté configurado como `true`.
2. Configure las credenciales WiFi correctas en `ssid` y `password`.
3. El dispositivo se conectará a la red WiFi durante el inicio y estará disponible para actualizaciones OTA.

¿Le gustaría que profundice en algún aspecto específico de la documentación o que añada alguna información adicional?