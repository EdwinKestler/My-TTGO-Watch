#include "config.h"

#include "background_app_main.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"
#include "gui/gui.h"
#include "gui/mainbar/setup_tile/display_settings/display_settings.h"

#include "hardware/display.h"
#include "hardware/wifictl.h"
#include "utils/filepath_convert.h"
#include "utils/ftpserver/ftpserver.h"

#include <stdio.h>
#include <string.h>

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
    #include <WiFi.h>
    #include <SPIFFS.h>
    #include <ESPAsyncWebServer.h>

    extern "C" {
        unsigned lodepng_decode32_file( unsigned char **out, unsigned *w, unsigned *h, const char *filename );
    }
#endif

LV_FONT_DECLARE( Ubuntu_16px );

#define BACKGROUND_UPLOAD_TEMP      "/spiffs/bg.new"
#define BACKGROUND_UPLOAD_MAX       ( 256U * 1024U )
#define BACKGROUND_SCREEN_SECONDS   120

static lv_obj_t *background_label = NULL;
static lv_style_t background_style;
static char background_status[48] = "";
static bool background_session = false;
static bool background_holding = false;
static bool background_started_wifi = false;
static bool background_have_login = false;
static uint32_t background_saved_timeout = DISPLAY_MIN_TIMEOUT;
static bool background_saved_block = false;
static bool background_saved_standby = false;
static char background_user[32] = "";
static char background_pass[32] = "";
static uint16_t background_port = 80;

static void background_refresh_labels( void );
static void background_activate_cb( void );
static void background_hibernate_cb( void );
static void background_exit_cb( lv_obj_t *obj, lv_event_t event );
static bool background_wifi_cb( EventBits_t event, void *arg );
static bool background_style_cb( EventBits_t event, void *arg );

#ifndef NATIVE_64BIT
    enum {
        BACKGROUND_PHASE_IDLE = 0,
        BACKGROUND_PHASE_RECEIVING,
        BACKGROUND_PHASE_COMMIT
    };

    static SemaphoreHandle_t background_file_lock = NULL;
    static AsyncWebServer *background_http = NULL;
    static FILE *background_upload = NULL;
    static AsyncWebServerRequest *background_owner = NULL;
    static size_t background_upload_size = 0;
    static int background_phase = BACKGROUND_PHASE_IDLE;
    static int background_result_code = 400;
    static bool background_upload_ok = false;
    static bool background_upload_busy = false;
    static bool background_owner_finished = false;
    static bool background_result_ready = false;
    static volatile bool background_stop_uploads = false;
    static char background_result[160] = "";
    static char background_watch_note[48] = "";

    static void background_server_start( void );
    static void background_server_stop( void );
    static void background_copy_login( void );
    static uint16_t background_choose_port( void );
    static bool background_wifi_has_ip( void );
    static void background_http_result( int code, const char *http_text, const char *watch_text );
    static void background_show_status( const char *text );
    static bool background_file_paths( char *tmp, size_t tmp_len, char *dest, size_t dest_len );
    static void background_abort_upload_locked( void );
#endif

static void background_copy_text( char *dst, size_t len, const char *src ) {
    if ( dst == NULL || len == 0 ) {
        return;
    }
    if ( src == NULL ) {
        src = "";
    }
    strncpy( dst, src, len - 1 );
    dst[ len - 1 ] = '\0';
}

