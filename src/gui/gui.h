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
#ifndef _GUI_H
    #define _GUI_H
    
    #define BACKGROUNDIMAGE    "/spiffs/bg.png"
    /**
     * @brief GUI setup
     */
    void gui_setup( void );
    /**
     * @brief set the background
     * 
     * @param   background_image    0..3 built-in, 4 hidden, 5 /spiffs/bg.png
     */
    void gui_set_background_image ( uint32_t background_image);
    /**
     * @brief force a complete redraw cycle on next gui cycle
     * 
     * @param force  true for redraw
     */
    void gui_force_redraw( bool force );
    /**
     * @brief take gui control to make LVGL threas safe
     */    
    bool gui_take( void );
    /**
     * @brief take gui control to make LVGL threas safe
     */    
    void gui_give( void );
    /**
     * @brief run fn(arg) on the powermgm task, where the LVGL lock is already held.
     *        fn must not call gui_take. arg stays owned by the caller until fn returns.
     *        On the powermgm task, fn runs before this returns.
     */
    bool gui_dispatch( void ( *fn )( void *arg ), void *arg );
    /**
     * @brief same as gui_dispatch, but returns only after fn has run.
     *        arg may live on the caller's stack. Do not call this from an LVGL event.
     */
    bool gui_dispatch_sync( void ( *fn )( void *arg ), void *arg );

#endif // _STATUSBAR_H