  if (ghost_consume_power_press()) {
    if (!ghost_run_mode(ghost_next_mode)) enter_abort_halt("ghosting mode failed");
    ghost_next_mode = (ghost_next_mode + 1U) % GHOST_MODE_COUNT;
    Serial.printf("[X4LAB] ready; next mode %u. Press POWER or send n.\n",
                  static_cast<unsigned>(ghost_next_mode + 1U));
  }
  delay(5);
}