static void background_refresh_labels( void ) {
    char text[384];

    if ( background_label == NULL ) {
        return;
    }

#ifdef NATIVE_64BIT
    snprintf( text, sizeof( text ), "Background upload\nruns on the watch" );
#else
    if ( !background_have_login ) {
        snprintf( text, sizeof( text ), "Set a Wi-Fi\nFTP password" );
    }
    else {
        char address[48];
        char password_line[40];
        const char *status = background_status[ 0 ] != '\0' ? background_status : "open in a browser";
        IPAddress ip = WiFi.localIP();

        background_copy_text( address, sizeof( address ), "Connecting..." );
        password_line[ 0 ] = '\0';
        if ( background_wifi_has_ip() ) {
            String ip_text = ip.toString();
            if ( background_port == 80 ) {
                snprintf( address, sizeof( address ), "%s", ip_text.c_str() );
            }
            else {
                snprintf( address, sizeof( address ), "%s:%u", ip_text.c_str(), (unsigned)background_port );
            }
        }
        if ( strcmp( background_pass, FTPSERVER_PASSWORD ) == 0 ) {
            snprintf( password_line, sizeof( password_line ), "\npw: %s", FTPSERVER_PASSWORD );
        }
        snprintf( text, sizeof( text ), "%s\n\nPNG %ux%u\nlogin %s%s\n%s",
                  address,
                  (unsigned)RES_X_MAX,
                  (unsigned)RES_Y_MAX,
                  background_user,
                  password_line,
                  status );
    }
#endif
    lv_label_set_text( background_label, text );
}

static bool background_style_cb( EventBits_t event, void *arg ) {
    (void)arg;
    switch( event ) {
        case STYLE_CHANGE:
            lv_style_copy( &background_style, APP_STYLE );
            lv_style_set_text_font( &background_style, LV_STATE_DEFAULT, &Ubuntu_16px );
            if ( background_label != NULL ) {
                lv_obj_add_style( background_label, LV_OBJ_PART_MAIN, &background_style );
            }
            break;
    }
    return( true );
}

static void background_exit_cb( lv_obj_t *obj, lv_event_t event ) {
    (void)obj;
    switch( event ) {
        case LV_EVENT_CLICKED:
            mainbar_jump_back();
            break;
    }
}

#ifndef NATIVE_64BIT

static bool background_file_paths( char *tmp, size_t tmp_len, char *dest, size_t dest_len ) {
    if ( tmp == NULL || dest == NULL || tmp_len == 0 || dest_len == 0 ) {
        return( false );
    }
    tmp[ 0 ] = '\0';
    dest[ 0 ] = '\0';
    filepath_convert( tmp, (int)tmp_len, BACKGROUND_UPLOAD_TEMP );
    filepath_convert( dest, (int)dest_len, BACKGROUNDIMAGE );
    return( tmp[ 0 ] != '\0' && dest[ 0 ] != '\0' );
}

static void background_abort_upload_locked( void ) {
    char tmp[256];

    if ( background_upload != NULL ) {
        fclose( background_upload );
        background_upload = NULL;
    }
    if ( filepath_convert( tmp, (int)sizeof( tmp ), BACKGROUND_UPLOAD_TEMP ) != NULL ) {
        remove( tmp );
    }
    background_phase = BACKGROUND_PHASE_IDLE;
    background_upload_busy = false;
    background_owner = NULL;
    background_owner_finished = false;
    background_upload_ok = false;
}

static void background_http_result( int code, const char *http_text, const char *watch_text ) {
    background_result_code = code;
    background_copy_text( background_result, sizeof( background_result ), http_text );
    background_copy_text( background_watch_note, sizeof( background_watch_note ), watch_text );
    background_result_ready = true;
}

static void background_note_cb( void *arg ) {
    background_copy_text( background_status, sizeof( background_status ), (const char *)arg );
    background_refresh_labels();
}

static void background_show_status( const char *text ) {
    char note[48];

    background_copy_text( note, sizeof( note ), text );
    gui_dispatch_sync( background_note_cb, note );
}

static uint32_t background_be32( const uint8_t *bytes ) {
    return( ( (uint32_t)bytes[ 0 ] << 24 ) | ( (uint32_t)bytes[ 1 ] << 16 ) | ( (uint32_t)bytes[ 2 ] << 8 ) | bytes[ 3 ] );
}

static bool background_depth_ok( uint8_t depth, uint8_t color_type ) {
    switch( color_type ) {
        case 0: return( depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16 );
        case 2: return( depth == 8 || depth == 16 );
        case 3: return( depth == 1 || depth == 2 || depth == 4 || depth == 8 );
        case 4:
        case 6: return( depth == 8 || depth == 16 );
        default: return( false );
    }
}

