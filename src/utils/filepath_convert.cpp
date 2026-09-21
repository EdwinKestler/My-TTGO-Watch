#include "filepath_convert.h"

#ifdef NATIVE_64BIT
    #include <dirent.h>
    #include <iostream>
    #include <fstream>
    #include <string.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <pwd.h>
    #include "utils/logging.h"
#else
    #include <Arduino.h>

    #ifdef M5PAPER
    #elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
    #endif
#endif

#ifdef NATIVE_64BIT
static bool filepath_has_dotdot( const char *path ) {
    const char *cursor = path;
    if ( cursor == NULL ) {
        return( true );
    }
    while ( *cursor != '\0' ) {
        if ( cursor[ 0 ] == '.' && cursor[ 1 ] == '.' && ( cursor[ 2 ] == '/' || cursor[ 2 ] == '\0' ) ) {
            if ( cursor == path || cursor[ -1 ] == '/' ) {
                return( true );
            }
        }
        cursor++;
    }
    return( false );
}
#endif

char *filepath_convert( char * dst_str, int max_len, const char* local_path ) {
    if ( dst_str != NULL && max_len > 0 ) {
        dst_str[ 0 ] = '\0';
    }
    #ifdef NATIVE_64BIT
        struct passwd *pw = getpwuid( getuid() );
        const char *home = ( pw != NULL ) ? pw->pw_dir : getenv( "HOME" );
        if ( home == NULL || filepath_has_dotdot( local_path ) ) {
            log_e("filepath rejected");
            return( dst_str );
        }
        char hedge_config_path[512] = "";
        snprintf( hedge_config_path, sizeof( hedge_config_path ), "%s/.hedge", home );
        DIR *hedge_dir = opendir( hedge_config_path );
        if ( hedge_dir == NULL ) {
            log_i("create config path and dir");
            mkdir( hedge_config_path, 0700 );
            snprintf( hedge_config_path, sizeof( hedge_config_path ), "%s/.hedge/spiffs", home );
            mkdir( hedge_config_path, 0700 );
            snprintf( hedge_config_path, sizeof( hedge_config_path ), "%s/.hedge/sd", home );
            mkdir( hedge_config_path, 0700 );
        }
        else {
            closedir( hedge_dir );
        }
        snprintf( dst_str, max_len, "%s/.hedge/%s", home, local_path != NULL ? local_path : "" );
    #else
        snprintf( dst_str, max_len, "%s", local_path != NULL ? local_path : "" );
    #endif

    return( dst_str );
}
