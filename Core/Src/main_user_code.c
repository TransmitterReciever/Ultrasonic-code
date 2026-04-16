/**
 * @file    main_user_code.c
 * @brief   Code snippets to paste into CubeMX-generated main.c USER CODE regions.
 *
 * THIS IS NOT A STANDALONE FILE. Copy each section into the corresponding
 * USER CODE region in your CubeMX-generated main.c.
 *
 * After CubeMX generates main.c, open it and paste these blocks into
 * the matching USER CODE BEGIN / USER CODE END markers.
 */

/* =========================================================================
 * Paste into: USER CODE BEGIN Includes
 * ========================================================================= */
#if 0  /* --- COPY START --- */

#include "ultrasonic.h"

#endif /* --- COPY END --- */


/* =========================================================================
 * Paste into: USER CODE BEGIN 2  (after all MX_xxx_Init() calls)
 * ========================================================================= */
#if 0  /* --- COPY START --- */

  /* Initialise ultrasonic subsystem (reconfigures timers for our application) */
  Ultrasonic_Init();

  /* Start the transmit/receive cycle */
  Ultrasonic_StartTX();

#endif /* --- COPY END --- */


/* =========================================================================
 * Paste into: USER CODE BEGIN 3  (inside while(1) main loop)
 * ========================================================================= */
#if 0  /* --- COPY START --- */

    /* Process echo captures and validate */
    Ultrasonic_ProcessEcho();

    /* Update buzzer state (turn off after timeout) */
    Ultrasonic_BuzzerUpdate();

#endif /* --- COPY END --- */
