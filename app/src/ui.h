#ifndef DMX_UI_H
#define DMX_UI_H

#include "app_state.h"

void dmx_ui_draw(DmxApp *app, int screen_w, int screen_h);
/* Returns 1 if a clickable control was handled. */
int dmx_ui_handle_click(DmxApp *app, int x, int y, int screen_w, int screen_h);
/* Apply completed async file/folder dialog results (keeps main loop unblocked). */
void dmx_ui_poll(DmxApp *app);
/* Join any in-flight dialog thread before exit. */
void dmx_ui_shutdown(void);

#endif
