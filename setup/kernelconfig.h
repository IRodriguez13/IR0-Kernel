/*
Este panel te permite tener estrategias de compilación según el caso de uso que le des al Kernel IR0.
Si es para servidor o espacio de virtualización SSL, si es para integración con IoT, o si buscas utilizarlo en desk.

Cada uno referencia a un makefile que permite capar y compilar los subsistemas recomendados para tu objetivo, 
pero también podes compilar lo que necesites a mano.
*/

#ifdef IR0_DESKTOP
  init_gui();
  init_audio();
#endif

#ifdef IR0_SERVER_SSL
  init_networking();
  init_docker_runtime();
#endif

#ifdef IR0_IOT
  init_lapic_timer();
  init_low_power_mode();
#endif