/*
 * 0 accepted, 1 not a PNG, 2 wrong size, 3 will not decode, 4 unreadable
 */
static int background_inspect_png( const char *path ) {
    static const uint8_t signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    FILE *file = NULL;
    uint8_t header[29];
    size_t got = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    unsigned char *decoded = NULL;
    unsigned decoded_w = 0;
    unsigned decoded_h = 0;
    unsigned error = 0;

    file = fopen( path, "rb" );
    if ( file == NULL ) {
        return( 4 );
    }
    got = fread( header, 1, sizeof( header ), file );
    fclose( file );
    if ( got != sizeof( header ) || memcmp( header, signature, sizeof( signature ) ) != 0 || memcmp( header + 12, "IHDR", 4 ) != 0 ) {
        return( 1 );
    }
    if ( header[ 26 ] != 0 || header[ 27 ] != 0 || header[ 28 ] > 1 || !background_depth_ok( header[ 24 ], header[ 25 ] ) ) {
        return( 1 );
    }
    width = background_be32( header + 16 );
    height = background_be32( header + 20 );
    if ( width != RES_X_MAX || height != RES_Y_MAX ) {
        return( 2 );
    }

    error = lodepng_decode32_file( &decoded, &decoded_w, &decoded_h, path );
    if ( decoded != NULL ) {
        free( decoded );
    }
    if ( error != 0 || decoded_w != (unsigned)RES_X_MAX || decoded_h != (unsigned)RES_Y_MAX ) {
        return( 3 );
    }
    return( 0 );
}

static bool background_commit_file( const char *tmp, const char *dest ) {
    FILE *in = NULL;
    FILE *out = NULL;
    uint8_t buffer[512];
    size_t chunk = 0;
    bool ok = true;

    remove( dest );
    if ( rename( tmp, dest ) == 0 ) {
        return( true );
    }

    in = fopen( tmp, "rb" );
    out = fopen( dest, "wb" );
    if ( in == NULL || out == NULL ) {
        if ( in != NULL ) {
            fclose( in );
        }
        if ( out != NULL ) {
            fclose( out );
        }
        remove( tmp );
        return( false );
    }
    while ( ( chunk = fread( buffer, 1, sizeof( buffer ), in ) ) > 0 ) {
        if ( fwrite( buffer, 1, chunk, out ) != chunk ) {
            ok = false;
            break;
        }
    }
    if ( ferror( in ) ) {
        ok = false;
    }
    fclose( in );
    fclose( out );
    remove( tmp );
    if ( !ok ) {
        remove( dest );
    }
    return( ok );
}

static void background_apply_cb( void *arg ) {
    uint32_t timeout_now = 0;
    bool block_now = false;

    (void)arg;

    if ( background_holding ) {
        timeout_now = display_get_timeout();
        block_now = display_get_block_return_maintile();
        display_set_timeout( background_saved_timeout );
        display_set_block_return_maintile( background_saved_block );
    }
    display_set_background_image( 5 );
    display_save_config();
    if ( background_holding ) {
        display_set_timeout( timeout_now );
        display_set_block_return_maintile( block_now );
    }
    display_settings_select_background( 5 );
    gui_set_background_image( 5 );
    background_copy_text( background_status, sizeof( background_status ), "saved" );
    background_refresh_labels();
}

