/* ct3_menu.h: the port's own menu (+ and - together) and its saved options.
 * See ct3_menu.c. */
#ifndef CT3_MENU_H
#define CT3_MENU_H

/* Load ct3_options.txt, apply it and install the pad filter and the
 * overlay. Call once at boot, before the kernel reads RECOMP_WIDESCREEN. */
void ct3_menu_init(void);

/* Whether the saved options ask for CPU boost (read before the clocks are set). */
int ct3_options_cpu_boost(void);

#endif
