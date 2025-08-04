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