static void background_finish_upload( const char *tmp, const char *dest, bool commit ) {
    int inspected = 0;

    if ( !commit ) {
        remove( tmp );
        background_show_status( background_watch_note[ 0 ] != '\0' ? background_watch_note : "upload failed" );
        return;
    }

    inspected = background_inspect_png( tmp );
    if ( inspected != 0 ) {
        char http_text[96];
        char watch_text[48];

        remove( tmp );
        if ( inspected == 2 ) {
            snprintf( http_text, sizeof( http_text ), "Need a PNG of %u by %u pixels.", (unsigned)RES_X_MAX, (unsigned)RES_Y_MAX );
            snprintf( watch_text, sizeof( watch_text ), "need %ux%u", (unsigned)RES_X_MAX, (unsigned)RES_Y_MAX );
            background_http_result( 400, http_text, watch_text );
        }
        else if ( inspected == 3 ) {
            background_http_result( 400, "The PNG could not be decoded.", "decode failed" );
        }
        else if ( inspected == 4 ) {
            background_http_result( 400, "Could not read the upload.", "read failed" );
        }
        else {
            background_http_result( 400, "That file is not a PNG.", "not a PNG" );
        }
        if ( background_file_lock != NULL && xSemaphoreTake( background_file_lock, portMAX_DELAY ) == pdTRUE ) {
            background_phase = BACKGROUND_PHASE_IDLE;
            xSemaphoreGive( background_file_lock );
        }
        background_show_status( background_watch_note );
        return;
    }

    if ( !background_commit_file( tmp, dest ) ) {
        background_http_result( 400, "Could not store the image.", "store failed" );
        if ( background_file_lock != NULL && xSemaphoreTake( background_file_lock, portMAX_DELAY ) == pdTRUE ) {
            background_phase = BACKGROUND_PHASE_IDLE;
            xSemaphoreGive( background_file_lock );
        }
        background_show_status( "store failed" );
        return;
    }

    if ( background_file_lock != NULL && xSemaphoreTake( background_file_lock, portMAX_DELAY ) == pdTRUE ) {
        background_phase = BACKGROUND_PHASE_IDLE;
        xSemaphoreGive( background_file_lock );
    }
    if ( !gui_dispatch_sync( background_apply_cb, NULL ) ) {
        background_http_result( 200, "Saved the file. On the watch, choose bg.png in display settings.", "pick bg.png" );
        background_show_status( "pick bg.png" );
        return;
    }
    background_http_result( 200, "Saved. The watch background is updated.", "saved" );
}

static void background_handle_get( AsyncWebServerRequest *request ) {
    static const char *page_format =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>Watch background</title>"
        "<style>body{font-family:sans-serif;background:#111;color:#eee;margin:16px;line-height:1.45}"
        "button,input{font-size:18px;margin-top:12px}</style></head><body>"
        "<h1>Watch background</h1>"
        "<p>Upload one PNG that is exactly %u by %u pixels. "
        "Gray, palette, RGB, and transparent PNGs are fine. The watch does not scale the picture. "
        "Maximum %u KB.</p>"
        "<form method=\"POST\" action=\"/upload\" enctype=\"multipart/form-data\">"
        "<input type=\"file\" name=\"image\" accept=\"image/png,.png\" required>"
        "<br><button type=\"submit\">Replace background</button>"
        "</form></body></html>";
    char *page = NULL;
    int wrote = 0;

    if ( !request->authenticate( background_user, background_pass ) ) {
        request->requestAuthentication();
        return;
    }
    page = (char *)malloc( 1400 );
    if ( page == NULL ) {
        request->send( 500, "text/plain", "Out of memory." );
        return;
    }
    wrote = snprintf( page, 1400, page_format, (unsigned)RES_X_MAX, (unsigned)RES_Y_MAX, (unsigned)( BACKGROUND_UPLOAD_MAX / 1024 ) );
    if ( wrote < 0 || wrote >= 1400 ) {
        free( page );
        request->send( 500, "text/plain", "Could not build the page." );
        return;
    }
    request->send( 200, "text/html", page );
    free( page );
}

static void background_handle_missing( AsyncWebServerRequest *request ) {
    if ( !request->authenticate( background_user, background_pass ) ) {
        request->requestAuthentication();
        return;
    }
    request->send( 404, "text/plain", "Not found." );
}

