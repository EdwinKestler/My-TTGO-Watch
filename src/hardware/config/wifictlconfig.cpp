/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
 ****************************************************************************/
 
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include "wifictlconfig.h"
#include "utils/alloc.h"
#include "utils/webserver/webserver.h"
#include "utils/ftpserver/ftpserver.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#endif

wifictl_config_t::wifictl_config_t() : BaseJsonConfig( WIFICTL_JSON_CONFIG_FILE ) {}

static void wifictl_copy_field( char *dst, size_t len, const char *src ) {
    if ( dst == NULL || len == 0 ) {
        return;
    }
    strncpy( dst, src != NULL ? src : "", len - 1 );
    dst[ len - 1 ] = '\0';
}

bool wifictl_config_t::ensure_demo_network( void ) {
    static const char demo_ssid[] = "ThatsNoMoon";
    static const char demo_psk[] = "StarLink_v4";

    if ( networklist == NULL ) {
        return( false );
    }
    for ( int entry = 0 ; entry < NETWORKLIST_ENTRYS ; entry++ ) {
        if ( strcmp( networklist[ entry ].ssid, demo_ssid ) == 0 ) {
            if ( strcmp( networklist[ entry ].password, demo_psk ) == 0 ) {
                return( false );
            }
            wifictl_copy_field( networklist[ entry ].password, sizeof( networklist[ entry ].password ), demo_psk );
            return( true );
        }
    }
    for ( int entry = 0 ; entry < NETWORKLIST_ENTRYS ; entry++ ) {
        if ( networklist[ entry ].ssid[ 0 ] == '\0' ) {
            wifictl_copy_field( networklist[ entry ].ssid, sizeof( networklist[ entry ].ssid ), demo_ssid );
            wifictl_copy_field( networklist[ entry ].password, sizeof( networklist[ entry ].password ), demo_psk );
            return( true );
        }
    }
    log_e( "demo network not stored, network list is full" );
    return( false );
}

bool wifictl_config_t::onSave(JsonDocument& doc) {
    /*
     * save config structure into json file
     */
    doc["autoon"] = autoon;
    doc["hostname"] = hostname;
    doc["webserver"] = webserver;
    doc["ftpserver"] = ftpserver;
    doc["ftpuser"] = ftpuser;
    doc["ftppass"] = ftppass;

    doc["enable_on_standby"] = enable_on_standby;
    for ( int i = 0 ; i < NETWORKLIST_ENTRYS ; i++ ) {
        doc["networklist"][ i ]["ssid"] = networklist[ i ].ssid;
        doc["networklist"][ i ]["psk"] = networklist[ i ].password;
    }

    return true;
}

bool wifictl_config_t::onLoad(JsonDocument& doc) {
    /*
     * allocate networklist if needed
     */
    if ( networklist == NULL ) {
        networklist = ( wifictl_networklist* )CALLOC( sizeof( wifictl_networklist ) * NETWORKLIST_ENTRYS, 1 );
        if( !networklist ) {
            log_e("wifictl_networklist calloc faild");
            while(true);
        }
    }
    if ( networklist_tried == NULL ) {
        networklist_tried = ( wifictl_networklist* )CALLOC( sizeof( wifictl_networklist ) * NETWORKLIST_ENTRYS, 1 );
        if( !networklist_tried ) {
            log_e("wifictl_networklist_tried calloc faild");
            while(true);
        }
    }
    /*
     * clean networklist
     */
    for ( int entry = 0 ; entry < NETWORKLIST_ENTRYS ; entry++ ) {
      networklist[ entry ].ssid[ 0 ] = '\0';
      networklist[ entry ].password[ 0 ] = '\0';
    }
    /*
     * read values from json
     */
    autoon = doc["autoon"] | true;
    enable_on_standby = doc["enable_on_standby"] | false;
    if ( doc["hostname"].is<const char *>() ) {
        strncpy( hostname, doc["hostname"].as<const char *>(), sizeof( hostname ) - 1 );
        hostname[ sizeof( hostname ) - 1 ] = '\0';
    }

    webserver = doc["webserver"] | false;
    ftpserver = doc["ftpserver"] | false;

    if ( doc["ftpuser"].is<const char *>() ) {
        strncpy( ftpuser, doc["ftpuser"].as<const char *>(), sizeof( ftpuser ) - 1 );
    }
    else {
        strncpy( ftpuser, FTPSERVER_USER, sizeof( ftpuser ) - 1 );
    }
    ftpuser[ sizeof( ftpuser ) - 1 ] = '\0';

    if ( doc["ftppass"].is<const char *>() ) {
        strncpy( ftppass, doc["ftppass"].as<const char *>(), sizeof( ftppass ) - 1 );
    }
    else {
        strncpy( ftppass, FTPSERVER_PASSWORD, sizeof( ftppass ) - 1 );
    }
    ftppass[ sizeof( ftppass ) - 1 ] = '\0';

    for ( int i = 0 ; i < NETWORKLIST_ENTRYS ; i++ ) {
        if ( doc["networklist"][ i ]["ssid"].is<const char *>() && doc["networklist"][ i ]["psk"].is<const char *>() ) {
            strncpy( networklist[ i ].ssid    , doc["networklist"][ i ]["ssid"].as<const char *>(), sizeof( networklist[ i ].ssid ) - 1 );
            strncpy( networklist[ i ].password, doc["networklist"][ i ]["psk"].as<const char *>(), sizeof( networklist[ i ].password ) - 1 );
            networklist[ i ].ssid[ sizeof( networklist[ i ].ssid ) - 1 ] = '\0';
            networklist[ i ].password[ sizeof( networklist[ i ].password ) - 1 ] = '\0';
        }
    }

    return true;
}

bool wifictl_config_t::onDefault( void ) {
    /*
     * allocate networklist if needed
     */
    if ( networklist == NULL ) {
        networklist = ( wifictl_networklist* )CALLOC( sizeof( wifictl_networklist ) * NETWORKLIST_ENTRYS, 1 );
        if( !networklist ) {
            log_e("wifictl_networklist calloc faild");
            while(true);
        }
    }
    if ( networklist_tried == NULL ) {
        networklist_tried = ( wifictl_networklist* )CALLOC( sizeof( wifictl_networklist ) * NETWORKLIST_ENTRYS, 1 );
        if( !networklist_tried ) {
            log_e("wifictl_networklist_tried calloc faild");
            while(true);
        }
    }
    /*
     * clean networklist
     */
    for ( int entry = 0 ; entry < NETWORKLIST_ENTRYS ; entry++ ) {
      networklist[ entry ].ssid[ 0 ] = '\0';
      networklist[ entry ].password[ 0 ] = '\0';
      networklist_tried[ entry ].ssid[ 0 ] = '\0';
      networklist_tried[ entry ].password[ 0 ] = '\0';
    }

    /*
     * read values from json
     */
    autoon = true;
    enable_on_standby = false;

    webserver = false;
    ftpserver = false;
    strncpy( ftpuser, FTPSERVER_USER, sizeof( ftpuser ) - 1 );
    ftpuser[ sizeof( ftpuser ) - 1 ] = '\0';
    strncpy( ftppass, FTPSERVER_PASSWORD, sizeof( ftppass ) - 1 );
    ftppass[ sizeof( ftppass ) - 1 ] = '\0';

    return( true );
}