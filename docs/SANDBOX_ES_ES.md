**Ekron — Sandbox urbano**

**Dirección acordada**
Juego para Android en primera persona con exploración libre de una ciudad ficticia en tiempos de guerra y mecánicas de construcción. No hay misiones, campaña argumental, marcadores de objetivos ni una secuencia obligatoria de acciones.
Referencia visual: `concepts/ekron-war-city-ru.png`. Es arte conceptual, no una captura de pantalla del motor. Las misiones y el marcador de distancia que aparecen en ella NO forman parte del juego.

**Mundo**

* Ciudad parcialmente destruida: manzanas intactas, edificios dañados, escombros y coches abandonados.
* Los edificios accesibles tienen entradas e interiores reales, no solo fachadas.
* Se pueden colocar muebles y elementos de construcción dentro de las habitaciones.
* El objetivo es un mundo abierto; el primer prototipo técnico se limita a una sola manzana pequeña.

**Sandbox**

* Movimiento libre sin misiones, temporizadores ni objetivos obligatorios.
* Catálogo de construcción: pared, suelo, ventana, puerta, mesa, silla, armario, sofá.
* Selección de objetos, vista previa translúcida de la colocación, giro, confirmación de la colocación y cancelación.
* Selección y eliminación de los objetos colocados por el jugador.
* Guardado y carga de los objetos colocados.
* En el primer prototipo, los objetos de construcción están disponibles sin recolectar recursos: es una decisión técnica inicial, no la economía definitiva del juego.

**Controles e idioma**

* Interfaz, nombres de objetos y descripciones emergentes en cuatro idiomas: inglés, ruso, portugués europeo y español (España). El idioma sigue la configuración del sistema (en el ordenador también `--lang`, `EKRON_LANG`, la tecla `L` y el menú de pausa).
* Móvil en orientación horizontal.
* Lado izquierdo: joystick de movimiento; lado derecho: zona para mirar y botones para interactuar, saltar y construir.
* En el modo de construcción: selección de objetos, giro, colocación y cancelación.
* No se muestra un diario de misiones ni marcadores de misión.

**Primer hito de implementación verificable**

1. Versión de Android ejecutable con controles de movimiento y cámara.
2. Manzana pequeña con colisiones y un interior accesible.
3. Colocación, giro y eliminación de un solo tipo de objeto de construcción.
4. Guardado del estado y restauración tras reiniciar.
5. Ampliación del catálogo y poblado de la manzana.

**Alcance de los cambios actuales**
Se ha eliminado el prototipo para navegador: el usuario confirmó que los cambios deben hacerse directamente en el juego de Android. Se ha modificado el proyecto nativo existente `sys/android`: GameActivity → JNI → C++/OpenGL ES 3. Se han añadido una manzana pequeña con interiores accesibles, movimiento con colisiones, cámara controlada por gestos táctiles, ocho tipos de objetos colocables, giro, eliminación y guardado en un archivo privado de la aplicación. La versión de Android no usa WebView, JavaScript ni npm. Estos cambios afectan a la rama de código de Android; el motor de escritorio en Pascal no se ve afectado. Instrucciones: `sys/android/README.md`.
Por ahora los gráficos son primitivas y no alcanzan el nivel de detalle del arte conceptual. La imagen generada no sustituye a los modelos 3D, las texturas ni la optimización para móviles. La versión de Android aún no se ha probado. El multijugador, la destrucción completa de los edificios y los PNJ de combate no están acordados en esta fase.