static void background_handle_posted( AsyncWebServerRequest *request ) {
    char body[160];
    int code = 400;

    if ( !request->authenticate( background_user, background_pass ) ) {
        request->requestAuthentication();
        return;
    }
    if ( request != background_owner ) {
        request->send( 409, "text/plain", "Another upload is in progress." );
        return;
    }
    if ( background_result_ready ) {
        code = background_result_code;
        background_copy_text( body, sizeof( body ), background_result );
    }
    else {
        background_copy_text( body, sizeof( body ), "Choose a PNG file." );
    }
    background_owner = NULL;
    background_owner_finished = false;
    background_upload_busy = false;
    background_result_ready = false;
    request->send( code, "text/plain", body );
}

static void background_handle_upload( AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final ) {
    char tmp[256];
    char dest[256];
    bool commit = false;

    (void)filename;
    if ( background_file_lock == NULL || xSemaphoreTake( background_file_lock, portMAX_DELAY ) != pdTRUE ) {
        return;
    }
    if ( !background_file_paths( tmp, sizeof( tmp ), dest, sizeof( dest ) ) ) {
        xSemaphoreGive( background_file_lock );
        return;
    }
    if ( background_stop_uploads ) {
        if ( background_phase != BACKGROUND_PHASE_COMMIT ) {
            background_abort_upload_locked();
        }
        xSemaphoreGive( background_file_lock );
        return;
    }

    if ( index == 0 ) {
        if ( background_phase == BACKGROUND_PHASE_COMMIT || ( background_upload_busy && background_owner != NULL && !background_owner_finished && background_owner != request ) ) {
            xSemaphoreGive( background_file_lock );
            return;
        }
        if ( background_upload != NULL ) {
            fclose( background_upload );
            background_upload = NULL;
        }
        remove( tmp );
        background_upload_ok = false;
        background_upload_size = 0;
        background_result_ready = false;
        background_phase = BACKGROUND_PHASE_IDLE;
        if ( !request->authenticate( background_user, background_pass ) ) {
            background_owner = NULL;
            background_upload_busy = false;
            xSemaphoreGive( background_file_lock );
            return;
        }

        background_owner = request;
        background_owner_finished = false;
        background_upload_busy = true;
        if ( request->contentLength() > BACKGROUND_UPLOAD_MAX + 4096 ) {
            background_http_result( 413, "The image is larger than 256 KB.", "too large" );
        }
        else {
            size_t total = SPIFFS.totalBytes();
            size_t used = SPIFFS.usedBytes();
            size_t free_bytes = total > used ? total - used : 0;
            size_t announced = request->contentLength();
            if ( total == 0 || ( announced > 0 && announced > free_bytes ) ) {
                background_http_result( 400, "Not enough free space on the watch.", "no space" );
            }
            else {
                background_upload = fopen( tmp, "wb" );
                if ( background_upload == NULL ) {
                    background_http_result( 400, "Could not store the image.", "store failed" );
                }
                else {
                    background_upload_ok = true;
                    background_phase = BACKGROUND_PHASE_RECEIVING;
                }
            }
        }
    }

    if ( request != background_owner ) {
        xSemaphoreGive( background_file_lock );
        return;
    }
    if ( background_upload_ok && background_upload != NULL && len > 0 ) {
        if ( background_upload_size > BACKGROUND_UPLOAD_MAX || len > BACKGROUND_UPLOAD_MAX - background_upload_size ) {
            background_upload_ok = false;
            background_http_result( 413, "The image is larger than 256 KB.", "too large" );
        }
        else if ( fwrite( data, 1, len, background_upload ) != len ) {
            background_upload_ok = false;
            background_http_result( 400, "Not enough free space on the watch.", "no space" );
        }
        else {
            background_upload_size += len;
        }
    }

    if ( !final ) {
        xSemaphoreGive( background_file_lock );
        return;
    }

    if ( background_upload != NULL ) {
        fclose( background_upload );
        background_upload = NULL;
    }
    background_owner_finished = true;
    commit = background_upload_ok && background_upload_size > 0;
    background_phase = commit ? BACKGROUND_PHASE_COMMIT : BACKGROUND_PHASE_IDLE;
    if ( !commit && !background_result_ready ) {
        background_http_result( 400, "Choose a PNG file.", "choose a PNG" );
    }
    xSemaphoreGive( background_file_lock );
    background_finish_upload( tmp, dest, commit );
}

