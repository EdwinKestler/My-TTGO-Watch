#include "config.h"

#include "background_app.h"
#include "background_app_main.h"

#include "gui/mainbar/mainbar.h"
#include "gui/app.h"

LV_IMG_DECLARE( background_app_64px );

static uint32_t background_tile_num;
static icon_t *background_app = NULL;
static int registed = app_autocall_function( &background_app_setup, 18 );

static void enter_background_app_event_cb( lv_obj_t * obj, lv_event_t event );

void background_app_setup( void ) {
    if( !registed ) {
        return;
    }

    background_tile_num = mainbar_add_app_tile( 1, 1, "background" );
    background_app = app_register( "bg\nimage", &background_app_64px, enter_background_app_event_cb );
    background_app_main_setup( background_tile_num );
}

static void enter_background_app_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case LV_EVENT_CLICKED:
            mainbar_jump_to_tilenumber( background_tile_num, LV_ANIM_OFF, true );
            if ( background_app != NULL ) {
                app_hide_indicator( background_app );
            }
            break;
    }
}