static void background_install_routes( AsyncWebServer &server ) {
    server.on( "/", HTTP_GET, background_handle_get );
    server.on( "/upload", HTTP_POST, background_handle_posted, background_handle_upload );
    server.onNotFound( background_handle_missing );
}

static AsyncWebServer *background_server_for( uint16_t port ) {
    static AsyncWebServer on_port_80( 80 );
    static AsyncWebServer on_port_8080( 8080 );
    static bool routes_80 = false;
    static bool routes_8080 = false;

    if ( port == 80 ) {
        if ( !routes_80 ) {
            background_install_routes( on_port_80 );
            routes_80 = true;
        }
        return( &on_port_80 );
    }
    if ( !routes_8080 ) {
        background_install_routes( on_port_8080 );
        routes_8080 = true;
    }
    return( &on_port_8080 );
}

static uint16_t background_choose_port( void ) {
    const char *pass = wifictl_get_ftp_pass();
    /*
     * Port 80 is free when the firmware web server has no password, because that
     * server refuses to start. Use 8080 only when it will actually bind port 80.
     */
    if ( wifictl_get_webserver() && pass != NULL && pass[ 0 ] != '\0' ) {
        return( 8080 );
    }
    return( 80 );
}

static bool background_wifi_has_ip( void ) {
    return( WiFi.localIP() != IPAddress( 0, 0, 0, 0 ) );
}

static void background_server_start( void ) {
    AsyncWebServer *server = NULL;

    background_copy_login();
    if ( !background_wifi_has_ip() || background_http != NULL || background_file_lock == NULL || !background_have_login ) {
        return;
    }
    if ( xSemaphoreTake( background_file_lock, portMAX_DELAY ) == pdTRUE ) {
        background_stop_uploads = false;
        if ( background_phase != BACKGROUND_PHASE_COMMIT ) {
            background_abort_upload_locked();
        }
        xSemaphoreGive( background_file_lock );
    }
    background_port = background_choose_port();
    server = background_server_for( background_port );
    server->begin();
    background_http = server;
    log_i( "background upload on port %u", (unsigned)background_port );
}

static void background_server_stop( void ) {
    background_stop_uploads = true;
    if ( background_file_lock != NULL && xSemaphoreTake( background_file_lock, portMAX_DELAY ) == pdTRUE ) {
        if ( background_phase != BACKGROUND_PHASE_COMMIT ) {
            background_abort_upload_locked();
        }
        xSemaphoreGive( background_file_lock );
    }
    if ( background_http != NULL ) {
        background_http->end();
        background_http = NULL;
        log_i( "background upload stopped" );
    }
}

static void background_copy_login( void ) {
    const char *user = wifictl_get_ftp_user();
    const char *pass = wifictl_get_ftp_pass();

    if ( user == NULL || user[ 0 ] == '\0' ) {
        user = FTPSERVER_USER;
    }
    if ( pass == NULL || pass[ 0 ] == '\0' ) {
        pass = FTPSERVER_PASSWORD;
    }
    background_copy_text( background_user, sizeof( background_user ), user );
    background_copy_text( background_pass, sizeof( background_pass ), pass );
    background_have_login = background_user[ 0 ] != '\0' && background_pass[ 0 ] != '\0';
}

#endif

void background_app_main_setup( uint32_t tile_num ) {
    lv_obj_t *tile = mainbar_get_tile_obj( tile_num );
    lv_obj_t *exit_btn = NULL;

    lv_style_copy( &background_style, APP_STYLE );
    lv_style_set_text_font( &background_style, LV_STATE_DEFAULT, &Ubuntu_16px );

#ifndef NATIVE_64BIT
    if ( background_file_lock == NULL ) {
        background_file_lock = xSemaphoreCreateMutex();
    }
    background_copy_login();
    background_port = background_choose_port();
#endif
    background_label = lv_label_create( tile, NULL );
    lv_label_set_long_mode( background_label, LV_LABEL_LONG_BREAK );
    lv_obj_set_width( background_label, lv_disp_get_hor_res( NULL ) - 16 );
    lv_label_set_align( background_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_add_style( background_label, LV_OBJ_PART_MAIN, &background_style );
    lv_obj_align( background_label, tile, LV_ALIGN_IN_TOP_MID, 0, 36 );
    background_refresh_labels();

    exit_btn = wf_add_exit_button( tile, background_exit_cb );
    lv_obj_align( exit_btn, tile, LV_ALIGN_IN_BOTTOM_LEFT, THEME_PADDING, -THEME_PADDING );

    mainbar_add_tile_activate_cb( tile_num, background_activate_cb );
    mainbar_add_tile_hibernate_cb( tile_num, background_hibernate_cb );
    styles_register_cb( STYLE_CHANGE, background_style_cb, "background style" );
    wifictl_register_cb( WIFICTL_CONNECT_IP | WIFICTL_DISCONNECT | WIFICTL_OFF, background_wifi_cb, "background image" );
#ifndef NATIVE_64BIT
    /*
     * Wi-Fi may already have an address before this app is registered, so the
     * connect event will not arrive again. Listen as soon as an address exists.
     */
    if ( background_wifi_has_ip() ) {
        background_server_start();
    }
#endif
}

static void background_activate_cb( void ) {
#ifdef NATIVE_64BIT
    background_refresh_labels();
#else
    bool radio_off = false;

    if ( background_session ) {
        background_server_start();
        background_refresh_labels();
        return;
    }

    background_session = true;
    background_copy_text( background_status, sizeof( background_status ), "" );
    background_copy_login();
    if ( !background_have_login ) {
        background_refresh_labels();
        return;
    }

    background_saved_timeout = display_get_timeout();
    background_saved_block = display_get_block_return_maintile();
    background_saved_standby = wifictl_get_enable_on_standby();
    background_holding = true;
    /*
     * Keep the radio through screen-off, but let the backlight sleep.
     * Staying on this tile keeps the page up after the screen blanks.
     */
    display_set_timeout( BACKGROUND_SCREEN_SECONDS );
    display_set_block_return_maintile( true );
    if ( !background_saved_standby ) {
        wifictl_set_enable_on_standby( true );
    }
    background_port = background_choose_port();
    radio_off = WiFi.getMode() == WIFI_OFF;
    background_started_wifi = radio_off && !wifictl_get_autoon();
    if ( radio_off ) {
        wifictl_on();
    }
    background_server_start();
    background_refresh_labels();
#endif
}

static void background_hibernate_cb( void ) {
    if ( !background_session ) {
        return;
    }
#ifndef NATIVE_64BIT
    if ( background_holding ) {
        if ( background_started_wifi ) {
            wifictl_off();
            background_started_wifi = false;
        }
        if ( wifictl_get_enable_on_standby() != background_saved_standby ) {
            wifictl_set_enable_on_standby( background_saved_standby );
        }
        display_set_timeout( background_saved_timeout );
        display_set_block_return_maintile( background_saved_block );
        background_holding = false;
    }
#endif
    background_session = false;
    background_refresh_labels();
}

static bool background_wifi_cb( EventBits_t event, void *arg ) {
    (void)arg;
    switch( event ) {
        case WIFICTL_CONNECT_IP:
#ifndef NATIVE_64BIT
            background_server_stop();
            background_server_start();
#endif
            background_refresh_labels();
            break;
        case WIFICTL_OFF:
#ifndef NATIVE_64BIT
            background_server_stop();
#endif
            background_refresh_labels();
            break;
        case WIFICTL_DISCONNECT:
            background_refresh_labels();
            break;
    }
    return( true );
}
